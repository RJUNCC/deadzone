#include "windowlock.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>
#include <QThread>

#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace WindowLock {

QString path()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + "/deadzone-window.lock";
}

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

}
