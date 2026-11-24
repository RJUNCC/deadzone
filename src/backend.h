#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
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
#include "session.h"
#include "history.h"
#include "overlay.h"

class QThread;
class Remapper;
#include "remapper.h"

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
    Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY gameChanged)
    Q_PROPERTY(bool onlyWhileGame READ onlyWhileGame WRITE setOnlyWhileGame NOTIFY gameChanged)
    Q_PROPERTY(bool remapping READ remapping NOTIFY gameChanged)
    Q_PROPERTY(double gameTriggerDeadzone READ gameTriggerDeadzone NOTIFY gameChanged)
    Q_PROPERTY(int gameRevision READ gameRevision NOTIFY gameChanged)
    Q_PROPERTY(bool backgroundAvailable READ backgroundAvailable CONSTANT)
    Q_PROPERTY(bool background READ background WRITE setBackground NOTIFY gameChanged)
    Q_PROPERTY(QString rawHeldBy READ rawHeldBy NOTIFY hideChanged)
    Q_PROPERTY(bool overlayEnabled READ overlayEnabled WRITE setOverlayEnabled NOTIFY overlayChanged)
    Q_PROPERTY(int overlayPort READ overlayPort WRITE setOverlayPort NOTIFY overlayChanged)
    Q_PROPERTY(QString overlayUrl READ overlayUrl NOTIFY overlayChanged)
    Q_PROPERTY(QString overlayError READ overlayError NOTIFY overlayChanged)
    Q_PROPERTY(int overlayClients READ overlayClients NOTIFY overlayChanged)
    Q_PROPERTY(bool comboSwitch READ comboSwitch WRITE setComboSwitch NOTIFY settingsChanged)
    Q_PROPERTY(double rumble READ rumble WRITE setRumble NOTIFY settingsChanged)
    Q_PROPERTY(int reportRate READ reportRate NOTIFY liveChanged)
    Q_PROPERTY(bool bluetooth READ bluetooth NOTIFY controllersChanged)
    Q_PROPERTY(QString profileColor READ profileColor NOTIFY profileChanged)
    Q_PROPERTY(QString switchedTo READ switchedTo NOTIFY switched)
    Q_PROPERTY(QVariantList health READ health NOTIFY historyChanged)
    Q_PROPERTY(QString healthTrend READ healthTrend NOTIFY historyChanged)
    Q_PROPERTY(QString launchOptions READ launchOptions NOTIFY setupChanged)
    Q_PROPERTY(QString suggestedLaunch READ suggestedLaunch NOTIFY setupChanged)
    Q_PROPERTY(bool launchReady READ launchReady NOTIFY setupChanged)
    Q_PROPERTY(bool steamInputOff READ steamInputOff WRITE setSteamInputOff NOTIFY setupChanged)
    Q_PROPERTY(int jumpButton READ jumpButton WRITE setJumpButton NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList flips READ flips NOTIFY flipsChanged)
    Q_PROPERTY(QVariantMap flipStats READ flipStats NOTIFY flipsChanged)
    Q_PROPERTY(int batteryMinutes READ batteryMinutes NOTIFY batteryChanged)
    Q_PROPERTY(bool steamRunning READ steamRunning NOTIFY setupChanged)
    Q_PROPERTY(bool inputRecording READ inputRecording NOTIFY recordingChanged)
    Q_PROPERTY(int inputRecordingSeconds READ inputRecordingSeconds NOTIFY recordingChanged)
    Q_PROPERTY(QVariantList inputRecordings READ inputRecordings NOTIFY recordingChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY sessionChanged)
    Q_PROPERTY(int sessionElapsed READ sessionElapsed NOTIFY sessionChanged)
    Q_PROPERTY(int sessionDuration READ sessionDuration NOTIFY sessionChanged)
    Q_PROPERTY(bool sessionDone READ sessionDone NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList sessionSuggestions READ sessionSuggestions NOTIFY sessionChanged)

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
    explicit Backend(const QString &configPath = {}, bool watchDevices = true, QObject *parent = nullptr,
                     const QString &historyPath = {});
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
    bool gameRunning() const { return m_gameRunning; }
    bool onlyWhileGame() const { return m_onlyWhileGame; }
    void setOnlyWhileGame(bool only);
    bool remapping() const { return m_active && (!m_onlyWhileGame || m_gameRunning); }
    double gameTriggerDeadzone() const { return m_gameTriggerDeadzone; }
    int gameRevision() const { return m_gameRevision; }
    bool backgroundAvailable() const;
    bool background() const { return m_background; }
    void setBackground(bool enabled);
    QString rawHeldBy() const { return m_rawHeldBy; }
    bool overlayEnabled() const { return m_overlayEnabled; }
    void setOverlayEnabled(bool enabled);
    int overlayPort() const { return m_overlayPort; }
    void setOverlayPort(int port);
    QString overlayUrl() const { return QString("http://localhost:%1/").arg(m_overlayPort); }
    QString overlayError() const { return m_overlay.error(); }
    int overlayClients() const { return m_overlay.clients(); }
    bool comboSwitch() const { return m_comboSwitch; }
    void setComboSwitch(bool enabled);
    double rumble() const { return m_rumble; }
    void setRumble(double strength);
    int reportRate() const { return int(std::lround(m_live.rate)); }
    bool bluetooth() const { return m_selected >= 0 && m_selected < m_devices.size() && m_devices.at(m_selected).bluetooth; }
    QString profileColor() const;
    QString switchedTo() const { return m_switchedTo; }
    QVariantList health() const;
    QString healthTrend() const;
    QString launchOptions() const { return m_launchOptions; }
    QString suggestedLaunch() const;
    bool launchReady() const { return m_launchOptions.contains("SDL_GAMECONTROLLER_IGNORE_DEVICES=" + ignoreList()); }
    bool steamInputOff() const { return m_steamInputOff; }
    void setSteamInputOff(bool off);
    // Rocket League's launch options from Steam's localconfig.vdf text.
    static QString readLaunchOptions(const QString &vdf, const QString &app = "252950");
    int jumpButton() const { return m_jumpButton; }
    void setJumpButton(int bit);
    QVariantList flips() const { return m_flips; }
    QVariantMap flipStats() const;
    int batteryMinutes() const;
    bool steamRunning() const { return m_steamRunning; }
    // The vdf text with Rocket League's launch options set, or empty when
    // the game's block is not there.
    static QString withLaunchOptions(const QString &vdf, const QString &options, const QString &app = "252950");
    // A flip as the analyzer sees it, for the list and the stats.
    static QVariantMap describeFlip(double gapMs, double x, double y, double dodge);
    bool inputRecording() const { return m_inputRecording; }
    int inputRecordingSeconds() const { return m_inputRecording ? int(m_inputClock.elapsed() / 1000) : 0; }
    QVariantList inputRecordings() const;
    static QString recordingsDirectory();
    bool recording() const { return m_recording; }
    int sessionElapsed() const { return m_recording ? int(m_sessionClock.elapsed() / 1000) : m_sessionSeconds; }
    int sessionDuration() const { return m_sessionDuration; }
    bool sessionDone() const { return m_sessionDone; }
    QVariantList sessionSuggestions() const { return m_sessionDone ? m_session.suggestions(profile()) : QVariantList(); }
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
    // axis "y" gives the vertical response, which differs with separate curves.
    Q_INVOKABLE double responseAt(const QString &side, double magnitude, const QString &axis = {}) const;

    // Watches real play for a while, then suggests changes.
    Q_INVOKABLE void startSession(int minutes);
    Q_INVOKABLE void stopSession();
    Q_INVOKABLE void clearSession();
    Q_INVOKABLE void applySuggestion(int index);
    Q_INVOKABLE void applyAllSuggestions();
    Q_INVOKABLE QVariantList sessionHeatmap(bool left) const { return m_session.heatmap(left); }
    // For tests: feeds the session as if the stick had moved so.
    void addSessionSample(QPointF left, QPointF right, double l2, double r2) { m_session.add(left, right, l2, r2); }

    // Custom deadzone shape: eight sizes around the stick, clockwise from right.
    Q_INVOKABLE void setRadius(const QString &side, int index, double radius);
    // The deadzone's outline, as points in stick coordinates, for drawing.
    Q_INVOKABLE QVariantList deadzoneOutline(const QString &side) const;
    Q_INVOKABLE QStringList curvePresetIds() const { return Curves::curvePresets(); }
    Q_INVOKABLE QString curvePresetName(const QString &id) const;
    Q_INVOKABLE void applyCurvePreset(const QString &side, const QString &id);
    // The preset the stick's horizontal response matches, or empty.
    Q_INVOKABLE QString activeCurvePreset(const QString &side) const;
    // How far a pure push must travel before games receive at least this
    // much, or -1 if it never does.
    Q_INVOKABLE double travelFor(const QString &side, double output) const;

    // What the player has set inside Rocket League: deadzone, dodge,
    // steering, aerial. Deadzone uses them to show where flips and full
    // steer begin.
    Q_INVOKABLE double gameSetting(const QString &key) const { return m_game.value(key, 0.0); }
    Q_INVOKABLE void setGameSetting(const QString &key, double value);

    Q_INVOKABLE void setProfileColor(const QString &color);
    // A profile as a code to paste anywhere, and adding one back. Returns
    // an empty string on success, or what went wrong.
    Q_INVOKABLE QString exportProfile() const { return Codes::encode(profile()); }
    Q_INVOKABLE QString importProfile(const QString &code);
    // Precision trainer results; returns true for a new best.
    Q_INVOKABLE bool saveTrainerResult(const QString &mode, double milliseconds, int misses);
    Q_INVOKABLE QVariantList trainerHistory(const QString &mode) const;

    // Writes the suggested launch options into Steam's config. Steam must
    // be closed, or it would write its own copy back over the change.
    // Returns an empty string on success, or what went wrong.
    Q_INVOKABLE QString applyLaunchOptions();
    Q_INVOKABLE void clearFlips();
    // Button remapping for the current profile: what each physical button
    // (a LiveButton bit) sends, as Profile::buttons.
    Q_INVOKABLE QVariantList buttonMap() const;
    Q_INVOKABLE void setButtonTarget(int button, int target);
    Q_INVOKABLE void resetButtons();
    // Progress by day for a metric: flipGap, offDiagonal, halfCancel,
    // speedCancel, trainer:flick, trainer:control.
    Q_INVOKABLE QVariantList progress(const QString &metric) const;
    // Records what the controller does, for playing back on the overlay.
    Q_INVOKABLE void startInputRecording();
    Q_INVOKABLE QString stopInputRecording();
    Q_INVOKABLE void deleteInputRecording(const QString &name);
    Q_INVOKABLE QString replayUrl(const QString &name, bool forObs) const;
    // For tests: as if the remapper had seen a cancel.
    void addFlipCancel(double ms, double x, double y) { onFlipCancel(ms, x, y); }
    // For tests: as if the remapper had seen a flip.
    void addFlip(double gapMs, double x, double y) { onFlip(gapMs, x, y); }

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
    // Rocket League's own deadzone on the Xbox triggers, from TAInput.ini.
    static double readTriggerDeadzone(const QString &iniText);
    static QStringList inputIniPaths();
    // Names of other programs holding any of these device nodes open.
    static QStringList holders(const QStringList &nodes, const QString &procDirectory = "/proc");
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
    void gameChanged();
    void sessionChanged();
    void overlayChanged();
    void settingsChanged();
    void switched();
    void historyChanged();
    void setupChanged();
    void flipsChanged();
    void recordingChanged();

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
    void readGameIni();
    void stepProfile(int step);
    void publishOverlay();
    QString controllerId() const;
    QString ignoreList() const;
    void refreshLaunchOptions();
    void onFlip(double gapMs, double x, double y);
    void onFlipCancel(double ms, double x, double y);
    void checkHolders();
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

    LiveState m_live;

    struct {
        QString key;        // "left.drift", "right.edge", or empty.
        QElapsedTimer clock;
        int duration = 0;
        double peak = 0;
        double bins[36] = {};
        QString message;
    } m_calibration;

    bool m_gameRunning = false;
    bool m_onlyWhileGame = false;
    bool m_background = false;
    double m_gameTriggerDeadzone = -1;
    int m_gameRevision = 0;
    QHash<QString, double> m_game{{"deadzone", 0.0}, {"dodge", 0.5}, {"steering", 1.0}, {"aerial", 1.0}};
    QString m_rawHeldBy;
    OverlayServer m_overlay;
    History m_history;
    bool m_overlayEnabled = false, m_comboSwitch = true, m_lowBatteryWarned = false;
    int m_overlayPort = 7337;
    double m_rumble = 1.0;
    QString m_launchOptions;
    bool m_steamInputOff = false;
    bool m_steamRunning = false;
    int m_jumpButton = 0;
    QVariantList m_flips;
    bool m_inputRecording = false;
    QElapsedTimer m_inputClock;
    QJsonArray m_inputSamples;
    QList<QPair<qint64, int>> m_drops;   // When the battery reached each lower percentage.
    double m_drain = 0;
    QString m_switchedTo;
    QTimer m_switchedClear;
    Session m_session;
    bool m_recording = false, m_sessionDone = false;
    int m_sessionDuration = 300, m_sessionSeconds = 0;
    QElapsedTimer m_sessionClock;
    Lightbar m_lightbar;
    int m_battery = -1;
    bool m_charging = false;
    bool m_lightbarRule = false;
    QStringList m_hiddenIds;
    QProcess *m_hider = nullptr;
    QString m_notice, m_hideError;
};
