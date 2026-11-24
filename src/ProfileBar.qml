import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Which profile is in use, the game that switches it in, and the tips for
// the game its preset was made for.
Rectangle {
    id: root

    signal renameRequested()

    color: "#17171a"
    radius: theme.radius
    border.color: "#232328"
    implicitHeight: column.implicitHeight + 32

    property bool tipsOpen: true

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: qsTr("Profile")
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#e4e4e7"
            }
            ComboBox {
                Layout.preferredWidth: 240
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                model: backend.profileNames
                currentIndex: backend.profileIndex
                onActivated: index => backend.selectProfile(index)
            }
            Button {
                text: qsTr("New")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                onClicked: presetMenu.open()
                Menu {
                    id: presetMenu
                    y: parent.height
                    Repeater {
                        model: backend.presetIds()
                        delegate: MenuItem {
                            required property var modelData
                            text: qsTr("From %1").arg(backend.presetName(modelData))
                            onTriggered: backend.newProfile(modelData)
                        }
                    }
                }
            }
            Button {
                text: qsTr("Duplicate")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                onClicked: backend.duplicateProfile()
            }
            Button {
                text: qsTr("Rename")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                onClicked: root.renameRequested()
            }
            Button {
                text: qsTr("Delete")
                flat: true
                focusPolicy: Qt.NoFocus
                enabled: backend.profileNames.length > 1
                Material.roundedScale: theme.radius
                onClicked: backend.deleteProfile()
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: backend.autoGame.length > 0
                text: qsTr("Game running: switched in on its own")
                color: theme.accent
                font.pixelSize: 13
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TextField {
                id: matchField
                Layout.preferredWidth: 380
                placeholderText: qsTr("Switch in while this program runs")
                text: backend.profileMatch
                Material.roundedScale: theme.radius
                onEditingFinished: backend.setProfileMatch(text)
                // Escape leaves the field without saving.
                Keys.onEscapePressed: { text = backend.profileMatch; focus = false }
            }
            Label {
                visible: ApplicationWindow.window.showDescriptions
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#76767e"
                font.pixelSize: 12
                text: qsTr("Part of the game's program name, like RocketLeague.exe. While it runs this profile is used, and the one you picked comes back when it closes.")
            }
            Item { visible: !ApplicationWindow.window.showDescriptions; Layout.fillWidth: true }
            Button {
                text: root.tipsOpen ? qsTr("Hide tips") : qsTr("Show tips")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                onClicked: root.tipsOpen = !root.tipsOpen
            }
        }

        Rectangle {
            visible: root.tipsOpen
            Layout.fillWidth: true
            implicitHeight: tips.implicitHeight + 24
            radius: Math.min(4, theme.radius)
            color: "#1d1a10"
            border.color: Qt.darker(theme.accent, 2.4)
            ColumnLayout {
                id: tips
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4
                Label {
                    text: qsTr("Tips for %1").arg(backend.presetName(backend.profilePreset))
                    font.weight: Font.DemiBold
                    color: theme.accent
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#d4d4d8"
                    lineHeight: 1.15
                    text: { translator.language; return backend.presetTips(backend.profilePreset) }
                }
            }
        }
    }
}
