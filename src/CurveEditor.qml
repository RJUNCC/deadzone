import QtQuick
import QtQuick.Controls

// Stick travel left to right against what games receive bottom to top.
// The line is the whole profile (deadzone, curve, anti-deadzone, outer
// edge); with a custom curve its points can be dragged.
Item {
    id: root

    property string side: "left"
    property real live: 0          // Current raw stick travel.
    property color accent: "#FFD60A"
    readonly property bool custom: { backend.revision; return backend.setting(side + ".custom") >= 0.5 }
    readonly property real inner: { backend.revision; return backend.setting(side + ".inner") }
    readonly property real outer: { backend.revision; return backend.setting(side + ".outer") }
    readonly property real anti: { backend.revision; return backend.setting(side + ".anti") }
    readonly property var points: { backend.revision; return backend.curvePoints(side) }
    readonly property bool splitCurve: { backend.revision; return backend.setting(side + ".splitCurve") >= 0.5 }

    implicitWidth: 190
    implicitHeight: 190

    readonly property real pad: 8
    readonly property real plotW: width - 2 * pad
    readonly property real plotH: height - 2 * pad

    function toX(v) { return pad + v * plotW }
    function toY(v) { return pad + (1 - v) * plotH }

    // A drawn point lives after the deadzone; on the graph it sits where
    // that part of the travel actually is.
    function pointX(p) { return toX(inner + p.x * (outer - inner)) }
    function pointY(p) { return toY(anti + (1 - anti) * p.y) }

    Rectangle {
        anchors.fill: parent
        radius: Math.min(4, theme.radius)
        color: "#121215"
        border.color: "#2a2a30"
    }

    Canvas {
        id: plot
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = "#1f1f24"
            ctx.lineWidth = 1
            for (let i = 1; i < 4; ++i) {
                ctx.beginPath()
                ctx.moveTo(root.toX(i / 4), root.toY(0)); ctx.lineTo(root.toX(i / 4), root.toY(1))
                ctx.moveTo(root.toX(0), root.toY(i / 4)); ctx.lineTo(root.toX(1), root.toY(i / 4))
                ctx.stroke()
            }
            // The straight line is "what you push is what games get".
            ctx.strokeStyle = "#2e2e35"
            ctx.setLineDash([4, 4])
            ctx.beginPath()
            ctx.moveTo(root.toX(0), root.toY(0)); ctx.lineTo(root.toX(1), root.toY(1))
            ctx.stroke()
            ctx.setLineDash([])

            ctx.fillStyle = "#34343c"
            ctx.globalAlpha = 0.5
            ctx.fillRect(root.toX(0), root.toY(1), root.inner * root.plotW, root.plotH)
            ctx.fillStyle = Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.08)
            ctx.globalAlpha = 1
            ctx.fillRect(root.toX(root.outer), root.toY(1), (1 - root.outer) * root.plotW, root.plotH)

            // With separate curves, vertical is drawn too, in blue.
            const lines = root.splitCurve ? [["y", "#60a5fa"], ["x", root.accent]] : [["x", root.accent]]
            for (const [axis, color] of lines) {
                ctx.strokeStyle = color
                ctx.lineWidth = 2
                ctx.beginPath()
                for (let i = 0; i <= 120; ++i) {
                    const x = i / 120
                    const y = backend.responseAt(root.side, x, axis)
                    i === 0 ? ctx.moveTo(root.toX(x), root.toY(y)) : ctx.lineTo(root.toX(x), root.toY(y))
                }
                ctx.stroke()
            }
        }
    }

    Connections {
        target: backend
        function onRevisionChanged() { plot.requestPaint() }
    }
    onAccentChanged: plot.requestPaint()
    onWidthChanged: plot.requestPaint()

    // Where the stick is right now, on the line.
    Rectangle {
        width: 10; height: 10; radius: 5
        color: root.accent
        border.color: "#0e0e10"
        border.width: 2
        readonly property real travel: Math.min(1, root.live)
        x: root.toX(travel) - width / 2
        y: { backend.revision; return root.toY(backend.responseAt(root.side, travel)) - height / 2 }
    }

    Repeater {
        model: root.custom ? root.points.length : 0
        delegate: Rectangle {
            required property int index
            readonly property point p: root.points[index]
            width: 16; height: 16; radius: 8
            color: drag.containsMouse || drag.pressed ? root.accent : "#0e0e10"
            border.color: root.accent
            border.width: 2
            x: root.pointX(p) - width / 2
            y: root.pointY(p) - height / 2

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
                    const span = Math.max(0.001, root.outer - root.inner)
                    const x = ((at.x - root.pad) / root.plotW - root.inner) / span
                    const y = ((1 - (at.y - root.pad) / root.plotH) - root.anti) / Math.max(0.001, 1 - root.anti)
                    backend.setCurvePoint(root.side, index, x, y)
                }
            }
        }
    }
}
