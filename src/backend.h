#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QProcess>
#include <QSet>
#include <QStack>
#include <QTimer>
#include <QVariantList>

#include "curves.h"
#include "gamepads.h"

class QThread;
class Remapper;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList controllers READ controllers NOTIFY controllersChanged)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY controllersChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool hideFromGames READ hideFromGames WRITE setHideFromGames NOTIFY hideChanged)
    Q_PROPERTY(bool hiding READ hiding NOTIFY hideChanged)
    Q_PROPERTY(bool exposed READ exposed NOTIFY hideChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(int revision READ revision NOTIFY profileChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY profileChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY profileChanged)
    Q_PROPERTY(QPointF leftRaw READ leftRaw NOTIFY liveChanged)
    Q_PROPERTY(QPointF leftOut READ leftOut NOTIFY liveChanged)
    Q_PROPERTY(QPointF rightRaw READ rightRaw NOTIFY liveChanged)
    Q_PROPERTY(QPointF rightOut READ rightOut NOTIFY liveChanged)
    Q_PROPERTY(double l2Raw READ l2Raw NOTIFY liveChanged)
    Q_PROPERTY(double l2Out READ l2Out NOTIFY liveChanged)
    Q_PROPERTY(double r2Raw READ r2Raw NOTIFY liveChanged)
    Q_PROPERTY(double r2Out READ r2Out NOTIFY liveChanged)

public:
    // configPath and inputDirectory let tests run without touching the
    // user's settings or controllers; watchDevices false skips the scan.
    explicit Backend(const QString &configPath = {}, bool watchDevices = true, QObject *parent = nullptr);
    ~Backend() override;

    QVariantList controllers() const;
    int selected() const { return m_selected; }
    void setSelected(int index);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool hideFromGames() const { return m_hideFromGames; }
    void setHideFromGames(bool hide);
    bool hiding() const { return !m_hidden.isEmpty(); }
    bool exposed() const;
    QString error() const;
    int revision() const { return m_revision; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    const Profile &profile() const { return m_profile; }

    QPointF leftRaw() const { return m_live.leftRaw; }
    QPointF leftOut() const { return m_live.leftOut; }
    QPointF rightRaw() const { return m_live.rightRaw; }
    QPointF rightOut() const { return m_live.rightOut; }
    double l2Raw() const { return m_live.l2Raw; }
    double l2Out() const { return m_live.l2Out; }
    double r2Raw() const { return m_live.r2Raw; }
    double r2Out() const { return m_live.r2Out; }

    // A slider drag is one edit: begin on press, set while moving, end on
    // release, so one undo puts the whole drag back.
    Q_INVOKABLE double setting(const QString &key) const { return m_profile.value(key); }
    Q_INVOKABLE void beginEdit();
    Q_INVOKABLE void setSetting(const QString &key, double value);
    Q_INVOKABLE void endEdit();
    Q_INVOKABLE void change(const QString &key, double value);
    Q_INVOKABLE void resetProfile();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void selectNext();
    Q_INVOKABLE void rescan();
    // Restores anything hidden from games; called before quitting.
    Q_INVOKABLE void shutdown();

    static QString defaultConfigPath();

signals:
    void controllersChanged();
    void activeChanged();
    void hideChanged();
    void errorChanged();
    void profileChanged();
    void liveChanged();

private:
    struct Worker {
        GamepadInfo info;
        QThread *thread = nullptr;
        Remapper *remapper = nullptr;
        QString error;
    };

    void load();
    void save();
    void applyProfile();
    void startWorker(const GamepadInfo &info);
    void stopWorker(const QString &id);
    void restartWorkers();
    void pollLive();
    void updateHiding();
    void runAcl(const QStringList &paths, bool hide, bool wait = false);

    QString m_configPath;
    bool m_watchDevices;
    Profile m_profile;
    bool m_active = true;
    bool m_hideFromGames = false;
    bool m_editing = false;
    int m_revision = 0;
    int m_selected = -1;
    QStack<Profile> m_undo, m_redo;

    QList<GamepadInfo> m_devices;
    QHash<QString, Worker> m_workers;
    QFileSystemWatcher m_watcher;
    QTimer m_rescan, m_poll, m_fallbackScan;

    struct {
        QPointF leftRaw, leftOut, rightRaw, rightOut;
        double l2Raw = 0, l2Out = 0, r2Raw = 0, r2Out = 0;
    } m_live;

    QSet<QString> m_hidden;
    QProcess *m_acl = nullptr;
    bool m_aclAgain = false;
    QString m_aclError;
};
