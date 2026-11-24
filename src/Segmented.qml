import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A row of mutually exclusive choices; the checked one wears the accent,
// which slides over to a new choice rather than jumping.
Item {
    id: root

    property var options: []     // Labels.
    property int current: 0
    signal picked(int index)

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    // The accent behind the current choice.
    Rectangle {
        id: highlight
        readonly property Item target: buttons.count > 0 && root.current >= 0 ? buttons.itemAt(root.current) : null
        visible: target !== null
        radius: Math.min(4, theme.radius)
        color: theme.accent
        property real targetX: target ? target.x : 0
        property real targetWidth: target ? target.width : 0
        x: targetX
        y: target ? target.y : 0
        width: targetWidth
        height: target ? target.height : 0
        Behavior on targetX { enabled: theme.animations; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on targetWidth { enabled: theme.animations; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    }

    RowLayout {
    id: row
    anchors.fill: parent
    spacing: 2
    Repeater {
        id: buttons
        model: root.options
        delegate: Button {
            required property int index
            required property var modelData
            text: modelData
            checkable: true
            checked: root.current === index
            focusPolicy: Qt.NoFocus
            implicitHeight: 30
            leftPadding: 12
            rightPadding: 12
            contentItem: Label {
                text: parent.text
                font.pixelSize: 13
                font.weight: Font.Medium
                color: parent.checked ? theme.accentForeground : theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                Behavior on color { enabled: theme.animations; ColorAnimation { duration: 180 } }
            }
            background: Rectangle {
                radius: Math.min(4, theme.radius)
                color: parent.checked ? "transparent" : parent.hovered ? theme.hover : theme.raised
                Behavior on color { enabled: theme.animations; ColorAnimation { duration: 140 } }
            }
            onClicked: root.picked(index)
        }
    }
    }
}
