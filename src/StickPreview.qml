import QtQuick

// The stick's travel as a circle: the deadzone shaded in the middle, the
// outer edge as a ring, the raw position as a grey dot, and what games
// receive as the accent dot.
Item {
    id: root

    property point raw: Qt.point(0, 0)
    property point out: Qt.point(0, 0)
    property real inner: 0
    property real outer: 1
    property bool radial: true
    property color accent: "#FFD60A"

    implicitWidth: 220
    implicitHeight: 220

    readonly property real size: Math.min(width, height)
    readonly property real cx: width / 2
    readonly property real cy: height / 2

    Canvas {
        id: zones
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d")
            const r = root.size / 2
            ctx.reset()
            ctx.save()
            ctx.beginPath()
            ctx.arc(root.cx, root.cy, r - 1, 0, Math.PI * 2)
            ctx.fillStyle = "#121215"
            ctx.fill()
            ctx.clip()

            // Grid at quarter travel, so values can be read off by eye.
            ctx.strokeStyle = "#1f1f24"
            ctx.lineWidth = 1
            for (let i = 1; i < 4; ++i) {
                ctx.beginPath()
                ctx.arc(root.cx, root.cy, r * i / 4, 0, Math.PI * 2)
                ctx.stroke()
            }
            ctx.beginPath()
            ctx.moveTo(root.cx - r, root.cy); ctx.lineTo(root.cx + r, root.cy)
            ctx.moveTo(root.cx, root.cy - r); ctx.lineTo(root.cx, root.cy + r)
            ctx.stroke()

            // Past the outer edge every position reads as full travel.
            ctx.fillStyle = Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.08)
            ctx.beginPath()
            ctx.arc(root.cx, root.cy, r, 0, Math.PI * 2)
            ctx.arc(root.cx, root.cy, r * root.outer, 0, Math.PI * 2, true)
            ctx.fill()

            ctx.fillStyle = "#34343c"
            const d = r * root.inner
            if (root.radial) {
                ctx.beginPath()
                ctx.arc(root.cx, root.cy, d, 0, Math.PI * 2)
                ctx.fill()
            } else {
                ctx.fillRect(root.cx - r, root.cy - d, 2 * r, 2 * d)
                ctx.fillRect(root.cx - d, root.cy - r, 2 * d, 2 * r)
            }
            ctx.restore()

            ctx.strokeStyle = Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.55)
            ctx.lineWidth = 1.5
            ctx.beginPath()
            ctx.arc(root.cx, root.cy, r * root.outer, 0, Math.PI * 2)
            ctx.stroke()

            ctx.strokeStyle = "#2a2a30"
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.arc(root.cx, root.cy, r - 1, 0, Math.PI * 2)
            ctx.stroke()
        }
    }

    onInnerChanged: zones.requestPaint()
    onOuterChanged: zones.requestPaint()
    onRadialChanged: zones.requestPaint()
    onAccentChanged: zones.requestPaint()
    onWidthChanged: zones.requestPaint()
    onHeightChanged: zones.requestPaint()

    Rectangle {
        width: 10; height: 10; radius: 5
        color: "#8b8b93"
        x: root.cx + root.raw.x * root.size / 2 - width / 2
        y: root.cy + root.raw.y * root.size / 2 - height / 2
    }

    Rectangle {
        width: 14; height: 14; radius: 7
        color: root.accent
        border.color: "#0e0e10"
        border.width: 2
        x: root.cx + root.out.x * root.size / 2 - width / 2
        y: root.cy + root.out.y * root.size / 2 - height / 2
    }
}
