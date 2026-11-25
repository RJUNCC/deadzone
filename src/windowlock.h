#pragma once

#include <QString>

// Only one deadzone may hold the controllers. The window takes this lock for
// as long as it is open (the kernel drops it if the window crashes), and the
// background service steps aside whenever it sees the lock taken.
namespace WindowLock {

// $XDG_RUNTIME_DIR/deadzone-window.lock.
QString path();
// Takes the lock, waiting up to waitMs for a closing window to let go.
// Returns the file descriptor to keep open, or -1 if another window has it.
int acquire(const QString &path, int waitMs);
// Whether another process holds the lock right now.
bool held(const QString &path);

}
