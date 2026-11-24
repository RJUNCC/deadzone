#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>

// What deadzone has measured about each controller over time: stick drift
// and reach from session checks and Measure buttons, and precision trainer
// scores. Kept per controller, since two pads age differently.
class History {
public:
    explicit History(const QString &path = {});

    // A health reading; -1 leaves a value unmeasured.
    void addHealth(const QString &controller, double leftDrift, double rightDrift, double leftReach, double rightReach,
                   qint64 when = 0);
    QVariantList health(const QString &controller) const;
    // One sentence on how the left and right sticks have changed, or empty.
    static QString trend(const QVariantList &health, const QString &leftName, const QString &rightName);

    // Returns true when this is the best (fastest) result so far.
    bool addTrainer(const QString &controller, const QString &mode, double milliseconds, int misses, qint64 when = 0);
    QVariantList trainer(const QString &controller, const QString &mode) const;

    // How fast the battery drained last time, in percent an hour; 0 if unknown.
    void setDrain(const QString &controller, double percentPerHour);
    double drain(const QString &controller) const;

    // Adds one measurement to today's totals for a metric (flipGap,
    // offDiagonal, halfCancel, speedCancel), so progress can be charted by day.
    void addDaily(const QString &controller, const QString &metric, double value, qint64 when = 0);
    // Per day, oldest first: {day: "2026-11-24", value: average}. Trainer
    // modes ("trainer:flick") give the day's best time instead.
    QVariantList daily(const QString &controller, const QString &metric) const;

    static QString defaultPath();

private:
    void save() const;

    QString m_path;
    QJsonObject m_data;
};
