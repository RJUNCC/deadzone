// The Windows side of the remapper: reads the controller through SDL 3 and
// replays it as a virtual Xbox 360 pad through the ViGEmBus driver, with
// rumble from games passed back to the controller.

#include "remapper.h"

#include "gamepads.h"

#include <QElapsedTimer>
#include <QMutexLocker>
#include <QRecursiveMutex>
#include <QTimer>

#include <SDL3/SDL.h>

#include <windows.h>
#include <timeapi.h>
#include <ViGEm/Client.h>

#include <algorithm>
#include <iterator>

namespace {

// SDL's buttons, by position, as the Xbox pad's keys.
struct ButtonMap { SDL_GamepadButton sdl; int code; };
constexpr ButtonMap buttonMap[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, BTN_A},          {SDL_GAMEPAD_BUTTON_EAST, BTN_B},
    {SDL_GAMEPAD_BUTTON_WEST, BTN_X},           {SDL_GAMEPAD_BUTTON_NORTH, BTN_Y},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, BTN_TL}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, BTN_TR},
    {SDL_GAMEPAD_BUTTON_BACK, BTN_SELECT},      {SDL_GAMEPAD_BUTTON_START, BTN_START},
    {SDL_GAMEPAD_BUTTON_GUIDE, BTN_MODE},       {SDL_GAMEPAD_BUTTON_LEFT_STICK, BTN_THUMBL},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, BTN_THUMBR},
};

constexpr SDL_GamepadAxis axisMap[] = {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX,
                                       SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
                                       SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};

quint16 xusbButton(int code)
{
    switch (code) {
    case BTN_A: return XUSB_GAMEPAD_A;
    case BTN_B: return XUSB_GAMEPAD_B;
    case BTN_X: return XUSB_GAMEPAD_X;
    case BTN_Y: return XUSB_GAMEPAD_Y;
    case BTN_TL: return XUSB_GAMEPAD_LEFT_SHOULDER;
    case BTN_TR: return XUSB_GAMEPAD_RIGHT_SHOULDER;
    case BTN_SELECT: return XUSB_GAMEPAD_BACK;
    case BTN_START: return XUSB_GAMEPAD_START;
    case BTN_MODE: return XUSB_GAMEPAD_GUIDE;
    case BTN_THUMBL: return XUSB_GAMEPAD_LEFT_THUMB;
    case BTN_THUMBR: return XUSB_GAMEPAD_RIGHT_THUMB;
    default: return 0;
    }
}

qint16 stick(int value)
{
    return qint16(std::clamp(value, -32767, 32767));
}

double seconds()
{
    return double(SDL_GetTicksNS()) / 1e9;
}

// ViGEmBus calls this on its own thread when a game sets the motors.
VOID CALLBACK onX360Notification(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR largeMotor, UCHAR smallMotor, UCHAR, LPVOID self)
{
    static_cast<Remapper *>(self)->applyRumble(largeMotor, smallMotor);
}

}

void Remapper::start()
{
    Gamepads::initSdl();
    const SDL_JoystickID instance = SDL_JoystickID(m_path.mid(4).toUInt());
    {
        QMutexLocker lock(&Gamepads::sdlLock());
        m_pad = SDL_OpenGamepad(instance);
    }
    if (!m_pad) {
        emit failed(tr("Could not open the controller: %1").arg(QString::fromUtf8(SDL_GetError())));
        return;
    }

    // SDL gives sticks as -32768..32767 with down positive, like evdev, and
    // triggers as 0..32767.
    for (int axis = 0; axis < AxisCount; ++axis)
        m_ranges[axis] = axis == L2 || axis == R2 ? Range{0, 32767, true} : Range{-32768, 32767, true};
    resync();

    if (m_active && !createVirtual(true)) {
        QMutexLocker lock(&Gamepads::sdlLock());
        SDL_CloseGamepad(m_pad);
        m_pad = nullptr;
        return;
    }

    // SDL is polled once a millisecond, as fast as a controller reports.
    timeBeginPeriod(1);
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(1);
    connect(m_timer, &QTimer::timeout, this, &Remapper::poll);
    m_timer->start();
    flush();
    emit started(m_active);
}

bool Remapper::createVirtual(bool rumble)
{
    PVIGEM_CLIENT client = vigem_alloc();
    if (!client) {
        emit failed(tr("Could not create the virtual controller: out of memory."));
        return false;
    }
    VIGEM_ERROR error = vigem_connect(client);
    if (!VIGEM_SUCCESS(error)) {
        vigem_free(client);
        emit failed(error == VIGEM_ERROR_BUS_NOT_FOUND
                        ? tr("The ViGEmBus driver is not installed, so deadzone cannot create its virtual Xbox pad. Install it from github.com/nefarius/ViGEmBus/releases, then open deadzone again.")
                        : tr("Could not connect to the ViGEmBus driver (error 0x%1).").arg(quint32(error), 0, 16));
        return false;
    }
    PVIGEM_TARGET target = vigem_target_x360_alloc();
    error = vigem_target_add(client, target);
    if (!VIGEM_SUCCESS(error)) {
        vigem_target_free(target);
        vigem_disconnect(client);
        vigem_free(client);
        emit failed(tr("Could not create the virtual controller (ViGEmBus error 0x%1).").arg(quint32(error), 0, 16));
        return false;
    }
    m_client = client;
    m_target = target;
    if (rumble)
        vigem_target_x360_register_notification(client, target, &onX360Notification, this);
    m_report = {};
    m_reportDirty = true;
    return true;
}

void Remapper::applyRumble(int strong, int weak)
{
    const double strength = std::clamp(m_rumble.load(), 0.0, 1.0);
    QMutexLocker lock(&Gamepads::sdlLock());
    if (m_pad)
        // A duration of zero keeps the motors going until the game changes them.
        SDL_RumbleGamepad(m_pad, Uint16(strong * 257 * strength), Uint16(weak * 257 * strength), 0);
}

void Remapper::stop()
{
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
        timeEndPeriod(1);
    }
    if (m_target) {
        auto *client = static_cast<PVIGEM_CLIENT>(m_client);
        auto *target = static_cast<PVIGEM_TARGET>(m_target);
        vigem_target_x360_unregister_notification(target);
        vigem_target_remove(client, target);
        vigem_target_free(target);
        vigem_disconnect(client);
        vigem_free(client);
        m_client = m_target = nullptr;
    }
    if (m_pad) {
        QMutexLocker lock(&Gamepads::sdlLock());
        SDL_RumbleGamepad(m_pad, 0, 0, 0);
        SDL_CloseGamepad(m_pad);
        m_pad = nullptr;
    }
}

void Remapper::resync()
{
    QMutexLocker lock(&Gamepads::sdlLock());
    for (int axis = 0; axis < AxisCount; ++axis) {
        m_lastAxes[axis] = SDL_GetGamepadAxis(m_pad, axisMap[axis]);
        m_raw[axis] = normalize(axis, m_lastAxes[axis]);
    }
}

// Reads what changed since the last poll and handles it as the Linux side
// handles a report from the kernel.
void Remapper::poll()
{
    int axes[AxisCount];
    quint32 buttons = 0;
    bool up, down, left, right;
    {
        QMutexLocker lock(&Gamepads::sdlLock());
        SDL_UpdateGamepads();
        if (!SDL_GamepadConnected(m_pad)) {
            m_timer->stop();
            emit disconnected();
            return;
        }
        for (int axis = 0; axis < AxisCount; ++axis)
            axes[axis] = SDL_GetGamepadAxis(m_pad, axisMap[axis]);
        for (int i = 0; i < int(std::size(buttonMap)); ++i)
            if (SDL_GetGamepadButton(m_pad, buttonMap[i].sdl))
                buttons |= 1u << i;
        up = SDL_GetGamepadButton(m_pad, SDL_GAMEPAD_BUTTON_DPAD_UP);
        down = SDL_GetGamepadButton(m_pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        left = SDL_GetGamepadButton(m_pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        right = SDL_GetGamepadButton(m_pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    }

    bool changed = false;
    m_lastEventTime = seconds();
    for (int axis = 0; axis < AxisCount; ++axis) {
        if (axes[axis] != m_lastAxes[axis]) {
            m_lastAxes[axis] = axes[axis];
            m_raw[axis] = normalize(axis, axes[axis]);
            changed = true;
        }
    }
    const quint32 flipped = buttons ^ m_lastButtons;
    m_lastButtons = buttons;
    for (int i = 0; i < int(std::size(buttonMap)); ++i) {
        if (flipped & (1u << i)) {
            handleKey(buttonMap[i].code, (buttons >> i) & 1);
            changed = true;
        }
    }
    const int hatX = right ? 1 : left ? -1 : 0, hatY = down ? 1 : up ? -1 : 0;
    if (hatX != m_hatX) {
        m_hatX = hatX;
        handleHat(ABS_HAT0X, hatX);
        changed = true;
    }
    if (hatY != m_hatY) {
        m_hatY = hatY;
        handleHat(ABS_HAT0Y, hatY);
        changed = true;
    }
    if (!changed)
        return;
    noteReport(m_lastEventTime);
    flush();
}

void Remapper::emitEvent(int type, int code, int value)
{
    if (type == EV_SYN) {
        sendPending();
        return;
    }
    auto &r = m_report;
    if (type == EV_KEY) {
        const quint16 bit = xusbButton(code);
        r.buttons = value ? (r.buttons | bit) : (r.buttons & ~bit);
    } else if (type == EV_ABS) {
        switch (code) {
        // XInput counts up as positive; evdev, and so the remapper, down.
        case ABS_X: r.lx = stick(value); break;
        case ABS_Y: r.ly = stick(-value); break;
        case ABS_RX: r.rx = stick(value); break;
        case ABS_RY: r.ry = stick(-value); break;
        case ABS_Z: r.leftTrigger = quint8(std::clamp(value, 0, 255)); break;
        case ABS_RZ: r.rightTrigger = quint8(std::clamp(value, 0, 255)); break;
        case ABS_HAT0X:
            r.buttons &= ~(XUSB_GAMEPAD_DPAD_LEFT | XUSB_GAMEPAD_DPAD_RIGHT);
            r.buttons |= value < 0 ? XUSB_GAMEPAD_DPAD_LEFT : value > 0 ? XUSB_GAMEPAD_DPAD_RIGHT : 0;
            break;
        case ABS_HAT0Y:
            r.buttons &= ~(XUSB_GAMEPAD_DPAD_UP | XUSB_GAMEPAD_DPAD_DOWN);
            r.buttons |= value < 0 ? XUSB_GAMEPAD_DPAD_UP : value > 0 ? XUSB_GAMEPAD_DPAD_DOWN : 0;
            break;
        default: return;
        }
    }
    m_reportDirty = true;
}

bool Remapper::hasVirtual() const
{
    return m_target != nullptr;
}

void Remapper::discardPending()
{
    m_reportDirty = false;
}

void Remapper::sendPending()
{
    if (!m_reportDirty || !m_target)
        return;
    XUSB_REPORT report{};
    report.wButtons = m_report.buttons;
    report.bLeftTrigger = m_report.leftTrigger;
    report.bRightTrigger = m_report.rightTrigger;
    report.sThumbLX = m_report.lx;
    report.sThumbLY = m_report.ly;
    report.sThumbRX = m_report.rx;
    report.sThumbRY = m_report.ry;
    if (!VIGEM_SUCCESS(vigem_target_x360_update(static_cast<PVIGEM_CLIENT>(m_client), static_cast<PVIGEM_TARGET>(m_target), report)))
        qWarning("deadzone: the virtual controller did not take a report");
    m_reportDirty = false;
}
