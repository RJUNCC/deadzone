// The Windows side of the backend: HidHide hides the real controller from
// games, a Run entry in the registry stands in for the systemd service, and
// a process snapshot shows which games are running.

#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>

namespace {

const char *runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char *runName = "Deadzone";

QString exePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

bool serviceInstalled(const QString &name)
{
    QSettings service("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\" + name, QSettings::NativeFormat);
    return service.contains("Start") || service.contains("ImagePath");
}

QString hidHideCli()
{
    const QString programFiles = qEnvironmentVariable("ProgramFiles", "C:\\Program Files");
    for (const QString &candidate : {programFiles + "\\Nefarius Software Solutions\\HidHide\\x64\\HidHideCLI.exe",
                                     programFiles + "\\Nefarius Software Solutions\\HidHide\\HidHideCLI.exe"})
        if (QFile::exists(candidate))
            return candidate;
    return {};
}

QString quoted(const QString &argument)
{
    return '"' + argument + '"';
}

// Where deadzone remembers which controllers it asked HidHide to hide:
// reading HidHide's own list needs administrator rights.
QSettings &settings()
{
    static QSettings store(QSettings::NativeFormat, QSettings::UserScope, "deadzone", "deadzone");
    return store;
}

}

QString Backend::platform() const
{
    return QStringLiteral("windows");
}

QStringList Backend::missingDrivers() const
{
    QStringList missing;
    if (!serviceInstalled("ViGEmBus"))
        missing.append("ViGEmBus");
    if (!serviceInstalled("HidHide") || hidHideCli().isEmpty())
        missing.append("HidHide");
    return missing;
}

// Steam launch options are a Linux workaround; HidHide covers it here.
bool Backend::launchReady() const
{
    return true;
}

void Backend::refreshLaunchOptions()
{
}

bool Backend::queryBackground() const
{
    return QSettings(runKey, QSettings::NativeFormat).contains(runName);
}

// SDL's own thread notices controllers coming and going; checking each
// second picks that up.
void Backend::startDeviceWatch()
{
    m_fallbackScan.setInterval(1000);
}

void Backend::notify(const QString &, const QString &)
{
}

QStringList Backend::inputIniPaths()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return {documents + "/My Games/Rocket League/TAGame/Config/TAInput.ini"};
}

bool Backend::backgroundAvailable() const
{
    return true;
}

// The same remapper without a window, started at sign-in by Windows. While
// this window is open it waits; main.cpp starts it on quit if it is not
// already running.
void Backend::setBackground(bool enabled)
{
    if (enabled == m_background)
        return;
    QSettings run(runKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue(runName, quoted(exePath()) + " run --startup");
    else
        run.remove(runName);
    run.sync();
    if (run.status() == QSettings::NoError)
        m_background = enabled;
    else
        m_hideError = tr("Could not change the startup entry in the registry.");
    emit gameChanged();
    emit errorChanged();
}

// Windows does not say which programs hold a device open.
QStringList Backend::holders(const QStringList &, const QString &)
{
    return {};
}

void Backend::checkHolders()
{
}

int Backend::runningMatch(const QStringList &matches, const QString &)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return -1;
    int found = -1;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more && found < 0; more = Process32NextW(snapshot, &entry)) {
        const QString exe = QString::fromWCharArray(entry.szExeFile);
        for (int i = 0; i < matches.size(); ++i) {
            if (!matches.at(i).isEmpty() && exe.contains(matches.at(i), Qt::CaseInsensitive)) {
                found = i;
                break;
            }
        }
    }
    CloseHandle(snapshot);
    return found;
}

// ---- Hiding the real controller from games ----
//
// Games and Steam read a DualShock 4 or DualSense straight through HID, so
// a game sees the real pad next to the virtual one. HidHide, a filter
// driver, hides a device from every program except the ones on its list;
// deadzone puts itself on the list and the controller on the hidden list.

void Backend::readRule()
{
    m_hiddenIds = settings().value("hiddenDevices").toStringList();
    // SDL sets the lightbar without any extra permission.
    m_lightbarRule = true;
}

bool Backend::exposed() const
{
    if (!m_active)
        return false;
    for (const Worker &worker : m_workers)
        if (!worker.info.hidraw.isEmpty() && !m_hiddenIds.contains(worker.info.hidraw))
            return true;
    return false;
}

void Backend::setHideFromGames(bool hide)
{
    QStringList ids = hide ? m_hiddenIds : QStringList();
    if (hide)
        for (const Worker &worker : std::as_const(m_workers))
            if (!worker.info.hidraw.isEmpty())
                ids.append(worker.info.hidraw);
    ids.removeDuplicates();
    if (hide && ids.isEmpty()) {
        m_hideError = tr("No connected controller can be hidden. HidHide hides HID controllers such as the DualShock 4 and DualSense.");
        emit errorChanged();
        return;
    }
    if (ids == m_hiddenIds && !(hide && exposed()))
        return;
    writeRules(ids, false);
}

void Backend::allowLightbar()
{
}

// Runs HidHide's command-line tool as administrator, which shows the
// Windows prompt once, and waits for it without blocking the window.
void Backend::writeRules(const QStringList &ids, bool)
{
    if (m_hiding)
        return;
    const QString cli = hidHideCli();
    if (cli.isEmpty()) {
        m_hideError = tr("HidHide is not installed. Install it from github.com/nefarius/HidHide/releases, then try again.");
        emit errorChanged();
        emit hideChanged();
        return;
    }

    QStringList arguments;
    const bool hidingMore = std::any_of(ids.cbegin(), ids.cend(), [this](const QString &id) { return !m_hiddenIds.contains(id); });
    for (const QString &id : std::as_const(m_hiddenIds))
        if (!ids.contains(id))
            arguments << "--dev-unhide" << quoted(id);
    if (!ids.isEmpty()) {
        // Deadzone itself must still see the controller.
        arguments << "--app-reg" << quoted(exePath());
        for (const QString &id : ids)
            if (!m_hiddenIds.contains(id))
                arguments << "--dev-hide" << quoted(id);
        arguments << "--cloak-on";
    }

    const std::wstring file = cli.toStdWString();
    const std::wstring parameters = arguments.join(' ').toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    info.lpVerb = L"runas";
    info.lpFile = file.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    m_hideError.clear();
    if (!ShellExecuteExW(&info) || !info.hProcess) {
        m_hideError = GetLastError() == ERROR_CANCELLED ? tr("Cancelled: the Windows prompt was dismissed.")
                                                         : tr("Could not start HidHide's command-line tool.");
        emit errorChanged();
        emit hideChanged();
        return;
    }

    m_hiding = true;
    emit hideChanged();
    auto *wait = new QTimer(this);
    wait->setInterval(200);
    HANDLE process = info.hProcess;
    connect(wait, &QTimer::timeout, this, [this, wait, process, ids, hidingMore] {
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
            return;
        DWORD code = 1;
        GetExitCodeProcess(process, &code);
        CloseHandle(process);
        wait->deleteLater();
        m_hiding = false;
        if (code == 0) {
            m_hiddenIds = ids;
            settings().setValue("hiddenDevices", ids);
            settings().sync();
            // Programs that already opened the controller keep it until it
            // goes away, which a reconnect does.
            if (hidingMore)
                m_notice = tr("Hidden. Now turn the controller off and on once (or restart Steam) so games already running let go of it.");
            else if (m_hiddenIds.isEmpty())
                m_notice.clear();
        } else {
            m_hideError = tr("HidHide could not change its settings (exit code %1).").arg(code);
        }
        emit hideChanged();
        emit errorChanged();
    });
    wait->start();
}
