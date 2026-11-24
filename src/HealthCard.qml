import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// How this controller's sticks have aged: drift over time from every
// session check and Measure drift.
Rectangle {
    id: root

    readonly property var entries: backend.health
    readonly property var leftPoints: entries.filter(e => e.leftDrift >= 0)
    readonly property var rightPoints: entries.filter(e => e.rightDrift >= 0)

    color: theme.card
    radius: theme.radius
    border.color: theme.border
    implicitHeight: column.implicitHeight + 32

    function latest(points, key) {
        return points.length > 0 ? Math.round(points[points.length - 1][key] * 100) + "%" : qsTr("not measured")
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: qsTr("Controller health")
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: theme.text
            }
            InfoIcon {
                text: qsTr("Drift is how far a stick wanders while you are not touching it. It grows as a stick wears. Every session check and Measure drift adds a point here, kept per controller, so you can see when a stick starts to go.")
            }
            Item { Layout.fillWidth: true }
            LegendItem { color: theme.accent; mark: "line"; text: qsTr("Left stick %1").arg(root.latest(root.leftPoints, "leftDrift")) }
            LegendItem { color: "#60a5fa"; mark: "line"; text: qsTr("Right stick %1").arg(root.latest(root.rightPoints, "rightDrift")) }
        }

        Label {
            visible: root.leftPoints.length + root.rightPoints.length === 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.muted
            text: qsTr("Nothing measured yet. Run a session check, or Measure drift on a stick, to start tracking.")
        }

        Canvas {
            id: chart

            // Canvases draw with theme colors, so they repaint on a theme switch.

            Connections { target: theme; function onChanged() { chart.requestPaint() } }
            visible: root.leftPoints.length + root.rightPoints.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            onWidthChanged: requestPaint()
            Connections {
                target: backend
                function onHistoryChanged() { chart.requestPaint() }
            }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const all = root.entries
                if (all.length === 0)
                    return
                const t0 = all[0].t, t1 = Math.max(all[all.length - 1].t, t0 + 1)
                let top = 0.1
                for (const e of all)
                    top = Math.max(top, e.leftDrift, e.rightDrift)
                top = Math.ceil(top * 20) / 20
                const pad = 26
                const x = t => pad + (t - t0) / (t1 - t0) * (width - pad - 8)
                const y = v => 8 + (1 - v / top) * (height - 24)
                ctx.strokeStyle = theme.border
                ctx.fillStyle = theme.faint
                ctx.font = "11px sans-serif"
                for (let i = 0; i <= 2; ++i) {
                    const v = top * i / 2
                    ctx.beginPath()
                    ctx.moveTo(pad, y(v)); ctx.lineTo(width - 8, y(v))
                    ctx.stroke()
                    ctx.fillText(Math.round(v * 100) + "%", 0, y(v) + 4)
                }
                for (const [key, color] of [["leftDrift", String(theme.accent)], ["rightDrift", "#60a5fa"]]) {
                    const points = all.filter(e => e[key] >= 0)
                    if (points.length === 0)
                        continue
                    ctx.strokeStyle = color
                    ctx.fillStyle = color
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    points.forEach((e, i) => i === 0 ? ctx.moveTo(x(e.t), y(e[key])) : ctx.lineTo(x(e.t), y(e[key])))
                    ctx.stroke()
                    for (const e of points) {
                        ctx.beginPath()
                        ctx.arc(x(e.t), y(e[key]), 3, 0, Math.PI * 2)
                        ctx.fill()
                    }
                }
            }
        }

        Label {
            visible: backend.healthTrend.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.text
            text: backend.healthTrend
        }
    }
}
