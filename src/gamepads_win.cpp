// Finding controllers on Windows, through SDL 3. SDL reads a DualShock 4 or
// DualSense over USB and Bluetooth directly through HID, and other pads
// through XInput or DirectInput.

#include "gamepads.h"

#include <QMutex>
#include <QRecursiveMutex>

#include <SDL3/SDL.h>

#include <algorithm>
#include <mutex>

namespace {

// The SDL calls deadzone makes come from the window's thread and from each
// remapper's; one lock keeps them from overlapping.
QRecursiveMutex sdlMutex;

SDL_JoystickID instanceOf(const QString &target)
{
    return target.startsWith("sdl:") ? SDL_JoystickID(target.mid(4).toUInt()) : 0;
}

// \\?\HID#VID_054C&PID_09CC&MI_03#7&1a2b3c&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}
// becomes HID\VID_054C&PID_09CC&MI_03\7&1A2B3C&0&0000, the instance id
// HidHide hides. Only HID devices have one.
QString instanceFromPath(const QString &path)
{
    QString id = path;
    if (id.startsWith("\\\\?\\"))
        id = id.mid(4);
    if (!id.startsWith("HID#", Qt::CaseInsensitive))
        return {};
    const qsizetype guid = id.lastIndexOf("#{");
    if (guid > 0)
        id.truncate(guid);
    return id.replace('#', '\\').toUpper();
}

}

namespace Gamepads {

void initSdl()
{
    static std::once_flag once;
    std::call_once(once, [] {
        QMutexLocker lock(&sdlMutex);
        // No window and no event loop of SDL's: its own thread watches for
        // devices, deadzone polls their state, and events are never queued.
        SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
        if (!SDL_Init(SDL_INIT_GAMEPAD))
            qWarning("deadzone: SDL could not start: %s", SDL_GetError());
        SDL_SetJoystickEventsEnabled(false);
        SDL_SetGamepadEventsEnabled(false);
    });
}

QRecursiveMutex &sdlLock()
{
    return sdlMutex;
}

bool positionalFaceButtons(const QString &)
{
    // SDL already names buttons by position.
    return false;
}

QList<GamepadInfo> scan(const QString &, const QString &)
{
    initSdl();
    QMutexLocker lock(&sdlMutex);
    SDL_UpdateGamepads();
    QList<GamepadInfo> found;
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i) {
        const SDL_JoystickID instance = ids[i];
        GamepadInfo info;
        info.vendor = SDL_GetGamepadVendorForID(instance);
        info.product = SDL_GetGamepadProductForID(instance);
        // Deadzone's own virtual pad comes back through XInput as an Xbox
        // 360 pad; skipping that identity keeps the remapper off its output.
        if (info.vendor == 0x045e && info.product == 0x028e)
            continue;
        if (info.vendor == 0 && SDL_GetGamepadTypeForID(instance) == SDL_GAMEPAD_TYPE_XBOX360)
            continue;
        const char *name = SDL_GetGamepadNameForID(instance);
        const char *path = SDL_GetGamepadPathForID(instance);
        info.name = QString::fromUtf8(name ? name : "Controller");
        const QString devicePath = QString::fromUtf8(path ? path : "");
        info.path = info.lightbar = info.battery = QString("sdl:%1").arg(instance);
        info.driver = "sdl";
        info.hidraw = instanceFromPath(devicePath);
        // The Bluetooth HID service's id shows up in a Bluetooth pad's path.
        info.bluetooth = devicePath.contains("00001124-0000-1000-8000-00805f9b34fb", Qt::CaseInsensitive);
        // Only PlayStation pads have a lightbar worth driving.
        if (info.vendor != 0x054c)
            info.lightbar.clear();
        info.id = QString("%1:%2:%3").arg(info.vendor, 4, 16, QChar('0')).arg(info.product, 4, 16, QChar('0'))
                      .arg(devicePath.isEmpty() ? info.name : devicePath.toLower());
        info.readable = true;
        if (std::any_of(found.cbegin(), found.cend(), [&](const GamepadInfo &other) { return other.id == info.id; }))
            continue;
        found.append(info);
    }
    SDL_free(ids);
    return found;
}

void battery(const GamepadInfo &info, int &percent, bool &charging)
{
    percent = -1;
    charging = false;
    QMutexLocker lock(&sdlMutex);
    // Only an open controller reports its charge; its remapper keeps it open.
    SDL_Gamepad *pad = SDL_GetGamepadFromID(instanceOf(info.battery));
    if (!pad)
        return;
    int level = -1;
    const SDL_PowerState state = SDL_GetGamepadPowerInfo(pad, &level);
    if (state == SDL_POWERSTATE_ERROR || state == SDL_POWERSTATE_UNKNOWN || state == SDL_POWERSTATE_NO_BATTERY)
        return;
    percent = level;
    charging = state == SDL_POWERSTATE_CHARGING;
}

bool setLed(const QString &target, const QColor &color)
{
    QMutexLocker lock(&sdlMutex);
    SDL_Gamepad *pad = SDL_GetGamepadFromID(instanceOf(target));
    return pad && SDL_SetGamepadLED(pad, Uint8(color.red()), Uint8(color.green()), Uint8(color.blue()));
}

}
