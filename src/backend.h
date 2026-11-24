#pragma once

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QProcess>
#include <QSet>
#include <QStack>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include "curves.h"
#include "gamepads.h"
#include "lightbar.h"

class QThread;
class Remapper;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList controllers READ controllers NOTIFY controllersChanged)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY controllersChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool hideFromGames READ hideFromGames WRITE setHideFromGames NOTIFY hideChanged)
    Q_PROPERTY(bool hideBusy READ hideBusy NOTIFY hideChanged)
    Q_PROPERTY(bool exposed READ exposed NOTIFY hideChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY hideChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool lightbarAllowed READ lightbarAllowed NOTIFY hideChanged)
    Q_PROPERTY(Lightbar *lightbar READ lightbar CONSTANT)
    Q_PROPERTY(int battery READ battery NOTIFY batteryChanged)
    Q_PROPERTY(bool charging READ charging NOTIFY batteryChanged)

    Q_PROPERTY(int revision READ revision NOTIFY profileChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY profileChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY profileChanged)
    Q_PROPERTY(QStringList profileNames READ profileNames NOTIFY profileChanged)
    Q_PROPERTY(int profileIndex READ profileIndex NOTIFY profileChanged)
    Q_PROPERTY(QString profileMatch READ profileMatch NOTIFY profileChanged)
    Q_PROPERTY(QString profilePreset READ profilePreset NOTIFY profileChanged)
    Q_PROPERTY(QString autoGame READ autoGame NOTIFY profileChanged)

    Q_PROPERTY(QString calibrating READ calibrating NOTIFY calibrationChanged)
    Q_PROPERTY(double calibrationProgress READ calibrationProgress NOTIFY calibrationChanged)
    Q_PROPERTY(QString calibrationMessage READ calibrationMessage NOTIFY calibrationChanged)

    Q_PROPERTY(QPointF leftRaw READ leftRaw NOTIFY liveChanged)
    Q_PROPERTY(QPointF leftOut READ leftOut NOTIFY liveChanged)
    Q_PROPERTY(QPointF rightRaw READ rightRaw NOTIFY liveChanged)
    Q_PROPERTY(QPointF rightOut READ rightOut NOTIFY liveChanged)
    Q_PROPERTY(double l2Raw READ l2Raw NOTIFY liveChanged)
    Q_PROPERTY(double l2Out READ l2Out NOTIFY liveChanged)
    Q_PROPERTY(double r2Raw READ r2Raw NOTIFY liveChanged)
    Q_PROPERTY(double r2Out READ r2Out NOTIFY liveChanged)

public:
    // configPath lets tests run without touching the user's settings;
    // watchDevices false skips controllers, games, and the udev rule.
    explicit Backend(const QString &configPath = {}, bool watchDevices = true, QObject *parent = nullptr);
    ~Backend() override;

    QVariantList controllers() const;
    int selected() const { return m_selected; }
    void setSelected(int index);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool hideFromGames() const { return !m_hiddenIds.isEmpty(); }
    void setHideFromGames(bool hide);
    bool hideBusy() const { return m_hider != nullptr; }
    bool exposed() const;
    QString notice() const { return m_notice; }
    QString error() const;
    bool lightbarAllowed() const { return m_lightbarRule; }
    Lightbar *lightbar() { return &m_lightbar; }
    int battery() const { return m_battery; }
    bool charging() const { return m_charging; }

    int revision() const { return m_revision; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QStringList profileNames() const;
    int profileIndex() const { return m_current; }
    QString profileMatch() const { return profile().match; }
    QString profilePreset() const { return profile().preset; }
    QString autoGame() const { return m_autoIndex >= 0 ? m_profiles.at(m_autoIndex).name : QString(); }
    const Profile &profile() const { return m_profiles.at(m_current); }
    const QList<Profile> &profiles() const { return m_profiles; }

    QString calibrating() const { return m_calibration.key; }
    double calibrationProgress() const;
    QString calibrationMessage() const { return m_calibration.message; }

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
    Q_INVOKABLE double setting(const QString &key) const { return profile().value(key); }
    Q_INVOKABLE void beginEdit();
    Q_INVOKABLE void setSetting(const QString &key, double value);
    Q_INVOKABLE void endEdit();
    Q_INVOKABLE void change(const QString &key, double value);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Puts the profile back to the preset it started from.
    Q_INVOKABLE void resetProfile();

    // The drawn curve for a stick, and how a pure push of a given length
    // comes out, for plotting.
    Q_INVOKABLE QVariantList curvePoints(const QString &side) const;
    Q_INVOKABLE void setCurvePoint(const QString &side, int index, double x, double y);
    Q_INVOKABLE void resetCurve(const QString &side);
    Q_INVOKABLE double responseAt(const QString &side, double magnitude) const;

    Q_INVOKABLE void selectProfile(int index);
    Q_INVOKABLE void newProfile(const QString &preset);
    Q_INVOKABLE void duplicateProfile();
    Q_INVOKABLE void renameProfile(const QString &name);
    Q_INVOKABLE void deleteProfile();
    Q_INVOKABLE void setProfileMatch(const QString &match);
    Q_INVOKABLE QString presetName(const QString &preset) const;
    Q_INVOKABLE QString presetTips(const QString &preset) const;
    Q_INVOKABLE QStringList presetIds() const { return Presets::ids(); }

    // Measures a stick: "drift" while it rests, "edge" while it circles
    // the rim. The result becomes the deadzone or outer edge.
    Q_INVOKABLE void calibrate(const QString &side, const QString &kind);
    Q_INVOKABLE void cancelCalibration();

    // Installs the udev rule that lets deadzone set the lightbar.
    Q_INVOKABLE void allowLightbar();

    Q_INVOKABLE void selectNext();
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void shutdown();

    static QString defaultConfigPath();
    static QString rulePath();
    // The udev rule that keeps games off these controllers' raw HID nodes.
    // With a user name, it also hands that user the lightbar LEDs.
    static QString ruleText(const QStringList &ids, const QString &lightbarUser = {});
    static QStringList ruleIds(const QString &text);
    // Every process whose command line contains one of the matches; the
    // first match found wins. procDirectory is for tests.
    static int runningMatch(const QStringList &matches, const QString &procDirectory = "/proc");

signals:
    void controllersChanged();
    void activeChanged();
    void hideChanged();
    void errorChanged();
    void profileChanged();
    void calibrationChanged();
    void liveChanged();
    void batteryChanged();

private:
    struct Worker {
        GamepadInfo info;
        QThread *thread = nullptr;
        Remapper *remapper = nullptr;
        QString error;
    };
    struct Snapshot {
        QList<Profile> profiles;
        int manual = 0;
    };

    Profile &editable() { return m_profiles[m_current]; }
    void load();
    void save();
    void applyProfile();
    void pushUndo();
    void setCurrent(int index);
    void checkGames();
    void startWorker(const GamepadInfo &info);
    void stopWorker(const QString &id);
    void restartWorkers();
    void pollLive();
    void finishCalibration();
    void readRule();
    void writeRules(const QStringList &ids, bool lightbar);
    void updateBattery();
    QString uniqueName(const QString &base) const;

    QString m_configPath;
    bool m_watchDevices;
    QList<Profile> m_profiles{Profile()};
    int m_manual = 0;       // The profile picked by hand.
    int m_current = 0;      // The profile in use: the manual one, or a running game's.
    int m_autoIndex = -1;   // The game profile in use, if any.
    int m_dismissed = -1;   // A game profile the user switched away from while it ran.
    bool m_active = true;
    bool m_editing = false;
    int m_revision = 0;
    int m_selected = -1;
    QStack<Snapshot> m_undo, m_redo;

    QList<GamepadInfo> m_devices;
    QHash<QString, Worker> m_workers;
    QFileSystemWatcher m_watcher;
    QTimer m_rescan, m_poll, m_fallbackScan, m_games;

    struct {
        QPointF leftRaw, leftOut, rightRaw, rightOut;
        double l2Raw = 0, l2Out = 0, r2Raw = 0, r2Out = 0;
    } m_live;

    struct {
        QString key;        // "left.drift", "right.edge", or empty.
        QElapsedTimer clock;
        int duration = 0;
        double peak = 0;
        double bins[36] = {};
        QString message;
    } m_calibration;

    Lightbar m_lightbar;
    int m_battery = -1;
    bool m_charging = false;
    bool m_lightbarRule = false;
    QStringList m_hiddenIds;
    QProcess *m_hider = nullptr;
    QString m_notice, m_hideError;
};
