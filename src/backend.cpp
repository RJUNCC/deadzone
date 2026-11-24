#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>

#include "remapper.h"

#include <algorithm>
#include <cmath>
#include <pwd.h>
#include <unistd.h>

namespace {

QString userName()
{
    if (const passwd *pw = getpwuid(getuid()))
        return QString::fromLocal8Bit(pw->pw_name);
    return qEnvironmentVariable("USER");
}

bool readable(const QString &path)
{
    return ::access(QFile::encodeName(path).constData(), R_OK) == 0;
}

// The fixed script pkexec runs as root. Everything variable arrives as a
// positional argument, never inside the script text: the rule to install
// (empty removes it), the user, whether raw HID nodes are being hidden,
// then the nodes and LED files to fix up right away.
const char *ruleScript =
    "set -e\n"
    "if [ -s \"$1\" ]; then install -m 644 \"$1\" /etc/udev/rules.d/72-deadzone.rules;\n"
    "else rm -f /etc/udev/rules.d/72-deadzone.rules; fi\n"
    "udevadm control --reload\n"
    "user=\"$2\"; mode=\"$3\"; shift 3\n"
    "for path in \"$@\"; do\n"
    "  case \"$path\" in\n"
    "    /dev/hidraw*) if [ \"$mode\" = hide ]; then setfacl -x \"u:$user\" \"$path\" 2>/dev/null || true;\n"
    "                  else setfacl -m \"u:$user:rw\" \"$path\" 2>/dev/null || true; fi ;;\n"
    "    /sys/*/brightness) chown \"$user\" \"$path\" 2>/dev/null || true ;;\n"
    "  esac\n"
    "done\n";

}

Backend::Backend(const QString &configPath, bool watchDevices, QObject *parent)
    : QObject(parent), m_configPath(configPath.isEmpty() ? defaultConfigPath() : configPath),
      m_watchDevices(watchDevices)
{
    load();
    connect(&m_lightbar, &Lightbar::changed, this, &Backend::save);

    // Hotplug shows up as /dev/input changing; access rights land a moment
    // after the node, so wait briefly before looking.
    m_rescan.setSingleShot(true);
    m_rescan.setInterval(400);
    connect(&m_rescan, &QTimer::timeout, this, &Backend::rescan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_rescan.start(); });

    // The window shows the selected controller at display rate.
    m_poll.setInterval(16);
    connect(&m_poll, &QTimer::timeout, this, &Backend::pollLive);

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
        m_background = QProcess::execute("systemctl", {"--user", "--quiet", "is-enabled", "deadzone"}) == 0;
        m_watcher.addPath("/dev/input");
        m_poll.start();
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
                           {"lightbar", m_lightbar.toJson()}, {"onlyWhileGame", m_onlyWhileGame}};
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

double Backend::responseAt(const QString &side, double magnitude) const
{
    const StickSettings &s = side == "right" ? profile().right : profile().left;
    return Curves::shape(magnitude, s.inner, s.outer, s);
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

QStringList Backend::inputIniPaths()
{
    const QString tail = "/drive_c/users/steamuser/Documents/My Games/Rocket League/TAGame/Config/TAInput.ini";
    const QString home = QDir::homePath();
    QStringList paths{home + "/.local/share/Steam/steamapps/compatdata/252950/pfx" + tail,
                      home + "/.steam/steam/steamapps/compatdata/252950/pfx" + tail};
    // Heroic keeps Epic games in its own prefixes.
    for (const QString &prefix : QDir(home + "/Games/Heroic/Prefixes").entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        paths.append(home + "/Games/Heroic/Prefixes/" + prefix + "/pfx" + tail);
    return paths;
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

bool Backend::backgroundAvailable() const
{
    return QFile::exists("/usr/lib/systemd/user/deadzone.service");
}

// The background service is the same remapper without a window. It starts
// at login; while this window is open the window does the remapping and
// the service waits (main.cpp stops it and starts it again on quit).
void Backend::setBackground(bool enabled)
{
    if (enabled == m_background || !backgroundAvailable())
        return;
    const int code = QProcess::execute("systemctl", {"--user", enabled ? "enable" : "disable", "deadzone"});
    if (code == 0)
        m_background = enabled;
    else
        m_hideError = enabled ? tr("systemctl could not turn on the background service.")
                              : tr("systemctl could not turn off the background service.");
    emit gameChanged();
    emit errorChanged();
}

QStringList Backend::holders(const QStringList &nodes, const QString &procDirectory)
{
    QStringList names;
    if (nodes.isEmpty())
        return names;
    const QString self = QString::number(QCoreApplication::applicationPid());
    for (const QString &pid : QDir(procDirectory).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!pid.front().isDigit() || pid == self)
            continue;
        const QString fdDir = procDirectory + "/" + pid + "/fd";
        for (const QString &fd : QDir(fdDir).entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
            if (nodes.contains(QFileInfo(fdDir + "/" + fd).symLinkTarget())) {
                QFile comm(procDirectory + "/" + pid + "/comm");
                const QString name = comm.open(QIODevice::ReadOnly) ? QString::fromUtf8(comm.readAll()).trimmed() : pid;
                if (!names.contains(name))
                    names.append(name);
                break;
            }
        }
    }
    return names;
}

// Step two of hiding is done once nothing else still has the raw node
// open; a reconnect or a Steam restart are both ways to get there.
void Backend::checkHolders()
{
    QStringList nodes;
    if (!m_hiddenIds.isEmpty())
        for (const Worker &worker : std::as_const(m_workers))
            if (!worker.info.hidraw.isEmpty())
                nodes.append(worker.info.hidraw);
    const QString held = holders(nodes).join(", ");
    if (held.isEmpty())
        m_notice.clear();
    if (held != m_rawHeldBy) {
        m_rawHeldBy = held;
        emit hideChanged();
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

int Backend::runningMatch(const QStringList &matches, const QString &procDirectory)
{
    const auto pids = QDir(procDirectory).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        if (!pid.front().isDigit())
            continue;
        QFile file(procDirectory + "/" + pid + "/cmdline");
        if (!file.open(QIODevice::ReadOnly))
            continue;
        // Proton runs the game as a Windows path inside the command line,
        // so a plain substring finds RocketLeague.exe wherever it lives.
        const QString line = QString::fromUtf8(file.read(4096).replace('\0', ' '));
        for (int i = 0; i < matches.size(); ++i)
            if (!matches.at(i).isEmpty() && line.contains(matches.at(i), Qt::CaseInsensitive))
                return i;
    }
    return -1;
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
        QStringList lightbars;
        for (const GamepadInfo &info : std::as_const(m_devices))
            if (!info.lightbar.isEmpty())
                lightbars.append(info.lightbar);
        m_lightbar.setTargets(lightbars);
        updateBattery();
    }
    emit hideChanged();
}

void Backend::updateBattery()
{
    int percent = -1;
    bool charging = false;
    if (m_selected >= 0 && m_selected < m_devices.size() && !m_devices.at(m_selected).battery.isEmpty()) {
        const QString dir = m_devices.at(m_selected).battery;
        QFile capacity(dir + "/capacity"), status(dir + "/status");
        if (capacity.open(QIODevice::ReadOnly))
            percent = capacity.readAll().trimmed().toInt();
        if (status.open(QIODevice::ReadOnly))
            charging = status.readAll().trimmed() == "Charging";
    }
    m_lightbar.setBattery(percent);
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
    worker.remapper->moveToThread(worker.thread);
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

void Backend::pollLive()
{
    LiveState state;
    if (m_selected >= 0 && m_selected < m_devices.size() && m_workers.contains(m_devices.at(m_selected).id))
        state = m_workers.value(m_devices.at(m_selected).id).remapper->live();

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

    if (state.leftRaw == m_live.leftRaw && state.rightRaw == m_live.rightRaw && state.l2Raw == m_live.l2Raw
        && state.r2Raw == m_live.r2Raw && state.leftOut == m_live.leftOut && state.rightOut == m_live.rightOut
        && state.l2Out == m_live.l2Out && state.r2Out == m_live.r2Out)
        return;
    m_live = {state.leftRaw, state.leftOut, state.rightRaw, state.rightOut,
              state.l2Raw, state.l2Out, state.r2Raw, state.r2Out};
    emit liveChanged();
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
                        "DRIVERS==\"playstation|sony\", RUN+=\"/usr/bin/chown %1 /sys%p/brightness\"\n").arg(lightbarUser);
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

void Backend::readRule()
{
    QFile file(rulePath());
    const QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    m_hiddenIds = ruleIds(text);
    m_lightbarRule = text.contains("SUBSYSTEM==\"leds\"");
}

bool Backend::exposed() const
{
    if (!m_active)
        return false;
    for (const Worker &worker : m_workers)
        if (!worker.info.hidraw.isEmpty() && readable(worker.info.hidraw))
            return true;
    return false;
}

void Backend::setHideFromGames(bool hide)
{
    QStringList ids = hide ? m_hiddenIds : QStringList();
    if (hide)
        for (const Worker &worker : std::as_const(m_workers))
            if (!worker.info.hidraw.isEmpty())
                ids.append(QString("%1:%2").arg(worker.info.vendor, 4, 16, QChar('0')).arg(worker.info.product, 4, 16, QChar('0')).toUpper());
    ids.removeDuplicates();
    if (hide && ids.isEmpty()) {
        m_hideError = tr("No connected controller has a raw HID node to hide.");
        emit errorChanged();
        return;
    }
    if (ids == m_hiddenIds && !(hide && exposed()))
        return;
    writeRules(ids, m_lightbarRule);
}

void Backend::allowLightbar()
{
    if (!m_lightbarRule || !m_lightbar.writable())
        writeRules(m_hiddenIds, true);
}

void Backend::writeRules(const QStringList &ids, bool lightbar)
{
    if (m_hider)
        return;
    auto *ruleFile = new QTemporaryFile(this);
    if (!ruleFile->open()) {
        delete ruleFile;
        return;
    }
    const QString user = userName();
    if (!ids.isEmpty() || lightbar)
        ruleFile->write(ruleText(ids, lightbar ? user : QString()).toUtf8());
    ruleFile->flush();

    const bool hiding = !ids.isEmpty();
    const bool hidingMore = ids != m_hiddenIds && hiding;
    QStringList paths;
    for (const Worker &worker : std::as_const(m_workers))
        if (!worker.info.hidraw.isEmpty())
            paths.append(worker.info.hidraw);
    if (lightbar)
        paths += m_lightbar.files();

    m_hideError.clear();
    m_hider = new QProcess(this);
    connect(m_hider, &QProcess::finished, this, [this, hidingMore, ruleFile](int code, QProcess::ExitStatus status) {
        m_hider->deleteLater();
        m_hider = nullptr;
        ruleFile->deleteLater();
        readRule();
        if (status == QProcess::NormalExit && code == 0) {
            // Programs that already opened the raw node keep it until it goes
            // away, which a reconnect does.
            if (hidingMore)
                m_notice = tr("Hidden. Now turn the controller off and on once (or restart Steam) so games already running let go of it.");
            else if (m_hiddenIds.isEmpty())
                m_notice.clear();
        } else if (code == 126) {
            m_hideError = tr("Cancelled: the password prompt was dismissed.");
        } else {
            m_hideError = tr("Could not change the udev rule (exit code %1).").arg(code);
        }
        emit hideChanged();
        emit errorChanged();
        emit m_lightbar.targetsChanged();
        checkHolders();
    });
    connect(m_hider, &QProcess::errorOccurred, this, [this, ruleFile](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_hideError = tr("pkexec is missing, so deadzone cannot change the udev rule.");
        m_hider->deleteLater();
        m_hider = nullptr;
        ruleFile->deleteLater();
        emit hideChanged();
        emit errorChanged();
    });
    m_hider->start("pkexec", QStringList{"/bin/sh", "-c", ruleScript, "deadzone-rules", ruleFile->fileName(), user,
                                         hiding ? "hide" : "show"} + paths);
    emit hideChanged();
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
