import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Which profile is in use, and making, copying, renaming, and deleting them.
Rectangle {
    id: root

    signal renameRequested()

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
            InfoIcon {
                text: qsTr("Each profile is a full set of stick, trigger, and response settings. New ones start from a Rocket League preset: Standard, Freestyle with finer aerial control, or Worn stick for controllers that drift and stop short of the rim.")
            }
            Item { Layout.fillWidth: true }
        }

    }
}
