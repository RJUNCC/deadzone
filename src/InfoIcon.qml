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
    border.color: hover.hovered || pinned ? theme.accent : theme.faint

    property bool pinned: false

    Label {
        anchors.centerIn: parent
        text: "i"
        font.pixelSize: 11
        font.weight: Font.Bold
        font.italic: true
        color: hover.hovered || root.pinned ? theme.accentForeground : theme.muted
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
            color: theme.text
            font.pixelSize: 13
            lineHeight: 1.15
        }
        background: Rectangle {
            color: theme.border
            border.color: theme.lineStrong
            radius: Math.min(4, theme.radius)
        }
        onClosed: root.pinned = false
    }
}
