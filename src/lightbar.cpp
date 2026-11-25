#include "lightbar.h"

#include <QFile>
#include <algorithm>
#include <cmath>

#include "gamepads.h"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

int readValue(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed().toInt() : 0;
}

bool writeValue(const QString &path, int value)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QByteArray::number(value)) > 0;
}

// On Windows a target is a controller SDL drives, "sdl:<instance id>".
bool viaSdl(const QString &target)
{
    return target.startsWith(QLatin1String("sdl:"));
}

// A multicolor LED (DualSense) takes "r g b" in one file; the DualShock 4
// has a file per color.
bool multicolor(const QString &target)
{
    return !target.endsWith(":red");
}

QColor readColor(const QString &target)
{
    // SDL cannot read a lightbar back, so there is no color to restore.
    if (viaSdl(target))
        return {};
    if (multicolor(target)) {
        QFile file(target + "/multi_intensity");
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const QList<QByteArray> parts = file.readAll().trimmed().split(' ');
        return parts.size() == 3 ? QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt()) : QColor();
    }
    return QColor(readValue(Lightbar::channel(target, "red")), readValue(Lightbar::channel(target, "green")),
                  readValue(Lightbar::channel(target, "blue")));
}

bool writeColor(const QString &target, const QColor &color, const QColor &last, bool fresh)
{
#ifdef _WIN32
    if (viaSdl(target))
        return Gamepads::setLed(target, color);
#endif
    if (multicolor(target)) {
        QFile file(target + "/multi_intensity");
        const bool ok = file.open(QIODevice::WriteOnly)
                        && file.write(QString("%1 %2 %3").arg(color.red()).arg(color.green()).arg(color.blue()).toLatin1()) > 0;
        if (fresh)
            writeValue(target + "/brightness", readValue(target + "/max_brightness") > 0 ? readValue(target + "/max_brightness") : 255);
        return ok;
    }
    bool ok = true;
    if (fresh || last.red() != color.red()) ok &= writeValue(Lightbar::channel(target, "red"), color.red());
    if (fresh || last.green() != color.green()) ok &= writeValue(Lightbar::channel(target, "green"), color.green());
    if (fresh || last.blue() != color.blue()) ok &= writeValue(Lightbar::channel(target, "blue"), color.blue());
    if (fresh && QFile::exists(Lightbar::channel(target, "global")))
        writeValue(Lightbar::channel(target, "global"), 1);
    return ok;
}

}

Lightbar::Lightbar(QObject *parent) : QObject(parent)
{
    // 25 updates a second is smooth to the eye and light on Bluetooth.
    m_tick.setInterval(40);
    connect(&m_tick, &QTimer::timeout, this, &Lightbar::apply);
    m_clock.start();
    m_flash.setInterval(180);
    connect(&m_flash, &QTimer::timeout, this, [this] {
        if (--m_flashSteps <= 0) {
            m_flash.stop();
            m_written.clear();
            if (m_mode == Off)
                restore();
            else
                apply();
            return;
        }
        m_written.clear();
        write(m_flashSteps % 2 ? QColor(Qt::black) : m_flashColor);
    });
}

QString Lightbar::channel(const QString &target, const QString &name)
{
    QString base = target;
    base.chop(QStringLiteral("red").size());
    return base + name + "/brightness";
}

void Lightbar::setMode(int mode)
{
    mode = std::clamp(mode, 0, 4);
    if (mode == m_mode)
        return;
    m_mode = mode;
    if (m_mode == Off)
        restore();
    m_written.clear();
    apply();
    emit changed();
}

void Lightbar::setColor(const QColor &color)
{
    if (!color.isValid() || color == m_color)
        return;
    m_color = color;
    apply();
    emit changed();
}

void Lightbar::setSpeed(double speed)
{
    speed = std::clamp(speed, 0.02, 2.0);
    if (qFuzzyCompare(speed, m_speed))
        return;
    m_speed = speed;
    emit changed();
}

void Lightbar::setBrightness(double brightness)
{
    brightness = std::clamp(brightness, 0.05, 1.0);
    if (qFuzzyCompare(brightness, m_brightness))
        return;
    m_brightness = brightness;
    apply();
    emit changed();
}

bool Lightbar::writable() const
{
    for (const QString &target : m_targets) {
        if (viaSdl(target))
            continue;
#ifndef _WIN32
        const QString file = multicolor(target) ? target + "/multi_intensity" : channel(target, "red");
        if (::access(QFile::encodeName(file).constData(), W_OK) != 0)
            return false;
#endif
    }
    return !m_targets.isEmpty();
}

void Lightbar::setTargets(const QStringList &targets)
{
    if (targets == m_targets)
        return;
    // Remember each controller's own color the first time it is seen, so
    // turning the lightbar off or quitting gives it back.
    for (const QString &target : targets)
        if (!m_original.contains(target))
            m_original.insert(target, readColor(target));
    m_targets = targets;
    m_written.clear();
    apply();
    emit targetsChanged();
}

void Lightbar::setBattery(int percent)
{
    if (percent == m_battery)
        return;
    m_battery = percent;
    if (m_mode == Battery)
        apply();
}

void Lightbar::setProfileColor(const QColor &color)
{
    if (!color.isValid() || color == m_profileColor)
        return;
    m_profileColor = color;
    if (m_mode == ProfileColor)
        apply();
}

void Lightbar::flash(const QColor &color, int times)
{
    if (m_targets.isEmpty())
        return;
    m_flashColor = color;
    m_flashSteps = times * 2;
    m_written.clear();
    write(color);
    m_flash.start();
}

QColor Lightbar::rainbowAt(double seconds, double speed)
{
    const double hue = std::fmod(seconds * speed, 1.0);
    return QColor::fromHsvF(hue < 0 ? hue + 1 : hue, 1.0, 1.0);
}

// Red when empty, through amber, to green when full.
QColor Lightbar::batteryColor(int percent)
{
    const double charge = std::clamp(percent, 0, 100) / 100.0;
    return QColor::fromHsvF(charge * 0.33, 1.0, 1.0);
}

void Lightbar::apply()
{
    if (m_flash.isActive())
        return;
    if (m_mode == Off) {
        m_tick.stop();
        return;
    }
    if (m_mode == Rainbow && !m_tick.isActive())
        m_tick.start();
    else if (m_mode != Rainbow)
        m_tick.stop();

    QColor color = m_mode == Solid ? m_color
                 : m_mode == Rainbow ? rainbowAt(m_clock.elapsed() / 1000.0, m_speed)
                 : m_mode == ProfileColor ? m_profileColor
                                     : batteryColor(m_battery < 0 ? 100 : m_battery);
    color = QColor::fromRgbF(color.redF() * m_brightness, color.greenF() * m_brightness, color.blueF() * m_brightness);
    write(color);
}

void Lightbar::write(const QColor &color)
{
    if (color != m_current) {
        m_current = color;
        emit currentChanged();
    }
    for (const QString &target : std::as_const(m_targets)) {
        if (m_written.value(target) == color)
            continue;
        // Only the channels that changed, to keep reports to the controller few.
        const bool fresh = !m_written.contains(target);
        const bool ok = writeColor(target, color, m_written.value(target), fresh);
        if (ok)
            m_written.insert(target, color);
    }
}

void Lightbar::restore()
{
    m_tick.stop();
    for (const QString &target : std::as_const(m_targets)) {
        const QColor original = m_original.value(target);
        if (!original.isValid())
            continue;
        writeColor(target, original, QColor(), true);
    }
    m_written.clear();
}

QStringList Lightbar::files() const
{
    QStringList files;
    for (const QString &target : m_targets) {
        if (viaSdl(target))
            continue;
        if (multicolor(target)) {
            files << target + "/multi_intensity" << target + "/brightness";
            continue;
        }
        for (const char *name : {"red", "green", "blue", "global"})
            files.append(channel(target, name));
    }
    return files;
}

QJsonObject Lightbar::toJson() const
{
    const char *modes[] = {"off", "color", "rainbow", "battery", "profile"};
    return {{"mode", modes[m_mode]}, {"color", m_color.name()}, {"speed", m_speed}, {"brightness", m_brightness}};
}

void Lightbar::load(const QJsonObject &o)
{
    const QString mode = o.value("mode").toString("off");
    m_mode = mode == "color" ? Solid : mode == "rainbow" ? Rainbow : mode == "battery" ? Battery
           : mode == "profile" ? ProfileColor : Off;
    const QColor color(o.value("color").toString());
    if (color.isValid())
        m_color = color;
    m_speed = std::clamp(o.value("speed").toDouble(m_speed), 0.02, 2.0);
    m_brightness = std::clamp(o.value("brightness").toDouble(m_brightness), 0.05, 1.0);
}
