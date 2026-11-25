#include "windowlock.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>
#include <QThread>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace WindowLock {

QString path()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + "/deadzone-window.lock";
}

#ifdef _WIN32

// Windows has no flock; a named mutex does the same job, and Windows drops
// it when the process that made it ends, crash or not.
namespace {

std::wstring mutexName(const QString &path)
{
    QString name = QDir::cleanPath(path).toLower();
    name.replace('/', '_').replace('\\', '_').replace(':', '_');
    return (QStringLiteral("Local\\deadzone-") + name).toStdWString();
}

}

int acquire(const QString &path, int waitMs)
{
    const std::wstring name = mutexName(path);
    QElapsedTimer clock;
    clock.start();
    for (;;) {
        HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!mutex)
            return -1;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return 1;   // Kept open until the process ends.
        CloseHandle(mutex);
        if (clock.elapsed() >= waitMs)
            return -1;
        QThread::msleep(50);
    }
}

bool held(const QString &path)
{
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, mutexName(path).c_str());
    if (!mutex)
        return false;
    CloseHandle(mutex);
    return true;
}

#else

int acquire(const QString &path, int waitMs)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return -1;
    QElapsedTimer clock;
    clock.start();
    for (;;) {
        if (::flock(fd, LOCK_EX | LOCK_NB) == 0)
            return fd;
        if (errno != EWOULDBLOCK || clock.elapsed() >= waitMs)
            break;
        QThread::msleep(50);
    }
    ::close(fd);
    return -1;
}

bool held(const QString &path)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool taken = ::flock(fd, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK;
    ::close(fd);
    return taken;
}

#endif

}
