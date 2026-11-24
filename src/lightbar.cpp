#include "lightbar.h"

#include <QFile>
#include <algorithm>
#include <cmath>
#include <unistd.h>

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
    for (const QString &target : m_targets)
        if (::access(QFile::encodeName(channel(target, "red")).constData(), W_OK) != 0)
            return false;
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
            m_original.insert(target, QColor(readValue(channel(target, "red")), readValue(channel(target, "green")),
                                             readValue(channel(target, "blue"))));
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
        const QColor last = m_written.value(target);
        bool ok = true;
        if (fresh || last.red() != color.red()) ok &= writeValue(channel(target, "red"), color.red());
        if (fresh || last.green() != color.green()) ok &= writeValue(channel(target, "green"), color.green());
        if (fresh || last.blue() != color.blue()) ok &= writeValue(channel(target, "blue"), color.blue());
        if (fresh && QFile::exists(channel(target, "global")))
            writeValue(channel(target, "global"), 1);
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
        writeValue(channel(target, "red"), original.red());
        writeValue(channel(target, "green"), original.green());
        writeValue(channel(target, "blue"), original.blue());
    }
    m_written.clear();
}

QStringList Lightbar::files() const
{
    QStringList files;
    for (const QString &target : m_targets)
        for (const char *name : {"red", "green", "blue", "global"})
            files.append(channel(target, name));
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
