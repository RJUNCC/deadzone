#include "gamepads.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <linux/input.h>
#include <unistd.h>

namespace {

// sysfs prints a capability bitmap as hex words, most significant first,
// each as wide as the kernel's unsigned long.
bool hasBit(const QString &bitmap, int bit)
{
    const QStringList words = bitmap.split(' ', Qt::SkipEmptyParts);
    constexpr int wordBits = sizeof(unsigned long) * 8;
    const int index = words.size() - 1 - bit / wordBits;
    if (index < 0)
        return false;
    return (words.at(index).toULongLong(nullptr, 16) >> (bit % wordBits)) & 1ULL;
}

QString readSys(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

}

namespace Gamepads {

bool positionalFaceButtons(const QString &driver)
{
    return driver == "playstation" || driver == "sony" || driver == "nintendo";
}

QList<GamepadInfo> scan(const QString &inputDirectory, const QString &sysClass)
{
    QList<GamepadInfo> found;
    const auto entries = QDir(inputDirectory).entryList({"event*"}, QDir::System | QDir::Files, QDir::Name);
    for (const QString &entry : entries) {
        const QString sys = sysClass + "/" + entry + "/device";
        // uinput devices live under /devices/virtual; real controllers never do.
        if (QFileInfo(sys).canonicalFilePath().contains("/virtual/"))
            continue;

        GamepadInfo info;
        info.path = inputDirectory + "/" + entry;
        info.name = readSys(sys + "/name");
        info.vendor = readSys(sys + "/id/vendor").toInt(nullptr, 16);
        info.product = readSys(sys + "/id/product").toInt(nullptr, 16);
        info.bluetooth = readSys(sys + "/id/bustype").toInt(nullptr, 16) == 0x05;
        info.driver = QFileInfo(QFileInfo(sys + "/device/driver").symLinkTarget()).fileName();
        const QString uniq = readSys(sys + "/uniq");
        info.id = QString("%1:%2:%3").arg(info.vendor, 4, 16, QChar('0')).arg(info.product, 4, 16, QChar('0'))
                      .arg(uniq.isEmpty() ? info.name : uniq);
        const auto hidraws = QDir(sys + "/device/hidraw").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (!hidraws.isEmpty())
            info.hidraw = "/dev/" + hidraws.first();
        // The DualShock 4's lightbar is three LEDs named after the input
        // device: inputN:red, inputN:green, inputN:blue.
        const auto leds = QDir(sys + "/device/leds").entryList({"*:red"}, QDir::Dirs | QDir::NoDotAndDotDot);
        if (!leds.isEmpty())
            info.lightbar = QDir(sys + "/device/leds/" + leds.first()).absolutePath();
        const auto supplies = QDir(sys + "/device/power_supply").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (!supplies.isEmpty())
            info.battery = QDir(sys + "/device/power_supply/" + supplies.first()).absolutePath();

        // Capabilities come from sysfs, readable without access to the
        // device, so motion sensors and touchpads that share a controller's
        // name never pass for gamepads.
        const bool sticks = hasBit(readSys(sys + "/capabilities/abs"), ABS_X) && hasBit(readSys(sys + "/capabilities/abs"), ABS_Y);
        const QString keys = readSys(sys + "/capabilities/key");
        if (!sticks || !(hasBit(keys, BTN_SOUTH) || hasBit(keys, BTN_GAMEPAD)))
            continue;
        // One remapper per controller even if it exposes two gamepad nodes.
        if (std::any_of(found.cbegin(), found.cend(), [&](const GamepadInfo &other) { return other.id == info.id; }))
            continue;
        info.readable = ::access(QFile::encodeName(info.path).constData(), R_OK) == 0;
        found.append(info);
    }
    return found;
}

}
