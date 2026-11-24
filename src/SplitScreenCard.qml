import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Getting Rocket League to use deadzone's controller and nothing else. Linux
// cannot hide a controller from other programs, so the game and Steam have
// to be told to look away from the real one; each step ticks off when done.
Rectangle {
    id: root

    readonly property bool hidden: backend.hideFromGames && !backend.exposed
    readonly property bool running: backend.background && backend.onlyWhileGame
    readonly property bool complete: hidden && backend.steamInputOff && backend.launchReady && (running || backend.active)
    property bool expanded: !complete

    color: complete && !expanded ? "#121a14" : "#1d1a10"
    radius: theme.radius
    border.color: complete && !expanded ? "#1f4d2c" : Qt.darker(theme.accent, 2.2)
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
                color: step.done ? "#0e0e10" : theme.accent
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
                color: step.done ? "#8b8b93" : "#e4e4e7"
                font.weight: Font.Medium
                text: step.title
            }
            Label {
                visible: step.detail.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#8b8b93"
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
            color: "#d4d4d8"
            text: qsTr("Deadzone gives the game a virtual Xbox pad with your settings. But Linux has no way to hide the real controller from other programs: Steam still lists it, silent while deadzone holds it, and makes it player 1. Your input then arrives as player 2, so the controller seems dead and Start opens split screen. These steps tell Steam and the game to look only at deadzone's pad.")
        }

        Step {
            visible: root.expanded
            number: 1
            done: root.hidden
            title: qsTr("Hide the controller's raw device from games")
            detail: qsTr("Adds a udev rule, asking for your password once. Then unplug and replug the controller.")
            AccentButton {
                visible: !root.hidden
                text: backend.hideBusy ? qsTr("Waiting for password…") : qsTr("Hide from games")
                enabled: !backend.hideBusy
                onClicked: backend.hideFromGames = true
            }
        }

        Step {
            visible: root.expanded
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
                Label { text: qsTr("Done"); color: "#c9c9cf" }
            }
        }

        Step {
            visible: root.expanded
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
                    color: "#121215"
                    border.color: "#2a2a30"
                    Label {
                        id: launchText
                        anchors.fill: parent
                        anchors.margins: 8
                        wrapMode: Text.WrapAnywhere
                        text: backend.suggestedLaunch
                        color: "#e4e4e7"
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
                    color: text.length > 0 ? "#ffb4b4" : "#8b8b93"
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
                color: "#6b6b73"
                font.pixelSize: 12
                text: qsTr("Now: %1").arg(backend.launchOptions)
            }
        }

        Step {
            visible: root.expanded
            number: 4
            done: root.running
            title: qsTr("Keep deadzone remapping whenever you play")
            detail: qsTr("After step 3, Rocket League only sees deadzone's pad, so deadzone has to be running. These two switches start it at login and have it take over only while the game runs.")
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
            color: "#8b8b93"
            font.pixelSize: 12
            text: qsTr("Restart Rocket League after steps 2 and 3. To go back to the controller on its own, turn Steam Input back on and remove the SDL_GAMECONTROLLER_IGNORE_DEVICES part from the launch options.")
        }
    }
}
