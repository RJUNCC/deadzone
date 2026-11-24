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

// The fixed scripts pkexec runs as root. Everything variable arrives as a
// positional argument, never inside the script text.
const char *hideScript =
    "set -e\n"
    "install -m 644 \"$1\" /etc/udev/rules.d/72-deadzone.rules\n"
    "udevadm control --reload\n"
    "user=\"$2\"; shift 2\n"
    "for node in \"$@\"; do setfacl -x \"u:$user\" \"$node\" 2>/dev/null || true; done\n";

const char *showScript =
    "set -e\n"
    "rm -f /etc/udev/rules.d/72-deadzone.rules\n"
    "udevadm control --reload\n"
    "user=\"$1\"; shift\n"
    "for node in \"$@\"; do setfacl -m \"u:$user:rw\" \"$node\" 2>/dev/null || true; done\n";

}

Backend::Backend(const QString &configPath, bool watchDevices, QObject *parent)
    : QObject(parent), m_configPath(configPath.isEmpty() ? defaultConfigPath() : configPath),
      m_watchDevices(watchDevices)
{
    load();

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

    m_games.setInterval(2000);
    connect(&m_games, &QTimer::timeout, this, &Backend::checkGames);

    if (m_watchDevices) {
        readRule();
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
        m_profiles = {Presets::make("default"), Presets::make("rocketleague")};
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    m_active = root.value("active").toBool(true);

    m_profiles.clear();
    for (const QJsonValue &value : root.value("profiles").toArray())
        m_profiles.append(Profile::fromJson(value.toObject()));
    if (m_profiles.isEmpty()) {
        // Version 0.1 kept one unnamed profile; it becomes the default and
        // Rocket League joins it.
        Profile first = Profile::fromJson(root.value("profile").toObject());
        first.name = "Default";
        first.preset = "default";
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
    const QJsonObject root{{"version", 2}, {"profiles", profiles}, {"current", m_manual}, {"active", m_active}};
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
    Profile fresh = Presets::make(profile().preset.isEmpty() ? "default" : profile().preset);
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
    profile.name = uniqueName(presetName(profile.preset));
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
    if (preset == "rocketleague") return "Rocket League";
    if (preset == "shooter") return tr("Shooter");
    if (preset == "racing") return tr("Racing");
    return tr("Default");
}

QString Backend::presetTips(const QString &preset) const
{
    if (preset == "rocketleague")
        return tr("In Rocket League's controller settings, set Controller Deadzone to its lowest value so it does not stack on this one; if you keep some, set Anti-deadzone here to the same amount. "
                  "Dodge Deadzone is a different thing: how far you push before a jump becomes a flip in that direction. Leave it where flips feel right; many players use 0.50 to 0.70. "
                  "Steering and Aerial Sensitivity are taste; 1.20 to 1.60 is common. "
                  "Here, run Measure drift on the left stick and Measure edge on both, and keep Square output at 100% so a diagonal gives full steer and full pitch together, which half-flips and turning while boosting need.");
    if (preset == "shooter")
        return tr("Turn the game's own aim deadzone as low as it goes and let this one handle drift. Raise Response curve on the right stick for finer aim, lower it if turning feels slow. The short trigger travel fires sooner.");
    if (preset == "racing")
        return tr("Cross keeps steering separate from how far up or down the stick sits. A curve above 1 softens small corrections at speed. Set the game's steering deadzone to zero and its steering linearity to linear, so only this curve applies.");
    return tr("A neutral starting point: a small round deadzone and a straight response. Run Measure drift to fit the deadzone to your stick.");
}

// Switches to a game's profile while it runs and back afterwards.
void Backend::checkGames()
{
    QStringList matches;
    for (const Profile &profile : std::as_const(m_profiles))
        matches.append(profile.match);
    const int running = std::any_of(matches.cbegin(), matches.cend(), [](const QString &m) { return !m.isEmpty(); })
                            ? runningMatch(matches) : -1;
    if (running != m_dismissed)
        m_dismissed = -1;
    if (running >= 0 && running != m_dismissed && running != m_autoIndex) {
        m_autoIndex = running;
        setCurrent(running);
    } else if (running < 0 && m_autoIndex >= 0) {
        m_autoIndex = -1;
        setCurrent(m_manual);
    }
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
    }
    emit hideChanged();
}

void Backend::startWorker(const GamepadInfo &info)
{
    Worker worker;
    worker.info = info;
    worker.thread = new QThread(this);
    worker.thread->setObjectName("deadzone " + info.path);
    worker.remapper = new Remapper(info.path, info.driver, m_active, profile());
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

QString Backend::ruleText(const QStringList &ids)
{
    QString text = "# Written by deadzone. Keeps games and Steam off these controllers' raw HID\n"
                   "# nodes so they only see deadzone's virtual pad. Turn off \"Hide from games\"\n"
                   "# in deadzone, or delete this file, to undo.\n";
    for (const QString &id : ids)
        text += QString("KERNEL==\"hidraw*\", KERNELS==\"*%1*\", TAG-=\"uaccess\", MODE=\"0600\"\n").arg(id.toUpper());
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
    m_hiddenIds = file.open(QIODevice::ReadOnly) ? ruleIds(QString::fromUtf8(file.readAll())) : QStringList();
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
    if (m_hider)
        return;
    QStringList ids = hide ? m_hiddenIds : QStringList();
    QStringList nodes;
    for (const Worker &worker : std::as_const(m_workers)) {
        if (worker.info.hidraw.isEmpty())
            continue;
        nodes.append(worker.info.hidraw);
        if (hide)
            ids.append(QString("%1:%2").arg(worker.info.vendor, 4, 16, QChar('0')).arg(worker.info.product, 4, 16, QChar('0')).toUpper());
    }
    ids.removeDuplicates();
    if (hide && ids.isEmpty()) {
        m_hideError = tr("No connected controller has a raw HID node to hide.");
        emit errorChanged();
        return;
    }
    if (!hide && m_hiddenIds.isEmpty())
        return;

    QStringList arguments{"/bin/sh", "-c", hide ? hideScript : showScript, "deadzone-hide"};
    auto *ruleFile = new QTemporaryFile(this);
    if (hide) {
        if (!ruleFile->open()) {
            delete ruleFile;
            return;
        }
        ruleFile->write(ruleText(ids).toUtf8());
        ruleFile->flush();
        arguments << ruleFile->fileName();
    }
    arguments << userName() << nodes;

    m_hideError.clear();
    m_hider = new QProcess(this);
    connect(m_hider, &QProcess::finished, this, [this, hide, ruleFile](int code, QProcess::ExitStatus status) {
        m_hider->deleteLater();
        m_hider = nullptr;
        ruleFile->deleteLater();
        readRule();
        if (status == QProcess::NormalExit && code == 0) {
            // Programs that already opened the node keep it until it goes
            // away, which a reconnect does.
            m_notice = hide ? tr("Hidden. Turn the controller off and on once (or restart Steam) so games already running let go of it.")
                            : QString();
        } else if (code == 126) {
            m_hideError = tr("Cancelled: the password prompt was dismissed.");
        } else {
            m_hideError = tr("Could not change the udev rule (exit code %1).").arg(code);
        }
        emit hideChanged();
        emit errorChanged();
    });
    connect(m_hider, &QProcess::errorOccurred, this, [this, ruleFile](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_hideError = tr("pkexec is missing, so the controller cannot be hidden from games.");
        m_hider->deleteLater();
        m_hider = nullptr;
        ruleFile->deleteLater();
        emit hideChanged();
        emit errorChanged();
    });
    m_hider->start("pkexec", arguments);
    emit hideChanged();
}

void Backend::shutdown()
{
    if (m_hider)
        m_hider->waitForFinished(120000);
    for (const QString &id : m_workers.keys())
        stopWorker(id);
}
