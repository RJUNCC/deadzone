#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

#include "remapper.h"

#include <pwd.h>
#include <unistd.h>

namespace {

QString userName()
{
    if (const passwd *pw = getpwuid(getuid()))
        return QString::fromLocal8Bit(pw->pw_name);
    return qEnvironmentVariable("USER");
}

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

    if (m_watchDevices) {
        m_watcher.addPath("/dev/input");
        m_poll.start();
        m_fallbackScan.start();
        rescan();
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
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    m_profile = Profile::fromJson(root.value("profile").toObject());
    m_active = root.value("active").toBool(true);
    m_hideFromGames = root.value("hideFromGames").toBool(false);
}

// Written beside the file and renamed over it, so a crash never leaves a
// half-written config.
void Backend::save()
{
    QDir().mkpath(QFileInfo(m_configPath).absolutePath());
    QSaveFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject root{{"profile", m_profile.toJson()}, {"active", m_active}, {"hideFromGames", m_hideFromGames}};
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
    updateHiding();
}

void Backend::setHideFromGames(bool hide)
{
    if (hide == m_hideFromGames)
        return;
    m_hideFromGames = hide;
    m_aclError.clear();
    save();
    emit hideChanged();
    emit errorChanged();
    updateHiding();
}

bool Backend::exposed() const
{
    if (!m_active)
        return false;
    for (const Worker &worker : m_workers)
        if (!worker.info.hidraw.isEmpty() && !m_hidden.contains(worker.info.hidraw)
            && ::access(QFile::encodeName(worker.info.hidraw).constData(), R_OK) == 0)
            return true;
    return false;
}

QString Backend::error() const
{
    if (!m_aclError.isEmpty())
        return m_aclError;
    if (m_selected >= 0 && m_selected < m_devices.size()) {
        const GamepadInfo &info = m_devices.at(m_selected);
        if (!info.readable)
            return tr("No permission to read %1 (%2). Add a udev rule that gives your user access to it.").arg(info.name, info.path);
        if (m_workers.contains(info.id))
            return m_workers.value(info.id).error;
    }
    return {};
}

void Backend::beginEdit()
{
    if (m_editing)
        return;
    m_editing = true;
    m_undo.push(m_profile);
    m_redo.clear();
}

void Backend::setSetting(const QString &key, double value)
{
    Profile next = m_profile;
    if (!next.setValue(key, value) || next == m_profile)
        return;
    m_profile = next;
    applyProfile();
}

void Backend::endEdit()
{
    if (!m_editing)
        return;
    m_editing = false;
    // A press that changed nothing leaves no undo step behind.
    if (!m_undo.isEmpty() && m_undo.top() == m_profile)
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
    m_profile = Profile();
    applyProfile();
    endEdit();
}

void Backend::undo()
{
    if (m_undo.isEmpty())
        return;
    m_redo.push(m_profile);
    m_profile = m_undo.pop();
    applyProfile();
    save();
}

void Backend::redo()
{
    if (m_redo.isEmpty())
        return;
    m_undo.push(m_profile);
    m_profile = m_redo.pop();
    applyProfile();
    save();
}

void Backend::applyProfile()
{
    for (const Worker &worker : std::as_const(m_workers))
        worker.remapper->setProfile(m_profile);
    ++m_revision;
    emit profileChanged();
}

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
        if (it != m_workers.constEnd() && (it->info.path != info.path || it->info.readable != info.readable))
            stopWorker(info.id);
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
        updateHiding();
    }
    emit hideChanged();
}

void Backend::startWorker(const GamepadInfo &info)
{
    Worker worker;
    worker.info = info;
    worker.thread = new QThread(this);
    worker.thread->setObjectName("deadzone " + info.path);
    worker.remapper = new Remapper(info.path, info.driver, m_active, m_profile);
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
            updateHiding();
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
    m_hidden.remove(worker.info.hidraw);
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
    if (state.leftRaw == m_live.leftRaw && state.rightRaw == m_live.rightRaw && state.l2Raw == m_live.l2Raw
        && state.r2Raw == m_live.r2Raw && state.leftOut == m_live.leftOut && state.rightOut == m_live.rightOut
        && state.l2Out == m_live.l2Out && state.r2Out == m_live.r2Out)
        return;
    m_live = {state.leftRaw, state.leftOut, state.rightRaw, state.rightOut,
              state.l2Raw, state.l2Out, state.r2Raw, state.r2Out};
    emit liveChanged();
}

// Games and Steam can read a controller's raw HID node and skip the
// remapper entirely. Hiding takes the user's access to that node away while
// remapping; udev gives it back whenever the controller reconnects.
void Backend::updateHiding()
{
    if (m_acl) {
        m_aclAgain = true;
        return;
    }
    QSet<QString> wanted;
    if (m_active && m_hideFromGames)
        for (const Worker &worker : std::as_const(m_workers))
            if (!worker.info.hidraw.isEmpty() && worker.error.isEmpty())
                wanted.insert(worker.info.hidraw);

    QStringList hide, show;
    for (const QString &path : wanted) {
        if (m_hidden.contains(path))
            continue;
        // Already unreadable, from an earlier run that never restored it.
        if (::access(QFile::encodeName(path).constData(), R_OK) != 0)
            m_hidden.insert(path);
        else
            hide.append(path);
    }
    for (const QString &path : std::as_const(m_hidden)) {
        if (!wanted.contains(path)) {
            if (QFileInfo::exists(path))
                show.append(path);
            else
                m_hidden.remove(path);
        }
    }
    if (!hide.isEmpty())
        runAcl(hide, true);
    else if (!show.isEmpty())
        runAcl(show, false);
    emit hideChanged();
}

void Backend::runAcl(const QStringList &paths, bool hide, bool wait)
{
    const QString user = userName();
    QStringList arguments{"setfacl", hide ? "-x" : "-m", hide ? "u:" + user : "u:" + user + ":rw"};
    arguments += paths;

    m_acl = new QProcess(this);
    connect(m_acl, &QProcess::finished, this, [this, paths, hide](int code, QProcess::ExitStatus status) {
        m_acl->deleteLater();
        m_acl = nullptr;
        if (status == QProcess::NormalExit && code == 0) {
            for (const QString &path : paths) {
                if (hide)
                    m_hidden.insert(path);
                else
                    m_hidden.remove(path);
            }
            m_aclError.clear();
        } else if (hide) {
            // 126 is pkexec's answer when the password prompt is dismissed.
            m_aclError = code == 126 ? tr("Hiding the controller from games was cancelled.")
                                     : tr("Could not hide the controller from games (setfacl exited with %1).").arg(code);
            m_hideFromGames = false;
            save();
        } else {
            m_aclError = tr("Could not give the controller back to games. Reconnecting it restores access.");
            for (const QString &path : paths)
                m_hidden.remove(path);
        }
        emit hideChanged();
        emit errorChanged();
        if (m_aclAgain) {
            m_aclAgain = false;
            updateHiding();
        }
    });
    connect(m_acl, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_aclError = tr("pkexec is missing, so the controller cannot be hidden from games.");
            m_hideFromGames = false;
            m_acl->deleteLater();
            m_acl = nullptr;
            emit hideChanged();
            emit errorChanged();
        }
    });
    m_acl->start("pkexec", arguments);
    if (wait)
        if (QProcess *process = m_acl)
            process->waitForFinished(120000);
}

void Backend::shutdown()
{
    if (m_acl)
        m_acl->waitForFinished(120000);
    QCoreApplication::processEvents();
    const QStringList hidden(m_hidden.begin(), m_hidden.end());
    QStringList existing;
    for (const QString &path : hidden)
        if (QFileInfo::exists(path))
            existing.append(path);
    m_hidden.clear();
    if (!existing.isEmpty())
        runAcl(existing, false, true);
    for (const QString &id : m_workers.keys())
        stopWorker(id);
}
