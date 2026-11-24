#include "history.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMap>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

namespace {

// Enough to see a trend without the file growing forever.
constexpr int keep = 200;

qint64 now(qint64 when)
{
    return when > 0 ? when : QDateTime::currentSecsSinceEpoch();
}

}

History::History(const QString &path) : m_path(path.isEmpty() ? defaultPath() : path)
{
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly))
        m_data = QJsonDocument::fromJson(file.readAll()).object();
}

QString History::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/deadzone/history.json";
}

void History::save() const
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(m_data).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void History::addHealth(const QString &controller, double leftDrift, double rightDrift, double leftReach,
                        double rightReach, qint64 when)
{
    QJsonObject pad = m_data.value(controller).toObject();
    QJsonArray list = pad.value("health").toArray();
    list.append(QJsonObject{{"t", now(when)}, {"leftDrift", leftDrift}, {"rightDrift", rightDrift},
                            {"leftReach", leftReach}, {"rightReach", rightReach}});
    while (list.size() > keep)
        list.removeFirst();
    pad.insert("health", list);
    m_data.insert(controller, pad);
    save();
}

QVariantList History::health(const QString &controller) const
{
    return m_data.value(controller).toObject().value("health").toArray().toVariantList();
}

QString History::trend(const QVariantList &health, const QString &leftName, const QString &rightName)
{
    QStringList parts;
    for (const auto &[key, name] : {std::pair{QStringLiteral("leftDrift"), leftName}, std::pair{QStringLiteral("rightDrift"), rightName}}) {
        double first = -1, last = -1;
        qint64 since = 0;
        for (const QVariant &item : health) {
            const QVariantMap entry = item.toMap();
            const double value = entry.value(key).toDouble();
            if (value < 0)
                continue;
            if (first < 0) {
                first = value;
                since = entry.value("t").toLongLong();
            }
            last = value;
        }
        if (first < 0 || std::abs(last - first) < 0.015)
            continue;
        const QString date = QLocale().toString(QDateTime::fromSecsSinceEpoch(since).date(), QLocale::ShortFormat);
        parts.append((last > first ? QCoreApplication::translate("History", "%1 drift rose from %2% to %3% since %4.")
                                   : QCoreApplication::translate("History", "%1 drift fell from %2% to %3% since %4."))
                         .arg(name).arg(std::lround(first * 100)).arg(std::lround(last * 100)).arg(date));
    }
    return parts.join(' ');
}

bool History::addTrainer(const QString &controller, const QString &mode, double milliseconds, int misses, qint64 when)
{
    QJsonObject pad = m_data.value(controller).toObject();
    QJsonObject trainer = pad.value("trainer").toObject();
    QJsonArray list = trainer.value(mode).toArray();
    bool best = true;
    for (const QJsonValue &v : list)
        if (v.toObject().value("ms").toDouble() <= milliseconds)
            best = false;
    list.append(QJsonObject{{"t", now(when)}, {"ms", milliseconds}, {"misses", misses}});
    while (list.size() > keep)
        list.removeFirst();
    trainer.insert(mode, list);
    pad.insert("trainer", trainer);
    m_data.insert(controller, pad);
    save();
    return best;
}

void History::addDaily(const QString &controller, const QString &metric, double value, qint64 when)
{
    const QString day = QDateTime::fromSecsSinceEpoch(now(when)).date().toString(Qt::ISODate);
    QJsonObject pad = m_data.value(controller).toObject();
    QJsonObject days = pad.value("days").toObject();
    QJsonObject today = days.value(day).toObject();
    QJsonObject entry = today.value(metric).toObject();
    entry.insert("sum", entry.value("sum").toDouble() + value);
    entry.insert("count", entry.value("count").toInt() + 1);
    today.insert(metric, entry);
    days.insert(day, today);
    pad.insert("days", days);
    m_data.insert(controller, pad);
    save();
}

QVariantList History::daily(const QString &controller, const QString &metric) const
{
    QVariantList out;
    if (metric.startsWith("trainer:")) {
        // The best run of each day.
        QMap<QString, double> best;
        for (const QVariant &item : trainer(controller, metric.mid(8))) {
            const QVariantMap run = item.toMap();
            const QString day = QDateTime::fromSecsSinceEpoch(run.value("t").toLongLong()).date().toString(Qt::ISODate);
            const double ms = run.value("ms").toDouble();
            if (!best.contains(day) || ms < best.value(day))
                best.insert(day, ms);
        }
        for (auto it = best.cbegin(); it != best.cend(); ++it)
            out.append(QVariantMap{{"day", it.key()}, {"value", it.value()}});
        return out;
    }
    const QJsonObject days = m_data.value(controller).toObject().value("days").toObject();
    QStringList keys = days.keys();
    keys.sort();
    for (const QString &day : keys) {
        const QJsonObject entry = days.value(day).toObject().value(metric).toObject();
        const int count = entry.value("count").toInt();
        if (count > 0)
            out.append(QVariantMap{{"day", day}, {"value", entry.value("sum").toDouble() / count}, {"count", count}});
    }
    return out;
}

void History::setDrain(const QString &controller, double percentPerHour)
{
    QJsonObject pad = m_data.value(controller).toObject();
    pad.insert("drain", percentPerHour);
    m_data.insert(controller, pad);
    save();
}

double History::drain(const QString &controller) const
{
    return m_data.value(controller).toObject().value("drain").toDouble(0);
}

QVariantList History::trainer(const QString &controller, const QString &mode) const
{
    return m_data.value(controller).toObject().value("trainer").toObject().value(mode).toArray().toVariantList();
}
