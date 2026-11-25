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
#include <sys/socket.h>
#include <unistd.h>

#include "backend.h"
#include "clipboard.h"
#include "gamepads.h"
#include "theme.h"
#include "translator.h"
#include "windowlock.h"

namespace {

const char *version = "0.10.1";

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

int signalPipe[2] = {-1, -1};

void onSignal(int)
{
    const char byte = 1;
    [[maybe_unused]] ssize_t ignored = ::write(signalPipe[1], &byte, 1);
}

// Headless remapping, for starting at login. Ctrl+C or SIGTERM releases
// the controllers cleanly.
int run(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    app.setApplicationName("deadzone");

    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, signalPipe) != 0)
        return 1;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    QSocketNotifier quitter(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&quitter, &QSocketNotifier::activated, &app, &QCoreApplication::quit);

    Backend backend;
    if (!backend.active())
        backend.setActive(true);

    // While the window is open it has the controllers; this waits, and takes
    // them back when it closes. Checking once a second also covers a window
    // that opens while this is starting, or that crashed.
    const QString lockPath = WindowLock::path();
    QTimer windowCheck;
    windowCheck.setInterval(1000);
    auto checkWindow = [&backend, lockPath] {
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
    app.setWindowIcon(QIcon::fromTheme("deadzone"));

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
    if (QProcess::execute("systemctl", {"--user", "--quiet", "is-active", "deadzone"}) == 0)
        QProcess::execute("systemctl", {"--user", "stop", "deadzone"});

    Theme theme;
    Backend backend;
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &backend, [&backend] {
        backend.shutdown();
        if (backend.background())
            QProcess::startDetached("systemctl", {"--user", "start", "deadzone"});
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
