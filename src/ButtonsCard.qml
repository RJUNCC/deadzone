import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Button remapping for the current profile: each button on the controller,
// and what the game receives when it is pressed.
Rectangle {
    id: root

    readonly property var map: { backend.revision; return backend.buttonMap() }
    readonly property var names: [qsTr("Cross"), qsTr("Circle"), qsTr("Square"), qsTr("Triangle"), "L1", "R1",
                                  qsTr("Share"), qsTr("Options"), "PS", "L3", "R3",
                                  qsTr("D-pad up"), qsTr("D-pad down"), qsTr("D-pad left"), qsTr("D-pad right")]
    // What can be sent: every button, a full trigger press, or nothing.
    readonly property var targets: [{ value: -1, label: qsTr("Nothing") }]
        .concat(names.map((n, i) => ({ value: i, label: n })))
        .concat([{ value: 15, label: qsTr("L2, fully pressed") }, { value: 16, label: qsTr("R2, fully pressed") }])
    readonly property bool changed: map.some((t, i) => t !== i)

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
            spacing: 8
            Label {
                text: qsTr("Buttons")
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#e4e4e7"
            }
            InfoIcon {
                text: qsTr("What each button sends to the game, for this profile. Several buttons can send the same thing, and a button can press a trigger all the way, for example boost on R1. Rocket League's own bindings still apply on top. The overlay and flip analyzer show what the game receives, so they follow this.")
            }
            Item { Layout.fillWidth: true }
            Button {
                visible: root.changed
                text: qsTr("Reset")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                onClicked: backend.resetButtons()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 20
            rowSpacing: 4
            Repeater {
                model: root.names.length
                delegate: RowLayout {
                    required property int index
                    readonly property int target: root.map[index] !== undefined ? root.map[index] : index
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.preferredWidth: 86
                        text: root.names[index]
                        color: "#c9c9cf"
                        font.pixelSize: 13
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        focusPolicy: Qt.NoFocus
                        Material.roundedScale: theme.radius
                        // A changed button wears the accent, so remaps stand out.
                        Material.foreground: parent.target !== index ? theme.accent : "#e4e4e7"
                        font.pixelSize: 13
                        textRole: "label"
                        valueRole: "value"
                        model: root.targets
                        currentIndex: root.targets.findIndex(t => t.value === parent.target)
                        onActivated: backend.setButtonTarget(index, currentValue)
                    }
                }
            }
        }
    }
}
