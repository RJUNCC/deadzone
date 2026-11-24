import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Which profile is in use, and making, copying, renaming, and deleting them.
Rectangle {
    id: root

    signal renameRequested()
    signal importRequested()
    readonly property var swatches: ["#3b82f6", "#ef4444", "#22c55e", "#f59e0b", "#a855f7", "#06b6d4", "#ec4899", "#ffffff"]

    color: "#17171a"
    radius: theme.radius
    border.color: "#232328"
    implicitHeight: column.implicitHeight + 32


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
            // The profile's color, flashed on the lightbar when switched to.
            Rectangle {
                width: 22; height: 22; radius: 11
                color: backend.profileColor
                border.color: "#2a2a30"
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: colorMenu.open()
                }
                ToolTip.visible: colorHover.hovered
                ToolTip.text: qsTr("Profile color: the lightbar flashes it when you switch to this profile")
                HoverHandler { id: colorHover }
                Popup {
                    id: colorMenu
                    y: parent.height + 6
                    padding: 8
                    background: Rectangle { color: "#232328"; radius: Math.min(4, theme.radius); border.color: "#34343c" }
                    Row {
                        spacing: 6
                        Repeater {
                            model: root.swatches
                            delegate: Rectangle {
                                required property var modelData
                                width: 24; height: 24; radius: 12
                                color: modelData
                                border.width: Qt.colorEqual(backend.profileColor, modelData) ? 3 : 1
                                border.color: Qt.colorEqual(backend.profileColor, modelData) ? "#e4e4e7" : "#2a2a30"
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { backend.setProfileColor(modelData); colorMenu.close() }
                                }
                            }
                        }
                    }
                }
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
                id: exportButton
                property bool copied: false
                text: copied ? qsTr("Code copied") : qsTr("Export")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                ToolTip.visible: hovered && !copied
                ToolTip.delay: 300
                ToolTip.text: qsTr("Copies this profile as a code you can paste to a friend or keep as a backup")
                onClicked: { clipboard.setText(backend.exportProfile()); copied = true; exportTimer.restart() }
                Timer { id: exportTimer; interval: 1800; onTriggered: exportButton.copied = false }
            }
            Button {
                text: qsTr("Import")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                onClicked: root.importRequested()
            }
            Button {
                text: qsTr("Delete")
                flat: true
                focusPolicy: Qt.NoFocus
                enabled: backend.profileNames.length > 1
                Material.roundedScale: theme.radius
                onClicked: backend.deleteProfile()
            }
            InfoIcon {
                text: qsTr("Each profile is a full set of stick, trigger, and response settings. New ones start from a Rocket League preset: Standard, Freestyle with finer aerial control, or Worn stick for controllers that drift and stop short of the rim.")
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Switch {
                checked: backend.comboSwitch
                focusPolicy: Qt.NoFocus
                onToggled: backend.comboSwitch = checked
            }
            Label { text: qsTr("Switch profiles with PS + d-pad left or right"); color: "#c9c9cf"; font.pixelSize: 13 }
            InfoIcon {
                text: qsTr("Hold the PS button and press left or right on the d-pad to go to the previous or next profile, even in the middle of a match. The lightbar flashes the new profile's color. While PS is held the d-pad does not reach the game, and a PS press on its own still works as usual.")
            }
            Item { Layout.fillWidth: true }
        }

    }
}
