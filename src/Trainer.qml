import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The precision trainer: targets appear around the stick and the time to
// land on each, and hold it, is measured. It reads what the game receives,
// so it tests the profile as well as the hand.
Rectangle {
    id: root

    property string mode: "flick"     // flick: full-tilt directions; control: small targets mid-stick
    property string side: "left"
    signal closed()

    readonly property int count: 16
    readonly property real targetRadius: mode === "flick" ? 0.16 : 0.08
    readonly property int hold: mode === "flick" ? 100 : 350

    property string phase: "intro"    // intro, center, target, done
    property var targets: []
    property int index: 0
    property real spawned: 0
    property real enteredAt: -1
    property int misses: 0
    property var times: []
    property var directionTimes: [[], [], [], [], [], [], [], []]
    property bool newBest: false
    property real average: 0

    readonly property point stick: side === "left" ? backend.leftOut : backend.rightOut

    color: "#0e0e10"

    function directionName(i) {
        return [qsTr("right"), qsTr("down-right"), qsTr("down"), qsTr("down-left"),
                qsTr("left"), qsTr("up-left"), qsTr("up"), qsTr("up-right")][i]
    }

    function shuffle(list) {
        for (let i = list.length - 1; i > 0; --i) {
            const j = Math.floor(Math.random() * (i + 1))
            const t = list[i]; list[i] = list[j]; list[j] = t
        }
        return list
    }

    function start() {
        const list = []
        if (mode === "flick") {
            for (let k = 0; k < count; ++k)
                list.push({ dir: k % 8, angle: (k % 8) * Math.PI / 4, reach: 0.9 })
            shuffle(list)
        } else {
            for (let k = 0; k < count; ++k) {
                const angle = Math.random() * Math.PI * 2
                list.push({ dir: Math.round(angle / (Math.PI / 4)) % 8, angle: angle, reach: 0.3 + Math.random() * 0.35 })
            }
        }
        targets = list
        index = 0
        misses = 0
        times = []
        directionTimes = [[], [], [], [], [], [], [], []]
        newBest = false
        phase = mode === "flick" ? "center" : "target"
        spawned = Date.now()
        enteredAt = -1
    }

    function finish() {
        phase = "done"
        average = times.reduce((a, b) => a + b, 0) / Math.max(1, times.length)
        newBest = backend.saveTrainerResult(mode, average, misses)
    }

    Timer {
        interval: 16
        repeat: true
        running: root.visible && (root.phase === "center" || root.phase === "target")
        onTriggered: {
            const s = root.stick
            const now = Date.now()
            if (root.phase === "center") {
                // Flicks start from a released stick, as flips do.
                if (Math.hypot(s.x, s.y) < 0.2) {
                    root.phase = "target"
                    root.spawned = now
                    root.enteredAt = -1
                }
                return
            }
            const t = root.targets[root.index]
            const tx = Math.cos(t.angle) * t.reach, ty = Math.sin(t.angle) * t.reach
            const inside = Math.hypot(s.x - tx, s.y - ty) <= root.targetRadius
            if (inside && root.enteredAt < 0) {
                root.enteredAt = now
            } else if (!inside && root.enteredAt >= 0) {
                // Landed, then slid off before holding it: an overshoot.
                root.enteredAt = -1
                ++root.misses
            } else if (inside && now - root.enteredAt >= root.hold) {
                const time = root.enteredAt - root.spawned
                root.times = root.times.concat([time])
                const d = root.directionTimes
                d[t.dir].push(time)
                root.directionTimes = d
                if (++root.index >= root.count) {
                    root.finish()
                } else {
                    root.phase = root.mode === "flick" ? "center" : "target"
                    root.spawned = now
                    root.enteredAt = -1
                }
            }
        }
    }

    Shortcut { sequence: "Escape"; enabled: root.visible; onActivated: root.closed() }
    Shortcut { sequence: "Space"; enabled: root.visible && root.phase !== "center" && root.phase !== "target"; onActivated: root.start() }
    MouseArea { anchors.fill: parent }   // Nothing behind is clickable while training.

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 760)
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Precision trainer")
                font.pixelSize: 22
                font.weight: Font.Bold
                color: "#e4e4e7"
            }
            Item { Layout.fillWidth: true }
            Segmented {
                enabled: root.phase === "intro" || root.phase === "done"
                options: [qsTr("Flick"), qsTr("Control")]
                current: root.mode === "flick" ? 0 : 1
                onPicked: index => { root.mode = index === 0 ? "flick" : "control"; root.phase = "intro" }
            }
            Segmented {
                enabled: root.phase === "intro" || root.phase === "done"
                options: [qsTr("Left stick"), qsTr("Right stick")]
                current: root.side === "left" ? 0 : 1
                onPicked: index => root.side = index === 0 ? "left" : "right"
            }
            Button {
                text: qsTr("Close")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                onClicked: root.closed()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 28

            // The arena: the stick's travel, the target, and where the game's stick is.
            Item {
                id: arena
                Layout.preferredWidth: 340
                Layout.preferredHeight: 340
                readonly property real r: width / 2

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "#121215"
                    border.color: "#2a2a30"
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: arena.width * 0.4; height: width; radius: width / 2
                    color: "transparent"
                    border.color: root.phase === "center" ? theme.accent : "#1f1f24"
                    border.width: root.phase === "center" ? 2 : 1
                }
                Rectangle {
                    visible: root.phase === "target"
                    readonly property var t: root.targets[root.index] || { angle: 0, reach: 0 }
                    width: root.targetRadius * 2 * arena.r; height: width; radius: width / 2
                    x: arena.r + Math.cos(t.angle) * t.reach * arena.r - width / 2
                    y: arena.r + Math.sin(t.angle) * t.reach * arena.r - height / 2
                    color: root.enteredAt >= 0 ? Qt.rgba(0.29, 0.87, 0.5, 0.45) : Qt.rgba(1, 1, 1, 0.08)
                    border.color: root.enteredAt >= 0 ? "#4ade80" : "#e4e4e7"
                    border.width: 2
                }
                Rectangle {
                    width: 16; height: 16; radius: 8
                    color: theme.accent
                    border.color: "#0e0e10"
                    border.width: 2
                    x: arena.r + root.stick.x * arena.r - width / 2
                    y: arena.r + root.stick.y * arena.r - height / 2
                }
                Label {
                    anchors.centerIn: parent
                    visible: root.phase === "center"
                    text: qsTr("Let go")
                    color: theme.accent
                    font.pixelSize: 15
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 12

                Label {
                    visible: root.phase === "intro"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#d4d4d8"
                    lineHeight: 1.2
                    text: root.mode === "flick"
                          ? qsTr("Flick: a target appears near the rim in one of eight directions. Let go of the stick, then flick into the target as fast and clean as you can, the way you set up flips, half-flips, and wavedashes. %1 targets, two in each direction.").arg(root.count)
                          : qsTr("Control: small targets appear part of the way out. Move onto each and hold still for a moment, the way you steer through a dribble or line up an aerial. Slipping off before the hold counts as an overshoot. %1 targets.").arg(root.count)
                }
                Label {
                    visible: root.phase === "center" || root.phase === "target"
                    text: qsTr("Target %1 of %2").arg(root.index + 1).arg(root.count)
                    color: "#e4e4e7"
                    font.pixelSize: 18
                }
                Label {
                    visible: root.phase === "center" || root.phase === "target"
                    text: qsTr("Overshoots: %1").arg(root.misses)
                    color: "#8b8b93"
                }

                ColumnLayout {
                    visible: root.phase === "done"
                    spacing: 6
                    Label {
                        text: qsTr("%1 ms on average").arg(Math.round(root.average))
                        color: "#e4e4e7"
                        font.pixelSize: 26
                        font.weight: Font.Bold
                    }
                    Label {
                        visible: root.newBest
                        text: qsTr("New best")
                        color: theme.accent
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: qsTr("%n overshoot(s)", "", root.misses)
                        color: "#c9c9cf"
                    }
                    Label {
                        visible: root.mode === "flick"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: "#c9c9cf"
                        text: {
                            let slowest = -1, worst = -1
                            for (let i = 0; i < 8; ++i) {
                                const list = root.directionTimes[i]
                                if (list.length === 0) continue
                                const avg = list.reduce((a, b) => a + b, 0) / list.length
                                if (avg > worst) { worst = avg; slowest = i }
                            }
                            return slowest < 0 ? "" : qsTr("Slowest direction: %1, at %2 ms.").arg(root.directionName(slowest)).arg(Math.round(worst))
                        }
                    }
                    // The last results in this mode, fastest lowest.
                    Canvas {
                        id: progress
                        Layout.preferredWidth: 300
                        Layout.preferredHeight: 70
                        property var history: []
                        onVisibleChanged: if (visible) { history = backend.trainerHistory(root.mode).slice(-20); requestPaint() }
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            if (history.length < 2) return
                            const values = history.map(h => h.ms)
                            const lo = Math.min(...values), hi = Math.max(...values, lo + 1)
                            ctx.strokeStyle = String(theme.accent)
                            ctx.lineWidth = 2
                            ctx.beginPath()
                            values.forEach((v, i) => {
                                const x = i / (values.length - 1) * (width - 4) + 2
                                const y = 4 + (v - lo) / (hi - lo) * (height - 8)
                                i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y)
                            })
                            ctx.stroke()
                        }
                    }
                    Label {
                        visible: progress.history.length >= 2
                        text: qsTr("Your last %n run(s); lower is faster.", "", progress.history.length)
                        color: "#6b6b73"
                        font.pixelSize: 12
                    }
                }

                Button {
                    visible: root.phase === "intro" || root.phase === "done"
                    text: root.phase === "done" ? qsTr("Go again") : qsTr("Start")
                    focusPolicy: Qt.NoFocus
                    Material.foreground: theme.accentForeground
                    background: Rectangle {
                        implicitHeight: 38
                        implicitWidth: 120
                        radius: Math.min(4, theme.radius)
                        color: parent.down ? Qt.darker(theme.accent, 1.2) : theme.accent
                    }
                    onClicked: root.start()
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#6b6b73"
                    font.pixelSize: 12
                    text: qsTr("It reads what the game receives, so your deadzone and curve count too: try a profile change, then run it again. Space starts, Esc closes.")
                }
            }
        }
    }
}
