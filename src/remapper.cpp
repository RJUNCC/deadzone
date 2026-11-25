#include "remapper.h"

#include "gamepads.h"

#include <QMutexLocker>

#include <algorithm>
#include <cmath>

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
    m_profileGeneration.fetch_add(1, std::memory_order_release);
}

LiveState Remapper::live() const
{
    QMutexLocker lock(&m_mutex);
    m_liveDirty.store(false, std::memory_order_relaxed);
    return m_live;
}

// Sticks become -1..1 around their center, triggers 0..1.
double Remapper::normalize(int axis, int value) const
{
    const Range &r = m_ranges[axis];
    if (axis == L2 || axis == R2)
        return std::clamp(double(value - r.min) / (r.max - r.min), 0.0, 1.0);
    return std::clamp((value - (r.min + r.max) / 2.0) / ((r.max - r.min) / 2.0), -1.0, 1.0);
}

namespace {

// The Xbox pad's key for each LiveButton that is a key.
const int buttonCodes[] = {BTN_A, BTN_B, BTN_X, BTN_Y, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};

int bitForCode(int code)
{
    for (int bit = 0; bit < int(std::size(buttonCodes)); ++bit)
        if (buttonCodes[bit] == code)
            return bit;
    return -1;
}

}

void Remapper::setPhysical(int bit, bool down)
{
    if (bit < 0)
        return;
    if (down)
        m_physical |= 1u << bit;
    else
        m_physical &= ~(1u << bit);
}

const Profile &Remapper::readerProfile()
{
    const int generation = m_profileGeneration.load(std::memory_order_acquire);
    if (generation != m_readerGeneration) {
        QMutexLocker lock(&m_mutex);
        m_readerProfile = m_profile;
        m_readerGeneration = generation;
    }
    return m_readerProfile;
}

// Works out what the game should see from what is held, through the
// profile's button map, and sends only what changed. Several buttons may
// share a target; it stays pressed while any of them is held.
void Remapper::updateVirtual()
{
    const QList<int> &map = readerProfile().buttons;
    quint32 next = 0;
    for (int bit = 0; bit < Profile::ButtonCount; ++bit)
        if (m_physical & (1u << bit)) {
            const int target = map.value(bit, bit);
            if (target >= 0)
                next |= 1u << target;
        }
    const quint32 changed = next ^ m_gameButtons;
    if (!changed)
        return;
    const quint32 before = m_gameButtons;
    m_gameButtons = next;

    for (int bit = 0; bit < int(std::size(buttonCodes)); ++bit)
        if (changed & (1u << bit))
            emitEvent(EV_KEY, buttonCodes[bit], (next >> bit) & 1);
    if (changed & ((1u << DpadLeft) | (1u << DpadRight)))
        emitEvent(EV_ABS, ABS_HAT0X, (next & (1u << DpadRight)) ? 1 : (next & (1u << DpadLeft)) ? -1 : 0);
    if (changed & ((1u << DpadUp) | (1u << DpadDown)))
        emitEvent(EV_ABS, ABS_HAT0Y, (next & (1u << DpadDown)) ? 1 : (next & (1u << DpadUp)) ? -1 : 0);
    // Triggers pressed by a button are applied in flush().

    // Rocket League allows a second jump for a while after the first; a
    // press inside that window is a flip or a double jump. Jump is watched
    // on what the game receives, so remapping is taken into account.
    const quint32 jump = 1u << m_jumpBit;
    if ((next & jump) && !(before & jump)) {
        const double now = m_lastEventTime;
        if (m_firstJump >= 0 && now - m_firstJump < 1.5) {
            QPointF out;
            {
                QMutexLocker lock(&m_mutex);
                out = m_live.leftOut;
            }
            emit flip((now - m_firstJump) * 1000.0, out.x(), out.y());
            m_firstJump = -1;
            // Up is forward. A back flip may be cancelled forward (half-flip);
            // a forward diagonal may be cancelled back (speed flip).
            m_flipKind = out.y() > 0.4 && std::abs(out.x()) < out.y() ? 1
                       : out.y() < -0.25 && std::abs(out.x()) > 0.25 ? 2 : 0;
            m_flipTime = m_flipKind ? now : -1;
        } else {
            m_firstJump = now;
        }
    }
}

void Remapper::handleKey(int code, int value)
{
    // Codes here are already the Xbox pad's: A is the bottom button.
    if (value == 2)
        return;
    const int bit = bitForCode(code);

    // PS is held back until it is clear whether it starts a combo: released
    // without one, it reaches the game as a quick press.
    if (code == BTN_MODE && m_comboEnabled) {
        if (value == 1) {
            m_modeHeld = true;
            m_comboUsed = false;
        } else {
            m_modeHeld = false;
            if (!m_comboUsed) {
                setPhysical(PS, true);
                updateVirtual();
                emitEvent(EV_SYN, SYN_REPORT, 0);
                setPhysical(PS, false);
                updateVirtual();
            }
        }
        return;
    }
    setPhysical(bit, value != 0);
    updateVirtual();
}

void Remapper::handleHat(int code, int value)
{
    if (m_modeHeld) {
        if (code == ABS_HAT0X && value != 0) {
            m_comboUsed = true;
            emit profileStep(value);
        }
        // Nothing the d-pad does while PS is held reaches the game.
        return;
    }
    if (code == ABS_HAT0X) {
        setPhysical(DpadLeft, value < 0);
        setPhysical(DpadRight, value > 0);
    } else {
        setPhysical(DpadUp, value < 0);
        setPhysical(DpadDown, value > 0);
    }
    updateVirtual();
}

// The rate counts reports over the last half second. A resting controller
// sends nothing, so the last rate measured while moving is kept.
void Remapper::noteReport(double now)
{
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
    const Profile &profile = readerProfile();
    const QPointF leftRaw(m_raw[LX], m_raw[LY]), rightRaw(m_raw[RX], m_raw[RY]);
    const QPointF leftOut = Curves::stick(leftRaw, profile.left);
    const QPointF rightOut = Curves::stick(rightRaw, profile.right);
    // A button mapped to a trigger presses it all the way.
    const double l2Out = (m_gameButtons & (1u << Profile::FullL2)) ? 1.0 : Curves::trigger(m_raw[L2], profile.l2);
    const double r2Out = (m_gameButtons & (1u << Profile::FullR2)) ? 1.0 : Curves::trigger(m_raw[R2], profile.r2);

    if (m_flipTime >= 0) {
        const double since = m_lastEventTime - m_flipTime;
        if (since > 0.6) {
            m_flipTime = -1;
        } else if ((m_flipKind == 1 && leftOut.y() < -0.6) || (m_flipKind == 2 && leftOut.y() > 0.6)) {
            emit flipCancel(since * 1000.0, leftOut.x(), leftOut.y());
            m_flipTime = -1;
        }
    }
    {
        QMutexLocker lock(&m_mutex);
        m_live = {leftRaw, leftOut, rightRaw, rightOut, m_raw[L2], l2Out, m_raw[R2], r2Out, m_gameButtons & 0x7fffu, m_rate};
    }
    if (!m_liveDirty.exchange(true, std::memory_order_relaxed))
        emit liveUpdated();

    if (!hasVirtual()) {
        discardPending();
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
    sendPending();
}
