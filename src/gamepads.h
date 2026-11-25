#pragma once

#include <QList>
#include <QString>

#include <QColor>

class QRecursiveMutex;

// A physical controller: found under /dev/input on Linux, through SDL on
// Windows, where path, lightbar, and battery are "sdl:<instance id>" and
// hidraw is the HID device instance that HidHide hides.
struct GamepadInfo {
    QString path;       // /dev/input/eventN
    QString name;
    QString id;         // Stable across reconnects: vendor:product plus serial or name.
    QString driver;     // Kernel driver, e.g. "playstation" or "xpad".
    QString hidraw;     // /dev/hidrawN, when the controller has one games could read directly.
    QString lightbar;   // The red LED's sysfs directory (green and blue sit beside it), or empty.
    QString battery;    // The battery's power_supply directory, or empty.
    int vendor = 0, product = 0;
    bool bluetooth = false;
    bool readable = false;

    bool operator==(const GamepadInfo &) const = default;
};

namespace Gamepads {
// Every physical gamepad, skipping virtual ones (ours, Steam's) so the
// remapper never feeds on its own output.
QList<GamepadInfo> scan(const QString &inputDirectory = "/dev/input",
                        const QString &sysClass = "/sys/class/input");

// Drivers that report face buttons by position (BTN_NORTH is the top
// button), unlike xpad, whose BTN_X is the left one. Games see an Xbox 360
// pad, so these controllers need their north and west buttons swapped.
bool positionalFaceButtons(const QString &driver);

// The controller's charge, or -1 when it does not say.
void battery(const GamepadInfo &info, int &percent, bool &charging);

#ifdef _WIN32
// Sets a lightbar through SDL; target is GamepadInfo::lightbar.
bool setLed(const QString &target, const QColor &color);
// Starts SDL's gamepad support once, from any thread.
void initSdl();
// Held around every SDL call, which come from several threads.
QRecursiveMutex &sdlLock();
#endif
}
