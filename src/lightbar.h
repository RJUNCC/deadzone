#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>

// Drives the lightbar of every connected DualShock 4 through the kernel's
// LED files (inputN:red, :green, :blue). Writing them needs the udev rule
// deadzone can install; until then the window says so.
class Lightbar : public QObject {
    Q_OBJECT
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY changed)
    Q_PROPERTY(double brightness READ brightness WRITE setBrightness NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY targetsChanged)
    Q_PROPERTY(bool writable READ writable NOTIFY targetsChanged)
    Q_PROPERTY(QColor current READ current NOTIFY currentChanged)

public:
    // Off leaves the controller's own color alone.
    enum Mode { Off = 0, Solid = 1, Rainbow = 2, Battery = 3 };

    explicit Lightbar(QObject *parent = nullptr);

    int mode() const { return m_mode; }
    void setMode(int mode);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    double speed() const { return m_speed; }
    void setSpeed(double speed);
    double brightness() const { return m_brightness; }
    void setBrightness(double brightness);
    bool available() const { return !m_targets.isEmpty(); }
    bool writable() const;
    QColor current() const { return m_current; }

    // Each target is a lightbar's red LED directory.
    void setTargets(const QStringList &targets);
    void setBattery(int percent);
    // Puts back the colors the controllers had before deadzone changed them.
    void restore();
    // Files the udev rule must hand to the user, for the lightbars present now.
    QStringList files() const;

    QJsonObject toJson() const;
    void load(const QJsonObject &object);

    static QColor rainbowAt(double seconds, double speed);
    static QColor batteryColor(int percent);
    static QString channel(const QString &target, const QString &name);

signals:
    void changed();
    void targetsChanged();
    void currentChanged();

private:
    void apply();
    void write(const QColor &color);

    int m_mode = Off;
    QColor m_color = QColor("#7c3aed");
    double m_speed = 0.25;        // Trips round the color wheel per second.
    double m_brightness = 1.0;
    int m_battery = -1;
    QStringList m_targets;
    QHash<QString, QColor> m_original, m_written;
    QColor m_current;
    QTimer m_tick;
    QElapsedTimer m_clock;
};
