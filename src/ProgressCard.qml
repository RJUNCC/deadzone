import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Practice numbers by day, so improvement over weeks is visible.
Rectangle {
    id: root

    property int metric: 0
    readonly property var metrics: [
        { key: "flipGap", label: qsTr("Flip gap"), unit: "ms", lowerIsBetter: true,
          help: qsTr("Average time between the first jump and the flip, each day.") },
        { key: "offDiagonal", label: qsTr("Diagonal accuracy"), unit: "°", lowerIsBetter: true,
          help: qsTr("How far diagonal flips were from a true diagonal, on average, each day.") },
        { key: "halfCancel", label: qsTr("Half-flip cancel"), unit: "ms", lowerIsBetter: true,
          help: qsTr("How soon after a back flip the stick came forward, on average, each day.") },
        { key: "speedCancel", label: qsTr("Speed flip cancel"), unit: "ms", lowerIsBetter: true,
          help: qsTr("How soon after a forward diagonal flip the stick came back, on average, each day.") },
        { key: "trainer:flick", label: qsTr("Trainer flick"), unit: "ms", lowerIsBetter: true,
          help: qsTr("Your best Flick run in the precision trainer, each day.") },
        { key: "trainer:control", label: qsTr("Trainer control"), unit: "ms", lowerIsBetter: true,
          help: qsTr("Your best Control run in the precision trainer, each day.") }
    ]
    readonly property var current: metrics[metric]
    readonly property var points: { backend.health; backend.flips; return backend.progress(current.key).slice(-30) }

    color: theme.card
    radius: theme.radius
    border.color: theme.border
    implicitHeight: column.implicitHeight + 32

    function format(v) {
        return current.unit === "°" ? v.toLocaleString(Qt.locale(), 'f', 1) + "°" : qsTr("%1 ms").arg(Math.round(v))
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
                text: qsTr("Progress")
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: theme.text
            }
            InfoIcon { text: root.current.help + " " + qsTr("Lower is better. Kept per controller, for the last 30 days you practised.") }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Lower is better"); color: theme.faint; font.pixelSize: 12 }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: root.metrics
                delegate: Button {
                    required property var modelData
                    required property int index
                    text: modelData.label
                    focusPolicy: Qt.NoFocus
                    implicitHeight: 30
                    leftPadding: 10
                    rightPadding: 10
                    contentItem: Label {
                        text: parent.text
                        font.pixelSize: 12
                        color: root.metric === parent.index ? theme.accentForeground : theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: root.metric === parent.index ? theme.accent : parent.hovered ? theme.hover : theme.raised
                    }
                    onClicked: root.metric = index
                }
            }
        }

        Label {
            visible: root.points.length === 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.muted
            text: qsTr("Nothing for this yet. It fills in as you play and practise, one point per day.")
        }

        Canvas {
            id: chart

            // Canvases draw with theme colors, so they repaint on a theme switch.

            Connections { target: theme; function onChanged() { chart.requestPaint() } }
            visible: root.points.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            onWidthChanged: requestPaint()
            Connections {
                target: root
                function onPointsChanged() { chart.requestPaint() }
            }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const pts = root.points
                if (!pts.length) return
                const values = pts.map(p => p.value)
                let lo = Math.min(...values), hi = Math.max(...values)
                if (hi - lo < 1e-6) { lo -= 1; hi += 1 }
                const pad = 44
                const x = i => pad + (pts.length === 1 ? (width - pad) / 2 : i / (pts.length - 1) * (width - pad - 10))
                const y = v => 10 + (1 - (v - lo) / (hi - lo)) * (height - 34)
                ctx.font = "11px sans-serif"
                ctx.fillStyle = theme.faint
                ctx.strokeStyle = theme.border
                for (const v of [lo, (lo + hi) / 2, hi]) {
                    ctx.beginPath(); ctx.moveTo(pad, y(v)); ctx.lineTo(width - 10, y(v)); ctx.stroke()
                    ctx.fillText(root.format(v), 0, y(v) + 4)
                }
                ctx.fillText(pts[0].day, pad, height - 4)
                if (pts.length > 1) ctx.fillText(pts[pts.length - 1].day, width - 76, height - 4)
                ctx.strokeStyle = String(theme.accent)
                ctx.fillStyle = String(theme.accent)
                ctx.lineWidth = 2
                ctx.beginPath()
                pts.forEach((p, i) => i === 0 ? ctx.moveTo(x(i), y(p.value)) : ctx.lineTo(x(i), y(p.value)))
                ctx.stroke()
                pts.forEach((p, i) => { ctx.beginPath(); ctx.arc(x(i), y(p.value), 3, 0, Math.PI * 2); ctx.fill() })
            }
        }

        Label {
            visible: root.points.length >= 2
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.text
            text: {
                const pts = root.points
                if (pts.length < 2) return ""
                const first = pts[0].value, last = pts[pts.length - 1].value
                const better = last < first
                return (better ? qsTr("Improved from %1 to %2 over %n day(s) of practice.", "", pts.length)
                               : qsTr("From %1 to %2 over %n day(s) of practice.", "", pts.length))
                       .arg(root.format(first)).arg(root.format(last))
            }
        }
    }
}
