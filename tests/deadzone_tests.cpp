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
        s.radial = false;
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

    void profileRoundTripsAndClamps()
    {
        Profile p;
        p.left.inner = 0.12;
        p.right.radial = false;
        p.r2.outer = 0.8;
        QCOMPARE(Profile::fromJson(p.toJson()), p);

        QJsonObject wild{{"left", QJsonObject{{"inner", 5.0}, {"curve", -1.0}}}};
        const Profile clamped = Profile::fromJson(wild);
        QCOMPARE(clamped.left.inner, 0.9);
        QCOMPARE(clamped.left.curve, 0.2);
    }

    void settingKeys()
    {
        Profile p;
        QVERIFY(p.setValue("right.curve", 2.0));
        QCOMPARE(p.value("right.curve"), 2.0);
        QVERIFY(p.setValue("left.radial", 0));
        QCOMPARE(p.left.radial, false);
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
        QCOMPARE(backend.setting("left.inner"), Profile().left.inner);
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
        QCOMPARE(backend.profile(), Profile());
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
