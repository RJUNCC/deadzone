#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "curves.h"
#include "gamepads.h"
#include "lightbar.h"
#include "session.h"
#include "history.h"
#include "overlay.h"
#include <QTcpSocket>
#include <QProcess>
#include <QStandardPaths>
#include "theme.h"
#include "translator.h"

class DeadzoneTests : public QObject {
    Q_OBJECT

private slots:
    void insideTheDeadzoneIsCentered()
    {
        StickSettings s;
        s.inner = 0.1;
        QCOMPARE(Curves::stick({0.05, 0.05}, s), QPointF(0, 0));
        QVERIFY(Curves::stick({0.2, 0}, s).x() > 0);
    }

    void outputStartsFromZeroAtTheDeadzoneEdge()
    {
        // Without rescaling, crossing the deadzone would jump straight to 10%.
        QVERIFY(qAbs(Curves::shape(0.1001, 0.1, 1.0, 0.0, 1.0)) < 0.001);
        QCOMPARE(Curves::shape(0.55, 0.1, 1.0, 0.0, 1.0), 0.5);
    }

    void outerEdgeSaturates()
    {
        QCOMPARE(Curves::shape(0.9, 0.0, 0.9, 0.0, 1.0), 1.0);
        QCOMPARE(Curves::shape(1.0, 0.0, 0.9, 0.0, 1.0), 1.0);
    }

    void antiDeadzoneSetsTheFloor()
    {
        QVERIFY(qAbs(Curves::shape(0.1001, 0.1, 1.0, 0.25, 1.0) - 0.25) < 0.001);
        QCOMPARE(Curves::shape(0.05, 0.1, 1.0, 0.25, 1.0), 0.0);
    }

    void curveBendsTheMiddle()
    {
        QCOMPARE(Curves::shape(0.5, 0.0, 1.0, 0.0, 2.0), 0.25);
        QCOMPARE(Curves::shape(1.0, 0.0, 1.0, 0.0, 2.0), 1.0);
    }

    void radialKeepsDirection()
    {
        StickSettings s;
        s.inner = 0.0;
        s.outer = 1.0;
        const QPointF out = Curves::stick({0.3, -0.4}, s);
        QVERIFY(qAbs(out.x() / out.y() - 0.3 / -0.4) < 1e-9);
        // Corners past the unit circle are capped at full travel.
        const QPointF corner = Curves::stick({1, 1}, s);
        QVERIFY(qAbs(std::hypot(corner.x(), corner.y()) - 1.0) < 1e-9);
    }

    void axialTreatsAxesAlone()
    {
        StickSettings s;
        s.inner = 0.2;
        s.shape = StickSettings::Cross;
        // Straight up with a little drift sideways: the drift is dropped.
        const QPointF out = Curves::stick({0.1, -0.8}, s);
        QCOMPARE(out.x(), 0.0);
        QVERIFY(out.y() < 0);
    }

    void triggerRange()
    {
        TriggerSettings t;
        t.inner = 0.1;
        t.outer = 0.9;
        QCOMPARE(Curves::trigger(0.05, t), 0.0);
        QCOMPARE(Curves::trigger(0.5, t), 0.5);
        QCOMPARE(Curves::trigger(0.95, t), 1.0);
    }

    void squareDeadzoneReachesFurtherOnDiagonals()
    {
        StickSettings s;
        s.shape = StickSettings::Square;
        s.inner = 0.1;
        // 0.12 straight right is out; the same length on a diagonal is inside the square.
        QVERIFY(Curves::stick({0.12, 0}, s).x() > 0);
        QCOMPARE(Curves::stick({0.0849, 0.0849}, s), QPointF(0, 0));
        // Leaving the square edge starts from zero rather than jumping.
        const QPointF edge = Curves::stick({0.1001, 0.1001}, s);
        QVERIFY(std::hypot(edge.x(), edge.y()) < 0.01);
    }

    void squareOutputFillsTheDiagonal()
    {
        StickSettings s;
        s.inner = 0;
        s.outer = 1;
        s.square = 1;
        const double d = std::sqrt(0.5);
        const QPointF out = Curves::stick({d, -d}, s);
        QVERIFY(qAbs(out.x() - 1.0) < 1e-6 && qAbs(out.y() + 1.0) < 1e-6);
        // Straight pushes are untouched.
        QVERIFY(qAbs(Curves::stick({0.5, 0}, s).x() - 0.5) < 1e-9);
        s.square = 0;
        QVERIFY(qAbs(Curves::stick({d, -d}, s).x() - d) < 1e-9);
    }

    void customCurveFollowsThePoints()
    {
        StickSettings s;
        s.custom = true;
        s.points = {{0, 0}, {0.5, 0.2}, {1, 1}};
        QVERIFY(qAbs(Curves::response(0.25, s) - 0.1) < 1e-9);
        QVERIFY(qAbs(Curves::response(0.75, s) - 0.6) < 1e-9);
        QCOMPARE(Curves::response(1.0, s), 1.0);
    }

    void triggerAntiDeadzone()
    {
        TriggerSettings t{0.02, 0.85, 0.10};
        QCOMPARE(Curves::trigger(0.01, t), 0.0);
        QVERIFY(Curves::trigger(0.03, t) >= 0.10);
        QCOMPARE(Curves::trigger(0.9, t), 1.0);
    }

    void oldConfigBecomesTwoProfiles()
    {
        QTemporaryDir dir;
        write(dir.filePath("config.json"), R"({"profile": {"left": {"mode": "axial", "inner": 0.2}}, "active": false})");
        Backend backend(dir.filePath("config.json"), false);
        QCOMPARE(backend.profileNames(), QStringList({"Default", "Rocket League"}));
        QCOMPARE(backend.profile().left.shape, StickSettings::Cross);
        QCOMPARE(backend.profile().left.inner, 0.2);
        QCOMPARE(backend.active(), false);
    }

    void profilesAreUndoable()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("config.json"), false);
        QCOMPARE(backend.profileNames(), QStringList({"Rocket League"}));
        backend.newProfile("rocketleague");
        QCOMPARE(backend.profileNames().last(), QString("Rocket League 2"));
        backend.renameProfile("Ranked");
        QCOMPARE(backend.profileNames().last(), QString("Ranked"));
        backend.deleteProfile();
        QCOMPARE(backend.profileNames().size(), 1);
        backend.undo();
        QCOMPARE(backend.profileNames().last(), QString("Ranked"));
        backend.undo();
        backend.undo();
        QCOMPARE(backend.profileNames().size(), 1);
    }

    void resetGoesBackToThePreset()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("config.json"), false);
        backend.change("l2.anti", 0.4);
        backend.resetProfile();
        QCOMPARE(backend.setting("l2.anti"), 0.10);
        QCOMPARE(backend.profile().name, QString("Rocket League"));
    }

    void curvePointsStayInOrder()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("config.json"), false);
        backend.beginEdit();
        backend.setCurvePoint("left", 2, 0.9, 0.4);    // Past its right neighbour at 0.75.
        backend.setCurvePoint("left", 0, 0.3, 0.1);    // The first point stays at x 0.
        backend.endEdit();
        const auto &points = backend.profile().left.points;
        QVERIFY(points[2].x() < points[3].x());
        QCOMPARE(points[0], QPointF(0.0, 0.1));
        QCOMPARE(points[2].y(), 0.4);
    }

    void findsRunningGames()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("123");
        QDir(dir.path()).mkpath("self");
        write(dir.filePath("123/cmdline"), QByteArray("S:\\steamapps\\common\\rocketleague\\Binaries\\Win64\\RocketLeague.exe\0-nomovie\0", 70));
        QCOMPARE(Backend::runningMatch({"", "rocketleague.exe"}, dir.path()), 1);
        QCOMPARE(Backend::runningMatch({"Celeste.exe"}, dir.path()), -1);
        QCOMPARE(Backend::runningMatch({""}, dir.path()), -1);
    }

    void udevRuleRoundTrips()
    {
        const QString text = Backend::ruleText({"054c:09cc", "054C:0CE6"});
        QVERIFY(text.contains(R"(KERNELS=="*054C:09CC*", TAG-="uaccess")"));
        QVERIFY(!text.contains("leds"));
        QCOMPARE(Backend::ruleIds(text), QStringList({"054C:09CC", "054C:0CE6"}));
    }

    void udevRuleIsValid()
    {
        const QString text = Backend::ruleText({"054c:09cc"}, "someone");
        QVERIFY(text.contains("chown someone /sys%p/brightness"));
        // Anything that does not look like a user name stays out of the rule.
        QVERIFY(!Backend::ruleText({}, "x\"; rm -rf /").contains("chown"));
        const QString udevadm = QStandardPaths::findExecutable("udevadm");
        if (udevadm.isEmpty())
            QSKIP("udevadm is not installed");
        QTemporaryDir dir;
        write(dir.filePath("72-deadzone.rules"), text.toUtf8());
        QProcess verify;
        verify.start(udevadm, {"verify", "--no-summary", dir.filePath("72-deadzone.rules")});
        QVERIFY(verify.waitForFinished());
        QVERIFY2(verify.exitCode() == 0, verify.readAllStandardError().constData());
    }

    void lightbarColors()
    {
        QCOMPARE(Lightbar::rainbowAt(0, 0.25).hsvHue(), 0);
        QCOMPARE(Lightbar::rainbowAt(2, 0.25).hsvHue(), 180);
        QCOMPARE(Lightbar::batteryColor(0).hsvHue(), 0);
        QVERIFY(qAbs(Lightbar::batteryColor(100).hsvHue() - 119) <= 1);
        QCOMPARE(Lightbar::channel("/sys/class/leds/input57:red", "green"), QString("/sys/class/leds/input57:green/brightness"));
    }

    void lightbarWritesAndRestores()
    {
        // A fake LED directory stands in for the kernel's.
        QTemporaryDir dir;
        for (const char *c : {"red", "green", "blue", "global"}) {
            QDir(dir.path()).mkpath(QString("input9:%1").arg(c));
            write(dir.filePath(QString("input9:%1/brightness").arg(c)), c == QByteArray("blue") ? "64" : "0");
        }
        auto read = [&](const char *c) {
            QFile f(dir.filePath(QString("input9:%1/brightness").arg(c)));
            f.open(QIODevice::ReadOnly);
            return f.readAll().toInt();
        };
        Lightbar lightbar;
        lightbar.setTargets({dir.filePath("input9:red")});
        QVERIFY(lightbar.writable());
        lightbar.setColor(QColor(255, 0, 128));
        lightbar.setMode(Lightbar::Solid);
        QCOMPARE(read("red"), 255);
        QCOMPARE(read("blue"), 128);
        QCOMPARE(read("global"), 1);
        lightbar.setMode(Lightbar::Off);
        QCOMPARE(read("red"), 0);
        QCOMPARE(read("blue"), 64);
    }

    void lightbarSettingsRoundTrip()
    {
        Lightbar a;
        a.setColor(QColor("#123456"));
        a.setSpeed(0.5);
        Lightbar b;
        QJsonObject o = a.toJson();
        o["mode"] = "rainbow";
        b.load(o);
        QCOMPARE(b.mode(), int(Lightbar::Rainbow));
        QCOMPARE(b.color(), QColor("#123456"));
        QCOMPARE(b.speed(), 0.5);
    }

    void splitDeadzoneIsAnOval()
    {
        StickSettings s;
        s.inner = 0.1;
        s.innerY = 0.3;
        s.split = true;
        QVERIFY(qAbs(Curves::boundary(1, 0, s) - 0.1) < 1e-9);
        QVERIFY(qAbs(Curves::boundary(0, 1, s) - 0.3) < 1e-9);
        // Straight up at 0.2 is still inside; straight right at 0.2 is out.
        QCOMPARE(Curves::stick({0, 0.2}, s), QPointF(0, 0));
        QVERIFY(Curves::stick({0.2, 0}, s).x() > 0);
        s.shape = StickSettings::Cross;
        QCOMPARE(Curves::stick({0.05, 0.25}, s), QPointF(0, 0));
    }

    void customShapeFollowsItsHandles()
    {
        StickSettings s;
        s.shape = StickSettings::Custom;
        s.radii = {0.4, 0.1, 0.1, 0.1, 0.4, 0.1, 0.1, 0.1};
        QVERIFY(qAbs(Curves::boundary(1, 0, s) - 0.4) < 1e-9);
        QVERIFY(qAbs(Curves::boundary(0, 1, s) - 0.1) < 1e-9);
        // Halfway between right (0.4) and down-right (0.1).
        const double a = M_PI / 8;
        QVERIFY(qAbs(Curves::boundary(std::cos(a), std::sin(a), s) - 0.25) < 1e-9);
    }

    void customShapeStartsAsThePreviousShape()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("config.json"), false);
        backend.change("left.shape", StickSettings::Square);
        backend.change("left.inner", 0.2);
        backend.change("left.shape", StickSettings::Custom);
        QVERIFY(qAbs(backend.profile().left.radii[0] - 0.2) < 1e-9);
        QVERIFY(qAbs(backend.profile().left.radii[1] - 0.2 * std::sqrt(2.0)) < 1e-9);
    }

    void deadzoneReachesFullTravel()
    {
        Profile p;
        QVERIFY(p.setValue("left.inner", 1.0));
        QCOMPARE(p.left.inner, 1.0);
        QCOMPARE(Curves::stick({0.9, 0}, p.left), QPointF(0, 0));
    }

    void curvePresets()
    {
        StickSettings s;
        Curves::applyCurvePreset(s, "precise");
        QVERIFY(!s.custom && s.curve > 2);
        Curves::applyCurvePreset(s, "scurve");
        QVERIFY(s.custom);
        QVERIFY(Curves::response(0.25, s) < 0.25 && Curves::response(0.75, s) > 0.75);
        Curves::applyCurvePreset(s, "linear");
        QVERIFY(!s.custom && s.curve == 1.0);
    }

    void readsRocketLeagueTriggerDeadzone()
    {
        const QString ini = "GamepadDeadzone=0.3\n"
                            "GamepadDeadzones=( Type=IPT_360, Key=XboxTypeS_RightTriggerAxis, DeadZone=0.1 )\n"
                            "GamepadDeadzones=( Type=IPT_PS4, Key=XboxTypeS_RightTriggerAxis, DeadZone=0.4 )\n";
        QCOMPARE(Backend::readTriggerDeadzone(ini), 0.1);
        QCOMPARE(Backend::readTriggerDeadzone("nothing"), -1.0);
    }

    void travelForInvertsTheResponse()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("config.json"), false);
        // Standard preset: deadzone 5%, outer edge 90%, linear.
        QVERIFY(qAbs(backend.travelFor("left", 0.5) - (0.05 + 0.5 * 0.85)) < 1e-6);
        QVERIFY(qAbs(backend.travelFor("left", 1.0) - 0.90) < 1e-6);
        backend.change("left.outer", 1.0);
        backend.change("left.curve", 2.0);
        QCOMPARE(backend.travelFor("left", 2.0), -1.0);
    }

    void findsProgramsHoldingANode()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("41/fd");
        QDir(dir.path()).mkpath("42/fd");
        write(dir.filePath("41/comm"), "steam\n");
        write(dir.filePath("42/comm"), "bash\n");
        write(dir.filePath("node"), "");
        QFile::link(dir.filePath("node"), dir.filePath("41/fd/7"));
        QCOMPARE(Backend::holders({dir.filePath("node")}, dir.path()), QStringList({"steam"}));
        QVERIFY(Backend::holders({}, dir.path()).isEmpty());
    }

    void separateCurvesBendEachAxis()
    {
        StickSettings s;
        s.inner = 0;
        s.outer = 1;
        s.splitCurve = true;
        s.curve = 1.0;
        s.curveY = 2.0;
        QVERIFY(qAbs(Curves::stick({0.5, 0}, s).x() - 0.5) < 1e-9);
        QVERIFY(qAbs(Curves::stick({0, 0.5}, s).y() - 0.25) < 1e-9);
        // A full diagonal with full reach still sends 100% on both axes.
        s.square = 1;
        const double d = std::sqrt(0.5);
        const QPointF out = Curves::stick({d, d}, s);
        QVERIFY(qAbs(out.x() - 1) < 1e-6 && qAbs(out.y() - 1) < 1e-6);
    }

    void sessionSuggestsFromRealUse()
    {
        Session session;
        // Two minutes: rests that drift to 9%, hard diagonals on the left
        // stick that only reach 85% toward the left, and a right trigger
        // never pressed past 80%.
        for (int i = 0; i < 7200; ++i) {
            const int phase = (i / 300) % 4;
            QPointF left, right(0.01, 0);
            if (phase == 0) {
                left = {0.09 * std::cos(0.3), 0.09 * std::sin(0.3)};
            } else {
                const double angle = (i % 360) * M_PI / 180;
                const double reach = std::cos(angle) < -0.5 ? 0.85 : 1.0;
                // Pull toward the diagonals.
                const double snapped = std::round((angle - M_PI / 4) / (M_PI / 2)) * (M_PI / 2) + M_PI / 4;
                const double a = phase == 1 ? snapped : angle;
                left = {std::cos(a) * reach, std::sin(a) * reach};
            }
            session.add(left, right, 0.0, phase == 2 ? 0.8 : 0.0);
        }
        Profile p = Presets::make("rocketleague");
        p.left.square = 0;
        QStringList keys;
        for (const QVariant &item : session.suggestions(p))
            keys += item.toMap().value("settings").toMap().keys();
        QVERIFY2(keys.contains("left.inner"), qPrintable(keys.join(",")));
        QVERIFY2(keys.contains("left.outer"), qPrintable(keys.join(",")));
        QVERIFY2(keys.contains("left.square"), qPrintable(keys.join(",")));
        QVERIFY2(keys.contains("r2.outer"), qPrintable(keys.join(",")));
        QVERIFY(!keys.contains("l2.outer"));
    }

    void sessionNeedsEnoughPlay()
    {
        Session session;
        for (int i = 0; i < 600; ++i)
            session.add({0.2, 0}, {}, 0, 0);
        QVERIFY(session.suggestions(Profile()).isEmpty());
        QCOMPARE(session.heatmap(true).size(), Session::Grid * Session::Grid);
    }

    void applyingASuggestionIsOneUndo()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("config.json"), false);
        backend.startSession(1);
        for (int i = 0; i < 4000; ++i)
            backend.addSessionSample({0.12, 0.0}, {}, 0, 0);
        backend.stopSession();
        const QVariantList list = backend.sessionSuggestions();
        QVERIFY(!list.isEmpty());
        backend.applySuggestion(0);
        QVERIFY(backend.setting("left.inner") > 0.12);
        backend.undo();
        QCOMPARE(backend.setting("left.inner"), 0.05);
    }

    void profileCodesRoundTrip()
    {
        Profile p = Presets::make("freestyle");
        p.left.shape = StickSettings::Custom;
        p.left.radii[3] = 0.4;
        p.color = "#a855f7";
        const QString code = Codes::encode(p);
        QVERIFY(code.startsWith("DZ1-"));
        Profile back;
        QVERIFY(Codes::decode(code, back));
        QCOMPARE(back.left, p.left);
        QCOMPARE(back.color, p.color);
        QCOMPARE(back.name, p.name);
        // Junk, truncation, and other text are refused.
        QVERIFY(!Codes::decode("hello", back));
        QVERIFY(!Codes::decode("DZ1-!!!!", back));
        QVERIFY(!Codes::decode(code.left(code.size() / 2), back));
    }

    void importAddsAProfile()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("config.json"), false);
        const QString code = backend.exportProfile();
        QVERIFY(backend.importProfile("nonsense").length() > 0);
        QCOMPARE(backend.importProfile(code), QString());
        QCOMPARE(backend.profileNames(), QStringList({"Rocket League", "Rocket League 2"}));
        backend.undo();
        QCOMPARE(backend.profileNames().size(), 1);
    }

    void historyTracksDriftAndBests()
    {
        QTemporaryDir dir;
        {
            History history(dir.filePath("history.json"));
            history.addHealth("pad", 0.04, -1, -1, -1, 1700000000);
            history.addHealth("pad", 0.07, 0.03, 0.95, -1, 1702000000);
            QVERIFY(history.addTrainer("pad", "flick", 400, 2));
            QVERIFY(!history.addTrainer("pad", "flick", 450, 0));
            QVERIFY(history.addTrainer("pad", "flick", 350, 1));
        }
        History again(dir.filePath("history.json"));
        QCOMPARE(again.health("pad").size(), 2);
        QCOMPARE(again.trainer("pad", "flick").size(), 3);
        QVERIFY(again.health("other").isEmpty());
        const QString trend = History::trend(again.health("pad"), "Left", "Right");
        QVERIFY2(trend.contains("Left drift rose from 4% to 7%"), qPrintable(trend));
        QVERIFY(!trend.contains("Right"));
    }

    void sessionMeasures()
    {
        Session session;
        for (int i = 0; i < 400; ++i)
            session.add({0.05, 0}, {}, 0, 0);
        const QVariantMap m = session.measurements();
        QVERIFY(qAbs(m.value("leftDrift").toDouble() - 0.05) < 1e-9);
        QCOMPARE(m.value("leftReach").toDouble(), -1.0);
    }

    void overlayServesPageAndEvents()
    {
        OverlayServer server;
        QVERIFY(server.start(0) || server.start(47392));
        const quint16 port = server.port();
        server.publish(R"({"lx":0.5})");

        QTcpSocket page;
        page.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(page.waitForConnected());
        page.write("GET /nope HTTP/1.1\r\n\r\n");
        QTRY_VERIFY(page.bytesAvailable() > 0 || page.waitForReadyRead(50));
        QVERIFY(page.readAll().startsWith("HTTP/1.1 404"));

        QTcpSocket events;
        events.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(events.waitForConnected());
        events.write("GET /events HTTP/1.1\r\n\r\n");
        QByteArray got;
        QTRY_VERIFY((got += events.readAll()).contains(R"(data: {"lx":0.5})"));
        QTRY_COMPARE(server.clients(), 1);
        server.publish(R"({"lx":0.7})");
        QTRY_VERIFY((got += events.readAll()).contains(R"(data: {"lx":0.7})"));
    }

    void lightbarProfileMode()
    {
        QTemporaryDir dir;
        for (const char *c : {"red", "green", "blue", "global"}) {
            QDir(dir.path()).mkpath(QString("input9:%1").arg(c));
            write(dir.filePath(QString("input9:%1/brightness").arg(c)), "0");
        }
        Lightbar lightbar;
        lightbar.setTargets({dir.filePath("input9:red")});
        lightbar.setProfileColor(QColor(0, 255, 0));
        lightbar.setMode(Lightbar::ProfileColor);
        QFile green(dir.filePath("input9:green/brightness"));
        QVERIFY(green.open(QIODevice::ReadOnly));
        QCOMPARE(green.readAll().toInt(), 255);
    }

    void profileRoundTripsAndClamps()
    {
        Profile p;
        p.left.inner = 0.12;
        p.right.shape = StickSettings::Square;
        p.left.custom = true;
        p.left.points[2] = {0.5, 0.3};
        p.name = "Rocket League";
        p.match = "RocketLeague.exe";
        p.r2.outer = 0.8;
        QCOMPARE(Profile::fromJson(p.toJson()), p);

        QJsonObject wild{{"left", QJsonObject{{"inner", 5.0}, {"curve", -1.0}}}};
        const Profile clamped = Profile::fromJson(wild);
        QCOMPARE(clamped.left.inner, 1.0);
        QCOMPARE(clamped.left.curve, 0.2);
    }

    void settingKeys()
    {
        Profile p;
        QVERIFY(p.setValue("right.curve", 2.0));
        QCOMPARE(p.value("right.curve"), 2.0);
        QVERIFY(p.setValue("left.shape", 2));
        QCOMPARE(p.left.shape, StickSettings::Cross);
        QVERIFY(p.setValue("left.shape", 9));
        QCOMPARE(p.left.shape, StickSettings::Custom);
        QVERIFY(!p.setValue("left.bogus", 1));
        QVERIFY(!p.setValue("nothing", 1));
    }

    void dragIsOneUndoStep()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("config.json"), false);
        backend.beginEdit();
        backend.setSetting("left.inner", 0.2);
        backend.setSetting("left.inner", 0.3);
        backend.endEdit();
        QCOMPARE(backend.setting("left.inner"), 0.3);
        QVERIFY(backend.canUndo());
        backend.undo();
        QCOMPARE(backend.setting("left.inner"), Presets::make("rocketleague").left.inner);
        QVERIFY(!backend.canUndo());
        backend.redo();
        QCOMPARE(backend.setting("left.inner"), 0.3);
    }

    void pressWithoutChangeLeavesNoUndo()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("config.json"), false);
        backend.beginEdit();
        backend.endEdit();
        QVERIFY(!backend.canUndo());
    }

    void settingsPersist()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("nested/config.json");
        {
            Backend backend(path, false);
            backend.change("right.outer", 0.8);
            backend.setActive(false);
        }
        Backend again(path, false);
        QCOMPARE(again.setting("right.outer"), 0.8);
        QCOMPARE(again.active(), false);
    }

    void resetIsUndoable()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("config.json"), false);
        backend.change("l2.inner", 0.3);
        backend.resetProfile();
        QCOMPARE(backend.profile(), Presets::make("rocketleague"));
        backend.undo();
        QCOMPARE(backend.setting("l2.inner"), 0.3);
    }

    void scanSkipsVirtualAndNonGamepads()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        root.mkpath("input");
        root.mkpath("devices/virtual/input/input9");
        root.mkpath("devices/pci/input/input3");
        root.mkpath("class");
        write(dir.filePath("devices/virtual/input/input9/name"), "Deadzone Xbox 360 Pad\n");
        write(dir.filePath("devices/pci/input/input3/name"), "Fancy Wireless Controller\n");
        // A DualShock 4's gamepad node, its motion sensors under the same
        // name, and a virtual pad with gamepad capabilities.
        const QByteArray gamepadKeys = "7fdb000000000000 0 0 0 0\n", gamepadAbs = "3003f\n";
        root.mkpath("devices/pci/input/input3/capabilities");
        write(dir.filePath("devices/pci/input/input3/capabilities/key"), gamepadKeys);
        write(dir.filePath("devices/pci/input/input3/capabilities/abs"), gamepadAbs);
        root.mkpath("devices/pci/input/input4/capabilities");
        write(dir.filePath("devices/pci/input/input4/name"), "Fancy Wireless Controller Motion Sensors\n");
        write(dir.filePath("devices/pci/input/input4/capabilities/key"), "0\n");
        write(dir.filePath("devices/pci/input/input4/capabilities/abs"), "3f\n");
        root.mkpath("devices/virtual/input/input9/capabilities");
        write(dir.filePath("devices/virtual/input/input9/capabilities/key"), gamepadKeys);
        write(dir.filePath("devices/virtual/input/input9/capabilities/abs"), gamepadAbs);
        for (const char *n : {"3", "4", "9"})
            root.mkpath(QString("class/event%1").arg(n));
        QFile::link(dir.filePath("devices/virtual/input/input9"), dir.filePath("class/event9/device"));
        QFile::link(dir.filePath("devices/pci/input/input3"), dir.filePath("class/event3/device"));
        QFile::link(dir.filePath("devices/pci/input/input4"), dir.filePath("class/event4/device"));
        write(dir.filePath("input/event9"), "");
        write(dir.filePath("input/event3"), "");
        write(dir.filePath("input/event4"), "");
        // Unreadable nodes are still listed, marked so the window can say why.
        QFile::setPermissions(dir.filePath("input/event3"), {});

        const auto pads = Gamepads::scan(dir.filePath("input"), dir.filePath("class"));
        QCOMPARE(pads.size(), 1);
        QCOMPARE(pads.first().name, QString("Fancy Wireless Controller"));
        QVERIFY(!pads.first().readable);
    }

    void faceButtonRule()
    {
        QVERIFY(Gamepads::positionalFaceButtons("playstation"));
        QVERIFY(!Gamepads::positionalFaceButtons("xpad"));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(DeadzoneTests)
#include "deadzone_tests.moc"
