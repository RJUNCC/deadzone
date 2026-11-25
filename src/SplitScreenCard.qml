import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Getting Rocket League to use deadzone's controller and nothing else. Linux
// cannot hide a controller from other programs, so the game and Steam have
// to be told to look away from the real one; each step ticks off when done.
// Windows can, with the HidHide driver, so its steps are fewer.
Rectangle {
    id: root

    readonly property bool win: backend.platform === "windows"
    readonly property bool drivers: backend.missingDrivers.length === 0
    readonly property bool hidden: backend.hideFromGames && !backend.exposed
    readonly property bool running: backend.background && backend.onlyWhileGame
    readonly property bool complete: win ? drivers && hidden && (running || backend.active)
                                         : hidden && backend.steamInputOff && backend.launchReady && (running || backend.active)
    property bool expanded: !complete

    color: complete && !expanded ? Qt.tint(theme.surface, "#1a22c55e") : Qt.tint(theme.surface, "#1feab308")
    radius: theme.radius
    border.color: complete && !expanded ? Qt.tint(theme.surface, "#6622c55e") : Qt.tint(theme.surface, Qt.rgba(Qt.color(theme.accent).r, Qt.color(theme.accent).g, Qt.color(theme.accent).b, 0.45))
    implicitHeight: column.implicitHeight + 28

    component Step: RowLayout {
        id: step
        property int number
        property bool done
        property string title
        property string detail
        default property alias extra: more.data
        Layout.fillWidth: true
        spacing: 10
        Rectangle {
            Layout.alignment: Qt.AlignTop
            width: 22; height: 22; radius: 11
            color: step.done ? "#4ade80" : "transparent"
            border.color: step.done ? "#4ade80" : theme.accent
            Label {
                anchors.centerIn: parent
                text: step.done ? "✓" : step.number
                color: step.done ? theme.background : theme.accent
                font.pixelSize: 12
                font.weight: Font.Bold
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: step.done ? theme.muted : theme.text
                font.weight: Font.Medium
                text: step.title
            }
            Label {
                visible: step.detail.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.muted
                font.pixelSize: 12
                text: step.detail
            }
            ColumnLayout { id: more; Layout.fillWidth: true; spacing: 6 }
        }
    }

    component AccentButton: Button {
        focusPolicy: Qt.NoFocus
        Material.foreground: theme.accentForeground
        background: Rectangle {
            implicitHeight: 34
            radius: Math.min(4, theme.radius)
            color: parent.down ? Qt.darker(theme.accent, 1.2) : theme.accent
        }
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.complete ? qsTr("Rocket League is set up to use deadzone's controller.")
                                    : qsTr("Set up Rocket League to use deadzone's controller")
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: root.complete ? "#4ade80" : theme.accent
            }
            Button {
                visible: root.complete
                text: root.expanded ? qsTr("Hide steps") : qsTr("Show steps")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                onClicked: root.expanded = !root.expanded
            }
        }

        Label {
            visible: root.expanded
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.text
            text: root.win
                  ? qsTr("Deadzone gives the game a virtual Xbox pad with your settings. But the game and Steam can still read the real controller next to it, so your input arrives twice, or as player 2 with Start opening split screen. These steps hide the real controller so the game sees only deadzone's pad.")
                  : qsTr("Deadzone gives the game a virtual Xbox pad with your settings. But Linux has no way to hide the real controller from other programs: Steam still lists it, silent while deadzone holds it, and makes it player 1. Your input then arrives as player 2, so the controller seems dead and Start opens split screen. These steps tell Steam and the game to look only at deadzone's pad.")
        }

        Step {
            visible: root.expanded && root.win
            number: 1
            done: root.drivers
            title: qsTr("Install ViGEmBus and HidHide")
            detail: root.drivers
                    ? qsTr("Both drivers are installed.")
                    : qsTr("Two free drivers deadzone needs: ViGEmBus creates its virtual Xbox pad, and HidHide hides the real controller from games. Install them, restart the computer, then open deadzone again. Missing: %1.").arg(backend.missingDrivers.join(", "))
            RowLayout {
                visible: !root.drivers
                spacing: 8
                AccentButton {
                    visible: backend.missingDrivers.indexOf("ViGEmBus") >= 0
                    text: qsTr("Get ViGEmBus")
                    onClicked: Qt.openUrlExternally("https://github.com/nefarius/ViGEmBus/releases/latest")
                }
                AccentButton {
                    visible: backend.missingDrivers.indexOf("HidHide") >= 0
                    text: qsTr("Get HidHide")
                    onClicked: Qt.openUrlExternally("https://github.com/nefarius/HidHide/releases/latest")
                }
            }
        }

        Step {
            visible: root.expanded
            number: root.win ? 2 : 1
            done: root.hidden
            title: root.win ? qsTr("Hide the real controller from games") : qsTr("Hide the controller's raw device from games")
            detail: root.win ? qsTr("Uses HidHide, and Windows asks for permission once. Then turn the controller off and on.")
                             : qsTr("Adds a udev rule, asking for your password once. Then unplug and replug the controller.")
            AccentButton {
                visible: !root.hidden
                text: backend.hideBusy ? (root.win ? qsTr("Waiting for Windows…") : qsTr("Waiting for password…")) : qsTr("Hide from games")
                enabled: !backend.hideBusy && (!root.win || root.drivers)
                onClicked: backend.hideFromGames = true
            }
        }

        Step {
            visible: root.expanded && !root.win
            number: 2
            done: backend.steamInputOff
            title: qsTr("Turn off Steam Input for Rocket League")
            detail: qsTr("In Steam: Library, right-click Rocket League, Properties, Controller, and set it to Disable Steam Input. Otherwise Steam keeps adding the real controller as player 1 and puts its own deadzones on top of yours. Deadzone cannot see this setting, so tick it once done.")
            RowLayout {
                spacing: 4
                CheckBox {
                    checked: backend.steamInputOff
                    focusPolicy: Qt.NoFocus
                    onToggled: backend.steamInputOff = checked
                }
                Label { text: qsTr("Done"); color: theme.textSecondary }
            }
        }

        Step {
            visible: root.expanded && !root.win
            number: 3
            done: backend.launchReady
            title: qsTr("Tell Rocket League to ignore the real controller")
            detail: backend.launchReady
                    ? qsTr("Rocket League's launch options already have it.")
                    : qsTr("In Steam: Rocket League, Properties, General, Launch Options. Replace what is there with this line, which keeps your current options. It ticks off once Steam saves it.")
            RowLayout {
                visible: !backend.launchReady
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: launchText.implicitHeight + 16
                    radius: Math.min(4, theme.radius)
                    color: theme.sunken
                    border.color: theme.line
                    Label {
                        id: launchText
                        anchors.fill: parent
                        anchors.margins: 8
                        wrapMode: Text.WrapAnywhere
                        text: backend.suggestedLaunch
                        color: theme.text
                        font.family: "monospace"
                        font.pixelSize: 12
                    }
                }
                AccentButton {
                    id: copyLaunch
                    property bool copied: false
                    text: copied ? qsTr("Copied") : qsTr("Copy")
                    onClicked: { clipboard.setText(backend.suggestedLaunch); copied = true; copyTimer.restart() }
                    Timer { id: copyTimer; interval: 1500; onTriggered: copyLaunch.copied = false }
                }
            }
            RowLayout {
                visible: !backend.launchReady
                Layout.fillWidth: true
                spacing: 8
                Button {
                    id: applyLaunch
                    text: qsTr("Apply it for me")
                    flat: true
                    enabled: !backend.steamRunning
                    focusPolicy: Qt.NoFocus
                    Material.roundedScale: theme.radius
                    onClicked: applyResult.text = backend.applyLaunchOptions()
                }
                Label {
                    id: applyResult
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: text.length > 0 ? (theme.light ? "#b42318" : "#ffb4b4") : theme.muted
                    font.pixelSize: 12
                    text: ""
                    Binding on text {
                        when: backend.steamRunning
                        value: qsTr("Deadzone can write this into Steam's settings itself, but only while Steam is closed: Steam menu, Exit. A backup of the file is kept.")
                        restoreMode: Binding.RestoreNone
                    }
                }
            }
            Label {
                visible: !backend.launchReady && backend.launchOptions.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.faint
                font.pixelSize: 12
                text: qsTr("Now: %1").arg(backend.launchOptions)
            }
        }

        Step {
            visible: root.expanded
            number: root.win ? 3 : 4
            done: root.running
            title: qsTr("Keep deadzone remapping whenever you play")
            detail: root.win ? qsTr("Once the controller is hidden, Rocket League only sees deadzone's pad, so deadzone has to be running. These two switches start it when you sign in and have it take over only while the game runs.")
                             : qsTr("After step 3, Rocket League only sees deadzone's pad, so deadzone has to be running. These two switches start it at login and have it take over only while the game runs.")
            AccentButton {
                visible: !root.running
                text: qsTr("Turn both on")
                enabled: backend.backgroundAvailable
                onClicked: { backend.background = true; backend.onlyWhileGame = true; backend.active = true }
            }
        }

        Label {
            visible: root.expanded
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.muted
            font.pixelSize: 12
            text: root.win ? qsTr("Restart Rocket League after step 2. To go back to the controller on its own, turn off hiding with the switch at the bottom of the window.")
                           : qsTr("Restart Rocket League after steps 2 and 3. To go back to the controller on its own, turn Steam Input back on and remove the SDL_GAMECONTROLLER_IGNORE_DEVICES part from the launch options.")
        }
    }
}
