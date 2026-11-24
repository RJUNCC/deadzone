import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The controller's lightbar and battery.
Rectangle {
    id: root

    readonly property var light: backend.lightbar
    readonly property bool descriptions: ApplicationWindow.window.showDescriptions
    readonly property var swatches: ["#ef4444", "#f97316", "#facc15", "#22c55e", "#06b6d4", "#3b82f6", "#7c3aed", "#ec4899", "#ffffff"]
    readonly property var modeDescriptions: [
        qsTr("The controller keeps its own color."),
        qsTr("One color of your choice."),
        qsTr("Cycles smoothly through every color."),
        qsTr("Green when full, through amber, to red when nearly empty.")
    ]

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
            spacing: 12
            Label {
                text: qsTr("Lightbar")
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#e4e4e7"
            }
            // What the lightbar is showing right now.
            Rectangle {
                visible: root.light.mode !== 0
                width: 16; height: 16; radius: 8
                color: root.light.current
                border.color: "#2a2a30"
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: backend.battery >= 0
                text: backend.charging ? qsTr("Battery %1%, charging").arg(backend.battery)
                                       : qsTr("Battery %1%").arg(backend.battery)
                color: backend.battery >= 0 && backend.battery <= 20 && !backend.charging ? "#f87171" : "#c9c9cf"
                font.pixelSize: 13
            }
        }

        Label {
            visible: !root.light.available
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#8b8b93"
            text: qsTr("This controller has no lightbar deadzone can set.")
        }

        RowLayout {
            visible: root.light.available
            Layout.fillWidth: true
            Segmented {
                options: [qsTr("Off"), qsTr("Color"), qsTr("Rainbow"), qsTr("Battery")]
                current: root.light.mode
                onPicked: index => root.light.mode = index
            }
            Label {
                visible: root.descriptions
                Layout.fillWidth: true
                Layout.leftMargin: 8
                wrapMode: Text.Wrap
                color: "#76767e"
                font.pixelSize: 12
                text: root.modeDescriptions[root.light.mode]
            }
        }

        // Writing the lightbar needs permission once.
        RowLayout {
            visible: root.light.available && root.light.mode !== 0 && !root.light.writable
            Layout.fillWidth: true
            spacing: 12
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#e4e4e7"
                text: qsTr("Only root can change the lightbar until deadzone adds a udev rule that lets you. It asks for your password once.")
            }
            Button {
                text: backend.hideBusy ? qsTr("Waiting for password…") : qsTr("Allow")
                enabled: !backend.hideBusy
                focusPolicy: Qt.NoFocus
                Material.foreground: theme.accentForeground
                background: Rectangle {
                    implicitHeight: 36
                    radius: Math.min(4, theme.radius)
                    color: parent.down ? Qt.darker(theme.accent, 1.2) : theme.accent
                }
                onClicked: backend.allowLightbar()
            }
        }

        Flow {
            visible: root.light.available && root.light.mode === 1
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: root.swatches.concat([theme.accent])
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: 30; height: 30; radius: 15
                    color: modelData
                    border.width: Qt.colorEqual(root.light.color, modelData) ? 3 : 1
                    border.color: Qt.colorEqual(root.light.color, modelData) ? "#e4e4e7" : "#2a2a30"
                    ToolTip.visible: tipArea.containsMouse && index === root.swatches.length
                    ToolTip.text: qsTr("Theme accent")
                    MouseArea {
                        id: tipArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.light.color = parent.color
                    }
                }
            }
        }

        RowLayout {
            visible: root.light.available && root.light.mode === 2
            Layout.fillWidth: true
            Label { text: qsTr("Speed"); color: "#c9c9cf"; font.pixelSize: 13; Layout.preferredWidth: 90 }
            Slider {
                Layout.fillWidth: true
                from: 0.02
                to: 1
                value: root.light.speed
                onMoved: root.light.speed = value
            }
            Label {
                text: qsTr("%1 s per cycle").arg((1 / root.light.speed).toLocaleString(Qt.locale(), 'f', 1))
                color: "#8b8b93"
                font.pixelSize: 13
                font.features: { "tnum": 1 }
            }
        }

        RowLayout {
            visible: root.light.available && root.light.mode !== 0
            Layout.fillWidth: true
            Label { text: qsTr("Brightness"); color: "#c9c9cf"; font.pixelSize: 13; Layout.preferredWidth: 90 }
            Slider {
                Layout.fillWidth: true
                from: 0.05
                to: 1
                value: root.light.brightness
                onMoved: root.light.brightness = value
            }
            Label {
                text: Math.round(root.light.brightness * 100) + "%"
                color: "#8b8b93"
                font.pixelSize: 13
                font.features: { "tnum": 1 }
            }
        }
        Label {
            visible: root.descriptions && root.light.available && root.light.mode !== 0
            Layout.fillWidth: true
            Layout.topMargin: -6
            wrapMode: Text.Wrap
            color: "#76767e"
            font.pixelSize: 12
            text: qsTr("A dimmer lightbar lasts longer on battery. The controller gets its own color back when deadzone closes.")
        }
    }
}
