#include "remapper.h"

#include "gamepads.h"

#include <QFile>
#include <QMutexLocker>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr int bitsPerLong = sizeof(unsigned long) * 8;

bool testBit(const unsigned long *bits, int bit)
{
    return (bits[bit / bitsPerLong] >> (bit % bitsPerLong)) & 1UL;
}

// The buttons xpad gives an Xbox 360 pad, which every game and SDL's
// controller database already understand.
constexpr int virtualButtons[] = {BTN_A, BTN_B, BTN_X, BTN_Y, BTN_TL, BTN_TR, BTN_SELECT,
                                  BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};

QString errorText(int error)
{
    return QString::fromLocal8Bit(std::strerror(error));
}

}

Remapper::Remapper(const QString &path, const QString &driver, bool active, const Profile &profile)
    : m_path(path), m_driver(driver), m_active(active), m_profile(profile)
{
    m_swapFaces = Gamepads::positionalFaceButtons(driver);
}

Remapper::~Remapper()
{
    stop();
}

void Remapper::setProfile(const Profile &profile)
{
    QMutexLocker lock(&m_mutex);
    m_profile = profile;
}

LiveState Remapper::live() const
{
    QMutexLocker lock(&m_mutex);
    return m_live;
}

void Remapper::start()
{
    // Read-write so rumble can be written back; read-only still remaps.
    const QByteArray path = QFile::encodeName(m_path);
    m_source = ::open(path.constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_source < 0)
        m_source = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_source < 0) {
        emit failed(errno == EACCES ? tr("No permission to read %1. Add a udev rule that gives your user access to it.").arg(m_path)
                                    : tr("Could not open %1: %2").arg(m_path, errorText(errno)));
        return;
    }

    unsigned long abs[ABS_CNT / bitsPerLong + 1] = {}, ff[FF_CNT / bitsPerLong + 1] = {};
    ioctl(m_source, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
    ioctl(m_source, EVIOCGBIT(EV_FF, sizeof(ff)), ff);

    // Most pads put the right stick on RX/RY and triggers on Z/RZ; some
    // older ones use Z/RZ for the right stick and gas/brake for triggers.
    const bool classic = testBit(abs, ABS_RX);
    const int codes[AxisCount] = {ABS_X, ABS_Y, classic ? ABS_RX : ABS_Z, classic ? ABS_RY : ABS_RZ,
                                  classic ? ABS_Z : ABS_BRAKE, classic ? ABS_RZ : ABS_GAS};
    for (int axis = 0; axis < AxisCount; ++axis) {
        m_codes[axis] = codes[axis];
        input_absinfo info{};
        if (testBit(abs, codes[axis]) && ioctl(m_source, EVIOCGABS(codes[axis]), &info) == 0 && info.maximum > info.minimum)
            m_ranges[axis] = {info.minimum, info.maximum, true};
    }
    resync();

    if (m_active) {
        if (ioctl(m_source, EVIOCGRAB, 1) < 0) {
            const int error = errno;
            emit failed(error == EBUSY ? tr("Another program already holds this controller, maybe a running \"deadzone run\" or Steam Input.")
                                       : tr("Could not take over the controller: %1").arg(errorText(error)));
            ::close(m_source);
            m_source = -1;
            return;
        }
        if (!createVirtual(testBit(ff, FF_RUMBLE))) {
            ioctl(m_source, EVIOCGRAB, 0);
            ::close(m_source);
            m_source = -1;
            return;
        }
    }

    m_sourceNotifier = new QSocketNotifier(m_source, QSocketNotifier::Read, this);
    connect(m_sourceNotifier, &QSocketNotifier::activated, this, &Remapper::readSource);
    if (m_virtual >= 0) {
        m_virtualNotifier = new QSocketNotifier(m_virtual, QSocketNotifier::Read, this);
        connect(m_virtualNotifier, &QSocketNotifier::activated, this, &Remapper::readVirtual);
    }
    flush();
    emit started(m_active);
}

bool Remapper::createVirtual(bool rumble)
{
    m_virtual = ::open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_virtual < 0) {
        const int error = errno;
        emit failed(error == EACCES ? tr("No permission to create a virtual controller (/dev/uinput). Installing Steam or game-devices-udev adds the rule that allows it.")
                                    : tr("Could not open /dev/uinput: %1").arg(errorText(error)));
        return false;
    }

    ioctl(m_virtual, UI_SET_EVBIT, EV_SYN);
    ioctl(m_virtual, UI_SET_EVBIT, EV_KEY);
    ioctl(m_virtual, UI_SET_EVBIT, EV_ABS);
    for (int button : virtualButtons)
        ioctl(m_virtual, UI_SET_KEYBIT, button);

    // Flat is zero on purpose: the deadzone is ours, and a nonzero flat
    // would invite games to stack another one on top.
    auto axis = [&](int code, int min, int max) {
        uinput_abs_setup setup{};
        setup.code = code;
        setup.absinfo.minimum = min;
        setup.absinfo.maximum = max;
        ioctl(m_virtual, UI_SET_ABSBIT, code);
        ioctl(m_virtual, UI_ABS_SETUP, &setup);
    };
    for (int code : {ABS_X, ABS_Y, ABS_RX, ABS_RY})
        axis(code, -32768, 32767);
    axis(ABS_Z, 0, 255);
    axis(ABS_RZ, 0, 255);
    axis(ABS_HAT0X, -1, 1);
    axis(ABS_HAT0Y, -1, 1);

    if (rumble) {
        ioctl(m_virtual, UI_SET_EVBIT, EV_FF);
        ioctl(m_virtual, UI_SET_FFBIT, FF_RUMBLE);
    }

    // The Xbox 360 identity makes SDL, Proton, and Steam map it without help.
    uinput_setup setup{};
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x045e;
    setup.id.product = 0x028e;
    setup.id.version = 0x0114;
    std::strncpy(setup.name, "Deadzone Xbox 360 Pad", UINPUT_MAX_NAME_SIZE - 1);
    setup.ff_effects_max = rumble ? 16 : 0;
    ioctl(m_virtual, UI_SET_PHYS, "deadzone/virtual");
    if (ioctl(m_virtual, UI_DEV_SETUP, &setup) < 0 || ioctl(m_virtual, UI_DEV_CREATE) < 0) {
        const int error = errno;
        emit failed(tr("Could not create the virtual controller: %1").arg(errorText(error)));
        ::close(m_virtual);
        m_virtual = -1;
        return false;
    }
    return true;
}

void Remapper::stop()
{
    delete m_sourceNotifier;
    delete m_virtualNotifier;
    m_sourceNotifier = m_virtualNotifier = nullptr;
    if (m_source >= 0) {
        for (int physical : std::as_const(m_effects))
            ioctl(m_source, EVIOCRMFF, physical);
        m_effects.clear();
        ioctl(m_source, EVIOCGRAB, 0);
        ::close(m_source);
        m_source = -1;
    }
    if (m_virtual >= 0) {
        ioctl(m_virtual, UI_DEV_DESTROY);
        ::close(m_virtual);
        m_virtual = -1;
    }
}

// Reads every axis directly, used at start and after the kernel reports
// that events were dropped.
void Remapper::resync()
{
    for (int axis = 0; axis < AxisCount; ++axis) {
        input_absinfo info{};
        if (m_ranges[axis].present && ioctl(m_source, EVIOCGABS(m_codes[axis]), &info) == 0)
            m_raw[axis] = normalize(axis, info.value);
    }
}

// Sticks become -1..1 around their center, triggers 0..1.
double Remapper::normalize(int axis, int value) const
{
    const Range &r = m_ranges[axis];
    if (axis == L2 || axis == R2)
        return std::clamp(double(value - r.min) / (r.max - r.min), 0.0, 1.0);
    return std::clamp((value - (r.min + r.max) / 2.0) / ((r.max - r.min) / 2.0), -1.0, 1.0);
}

void Remapper::readSource()
{
    input_event events[64];
    for (;;) {
        const ssize_t bytes = ::read(m_source, events, sizeof(events));
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EINTR)
                return;
            // ENODEV: unplugged, or the Bluetooth link dropped.
            m_sourceNotifier->setEnabled(false);
            emit disconnected();
            return;
        }
        const int count = int(bytes / sizeof(input_event));
        for (int i = 0; i < count; ++i) {
            const input_event &ev = events[i];
            if (ev.type == EV_SYN) {
                if (ev.code == SYN_DROPPED) {
                    m_dropped = true;
                } else if (ev.code == SYN_REPORT) {
                    noteReport(ev.time);
                    if (m_dropped) {
                        resync();
                        m_dropped = false;
                    }
                    flush();
                }
                continue;
            }
            if (m_dropped)
                continue;

            if (ev.type == EV_ABS) {
                if (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y) {
                    handleHat(ev.code, std::clamp(ev.value, -1, 1));
                    continue;
                }
                for (int axis = 0; axis < AxisCount; ++axis)
                    if (m_codes[axis] == ev.code && m_ranges[axis].present)
                        m_raw[axis] = normalize(axis, ev.value);
            } else if (ev.type == EV_KEY) {
                int code = ev.code;
                if (m_swapFaces && code == BTN_NORTH)
                    code = BTN_WEST;
                else if (m_swapFaces && code == BTN_WEST)
                    code = BTN_NORTH;
                // Pads that report the d-pad as buttons become a hat, the way
                // an Xbox 360 pad reports it.
                if (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT) {
                    const int hat = code == BTN_DPAD_LEFT || code == BTN_DPAD_RIGHT ? ABS_HAT0X : ABS_HAT0Y;
                    const int dir = code == BTN_DPAD_LEFT || code == BTN_DPAD_UP ? -1 : 1;
                    handleHat(hat, ev.value ? dir : 0);
                    continue;
                }
                // Digital triggers only stand in when there are no analog ones.
                if ((code == BTN_TL2 && !m_ranges[L2].present) || (code == BTN_TR2 && !m_ranges[R2].present)) {
                    m_raw[code == BTN_TL2 ? L2 : R2] = ev.value ? 1.0 : 0.0;
                    continue;
                }
                if (std::find(std::begin(virtualButtons), std::end(virtualButtons), code) != std::end(virtualButtons))
                    handleKey(code, ev.value);
            }
        }
    }
}

void Remapper::setButton(int bit, bool down)
{
    if (down)
        m_buttons |= 1u << bit;
    else
        m_buttons &= ~(1u << bit);
}

void Remapper::handleKey(int code, int value)
{
    // Codes here are already the Xbox pad's: A is the bottom button.
    static const QHash<int, int> bits{{BTN_A, Cross}, {BTN_B, Circle}, {BTN_X, Square}, {BTN_Y, Triangle},
                                      {BTN_TL, L1}, {BTN_TR, R1}, {BTN_SELECT, Share}, {BTN_START, Options},
                                      {BTN_MODE, PS}, {BTN_THUMBL, L3}, {BTN_THUMBR, R3}};
    if (value != 2)
        setButton(bits.value(code), value != 0);

    // PS is held back until it is clear whether it starts a combo: released
    // without one, it reaches the game as a quick press.
    if (code == BTN_MODE && m_comboEnabled) {
        if (value == 1) {
            m_modeHeld = true;
            m_comboUsed = false;
        } else if (value == 0) {
            m_modeHeld = false;
            if (!m_comboUsed) {
                emitEvent(EV_KEY, BTN_MODE, 1);
                emitEvent(EV_SYN, SYN_REPORT, 0);
                emitEvent(EV_KEY, BTN_MODE, 0);
            }
        }
        return;
    }
    emitEvent(EV_KEY, code, value);
}

void Remapper::handleHat(int code, int value)
{
    if (code == ABS_HAT0X) {
        setButton(DpadLeft, value < 0);
        setButton(DpadRight, value > 0);
    } else {
        setButton(DpadUp, value < 0);
        setButton(DpadDown, value > 0);
    }
    if (m_modeHeld) {
        if (code == ABS_HAT0X && value != 0) {
            m_comboUsed = true;
            emit profileStep(value);
        }
        // Nothing the d-pad does while PS is held reaches the game.
        return;
    }
    emitEvent(EV_ABS, code, value);
}

// The rate counts reports over the last half second. A resting controller
// sends nothing, so the last rate measured while moving is kept.
void Remapper::noteReport(const timeval &time)
{
    const double now = time.tv_sec + time.tv_usec / 1e6;
    if (!m_reports.empty() && now - m_reports.back() > 0.05)
        m_reports.clear();   // A pause: start counting again.
    m_reports.push_back(now);
    while (m_reports.size() > 2 && now - m_reports.front() > 0.5)
        m_reports.erase(m_reports.begin());
    if (m_reports.size() >= 40)
        m_rate = (m_reports.size() - 1) / (m_reports.back() - m_reports.front());
}

// Applies the profile to the axes and sends one report to the virtual pad.
void Remapper::flush()
{
    Profile profile;
    {
        QMutexLocker lock(&m_mutex);
        profile = m_profile;
    }
    const QPointF leftRaw(m_raw[LX], m_raw[LY]), rightRaw(m_raw[RX], m_raw[RY]);
    const QPointF leftOut = Curves::stick(leftRaw, profile.left);
    const QPointF rightOut = Curves::stick(rightRaw, profile.right);
    const double l2Out = Curves::trigger(m_raw[L2], profile.l2);
    const double r2Out = Curves::trigger(m_raw[R2], profile.r2);
    {
        QMutexLocker lock(&m_mutex);
        m_live = {leftRaw, leftOut, rightRaw, rightOut, m_raw[L2], l2Out, m_raw[R2], r2Out, m_buttons, m_rate};
    }

    if (m_virtual < 0) {
        m_pending.clear();
        return;
    }
    auto stickValue = [](double v) { return int(std::lround(std::clamp(v, -1.0, 1.0) * 32767.0)); };
    auto triggerValue = [](double v) { return int(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    const int values[AxisCount] = {stickValue(leftOut.x()), stickValue(leftOut.y()), stickValue(rightOut.x()),
                                   stickValue(rightOut.y()), triggerValue(l2Out), triggerValue(r2Out)};
    const int codes[AxisCount] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ};
    for (int axis = 0; axis < AxisCount; ++axis) {
        if (values[axis] != m_sent[axis]) {
            emitEvent(EV_ABS, codes[axis], values[axis]);
            m_sent[axis] = values[axis];
        }
    }
    if (m_pending.empty())
        return;
    emitEvent(EV_SYN, SYN_REPORT, 0);
    const ssize_t size = ssize_t(m_pending.size() * sizeof(input_event));
    if (::write(m_virtual, m_pending.data(), size) != size)
        qWarning("deadzone: short write to the virtual controller");
    m_pending.clear();
}

void Remapper::emitEvent(int type, int code, int value)
{
    input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    m_pending.push_back(ev);
}

// Rumble requests from games arrive on the virtual pad and are replayed on
// the real one, with effect ids translated between the two.
void Remapper::readVirtual()
{
    input_event events[16];
    for (;;) {
        const ssize_t bytes = ::read(m_virtual, events, sizeof(events));
        if (bytes <= 0)
            return;
        for (int i = 0; i < int(bytes / sizeof(input_event)); ++i) {
            const input_event &ev = events[i];
            if (ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD) {
                uploadEffect(ev.value);
            } else if (ev.type == EV_UINPUT && ev.code == UI_FF_ERASE) {
                eraseEffect(ev.value);
            } else if (ev.type == EV_FF) {
                input_event out{};
                out.type = EV_FF;
                if (ev.code == FF_GAIN) {
                    out.code = FF_GAIN;
                } else if (m_effects.contains(ev.code)) {
                    out.code = m_effects.value(ev.code);
                } else {
                    continue;
                }
                out.value = ev.value;
                if (::write(m_source, &out, sizeof(out)) < 0)
                    continue;   // A read-only source simply has no rumble.
            }
        }
    }
}

void Remapper::uploadEffect(int requestId)
{
    uinput_ff_upload upload{};
    upload.request_id = requestId;
    if (ioctl(m_virtual, UI_BEGIN_FF_UPLOAD, &upload) < 0)
        return;
    ff_effect effect = upload.effect;
    effect.id = m_effects.value(upload.effect.id, -1);
    if (effect.type == FF_RUMBLE) {
        const double strength = std::clamp(m_rumble.load(), 0.0, 1.0);
        effect.u.rumble.strong_magnitude = quint16(effect.u.rumble.strong_magnitude * strength);
        effect.u.rumble.weak_magnitude = quint16(effect.u.rumble.weak_magnitude * strength);
    }
    if (ioctl(m_source, EVIOCSFF, &effect) == 0) {
        m_effects.insert(upload.effect.id, effect.id);
        upload.retval = 0;
    } else {
        upload.retval = -errno;
    }
    ioctl(m_virtual, UI_END_FF_UPLOAD, &upload);
}

void Remapper::eraseEffect(int requestId)
{
    uinput_ff_erase erase{};
    erase.request_id = requestId;
    if (ioctl(m_virtual, UI_BEGIN_FF_ERASE, &erase) < 0)
        return;
    if (m_effects.contains(erase.effect_id))
        ioctl(m_source, EVIOCRMFF, m_effects.take(erase.effect_id));
    erase.retval = 0;
    ioctl(m_virtual, UI_END_FF_ERASE, &erase);
}
