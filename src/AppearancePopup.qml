import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Themes, and whether things move. A theme is a whole palette; Omarchy
// follows the desktop's colors and changes with it.
Popup {
    id: root

    signal picked(string id)

    modal: true
    anchors.centerIn: Overlay.overlay
    padding: 24
    Material.roundedScale: theme.radius
    background: Rectangle {
        radius: theme.radius
        color: theme.surface
        border.color: theme.line
    }
    Overlay.modal: Rectangle {
        color: theme.scrim
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }
    enter: Transition {
        enabled: theme.animations
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 240; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        enabled: theme.animations
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: 140; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; to: 0.98; duration: 140; easing.type: Easing.InCubic }
        }
    }

    ColumnLayout {
        spacing: 16

        Label {
            text: qsTr("Appearance")
            font.pixelSize: 18
            font.weight: Font.DemiBold
            color: theme.text
        }

        GridLayout {
            columns: 4
            columnSpacing: 12
            rowSpacing: 12
            Repeater {
                model: theme.themes
                delegate: Item {
                    id: swatch
                    required property var modelData
                    readonly property bool selected: theme.name === modelData.id
                    implicitWidth: 124
                    implicitHeight: 98

                    Rectangle {
                        id: preview
                        width: parent.width
                        height: 72
                        radius: theme.radius
                        color: swatch.modelData.background
                        border.width: swatch.selected ? 2 : 1
                        border.color: swatch.selected ? theme.accent : theme.line
                        scale: hover.hovered && theme.animations ? 1.04 : 1
                        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                        Behavior on border.color { enabled: theme.animations; ColorAnimation { duration: 160 } }

                        // A tiny card: a surface, a line of text, a slider in the accent.
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 10
                            radius: Math.min(4, theme.radius)
                            color: swatch.modelData.surface
                            Rectangle { x: 8; y: 9; width: parent.width * 0.45; height: 5; radius: 2.5; color: swatch.modelData.accent; opacity: 0.9 }
                            Rectangle { x: 8; y: 21; width: parent.width - 16; height: 3; radius: 1.5; color: Qt.rgba(0.5, 0.5, 0.5, 0.35) }
                            Rectangle { x: 8; y: 21; width: (parent.width - 16) * 0.6; height: 3; radius: 1.5; color: swatch.modelData.accent }
                            Rectangle {
                                x: 8 + (parent.width - 16) * 0.6 - 5; y: 17.5; width: 10; height: 10; radius: 5
                                color: swatch.modelData.accent
                            }
                        }
                    }
                    Label {
                        anchors.top: preview.bottom
                        anchors.topMargin: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: swatch.modelData.name
                        font.pixelSize: 13
                        font.weight: swatch.selected ? Font.DemiBold : Font.Normal
                        color: swatch.selected ? theme.text : theme.muted
                    }
                    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.picked(swatch.modelData.id) }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }

        Repeater {
            model: [
                { key: "animations", label: qsTr("Animations"), help: qsTr("Tabs glide in, scrolling eases, choices slide, and themes fade into each other.") },
                { key: "particles", label: qsTr("Particles"), help: qsTr("Motes drifting behind the cards, sparks when the stick snaps across, and a burst on every flip. They pause while the window is in the background.") }
            ]
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label { text: modelData.label; color: theme.text; font.pixelSize: 14 }
                    Label {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 460
                        text: modelData.help
                        wrapMode: Text.Wrap
                        color: theme.muted
                        font.pixelSize: 12
                    }
                }
                Switch {
                    focusPolicy: Qt.NoFocus
                    checked: theme[modelData.key]
                    onToggled: theme[modelData.key] = checked
                }
            }
        }
    }
}
