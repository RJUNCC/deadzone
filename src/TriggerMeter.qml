import QtQuick

// A trigger's travel left to right: shaded ends where nothing changes, the
// accent fill for what games receive, and a line for the raw position.
Item {
    id: root

    property real raw: 0
    property real out: 0
    property real inner: 0
    property real outer: 1
    property color accent: "#FFD60A"

    implicitHeight: 16

    Rectangle {
        anchors.fill: parent
        radius: Math.min(4, theme.radius)
        color: theme.sunken
        border.color: theme.line
        clip: true

        Rectangle {
            width: parent.width * root.inner
            height: parent.height
            color: theme.lineStrong
        }
        Rectangle {
            x: parent.width * root.outer
            width: parent.width * (1 - root.outer)
            height: parent.height
            color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.12)
        }
        Rectangle {
            width: parent.width * root.out
            height: parent.height
            color: root.accent
            opacity: 0.85
        }
        Rectangle {
            x: Math.min(parent.width - width, parent.width * root.raw)
            width: 2
            height: parent.height
            color: theme.text
        }
    }
}
