import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Why a game can see two controllers, and the two steps that fix it, each
// ticked off as it is done.
Rectangle {
    id: root

    readonly property bool hidden: backend.hideFromGames && !backend.exposed
    readonly property bool reconnected: hidden && backend.notice.length === 0

    color: "#1d1a10"
    radius: theme.radius
    border.color: Qt.darker(theme.accent, 2.2)
    implicitHeight: column.implicitHeight + 28

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        Label {
            text: qsTr("Does Start open split screen? Games are seeing two controllers.")
            font.pixelSize: 15
            font.weight: Font.DemiBold
            color: theme.accent
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#d4d4d8"
            text: qsTr("Deadzone takes over the controller and gives games a virtual Xbox pad. But Steam and Proton also read a DualShock 4 a second way, straight from its raw HID device, which no program can take over. So the game gets the virtual pad and the real controller, and Start on the second one joins a second player.")
        }

        Repeater {
            model: [
                { done: root.hidden,
                  text: qsTr("Hide the real controller from games. Deadzone adds a udev rule that keeps the raw device away from Steam and games, now and on every reconnect. It asks for your password once.") },
                { done: root.reconnected,
                  text: qsTr("Turn the controller off and on once, or restart Steam. Steam already has the raw device open and keeps it until it goes away.") }
            ]
            delegate: RowLayout {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: 10
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    width: 22; height: 22; radius: 11
                    color: modelData.done ? theme.accent : "transparent"
                    border.color: theme.accent
                    Label {
                        anchors.centerIn: parent
                        text: modelData.done ? "✓" : (index + 1)
                        color: modelData.done ? theme.accentForeground : theme.accent
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: modelData.done ? "#8b8b93" : "#e4e4e7"
                    text: modelData.text
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#8b8b93"
                font.pixelSize: 12
                text: qsTr("With deadzone closed, games still get the controller the ordinary way. Turn off \"Hide from games\" at the bottom of the window to undo the rule.")
            }
            Button {
                visible: !root.hidden
                text: backend.hideBusy ? qsTr("Waiting for password…") : qsTr("Hide from games")
                enabled: !backend.hideBusy
                focusPolicy: Qt.NoFocus
                Material.foreground: theme.accentForeground
                background: Rectangle {
                    implicitHeight: 36
                    radius: Math.min(4, theme.radius)
                    color: parent.down ? Qt.darker(theme.accent, 1.2) : theme.accent
                }
                onClicked: backend.hideFromGames = true
            }
        }
    }
}
