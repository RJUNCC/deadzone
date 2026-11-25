// The Linux side of the backend: the udev rule that hides controllers and
// opens up their lightbars, the systemd user service, and reading /proc to
// see which games and programs are running.

#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryFile>

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
    "    /sys/*/brightness|/sys/*/multi_intensity) chown \"$user\" \"$path\" 2>/dev/null || true ;;\n"
    "  esac\n"
    "done\n";

}


QString Backend::platform() const
{
    return QStringLiteral("linux");
}

QStringList Backend::missingDrivers() const
{
    return {};
}

bool Backend::launchReady() const
{
    return m_launchOptions.contains("SDL_GAMECONTROLLER_IGNORE_DEVICES=" + ignoreList());
}

bool Backend::queryBackground() const
{
    return QProcess::execute("systemctl", {"--user", "--quiet", "is-enabled", "deadzone"}) == 0;
}

// Hotplug shows up as /dev/input changing.
void Backend::startDeviceWatch()
{
    m_watcher.addPath("/dev/input");
}

void Backend::notify(const QString &title, const QString &body)
{
    QProcess::startDetached("notify-send", {"-a", "deadzone", "-u", "critical", title, body});
}

void Backend::refreshLaunchOptions()
{
    QString options;
    const QString base = QDir::homePath() + "/.local/share/Steam/userdata";
    for (const QString &user : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile file(base + "/" + user + "/config/localconfig.vdf");
        if (file.open(QIODevice::ReadOnly)) {
            options = readLaunchOptions(QString::fromUtf8(file.readAll()));
            if (!options.isEmpty())
                break;
        }
    }
    // Steam's client process is named steam.
    bool steam = false;
    for (const QString &pid : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!pid.front().isDigit())
            continue;
        QFile comm("/proc/" + pid + "/comm");
        if (comm.open(QIODevice::ReadOnly) && comm.readAll().trimmed() == "steam") {
            steam = true;
            break;
        }
    }
    if (options != m_launchOptions || steam != m_steamRunning) {
        m_launchOptions = options;
        m_steamRunning = steam;
        emit setupChanged();
    }
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
