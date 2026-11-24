import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Watches a stretch of real play, then shows where the sticks spent their
// time and suggests changes, each with its reason and an Apply button.
Rectangle {
    id: root

    property int minutes: 5
    readonly property var durations: [2, 5, 10]

    color: theme.card
    radius: theme.radius
    border.color: theme.border
    implicitHeight: column.implicitHeight + 32

    function clock(seconds) {
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }

    component Heatmap: ColumnLayout {
        id: heat
        property bool leftStick: true
        property string title
        spacing: 4
        Canvas {
            id: canvas
            // Canvases draw with theme colors, so they repaint on a theme switch.
            Connections { target: theme; function onChanged() { canvas.requestPaint() } }
            Layout.preferredWidth: 150
            Layout.preferredHeight: 150
            Connections {
                target: backend
                function onSessionChanged() { canvas.requestPaint() }
            }
            onPaint: {
                const ctx = getContext("2d")
                const cells = backend.sessionHeatmap(heat.leftStick)
                const n = Math.round(Math.sqrt(cells.length))
                const size = width / n
                const a = Qt.color(theme.accent)
                ctx.reset()
                ctx.save()
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, width / 2 - 1, 0, Math.PI * 2)
                ctx.fillStyle = theme.sunken
                ctx.fill()
                ctx.clip()
                for (let i = 0; i < cells.length; ++i) {
                    if (cells[i] <= 0)
                        continue
                    ctx.fillStyle = Qt.rgba(a.r, a.g, a.b, 0.12 + 0.88 * cells[i])
                    ctx.fillRect((i % n) * size, Math.floor(i / n) * size, size + 0.5, size + 0.5)
                }
                ctx.restore()
                ctx.strokeStyle = theme.line
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, width / 2 - 1, 0, Math.PI * 2)
                ctx.stroke()
            }
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: heat.title
            color: theme.muted
            font.pixelSize: 12
        }
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: qsTr("Session check")
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: theme.text
            }
            InfoIcon {
                text: qsTr("Play normally while deadzone watches the controller, not the game. Afterwards it suggests changes from what it saw: how much the sticks drift at rest, how far they reach in each direction, where on the stick you spend your time, and how deep you press the triggers. It cannot tell whether a change makes you play better, only whether the settings fit how you move. Keep this window open while it records.")
            }
            Item { Layout.fillWidth: true }
        }

        // Before: pick a length and start.
        RowLayout {
            visible: !backend.recording && !backend.sessionDone
            Layout.fillWidth: true
            spacing: 12
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.textSecondary
                text: qsTr("Start it, then play a few matches or free play as you normally would. Include some moments of letting go of the sticks.")
            }
            Segmented {
                options: root.durations.map(m => qsTr("%1 min").arg(m))
                current: root.durations.indexOf(root.minutes)
                onPicked: index => root.minutes = root.durations[index]
            }
            Button {
                text: qsTr("Start")
                enabled: backend.controllers.length > 0
                focusPolicy: Qt.NoFocus
                Material.foreground: theme.accentForeground
                background: Rectangle {
                    implicitHeight: 36
                    radius: Math.min(4, theme.radius)
                    color: parent.down ? Qt.darker(theme.accent, 1.2) : theme.accent
                }
                onClicked: backend.startSession(root.minutes)
            }
        }

        // During: how far along, and a way out.
        RowLayout {
            visible: backend.recording
            Layout.fillWidth: true
            spacing: 12
            Rectangle { width: 10; height: 10; radius: 5; color: "#ef4444" }
            Label {
                text: qsTr("Watching: %1 of %2. Play normally.").arg(root.clock(backend.sessionElapsed)).arg(root.clock(backend.sessionDuration))
                color: theme.text
                font.features: { "tnum": 1 }
            }
            ProgressBar {
                Layout.fillWidth: true
                value: backend.sessionDuration > 0 ? backend.sessionElapsed / backend.sessionDuration : 0
            }
            Button {
                text: qsTr("Stop now")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                onClicked: backend.stopSession()
            }
        }

        // After: where the time went, and what to change.
        ColumnLayout {
            visible: backend.sessionDone
            Layout.fillWidth: true
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 20
                Heatmap { leftStick: true; title: qsTr("Left stick") }
                Heatmap { leftStick: false; title: qsTr("Right stick") }
                Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    wrapMode: Text.Wrap
                    color: theme.muted
                    font.pixelSize: 12
                    text: qsTr("Brighter means more time spent there in %1 of play. The center, where the stick rests, is left out of the scale.").arg(root.clock(backend.sessionElapsed))
                }
            }

            Label {
                visible: backend.sessionElapsed < 60 && backend.sessionSuggestions.length === 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.accent
                text: qsTr("That was too short to judge. Record at least a minute of play.")
            }
            Label {
                visible: backend.sessionElapsed >= 60 && backend.sessionSuggestions.length === 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#4ade80"
                text: qsTr("Nothing to change: your settings fit how you used the controller.")
            }

            Repeater {
                model: backend.sessionSuggestions
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 10
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 4
                        width: 10; height: 10; radius: 5
                        color: theme.accent
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: theme.text
                            font.weight: Font.Medium
                            text: modelData.text
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: theme.muted
                            font.pixelSize: 12
                            text: modelData.why
                        }
                    }
                    Button {
                        text: qsTr("Apply")
                        flat: true
                        focusPolicy: Qt.NoFocus
                        Material.roundedScale: theme.radius
                        onClicked: backend.applySuggestion(index)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    visible: backend.sessionSuggestions.length > 1
                    text: qsTr("Apply all")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    Material.roundedScale: theme.radius
                    onClicked: backend.applyAllSuggestions()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Record again")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    Material.roundedScale: theme.radius
                    onClicked: backend.clearSession()
                }
            }
        }
    }
}
