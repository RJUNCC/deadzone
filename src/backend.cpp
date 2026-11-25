#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>
#include <QDateTime>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include "remapper.h"

#include <algorithm>
#include <cmath>

Backend::Backend(const QString &configPath, bool watchDevices, QObject *parent, const QString &historyPath)
    : QObject(parent), m_configPath(configPath.isEmpty() ? defaultConfigPath() : configPath),
      m_watchDevices(watchDevices),
      m_history(!historyPath.isEmpty() ? historyPath
                : configPath.isEmpty() ? QString() : QFileInfo(configPath).absolutePath() + "/history.json")
{
    connect(&m_overlay, &OverlayServer::clientsChanged, this, &Backend::overlayChanged);
    m_overlay.setRecordingsDirectory(recordingsDirectory());
    // A newly opened overlay gets the current state straight away, not on
    // the next stick movement.
    connect(&m_overlay, &OverlayServer::clientsChanged, this, &Backend::publishOverlay);
    m_switchedClear.setSingleShot(true);
    m_switchedClear.setInterval(2500);
    connect(&m_switchedClear, &QTimer::timeout, this, [this] { m_switchedTo.clear(); emit switched(); });
    load();
    connect(&m_lightbar, &Lightbar::changed, this, &Backend::save);

    // Hotplug shows up as /dev/input changing; access rights land a moment
    // after the node, so wait briefly before looking.
    m_rescan.setSingleShot(true);
    m_rescan.setInterval(400);
    connect(&m_rescan, &QTimer::timeout, this, &Backend::rescan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_rescan.start(); });

    // The window hears about stick movement from the remapper as it happens,
    // at most once a display frame. Steady 60 Hz sampling only runs while
    // something needs it, so an idle background service never wakes up.
    m_poll.setInterval(16);
    m_poll.setTimerType(Qt::PreciseTimer);
    connect(&m_poll, &QTimer::timeout, this, &Backend::pollLive);
    m_liveThrottle.setSingleShot(true);
    m_liveThrottle.setTimerType(Qt::PreciseTimer);
    connect(&m_liveThrottle, &QTimer::timeout, this, &Backend::refreshLive);
    m_liveShown.start();
    connect(this, &Backend::recordingChanged, this, &Backend::updateSampling);
    connect(this, &Backend::sessionChanged, this, &Backend::updateSampling);
    connect(this, &Backend::calibrationChanged, this, &Backend::updateSampling);
    connect(&m_overlay, &OverlayServer::clientsChanged, this, &Backend::updateSampling);
    connect(this, &Backend::controllersChanged, this, &Backend::refreshLive);

    // A slow safety net for permission changes the watcher never sees.
    m_fallbackScan.setInterval(3000);
    connect(&m_fallbackScan, &QTimer::timeout, this, &Backend::rescan);
    connect(&m_fallbackScan, &QTimer::timeout, this, &Backend::updateBattery);
    connect(&m_fallbackScan, &QTimer::timeout, this, &Backend::checkHolders);

    m_games.setInterval(2000);
    connect(&m_games, &QTimer::timeout, this, &Backend::checkGames);

    if (m_watchDevices) {
        readRule();
        readGameIni();
        m_background = queryBackground();
        if (m_overlayEnabled)
            m_overlay.start(m_overlayPort);
        refreshLaunchOptions();
        connect(&m_fallbackScan, &QTimer::timeout, this, &Backend::refreshLaunchOptions);
        startDeviceWatch();
        m_fallbackScan.start();
        m_games.start();
        rescan();
        checkGames();
    }
}

Backend::~Backend()
{
    shutdown();
}

QString Backend::defaultConfigPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/deadzone/config.json";
}

void Backend::load()
{
    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        // First run: a neutral profile, plus Rocket League ready to switch
        // in on its own.
        m_profiles = {Presets::make("rocketleague")};
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    m_active = root.value("active").toBool(true);
    m_lightbar.load(root.value("lightbar").toObject());
    m_onlyWhileGame = root.value("onlyWhileGame").toBool(false);
    m_overlayEnabled = root.value("overlay").toBool(false);
    m_overlayPort = std::clamp(root.value("overlayPort").toInt(7337), 1024, 65535);
    m_comboSwitch = root.value("comboSwitch").toBool(true);
    m_rumble = std::clamp(root.value("rumble").toDouble(1.0), 0.0, 1.0);
    m_steamInputOff = root.value("steamInputOff").toBool(false);
    m_jumpButton = std::clamp(root.value("jumpButton").toInt(0), 0, 14);
    const QJsonObject game = root.value("rocketLeague").toObject();
    for (auto it = m_game.begin(); it != m_game.end(); ++it)
        it.value() = game.value(it.key()).toDouble(it.value());

    m_profiles.clear();
    for (const QJsonValue &value : root.value("profiles").toArray())
        m_profiles.append(Profile::fromJson(value.toObject()));
    if (m_profiles.isEmpty()) {
        // Version 0.1 kept one unnamed profile; it becomes the default and
        // Rocket League joins it.
        Profile first = Profile::fromJson(root.value("profile").toObject());
        first.name = "Default";
        first.preset = "rocketleague";
        m_profiles = {first, Presets::make("rocketleague")};
    }
    m_manual = m_current = std::clamp(root.value("current").toInt(0), 0, int(m_profiles.size()) - 1);
}

// Written beside the file and renamed over it, so a crash never leaves a
// half-written config.
void Backend::save()
{
    QDir().mkpath(QFileInfo(m_configPath).absolutePath());
    QSaveFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    QJsonArray profiles;
    for (const Profile &profile : std::as_const(m_profiles))
        profiles.append(profile.toJson());
    QJsonObject root{{"version", 2}, {"profiles", profiles}, {"current", m_manual}, {"active", m_active},
                           {"lightbar", m_lightbar.toJson()}, {"onlyWhileGame", m_onlyWhileGame},
                     {"overlay", m_overlayEnabled}, {"overlayPort", m_overlayPort}, {"comboSwitch", m_comboSwitch},
                     {"rumble", m_rumble}, {"steamInputOff", m_steamInputOff},
                     {"jumpButton", m_jumpButton}};
    QJsonObject game;
    for (auto it = m_game.cbegin(); it != m_game.cend(); ++it)
        game.insert(it.key(), it.value());
    root.insert("rocketLeague", game);
    file.write(QJsonDocument(root).toJson());
    file.commit();
}

QVariantList Backend::controllers() const
{
    QVariantList list;
    for (const GamepadInfo &info : m_devices)
        list.append(QVariantMap{{"name", info.name}, {"path", info.path}, {"id", info.id}});
    return list;
}

void Backend::setSelected(int index)
{
    if (index == m_selected || index < -1 || index >= m_devices.size())
        return;
    m_selected = index;
    emit controllersChanged();
    emit errorChanged();
    updateBattery();
}

void Backend::selectNext()
{
    if (!m_devices.isEmpty())
        setSelected((m_selected + 1) % m_devices.size());
}

void Backend::updateLightbarTargets()
{
    QStringList lightbars;
    if (!m_yielding)
        for (const GamepadInfo &info : std::as_const(m_devices))
            if (!info.lightbar.isEmpty())
                lightbars.append(info.lightbar);
    m_lightbar.setTargets(lightbars);
}

void Backend::setYielding(bool yielding)
{
    if (yielding == m_yielding)
        return;
    m_yielding = yielding;
    updateLightbarTargets();
    emit gameChanged();
    restartWorkers();
    emit hideChanged();
}

void Backend::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    save();
    emit activeChanged();
    emit gameChanged();
    restartWorkers();
    emit hideChanged();
}

QString Backend::error() const
{
    if (!m_hideError.isEmpty())
        return m_hideError;
    if (m_selected >= 0 && m_selected < m_devices.size()) {
        const GamepadInfo &info = m_devices.at(m_selected);
        if (!info.readable)
            return tr("No permission to read %1 (%2). Add a udev rule that gives your user access to it.").arg(info.name, info.path);
        if (m_workers.contains(info.id))
            return m_workers.value(info.id).error;
    }
    return {};
}

// ---- Profiles and editing ----

QStringList Backend::profileNames() const
{
    QStringList names;
    for (const Profile &profile : m_profiles)
        names.append(profile.name);
    return names;
}

void Backend::pushUndo()
{
    m_undo.push({m_profiles, m_manual});
    m_redo.clear();
}

void Backend::beginEdit()
{
    if (m_editing)
        return;
    m_editing = true;
    pushUndo();
}

void Backend::setSetting(const QString &key, double value)
{
    Profile next = profile();
    if (!next.setValue(key, value) || next == profile())
        return;
    StickSettings &before = key.startsWith("right") ? editable().right : editable().left;
    StickSettings &after = key.startsWith("right") ? next.right : next.left;
    // A custom shape starts as the shape it replaces, so it can be nudged
    // rather than drawn from nothing; splitting starts square.
    if (key.endsWith(".shape") && after.shape == StickSettings::Custom && before.shape != StickSettings::Custom)
        for (int i = 0; i < 8; ++i)
            after.radii[i] = std::min(1.0, Curves::boundary(std::cos(i * M_PI / 4), std::sin(i * M_PI / 4), before));
    if (key.endsWith(".split") && after.split && !before.split)
        after.innerY = after.inner;
    editable() = next;
    applyProfile();
}

void Backend::endEdit()
{
    if (!m_editing)
        return;
    m_editing = false;
    // A press that changed nothing leaves no undo step behind.
    if (!m_undo.isEmpty() && m_undo.top().profiles == m_profiles && m_undo.top().manual == m_manual)
        m_undo.pop();
    save();
    emit profileChanged();
}

void Backend::change(const QString &key, double value)
{
    beginEdit();
    setSetting(key, value);
    endEdit();
}

void Backend::resetProfile()
{
    beginEdit();
    Profile fresh = Presets::make(profile().preset);
    fresh.name = profile().name;
    fresh.match = profile().match;
    editable() = fresh;
    applyProfile();
    endEdit();
}

void Backend::undo()
{
    if (m_undo.isEmpty())
        return;
    m_redo.push({m_profiles, m_manual});
    const Snapshot snapshot = m_undo.pop();
    m_profiles = snapshot.profiles;
    m_manual = snapshot.manual;
    m_autoIndex = -1;
    setCurrent(m_manual);
    save();
}

void Backend::redo()
{
    if (m_redo.isEmpty())
        return;
    m_undo.push({m_profiles, m_manual});
    const Snapshot snapshot = m_redo.pop();
    m_profiles = snapshot.profiles;
    m_manual = snapshot.manual;
    m_autoIndex = -1;
    setCurrent(m_manual);
    save();
}

void Backend::applyProfile()
{
    for (const Worker &worker : std::as_const(m_workers))
        worker.remapper->setProfile(profile());
    m_lightbar.setProfileColor(QColor(profileColor()));
    ++m_revision;
    emit profileChanged();
}

void Backend::setCurrent(int index)
{
    m_current = std::clamp(index, 0, int(m_profiles.size()) - 1);
    applyProfile();
}

QVariantList Backend::curvePoints(const QString &side) const
{
    QVariantList list;
    for (const QPointF &p : (side == "right" ? profile().right : profile().left).points)
        list.append(p);
    return list;
}

void Backend::setCurvePoint(const QString &side, int index, double x, double y)
{
    StickSettings &s = side == "right" ? editable().right : editable().left;
    if (index < 0 || index >= s.points.size())
        return;
    // The ends stay at the ends; points in between stay between their
    // neighbours, so the curve always reads left to right.
    const double low = index == 0 ? 0.0 : s.points.at(index - 1).x() + 0.01;
    const double high = index == s.points.size() - 1 ? 1.0 : s.points.at(index + 1).x() - 0.01;
    if (index == 0)
        x = 0.0;
    else if (index == s.points.size() - 1)
        x = 1.0;
    const QPointF point(std::clamp(x, low, high), std::clamp(y, 0.0, 1.0));
    if (s.points.at(index) == point)
        return;
    s.points[index] = point;
    applyProfile();
}

void Backend::resetCurve(const QString &side)
{
    beginEdit();
    (side == "right" ? editable().right : editable().left).points = StickSettings::defaultPoints();
    applyProfile();
    endEdit();
}

double Backend::responseAt(const QString &side, double magnitude, const QString &axis) const
{
    // A straight push along one axis: what the game gets for it.
    const StickSettings &s = side == "right" ? profile().right : profile().left;
    const QPointF out = axis == "y" ? Curves::stick({0, magnitude}, s) : Curves::stick({magnitude, 0}, s);
    return axis == "y" ? std::abs(out.y()) : std::abs(out.x());
}

// Steam keeps each game's launch options in localconfig.vdf:
//   "252950" { ... "LaunchOptions" "DXVK_FRAME_RATE=237 %command%" ... }
// The block that carries LaunchOptions is the one inside "apps".
QString Backend::readLaunchOptions(const QString &vdf, const QString &app)
{
    static const QRegularExpression key(R"re("LaunchOptions"\s+"((?:[^"\\]|\\.)*)")re");
    const QString marker = "\"" + app + "\"";
    for (qsizetype at = vdf.indexOf(marker); at >= 0; at = vdf.indexOf(marker, at + 1)) {
        const qsizetype open = vdf.indexOf('{', at);
        if (open < 0 || vdf.mid(at + marker.size(), open - at - marker.size()).trimmed().size() > 0)
            continue;
        int depth = 0;
        qsizetype close = open;
        for (; close < vdf.size(); ++close) {
            if (vdf.at(close) == '{') ++depth;
            else if (vdf.at(close) == '}' && --depth == 0) break;
        }
        // Only this block's own keys, not ones nested deeper.
        QString block = vdf.mid(open + 1, close - open - 1);
        static const QRegularExpression nested(R"(\{[^{}]*\})");
        while (block.contains(nested))
            block.remove(nested);
        const auto match = key.match(block);
        if (match.hasMatch())
            return match.captured(1).replace("\\\"", "\"").replace("\\\\", "\\");
    }
    return {};
}

// The controllers Rocket League should not see directly, as SDL wants them:
// 0x054c/0x09cc, comma separated.
QString Backend::ignoreList() const
{
    QStringList ids;
    for (const GamepadInfo &info : m_devices)
        ids.append(QString("0x%1/0x%2").arg(info.vendor, 4, 16, QChar('0')).arg(info.product, 4, 16, QChar('0')));
    if (ids.isEmpty())
        ids.append("0x054c/0x09cc");
    ids.removeDuplicates();
    return ids.join(',');
}

QString Backend::suggestedLaunch() const
{
    static const QRegularExpression old(R"(SDL_GAMECONTROLLER_IGNORE_DEVICES=\S*\s*)");
    QString rest = m_launchOptions;
    rest.remove(old);
    rest = rest.trimmed();
    if (!rest.contains("%command%"))
        rest = rest.isEmpty() ? "%command%" : rest + " %command%";
    return "SDL_GAMECONTROLLER_IGNORE_DEVICES=" + ignoreList() + " " + rest;
}

void Backend::setSteamInputOff(bool off)
{
    if (off == m_steamInputOff)
        return;
    m_steamInputOff = off;
    save();
    emit setupChanged();
}

int Backend::batteryMinutes() const
{
    if (m_battery < 0 || m_charging)
        return -1;
    // This session's rate once known, else the last one remembered.
    const double drain = m_drain > 0 ? m_drain : m_history.drain(controllerId());
    return drain > 0 ? int(m_battery / drain * 60) : -1;
}

void Backend::setJumpButton(int bit)
{
    bit = std::clamp(bit, 0, 14);
    if (bit == m_jumpButton)
        return;
    m_jumpButton = bit;
    for (const Worker &worker : std::as_const(m_workers))
        worker.remapper->setJumpButton(bit);
    save();
    emit settingsChanged();
}

QVariantMap Backend::describeFlip(double gapMs, double x, double y, double dodge)
{
    const double tilt = std::min(1.0, std::hypot(x, y));
    // Up on the stick is forward in Rocket League; angles run clockwise from forward.
    double angle = std::atan2(x, -y) * 180 / M_PI;
    if (angle < 0)
        angle += 360;
    const int sector = int(std::lround(angle / 45.0)) % 8;
    static const char *names[] = {"forward", "forward-right", "right", "back-right", "back", "back-left", "left", "forward-left"};
    const double offDiagonal = std::abs(std::fmod(angle, 90.0) - 45.0);
    const bool diagonal = sector % 2 == 1;
    return {{"gap", gapMs}, {"x", x}, {"y", y}, {"tilt", tilt}, {"angle", angle},
            {"direction", QString::fromLatin1(names[sector])}, {"directional", tilt >= dodge},
            {"nearThreshold", std::abs(tilt - dodge) < 0.08}, {"diagonal", diagonal},
            {"offDiagonal", diagonal ? offDiagonal : 0.0},
            {"axes", diagonal ? std::min(std::abs(x), std::abs(y)) : 0.0},
            {"t", QDateTime::currentMSecsSinceEpoch()}};
}

void Backend::onFlip(double gapMs, double x, double y)
{
    const QVariantMap flip = describeFlip(gapMs, x, y, m_game.value("dodge", 0.5));
    m_flips.prepend(flip);
    while (m_flips.size() > 40)
        m_flips.removeLast();
    const QString id = controllerId().isEmpty() ? "unknown" : controllerId();
    m_history.addDaily(id, "flipGap", gapMs);
    if (flip.value("directional").toBool() && flip.value("diagonal").toBool())
        m_history.addDaily(id, "offDiagonal", flip.value("offDiagonal").toDouble());
    emit flipsChanged();
    emit historyChanged();
}

// A cancel belongs to the flip just before it: forward after a back flip
// is a half-flip, back after a forward diagonal a speed flip.
void Backend::onFlipCancel(double ms, double x, double y)
{
    if (m_flips.isEmpty())
        return;
    QVariantMap flip = m_flips.first().toMap();
    const bool half = flip.value("y").toDouble() > 0;
    // How far the cancel was from straight forward or straight back.
    const double off = std::abs(std::atan2(x, half ? -y : y)) * 180 / M_PI;
    flip.insert("cancel", ms);
    flip.insert("cancelKind", half ? "half" : "speed");
    flip.insert("cancelOff", off);
    m_flips[0] = flip;
    m_history.addDaily(controllerId().isEmpty() ? "unknown" : controllerId(), half ? "halfCancel" : "speedCancel", ms);
    emit flipsChanged();
    emit historyChanged();
}

QVariantList Backend::buttonMap() const
{
    QVariantList list;
    for (int target : profile().buttons)
        list.append(target);
    return list;
}

void Backend::setButtonTarget(int button, int target)
{
    if (button < 0 || button >= Profile::ButtonCount || target < Profile::Unmapped || target > Profile::FullR2
        || profile().buttons.value(button) == target)
        return;
    pushUndo();
    editable().buttons[button] = target;
    applyProfile();
    save();
}

void Backend::resetButtons()
{
    if (profile().buttons == Profile::defaultButtons())
        return;
    pushUndo();
    editable().buttons = Profile::defaultButtons();
    applyProfile();
    save();
}

QVariantList Backend::progress(const QString &metric) const
{
    const QString id = metric.startsWith("trainer:") ? (controllerId().isEmpty() ? "unknown" : controllerId())
                                                     : (controllerId().isEmpty() ? "unknown" : controllerId());
    return m_history.daily(id, metric);
}

QString Backend::recordingsDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/deadzone inputs";
}

void Backend::startInputRecording()
{
    m_inputSamples = QJsonArray();
    m_inputRecording = true;
    m_inputClock.start();
    emit recordingChanged();
}

// Saved as JSON: one row per frame of
// [ms, lx, ly, rx, ry, lrx, lry, rrx, rry, l2, r2, buttons].
QString Backend::stopInputRecording()
{
    if (!m_inputRecording)
        return {};
    m_inputRecording = false;
    QDir().mkpath(recordingsDirectory());
    const QString name = "inputs-" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmmss") + ".json";
    const QJsonObject file{{"version", 1}, {"fps", 60}, {"profile", profile().name},
                           {"started", QDateTime::currentDateTime().addMSecs(-m_inputClock.elapsed()).toString(Qt::ISODate)},
                           {"samples", m_inputSamples}};
    QSaveFile out(recordingsDirectory() + "/" + name);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(file).toJson(QJsonDocument::Compact));
        out.commit();
    }
    m_inputSamples = QJsonArray();
    emit recordingChanged();
    return name;
}

QVariantList Backend::inputRecordings() const
{
    QVariantList list;
    const QFileInfoList files = QDir(recordingsDirectory()).entryInfoList({"inputs-*.json"}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &info : files)
        list.append(QVariantMap{{"name", info.fileName()}, {"size", info.size()}, {"when", info.lastModified()}});
    return list;
}

void Backend::deleteInputRecording(const QString &name)
{
    // Only plain names inside the recordings folder.
    if (name.contains('/') || !name.startsWith("inputs-") || !name.endsWith(".json"))
        return;
    QFile::remove(recordingsDirectory() + "/" + name);
    emit recordingChanged();
}

QString Backend::replayUrl(const QString &name, bool forObs) const
{
    return overlayUrl() + "?replay=" + QUrl::toPercentEncoding(name) + "&trail=1&history=1"
           + (forObs ? "&controls=0&loop=1" : "") + "&accent=" + QColor(profileColor()).name().mid(1);
}

void Backend::clearFlips()
{
    m_flips.clear();
    emit flipsChanged();
}

QVariantMap Backend::flipStats() const
{
    int directional = 0, near = 0, diagonals = 0;
    double gap = 0, off = 0, axes = 0;
    for (const QVariant &item : m_flips) {
        const QVariantMap f = item.toMap();
        gap += f.value("gap").toDouble();
        directional += f.value("directional").toBool();
        near += f.value("nearThreshold").toBool();
        if (f.value("directional").toBool() && f.value("diagonal").toBool()) {
            ++diagonals;
            off += f.value("offDiagonal").toDouble();
            axes += f.value("axes").toDouble();
        }
    }
    const int n = m_flips.size();
    return {{"count", n}, {"averageGap", n ? gap / n : 0.0}, {"directional", directional}, {"near", near},
            {"diagonals", diagonals}, {"offDiagonal", diagonals ? off / diagonals : 0.0},
            {"axes", diagonals ? axes / diagonals : 0.0}};
}

QString Backend::withLaunchOptions(const QString &vdf, const QString &options, const QString &app)
{
    const QString marker = "\"" + app + "\"";
    for (qsizetype at = vdf.indexOf(marker); at >= 0; at = vdf.indexOf(marker, at + 1)) {
        const qsizetype open = vdf.indexOf('{', at);
        if (open < 0 || vdf.mid(at + marker.size(), open - at - marker.size()).trimmed().size() > 0)
            continue;
        // Walk the block at its own depth, looking for its LaunchOptions line.
        int depth = 0;
        qsizetype close = open;
        qsizetype line = -1;
        for (qsizetype i = open; i < vdf.size(); ++i) {
            const QChar c = vdf.at(i);
            if (c == '"') {
                const qsizetype end = vdf.indexOf('"', i + 1);
                if (depth == 1 && vdf.mid(i, end - i + 1) == "\"LaunchOptions\"")
                    line = i;
                // Skip the string, minding escaped quotes.
                qsizetype j = i + 1;
                while (j < vdf.size() && (vdf.at(j) != '"' || vdf.at(j - 1) == '\\'))
                    ++j;
                i = j;
                continue;
            }
            if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) { close = i; break; }
        }
        QString escaped = options;
        escaped.replace("\\", "\\\\").replace("\"", "\\\"");
        if (line >= 0) {
            // Replace the value: the second quoted string on that line.
            const qsizetype keyEnd = vdf.indexOf('"', line + 1);
            const qsizetype valueStart = vdf.indexOf('"', keyEnd + 1);
            qsizetype valueEnd = valueStart + 1;
            while (valueEnd < vdf.size() && (vdf.at(valueEnd) != '"' || vdf.at(valueEnd - 1) == '\\'))
                ++valueEnd;
            return vdf.left(valueStart + 1) + escaped + vdf.mid(valueEnd);
        }
        // No launch options yet: add a line just inside the block, indented
        // like the block's other keys.
        const qsizetype lineStart = vdf.lastIndexOf('\n', at) + 1;
        const QString indent = vdf.mid(lineStart, at - lineStart) + "\t";
        Q_UNUSED(close);
        return vdf.left(open + 1) + "\n" + indent + "\"LaunchOptions\"\t\t\"" + escaped + "\"" + vdf.mid(open + 1);
    }
    return {};
}

QString Backend::applyLaunchOptions()
{
    refreshLaunchOptions();
    if (m_steamRunning)
        return tr("Close Steam first (Steam menu, Exit). While it runs, Steam writes its own copy of the settings back over any change.");
    const QString wanted = suggestedLaunch();
    const QString base = QDir::homePath() + "/.local/share/Steam/userdata";
    int changed = 0;
    for (const QString &user : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = base + "/" + user + "/config/localconfig.vdf";
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(file.readAll());
        file.close();
        const QString updated = withLaunchOptions(text, wanted);
        if (updated.isEmpty() || updated == text)
            continue;
        // A copy of Steam's file as it was, in case anything looks wrong.
        QFile::remove(path + ".deadzone-backup");
        QFile::copy(path, path + ".deadzone-backup");
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(updated.toUtf8()) < 0 || !out.commit())
            return tr("Could not write %1.").arg(path);
        ++changed;
    }
    refreshLaunchOptions();
    if (changed == 0 && !launchReady())
        return tr("Rocket League was not found in Steam's settings. Has it been launched from this Steam account?");
    return {};
}

QString Backend::controllerId() const
{
    return m_selected >= 0 && m_selected < m_devices.size() ? m_devices.at(m_selected).id : QString();
}

void Backend::setOverlayEnabled(bool enabled)
{
    if (enabled == m_overlayEnabled)
        return;
    m_overlayEnabled = enabled;
    if (enabled)
        m_overlay.start(m_overlayPort);
    else
        m_overlay.stop();
    save();
    emit overlayChanged();
}

void Backend::setOverlayPort(int port)
{
    port = std::clamp(port, 1024, 65535);
    if (port == m_overlayPort)
        return;
    m_overlayPort = port;
    if (m_overlayEnabled)
        m_overlay.start(m_overlayPort);
    save();
    emit overlayChanged();
}

void Backend::publishOverlay()
{
    if (!m_overlay.running() || m_overlay.clients() == 0)
        return;
    auto r = [](double v) { return std::round(v * 1000) / 1000; };
    const QJsonObject state{
        {"lx", r(m_live.leftOut.x())}, {"ly", r(m_live.leftOut.y())}, {"rx", r(m_live.rightOut.x())}, {"ry", r(m_live.rightOut.y())},
        {"lrx", r(m_live.leftRaw.x())}, {"lry", r(m_live.leftRaw.y())}, {"rrx", r(m_live.rightRaw.x())}, {"rry", r(m_live.rightRaw.y())},
        {"l2", r(m_live.l2Out)}, {"r2", r(m_live.r2Out)}, {"b", qint64(m_live.buttons)},
        {"p", profile().name}, {"c", !m_devices.isEmpty()}};
    m_overlay.publish(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

void Backend::setComboSwitch(bool enabled)
{
    if (enabled == m_comboSwitch)
        return;
    m_comboSwitch = enabled;
    for (const Worker &worker : std::as_const(m_workers))
        worker.remapper->setComboEnabled(enabled);
    save();
    emit settingsChanged();
}

void Backend::setRumble(double strength)
{
    strength = std::clamp(strength, 0.0, 1.0);
    if (qFuzzyCompare(strength + 1, m_rumble + 1))
        return;
    m_rumble = strength;
    for (const Worker &worker : std::as_const(m_workers))
        worker.remapper->setRumble(strength);
    save();
    emit settingsChanged();
}

// A palette that stays tellable apart on a lightbar, by profile position.
QString Backend::profileColor() const
{
    static const char *palette[] = {"#3b82f6", "#ef4444", "#22c55e", "#f59e0b", "#a855f7", "#06b6d4", "#ec4899", "#ffffff"};
    return profile().color.isEmpty() ? palette[m_current % 8] : profile().color;
}

void Backend::setProfileColor(const QString &color)
{
    if (!QColor(color).isValid() || color == profile().color)
        return;
    pushUndo();
    editable().color = QColor(color).name();
    applyProfile();
    save();
}

// PS + d-pad left or right: the next or previous profile, said with a flash
// of its color and a moment of its name in the window and overlay.
void Backend::stepProfile(int step)
{
    if (!remapping() || m_profiles.size() < 2)
        return;
    selectProfile((m_current + step + m_profiles.size()) % m_profiles.size());
    m_lightbar.flash(QColor(profileColor()), 2);
    m_switchedTo = profile().name;
    m_switchedClear.start();
    emit switched();
}

QString Backend::importProfile(const QString &code)
{
    Profile imported;
    if (!Codes::decode(code, imported))
        return tr("That is not a deadzone profile code. Codes start with DZ1-.");
    pushUndo();
    imported.name = uniqueName(imported.name.isEmpty() ? tr("Imported") : imported.name);
    imported.match.clear();
    m_profiles.append(imported);
    m_manual = m_profiles.size() - 1;
    setCurrent(m_manual);
    save();
    return {};
}

QVariantList Backend::health() const
{
    return m_history.health(controllerId());
}

QString Backend::healthTrend() const
{
    return History::trend(health(), tr("Left stick"), tr("Right stick"));
}

bool Backend::saveTrainerResult(const QString &mode, double milliseconds, int misses)
{
    const bool best = m_history.addTrainer(controllerId().isEmpty() ? "unknown" : controllerId(), mode, milliseconds, misses);
    emit historyChanged();
    return best;
}

QVariantList Backend::trainerHistory(const QString &mode) const
{
    return m_history.trainer(controllerId().isEmpty() ? "unknown" : controllerId(), mode);
}

void Backend::startSession(int minutes)
{
    m_session = Session();
    m_sessionDuration = std::clamp(minutes, 1, 30) * 60;
    m_sessionSeconds = 0;
    m_sessionDone = false;
    m_recording = true;
    m_sessionClock.start();
    emit sessionChanged();
}

void Backend::stopSession()
{
    if (!m_recording)
        return;
    m_recording = false;
    m_sessionSeconds = int(m_sessionClock.elapsed() / 1000);
    m_sessionDone = true;
    const QVariantMap m = m_session.measurements();
    if (!controllerId().isEmpty() && (m.value("leftDrift").toDouble() >= 0 || m.value("rightDrift").toDouble() >= 0)) {
        m_history.addHealth(controllerId(), m.value("leftDrift").toDouble(), m.value("rightDrift").toDouble(),
                            m.value("leftReach").toDouble(), m.value("rightReach").toDouble());
        emit historyChanged();
    }
    emit sessionChanged();
}

void Backend::clearSession()
{
    m_recording = m_sessionDone = false;
    m_session = Session();
    emit sessionChanged();
}

void Backend::applySuggestion(int index)
{
    const QVariantList list = sessionSuggestions();
    if (index < 0 || index >= list.size())
        return;
    beginEdit();
    const QVariantMap settings = list.at(index).toMap().value("settings").toMap();
    for (auto it = settings.cbegin(); it != settings.cend(); ++it)
        setSetting(it.key(), it.value().toDouble());
    endEdit();
    emit sessionChanged();
}

void Backend::applyAllSuggestions()
{
    const QVariantList list = sessionSuggestions();
    beginEdit();
    for (const QVariant &item : list) {
        const QVariantMap settings = item.toMap().value("settings").toMap();
        for (auto it = settings.cbegin(); it != settings.cend(); ++it)
            setSetting(it.key(), it.value().toDouble());
    }
    endEdit();
    emit sessionChanged();
}

QString Backend::uniqueName(const QString &base) const
{
    const QStringList names = profileNames();
    if (!names.contains(base))
        return base;
    for (int n = 2;; ++n)
        if (!names.contains(QString("%1 %2").arg(base).arg(n)))
            return QString("%1 %2").arg(base).arg(n);
}

void Backend::selectProfile(int index)
{
    if (index < 0 || index >= m_profiles.size() || index == m_current)
        return;
    // Picking another profile while a game's is in use means "not this one
    // for now"; it stays dismissed until that game exits.
    if (m_autoIndex >= 0)
        m_dismissed = m_autoIndex;
    m_autoIndex = -1;
    m_manual = index;
    setCurrent(index);
    save();
}

void Backend::newProfile(const QString &preset)
{
    pushUndo();
    Profile profile = Presets::make(preset);
    profile.name = uniqueName(profile.name);
    // Only one profile should claim a game, so a second copy waits to be told.
    for (const Profile &other : std::as_const(m_profiles))
        if (!profile.match.isEmpty() && other.match.compare(profile.match, Qt::CaseInsensitive) == 0)
            profile.match.clear();
    m_profiles.append(profile);
    m_autoIndex = -1;
    m_manual = m_profiles.size() - 1;
    setCurrent(m_manual);
    save();
}

void Backend::duplicateProfile()
{
    pushUndo();
    Profile copy = profile();
    copy.name = uniqueName(copy.name);
    copy.match.clear();
    m_profiles.append(copy);
    m_autoIndex = -1;
    m_manual = m_profiles.size() - 1;
    setCurrent(m_manual);
    save();
}

void Backend::renameProfile(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == profile().name)
        return;
    pushUndo();
    editable().name = uniqueName(trimmed);
    applyProfile();
    save();
}

void Backend::deleteProfile()
{
    if (m_profiles.size() <= 1)
        return;
    pushUndo();
    m_profiles.removeAt(m_current);
    m_autoIndex = -1;
    m_dismissed = -1;
    m_manual = std::clamp(m_current > 0 ? m_current - 1 : 0, 0, int(m_profiles.size()) - 1);
    setCurrent(m_manual);
    save();
}

void Backend::setProfileMatch(const QString &match)
{
    if (match.trimmed() == profile().match)
        return;
    pushUndo();
    editable().match = match.trimmed();
    applyProfile();
    save();
    checkGames();
}

QString Backend::presetName(const QString &preset) const
{
    if (preset == "freestyle") return tr("Freestyle");
    if (preset == "worn") return tr("Worn stick");
    return tr("Standard");
}

QString Backend::presetTips(const QString &preset) const
{
    if (preset == "freestyle")
        return tr("Freestyle keeps full diagonal reach on the left stick and adds a slight curve, so small corrections in the air are finer while a full push still turns at full speed. Raise Aerial Sensitivity in the game if air control feels slow.");
    if (preset == "worn")
        return tr("For sticks that drift and no longer reach the rim: a larger deadzone and a lower outer edge. Run Measure drift and Measure edge to fit both to your controller exactly.");
    // Standard, and profiles made from presets older versions had.
    return tr("In Rocket League's controller settings, set Controller Deadzone to its lowest value so it does not stack on this one; if you keep some, set Anti-deadzone here to the same amount. "
                  "Dodge Deadzone is a different thing: how far you push before a jump becomes a flip in that direction. Leave it where flips feel right; many players use 0.50 to 0.70. "
                  "Steering and Aerial Sensitivity are taste; 1.20 to 1.60 is common. "
                  "Here, run Measure drift on the left stick and Measure edge on both, and keep Diagonal reach at 100% so a diagonal gives full steer and full pitch together, which half-flips and turning while boosting need. "
                  "If Start opens split screen in training, the game sees your controller twice; the yellow card at the top walks through the fix.");
}

// Watches for Rocket League, which can be the only time remapping happens.
void Backend::checkGames()
{
    const bool running = runningMatch({"RocketLeague.exe"}) >= 0;
    if (running == m_gameRunning)
        return;
    m_gameRunning = running;
    readGameIni();
    ++m_gameRevision;
    emit gameChanged();
    if (m_onlyWhileGame)
        restartWorkers();
}

void Backend::setOnlyWhileGame(bool only)
{
    if (only == m_onlyWhileGame)
        return;
    m_onlyWhileGame = only;
    save();
    ++m_gameRevision;
    emit gameChanged();
    restartWorkers();
}

void Backend::setGameSetting(const QString &key, double value)
{
    if (!m_game.contains(key) || qFuzzyCompare(m_game.value(key) + 1, value + 1))
        return;
    m_game[key] = std::clamp(value, 0.0, 10.0);
    save();
    ++m_gameRevision;
    emit gameChanged();
}

double Backend::readTriggerDeadzone(const QString &text)
{
    // GamepadDeadzones=( Type=IPT_360, Key=XboxTypeS_RightTriggerAxis, DeadZone=0.1 )
    static const QRegularExpression line(R"(Type=IPT_360,\s*Key=XboxTypeS_(?:Right|Left)TriggerAxis,\s*DeadZone=([0-9.]+))");
    double value = -1;
    for (auto it = line.globalMatch(text); it.hasNext();)
        value = std::max(value, it.next().captured(1).toDouble());
    return value;
}

void Backend::readGameIni()
{
    double value = -1;
    for (const QString &path : inputIniPaths()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            value = readTriggerDeadzone(QString::fromUtf8(file.readAll()));
            break;
        }
    }
    if (value != m_gameTriggerDeadzone) {
        m_gameTriggerDeadzone = value;
        ++m_gameRevision;
        emit gameChanged();
    }
}

void Backend::setRadius(const QString &side, int index, double radius)
{
    StickSettings &s = side == "right" ? editable().right : editable().left;
    if (index < 0 || index >= s.radii.size())
        return;
    radius = std::clamp(radius, 0.0, 1.0);
    if (qFuzzyCompare(s.radii.at(index) + 1, radius + 1))
        return;
    s.radii[index] = radius;
    applyProfile();
}

QVariantList Backend::deadzoneOutline(const QString &side) const
{
    const StickSettings &s = side == "right" ? profile().right : profile().left;
    QVariantList points;
    for (int i = 0; i < 96; ++i) {
        const double angle = i * 2 * M_PI / 96;
        const double r = std::min(1.0, Curves::boundary(std::cos(angle), std::sin(angle), s));
        points.append(QPointF(std::cos(angle) * r, std::sin(angle) * r));
    }
    return points;
}

QString Backend::curvePresetName(const QString &id) const
{
    if (id == "soft") return tr("Soft center");
    if (id == "precise") return tr("Precise");
    if (id == "quick") return tr("Quick");
    if (id == "scurve") return tr("S-curve");
    if (id == "dual") return tr("Two-stage");
    return tr("Linear");
}

void Backend::applyCurvePreset(const QString &side, const QString &id)
{
    beginEdit();
    Curves::applyCurvePreset(side == "right" ? editable().right : editable().left, id);
    applyProfile();
    endEdit();
}

QString Backend::activeCurvePreset(const QString &side) const
{
    const StickSettings &current = side == "right" ? profile().right : profile().left;
    for (const QString &id : Curves::curvePresets()) {
        StickSettings preset = current;
        Curves::applyCurvePreset(preset, id);
        if (preset.custom == current.custom && qFuzzyCompare(preset.curve, current.curve)
            && (!current.custom || preset.points == current.points))
            return id;
    }
    return {};
}

double Backend::travelFor(const QString &side, double output) const
{
    if (output <= 0)
        return 0;
    if (responseAt(side, 1.0) < output - 1e-9)
        return -1;
    // The response never falls as travel grows, so halving finds it.
    double low = 0, high = 1;
    for (int i = 0; i < 40; ++i) {
        const double mid = (low + high) / 2;
        (responseAt(side, mid) >= output ? high : low) = mid;
    }
    return high;
}

// ---- Calibration ----

void Backend::calibrate(const QString &side, const QString &kind)
{
    m_calibration.key = side + "." + kind;
    m_calibration.duration = kind == "edge" ? 5000 : 3000;
    m_calibration.peak = 0;
    std::fill(std::begin(m_calibration.bins), std::end(m_calibration.bins), 0.0);
    m_calibration.message = kind == "edge" ? tr("Roll the stick slowly around its rim, all the way round, a few times.")
                                           : tr("Let go of the stick and leave it resting.");
    m_calibration.clock.start();
    emit calibrationChanged();
}

void Backend::cancelCalibration()
{
    if (m_calibration.key.isEmpty())
        return;
    m_calibration.key.clear();
    m_calibration.message.clear();
    emit calibrationChanged();
}

double Backend::calibrationProgress() const
{
    if (m_calibration.key.isEmpty())
        return 0;
    return std::clamp(double(m_calibration.clock.elapsed()) / m_calibration.duration, 0.0, 1.0);
}

void Backend::finishCalibration()
{
    const QString side = m_calibration.key.section('.', 0, 0), kind = m_calibration.key.section('.', 1);
    const QString stick = side == "left" ? tr("Left stick") : tr("Right stick");
    m_calibration.key.clear();
    if (kind == "drift") {
        // A little margin over the worst resting reading, rounded up.
        const double deadzone = std::clamp(std::ceil((m_calibration.peak + 0.02) * 100) / 100, 0.02, 0.5);
        change(side + ".inner", deadzone);
        if (!controllerId().isEmpty()) {
            m_history.addHealth(controllerId(), side == "left" ? m_calibration.peak : -1, side == "right" ? m_calibration.peak : -1, -1, -1);
            emit historyChanged();
        }
        m_calibration.message = tr("%1 rests within %2%. Deadzone set to %3%.")
                                    .arg(stick).arg(std::round(m_calibration.peak * 100)).arg(std::round(deadzone * 100));
    } else {
        // The rim's weakest direction decides the outer edge, so every
        // direction reaches full travel.
        int visited = 0;
        double weakest = 1.0;
        for (double reach : m_calibration.bins) {
            if (reach > 0.5) {
                ++visited;
                weakest = std::min(weakest, reach);
            }
        }
        if (visited < 30) {
            m_calibration.message = tr("Not enough of the rim was covered. Try again and roll the stick all the way round.");
        } else {
            const double edge = std::clamp(std::floor((weakest - 0.02) * 100) / 100, 0.5, 1.0);
            change(side + ".outer", edge);
            m_calibration.message = tr("%1 reaches at least %2% all the way round. Outer edge set to %3%.")
                                        .arg(stick).arg(std::round(weakest * 100)).arg(std::round(edge * 100));
        }
    }
    emit calibrationChanged();
}

// ---- Controllers ----

void Backend::rescan()
{
    if (!m_watchDevices)
        return;
    const QList<GamepadInfo> found = Gamepads::scan();
    const QString selectedId = m_selected >= 0 && m_selected < m_devices.size() ? m_devices.at(m_selected).id : QString();

    QSet<QString> present;
    for (const GamepadInfo &info : found) {
        present.insert(info.id);
        const auto it = m_workers.constFind(info.id);
        // A controller that came back under a new node, or became readable,
        // gets a fresh remapper.
        if (it != m_workers.constEnd() && (it->info.path != info.path || it->info.readable != info.readable)) {
            stopWorker(info.id);
            m_notice.clear();   // A reconnect is what the hide notice asked for.
        }
        if (info.readable && !m_workers.contains(info.id))
            startWorker(info);
    }
    for (const QString &id : m_workers.keys())
        if (!present.contains(id))
            stopWorker(id);

    if (found != m_devices) {
        m_devices = found;
        m_selected = -1;
        for (int i = 0; i < m_devices.size(); ++i)
            if (m_devices.at(i).id == selectedId)
                m_selected = i;
        if (m_selected < 0 && !m_devices.isEmpty())
            m_selected = 0;
        emit controllersChanged();
        emit errorChanged();
        emit setupChanged();
        updateLightbarTargets();
        updateBattery();
    }
    emit hideChanged();
}

void Backend::updateBattery()
{
    int percent = -1;
    bool charging = false;
    if (m_selected >= 0 && m_selected < m_devices.size())
        Gamepads::battery(m_devices.at(m_selected), percent, charging);
    m_lightbar.setBattery(percent);
    // One warning per discharge, before the controller dies mid-match.
    if (percent >= 0 && percent <= 15 && !charging && !m_lowBatteryWarned) {
        m_lowBatteryWarned = true;
        m_lightbar.flash(QColor("#ef4444"), 3);
        notify(tr("Controller battery low"), tr("%1% left. Plug it in soon.").arg(percent));
    } else if (charging || percent > 25) {
        m_lowBatteryWarned = false;
    }
    // Time left comes from how fast the charge falls while unplugged. A
    // DualShock 4 reports in steps, so each step down is timed.
    if (charging || percent < 0) {
        m_drops.clear();
    } else if (m_drops.isEmpty() || percent < m_drops.last().second) {
        m_drops.append({QDateTime::currentSecsSinceEpoch(), percent});
        if (m_drops.size() >= 2) {
            const double hours = (m_drops.last().first - m_drops.first().first) / 3600.0;
            const int fell = m_drops.first().second - m_drops.last().second;
            if (hours > 0.05 && fell >= 5) {
                m_drain = fell / hours;
                m_history.setDrain(controllerId(), m_drain);
            }
        }
    }
    if (percent != m_battery || charging != m_charging) {
        m_battery = percent;
        m_charging = charging;
        emit batteryChanged();
    }
}

void Backend::startWorker(const GamepadInfo &info)
{
    Worker worker;
    worker.info = info;
    worker.thread = new QThread(this);
    worker.thread->setObjectName("deadzone " + info.path);
    worker.remapper = new Remapper(info.path, info.driver, remapping(), profile());
    worker.remapper->setComboEnabled(m_comboSwitch);
    worker.remapper->setRumble(m_rumble);
    worker.remapper->setJumpButton(m_jumpButton);
    worker.remapper->moveToThread(worker.thread);
    connect(worker.remapper, &Remapper::profileStep, this, &Backend::stepProfile);
    connect(worker.remapper, &Remapper::flip, this, &Backend::onFlip);
    connect(worker.remapper, &Remapper::flipCancel, this, &Backend::onFlipCancel);
    connect(worker.thread, &QThread::started, worker.remapper, &Remapper::start);

    const QString id = info.id;
    connect(worker.remapper, &Remapper::failed, this, [this, id](const QString &message) {
        if (m_workers.contains(id)) {
            m_workers[id].error = message;
            emit errorChanged();
        }
    });
    connect(worker.remapper, &Remapper::started, this, [this, id] {
        if (m_workers.contains(id)) {
            m_workers[id].error.clear();
            emit errorChanged();
        }
    });
    connect(worker.remapper, &Remapper::disconnected, this, [this] { m_rescan.start(); });
    connect(worker.remapper, &Remapper::liveUpdated, this, [this, id] {
        if (m_selected >= 0 && m_selected < m_devices.size() && m_devices.at(m_selected).id == id)
            scheduleLive();
    });

    m_workers.insert(id, worker);
    worker.thread->start(QThread::TimeCriticalPriority);
}

void Backend::stopWorker(const QString &id)
{
    if (!m_workers.contains(id))
        return;
    Worker worker = m_workers.take(id);
    // Stopping runs on the worker's thread, where its notifiers live, and
    // waits so the controller is released before anything reopens it.
    QMetaObject::invokeMethod(worker.remapper, &Remapper::stop, Qt::BlockingQueuedConnection);
    worker.thread->quit();
    worker.thread->wait();
    delete worker.remapper;
    delete worker.thread;
}

void Backend::restartWorkers()
{
    const auto workers = m_workers.values();
    for (const Worker &worker : workers) {
        stopWorker(worker.info.id);
        startWorker(worker.info);
    }
    emit errorChanged();
}

void Backend::setDisplayRate(double hz)
{
    m_frameInterval = std::clamp(int(1000.0 / std::max(hz, 1.0)), 2, 33);
}

void Backend::scheduleLive()
{
    const qint64 since = m_liveShown.elapsed();
    if (since >= m_frameInterval)
        refreshLive();
    else if (!m_liveThrottle.isActive())
        m_liveThrottle.start(int(m_frameInterval - since));
}

void Backend::refreshLive()
{
    LiveState state;
    if (m_selected >= 0 && m_selected < m_devices.size() && m_workers.contains(m_devices.at(m_selected).id))
        state = m_workers.value(m_devices.at(m_selected).id).remapper->live();
    if (state.leftRaw == m_live.leftRaw && state.rightRaw == m_live.rightRaw && state.l2Raw == m_live.l2Raw
        && state.r2Raw == m_live.r2Raw && state.leftOut == m_live.leftOut && state.rightOut == m_live.rightOut
        && state.l2Out == m_live.l2Out && state.r2Out == m_live.r2Out && state.buttons == m_live.buttons
        && state.rate == m_live.rate)
        return;
    m_live = state;
    m_liveShown.restart();
    m_overlayDirty = true;
    emit liveChanged();
}

void Backend::updateSampling()
{
    const bool needed = m_inputRecording || m_recording || !m_calibration.key.isEmpty()
                        || (m_overlay.running() && m_overlay.clients() > 0);
    if (needed && !m_poll.isActive())
        m_poll.start();
    else if (!needed && m_poll.isActive())
        m_poll.stop();
}

void Backend::pollLive()
{
    refreshLive();
    const LiveState &state = m_live;
    if (m_inputRecording) {
        auto r = [](double v) { return std::round(v * 1000) / 1000; };
        m_inputSamples.append(QJsonArray{
            m_inputClock.elapsed(), r(state.leftOut.x()), r(state.leftOut.y()), r(state.rightOut.x()), r(state.rightOut.y()),
            r(state.leftRaw.x()), r(state.leftRaw.y()), r(state.rightRaw.x()), r(state.rightRaw.y()),
            r(state.l2Out), r(state.r2Out), qint64(state.buttons)});
        if (m_inputSamples.size() % 60 == 0)
            emit recordingChanged();
        // Half an hour is plenty for one clip and keeps files reasonable.
        if (m_inputClock.elapsed() > 30 * 60 * 1000)
            stopInputRecording();
    }

    if (m_recording) {
        m_session.add(state.leftRaw, state.rightRaw, state.l2Raw, state.r2Raw);
        if (m_sessionClock.elapsed() >= m_sessionDuration * 1000LL)
            stopSession();
        else if (m_session.samples() % 30 == 0)
            emit sessionChanged();
    }

    if (!m_calibration.key.isEmpty()) {
        const QPointF raw = m_calibration.key.startsWith("left") ? state.leftRaw : state.rightRaw;
        const double reach = std::hypot(raw.x(), raw.y());
        m_calibration.peak = std::max(m_calibration.peak, reach);
        const int bin = int((std::atan2(raw.y(), raw.x()) + M_PI) / (2 * M_PI) * 36) % 36;
        m_calibration.bins[bin] = std::max(m_calibration.bins[bin], reach);
        if (m_calibration.clock.elapsed() >= m_calibration.duration)
            finishCalibration();
        else
            emit calibrationChanged();
    }

    if (m_overlayDirty) {
        m_overlayDirty = false;
        publishOverlay();
    }
}

// ---- Hiding the real controller from games ----
//
// Steam, SDL, and Proton can read a DualShock 4 or DualSense through its
// raw HID node, which never passes through the remapper, so a game sees
// the real pad next to the virtual one (and Start on either joins split
// screen). A udev rule that runs before logind hands out access keeps that
// node to root, now and on every reconnect; the evdev node games fall back
// to is the one deadzone holds.

QString Backend::rulePath()
{
    return "/etc/udev/rules.d/72-deadzone.rules";
}

QString Backend::ruleText(const QStringList &ids, const QString &lightbarUser)
{
    QString text = "# Written by deadzone; change it from deadzone's window, or delete this file.\n";
    if (!ids.isEmpty())
        text += "# Keeps games and Steam off these controllers' raw HID nodes, so they only\n"
                "# see deadzone's virtual pad.\n";
    for (const QString &id : ids)
        text += QString("KERNEL==\"hidraw*\", KERNELS==\"*%1*\", TAG-=\"uaccess\", MODE=\"0600\"\n").arg(id.toUpper());
    // A user name is the only thing from outside that lands in the rule,
    // so it must look like one.
    static const QRegularExpression validUser("^[a-z_][a-z0-9_-]*$");
    if (validUser.match(lightbarUser).hasMatch())
        text += QString("# Lets %1 set PlayStation controller lightbars.\n"
                        "ACTION==\"add\", SUBSYSTEM==\"leds\", KERNEL==\"*:red|*:green|*:blue|*:global\", "
                        "DRIVERS==\"playstation|sony\", RUN+=\"/usr/bin/chown %1 /sys%p/brightness\"\n"
                        "ACTION==\"add\", SUBSYSTEM==\"leds\", KERNEL==\"*:rgb:indicator\", DRIVERS==\"playstation\", "
                        "RUN+=\"/usr/bin/chown %1 /sys%p/brightness /sys%p/multi_intensity\"\n").arg(lightbarUser);
    return text;
}

QStringList Backend::ruleIds(const QString &text)
{
    QStringList ids;
    static const QRegularExpression pattern(R"(KERNELS=="\*([0-9A-Fa-f]{4}:[0-9A-Fa-f]{4})\*")");
    for (auto it = pattern.globalMatch(text); it.hasNext();)
        ids.append(it.next().captured(1).toUpper());
    ids.removeDuplicates();
    return ids;
}

void Backend::shutdown()
{
    if (m_hider)
        m_hider->waitForFinished(120000);
    // The controller goes back to its own color when deadzone stops.
    if (m_lightbar.mode() != Lightbar::Off)
        m_lightbar.restore();
    for (const QString &id : m_workers.keys())
        stopWorker(id);
}
