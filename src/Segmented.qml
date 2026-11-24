import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A row of mutually exclusive choices; the checked one wears the accent.
RowLayout {
    id: root

    property var options: []     // Labels.
    property int current: 0
    signal picked(int index)

    spacing: 2

    Repeater {
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
                color: parent.checked ? theme.accentForeground : "#c9c9cf"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Math.min(4, theme.radius)
                color: parent.checked ? theme.accent : parent.hovered ? "#26262c" : "#1e1e23"
            }
            onClicked: root.picked(index)
        }
    }
}
