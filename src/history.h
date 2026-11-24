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

    static QString defaultPath();

private:
    void save() const;

    QString m_path;
    QJsonObject m_data;
};
