// deadzone — stick deadzones, response curves, and trigger ranges for any
// controller, replayed to games as a virtual Xbox 360 pad.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSocketNotifier>
#include <QTimer>
#include <QUrl>

#include <csignal>
#include <cstdio>

#ifdef _WIN32
#include <QSettings>
#include <windows.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "backend.h"
#include "clipboard.h"
#include "gamepads.h"
#include "theme.h"
#include "translator.h"
#include "windowlock.h"

namespace {

const char *version = "0.11.0";

void usage(FILE *out)
{
    std::fprintf(out,
                 "Usage: deadzone [command]\n\n"
                 "Commands:\n"
                 "  open           Open the window (the default)\n"
                 "  run            Remap every controller with the saved settings, without a window\n"
                 "  list [--json]  List the controllers deadzone can see\n"
                 "  --version      Print the version\n\n"
                 "Settings live in %s.\n",
                 qPrintable(Backend::defaultConfigPath()));
}

int list(bool json)
{
    const QList<GamepadInfo> pads = Gamepads::scan();
    if (json) {
        QJsonArray array;
        for (const GamepadInfo &pad : pads)
            array.append(QJsonObject{{"name", pad.name}, {"path", pad.path}, {"id", pad.id}, {"driver", pad.driver},
                                     {"hidraw", pad.hidraw}, {"readable", pad.readable}});
        std::printf("%s", QJsonDocument(array).toJson().constData());
    } else if (pads.isEmpty()) {
        std::fprintf(stderr, "No controllers found.\n");
    } else {
        for (const GamepadInfo &pad : pads)
            std::printf("%s\t%s%s\n", qPrintable(pad.path), qPrintable(pad.name), pad.readable ? "" : "\t(no permission)");
    }
    return pads.isEmpty() ? 1 : 0;
}

#ifdef _WIN32
// Ctrl+C in a console, or Windows signing out.
BOOL WINAPI onConsoleEvent(DWORD)
{
    QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
    return TRUE;
}

// The window is a GUI program, so a console it was started from only shows
// its output if it attaches; double-clicked, there is none to attach to.
void attachConsole()
{
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *ignored = nullptr;
        freopen_s(&ignored, "CONOUT$", "w", stdout);
        freopen_s(&ignored, "CONOUT$", "w", stderr);
    }
}

// The background copy holds this while it runs, so the window knows
// whether to start one when it closes.
QString runLockPath()
{
    return WindowLock::path() + ".run";
}
#else
int signalPipe[2] = {-1, -1};

void onSignal(int)
{
    const char byte = 1;
    [[maybe_unused]] ssize_t ignored = ::write(signalPipe[1], &byte, 1);
}
#endif

// Headless remapping, for starting at login. Ctrl+C or SIGTERM releases
// the controllers cleanly.
int run(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    app.setApplicationName("deadzone");

#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleEvent, TRUE);
    // One background copy at a time.
    if (WindowLock::acquire(runLockPath(), 0) < 0) {
        std::fprintf(stderr, "deadzone: already running in the background\n");
        return 0;
    }
    // Started at sign-in, it stops once the window turns the startup
    // entry off, as disabling the systemd service would.
    const bool startup = app.arguments().contains("--startup");
#else
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, signalPipe) != 0)
        return 1;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    QSocketNotifier quitter(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&quitter, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
#endif

    Backend backend;
    if (!backend.active())
        backend.setActive(true);

    // While the window is open it has the controllers; this waits, and takes
    // them back when it closes. Checking once a second also covers a window
    // that opens while this is starting, or that crashed.
    const QString lockPath = WindowLock::path();
    QTimer windowCheck;
    windowCheck.setInterval(1000);
    auto checkWindow = [&backend, lockPath
#ifdef _WIN32
                        , startup, &app
#endif
    ] {
#ifdef _WIN32
        if (startup && !QSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                  QSettings::NativeFormat).contains("Deadzone")) {
            app.quit();
            return;
        }
#endif
        const bool open = WindowLock::held(lockPath);
        if (open == backend.yielding())
            return;
        backend.setYielding(open);
        std::fprintf(stderr, open ? "deadzone: the window is open and has the controllers; waiting\n"
                                  : "deadzone: the window closed; remapping again\n");
    };
    QObject::connect(&windowCheck, &QTimer::timeout, &app, checkWindow);
    checkWindow();
    windowCheck.start();
    QObject::connect(&backend, &Backend::errorChanged, &app, [&backend] {
        if (!backend.error().isEmpty())
            std::fprintf(stderr, "deadzone: %s\n", qPrintable(backend.error()));
    });
    auto report = [&backend] {
        if (backend.controllers().isEmpty())
            std::fprintf(stderr, "deadzone: waiting for a controller\n");
        for (const QVariant &pad : backend.controllers())
            std::fprintf(stderr, "deadzone: remapping %s\n", qPrintable(pad.toMap().value("name").toString()));
    };
    QObject::connect(&backend, &Backend::controllersChanged, &app, report);
    std::fprintf(stderr, "deadzone: running; press Ctrl+C to stop\n");
    report();
    const int code = app.exec();
    backend.shutdown();
    return code;
}

}

int main(int argc, char *argv[])
{
#ifdef _WIN32
    attachConsole();
#endif
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString("open");
    if (command == "--help" || command == "-h" || command == "help") {
        usage(stdout);
        return 0;
    }
    if (command == "--version") {
        std::printf("deadzone %s\n", version);
        return 0;
    }
    if (command == "list")
        return list(argc > 2 && QString(argv[2]) == "--json");
    if (command == "run")
        return run(argc, argv);
    if (command != "open") {
        std::fprintf(stderr, "deadzone: unknown command \"%s\"\n\n", argv[1]);
        usage(stderr);
        return 1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("deadzone");
    app.setApplicationVersion(version);

    // Matches the window to deadzone.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("deadzone");
#ifdef _WIN32
    app.setWindowIcon(QIcon(":/deadzone.png"));
#else
    app.setWindowIcon(QIcon::fromTheme("deadzone"));
#endif

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    // One window at a time. A window that is still closing gets a moment to
    // let go; one that stays open keeps the controllers, and this one leaves.
    if (WindowLock::acquire(WindowLock::path(), 3000) < 0) {
        std::fprintf(stderr, "deadzone: already open in another window\n");
        return 0;
    }

    // Only one remapper can hold a controller. While the window is open it
    // does the work, so a running background service steps aside and is
    // started again when the window closes.
#ifndef _WIN32
    if (QProcess::execute("systemctl", {"--user", "--quiet", "is-active", "deadzone"}) == 0)
        QProcess::execute("systemctl", {"--user", "stop", "deadzone"});
#endif

    Theme theme;
    Backend backend;
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &backend, [&backend] {
        backend.shutdown();
#ifdef _WIN32
        // A background copy already running takes over by itself.
        if (backend.background() && !WindowLock::held(runLockPath()))
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {"run", "--startup"});
#else
        if (backend.background())
            QProcess::startDetached("systemctl", {"--user", "start", "deadzone"});
#endif
    });

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    Clipboard clipboard;
    engine.rootContext()->setContextProperty("clipboard", &clipboard);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
