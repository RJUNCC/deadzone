#pragma once

#include <QList>
#include <QString>

// A physical controller found under /dev/input.
struct GamepadInfo {
    QString path;       // /dev/input/eventN
    QString name;
    QString id;         // Stable across reconnects: vendor:product plus serial or name.
    QString driver;     // Kernel driver, e.g. "playstation" or "xpad".
    QString hidraw;     // /dev/hidrawN, when the controller has one games could read directly.
    int vendor = 0, product = 0;
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
}
