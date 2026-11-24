import QtQuick
import QtQuick.Controls

// A small "i" that explains the thing beside it on hover, or on click for
// those who never hover.
Rectangle {
    id: root

    property string text

    visible: text.length > 0
    implicitWidth: 16
    implicitHeight: 16
    radius: 8
    color: hover.hovered || pinned ? theme.accent : "transparent"
    border.color: hover.hovered || pinned ? theme.accent : "#55555d"

    property bool pinned: false

    Label {
        anchors.centerIn: parent
        text: "i"
        font.pixelSize: 11
        font.weight: Font.Bold
        font.italic: true
        color: hover.hovered || root.pinned ? theme.accentForeground : "#8b8b93"
    }

    HoverHandler { id: hover; cursorShape: Qt.WhatsThisCursor }
    TapHandler { onTapped: root.pinned = !root.pinned }

    ToolTip {
        visible: hover.hovered || root.pinned
        delay: root.pinned ? 0 : 150
        width: Math.min(360, implicitContentWidth + leftPadding + rightPadding)
        contentItem: Label {
            text: root.text
            wrapMode: Text.Wrap
            color: "#e4e4e7"
            font.pixelSize: 13
            lineHeight: 1.15
        }
        background: Rectangle {
            color: "#232328"
            border.color: "#34343c"
            radius: Math.min(4, theme.radius)
        }
        onClosed: root.pinned = false
    }
}
