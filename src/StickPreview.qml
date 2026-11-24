import QtQuick

// The stick's travel as a circle: the deadzone shaded in the middle in its
// shape, the outer edge as a ring, the raw position as a grey dot, and what
// games receive as the accent dot. A custom shape shows handles to drag.
Item {
    id: root

    property string side: "left"
    property point raw: Qt.point(0, 0)
    property point out: Qt.point(0, 0)
    property real outer: 1
    property real square: 0
    property real dodge: -1       // Stick travel where Rocket League flips go directional.
    property color accent: "#FFD60A"

    readonly property int shape: { backend.revision; return Math.round(backend.setting(side + ".shape")) }
    readonly property real innerX: { backend.revision; return backend.setting(side + ".inner") }
    readonly property real innerY: { backend.revision; return backend.setting(side + ".split") >= 0.5 ? backend.setting(side + ".innerY") : innerX }
    readonly property var outline: { backend.revision; return backend.deadzoneOutline(side) }
    readonly property var radii: { backend.revision; return [0, 1, 2, 3, 4, 5, 6, 7].map(i => Math.hypot(outline[i * 12].x, outline[i * 12].y)) }

    implicitWidth: 190
    implicitHeight: 190

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

            // Where the output dot can go: the circle stretched toward a
            // square as Diagonal reach rises.
            if (root.square > 0) {
                ctx.strokeStyle = Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.35)
                ctx.setLineDash([3, 3])
                ctx.lineWidth = 1
                ctx.beginPath()
                for (let i = 0; i <= 72; ++i) {
                    const a = i / 72 * Math.PI * 2
                    const c = Math.cos(a), s = Math.sin(a)
                    const reach = 1 + (1 / Math.max(Math.abs(c), Math.abs(s)) - 1) * root.square
                    const px = root.cx + Math.max(-1, Math.min(1, c * reach)) * r
                    const py = root.cy + Math.max(-1, Math.min(1, s * reach)) * r
                    i === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py)
                }
                ctx.stroke()
                ctx.setLineDash([])
            }

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
            if (root.shape === 2) {
                ctx.fillRect(root.cx - r, root.cy - r * root.innerY, 2 * r, 2 * r * root.innerY)
                ctx.fillRect(root.cx - r * root.innerX, root.cy - r, 2 * r * root.innerX, 2 * r)
            } else {
                ctx.beginPath()
                for (let i = 0; i < root.outline.length; ++i) {
                    const p = root.outline[i]
                    i === 0 ? ctx.moveTo(root.cx + p.x * r, root.cy + p.y * r) : ctx.lineTo(root.cx + p.x * r, root.cy + p.y * r)
                }
                ctx.closePath()
                ctx.fill()
            }
            ctx.restore()

            ctx.strokeStyle = Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.55)
            ctx.lineWidth = 1.5
            ctx.beginPath()
            ctx.arc(root.cx, root.cy, r * root.outer, 0, Math.PI * 2)
            ctx.stroke()

            // Rocket League's dodge deadzone, as stick travel: inside it a
            // jump goes straight up, outside it flips in that direction.
            if (root.dodge > 0 && root.dodge <= 1) {
                ctx.strokeStyle = "#60a5fa"
                ctx.setLineDash([5, 4])
                ctx.lineWidth = 1.5
                ctx.beginPath()
                ctx.arc(root.cx, root.cy, r * root.dodge, 0, Math.PI * 2)
                ctx.stroke()
                ctx.setLineDash([])
            }

            ctx.strokeStyle = "#2a2a30"
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.arc(root.cx, root.cy, r - 1, 0, Math.PI * 2)
            ctx.stroke()
        }
    }

    Connections {
        target: backend
        function onRevisionChanged() { zones.requestPaint() }
    }
    onOuterChanged: zones.requestPaint()
    onSquareChanged: zones.requestPaint()
    onDodgeChanged: zones.requestPaint()
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

    // Custom shape: one handle per direction, dragged in or out.
    Repeater {
        model: root.shape === 3 ? 8 : 0
        delegate: Rectangle {
            required property int index
            readonly property real angle: index * Math.PI / 4
            readonly property real reach: root.radii[index]
            width: 14; height: 14; radius: 7
            color: drag.pressed || drag.containsMouse ? root.accent : "#0e0e10"
            border.color: root.accent
            border.width: 2
            x: root.cx + Math.cos(angle) * reach * root.size / 2 - width / 2
            y: root.cy + Math.sin(angle) * reach * root.size / 2 - height / 2

            MouseArea {
                id: drag
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.SizeAllCursor
                onPressed: backend.beginEdit()
                onReleased: backend.endEdit()
                onPositionChanged: mouse => {
                    if (!pressed)
                        return
                    const at = mapToItem(root, mouse.x, mouse.y)
                    // Only the distance along this direction counts.
                    const along = ((at.x - root.cx) * Math.cos(angle) + (at.y - root.cy) * Math.sin(angle)) / (root.size / 2)
                    backend.setRadius(root.side, index, along)
                }
            }
        }
    }
}
