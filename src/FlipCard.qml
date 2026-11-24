import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every flip and double jump, timed from the controller itself: the gap
// between the two jumps, where the stick was, and how clean the diagonal
// was. It sees the controller, not the game, so it cannot tell whether the
// car was on the ground.
Rectangle {
    id: root

    readonly property var stats: backend.flipStats
    readonly property var last: backend.flips.length > 0 ? backend.flips[0] : null
    readonly property real dodge: { backend.gameRevision; return backend.gameSetting("dodge") }
    // Cancels across the recent flips: half-flips and speed flips.
    readonly property var cancels: {
        const out = { half: [], speed: [], speedAngle: [] }
        for (const f of backend.flips) {
            if (f.cancelKind === "half") out.half.push(f.cancel)
            if (f.cancelKind === "speed") { out.speed.push(f.cancel); out.speedAngle.push(Math.min(f.angle, 360 - f.angle)) }
        }
        return out
    }
    function average(list) { return list.reduce((a, b) => a + b, 0) / Math.max(1, list.length) }

    color: "#17171a"
    radius: theme.radius
    border.color: "#232328"
    implicitHeight: column.implicitHeight + 32

    function direction(key) {
        return ({
            "forward": qsTr("Front flip"), "back": qsTr("Back flip"), "left": qsTr("Left side flip"), "right": qsTr("Right side flip"),
            "forward-left": qsTr("Forward-left diagonal"), "forward-right": qsTr("Forward-right diagonal"),
            "back-left": qsTr("Back-left diagonal"), "back-right": qsTr("Back-right diagonal")
        })[key] || key
    }
    function describe(f, brief) {
        if (!f.directional && brief)
            return qsTr("Double jump")
        if (!f.directional)
            return qsTr("Double jump: the stick was at %1%, inside your dodge deadzone").arg(Math.round(f.tilt * 100))
        return direction(f.direction)
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
                text: qsTr("Flip analyzer")
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#e4e4e7"
            }
            InfoIcon {
                text: qsTr("Every time you press jump twice within a second and a half, deadzone times the gap between the presses to the millisecond and notes where the left stick was, as the game received it. Past your Dodge Deadzone that is a flip; inside it, a double jump. After a back flip, pushing the stick forward within 0.6 seconds counts as a half-flip cancel; after a forward diagonal, pulling back counts as a speed flip cancel. It watches the controller, not the game, so it cannot tell whether a cancel worked, and it also counts presses on the ground.")
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Jump button"); color: "#c9c9cf"; font.pixelSize: 13 }
            Segmented {
                options: ["✕", "○", "□", "△", "L1", "R1"]
                current: backend.jumpButton
                onPicked: index => backend.jumpButton = index
            }
            Button {
                visible: backend.flips.length > 0
                text: qsTr("Clear")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                onClicked: backend.clearFlips()
            }
        }

        Label {
            visible: root.last === null
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#8b8b93"
            text: qsTr("Do some flips in free play or a match and they show up here. Keep this window open, or the background service running.")
        }

        // The latest flip, large.
        ColumnLayout {
            visible: root.last !== null
            spacing: 2
            Label {
                text: root.last ? root.describe(root.last) : ""
                color: "#e4e4e7"
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            Label {
                text: {
                    if (!root.last) return ""
                    const parts = [qsTr("%1 ms after the first jump").arg(Math.round(root.last.gap))]
                    if (root.last.directional) parts.push(qsTr("%1% tilt").arg(Math.round(root.last.tilt * 100)))
                    if (root.last.directional && root.last.diagonal)
                        parts.push(qsTr("%1° off the diagonal").arg(root.last.offDiagonal.toLocaleString(Qt.locale(), 'f', 1)))
                    return parts.join(" · ")
                }
                color: "#8b8b93"
            }
            Label {
                visible: root.last !== null && root.last.cancel !== undefined
                text: {
                    if (!root.last || root.last.cancel === undefined) return ""
                    return (root.last.cancelKind === "half" ? qsTr("Half-flip cancel %1 ms after the flip, %2° from straight forward")
                                                            : qsTr("Speed flip cancel %1 ms after the flip, %2° from straight back"))
                           .arg(Math.round(root.last.cancel)).arg(Math.round(root.last.cancelOff))
                }
                color: "#8b8b93"
            }
        }

        // Across the recent flips.
        Flow {
            visible: root.stats.count > 1
            Layout.fillWidth: true
            spacing: 24
            Repeater {
                model: [
                    [qsTr("Flips"), root.stats.count],
                    [qsTr("Average gap"), qsTr("%1 ms").arg(Math.round(root.stats.averageGap))],
                    [qsTr("Directional"), qsTr("%1 of %2").arg(root.stats.directional).arg(root.stats.count)],
                    [qsTr("Diagonals"), root.stats.diagonals > 0 ? qsTr("%1° off on average").arg(root.stats.offDiagonal.toLocaleString(Qt.locale(), 'f', 1)) : "–"],
                    [qsTr("Half-flip cancels"), root.cancels.half.length > 0 ? qsTr("%1 · %2 ms on average").arg(root.cancels.half.length).arg(Math.round(root.average(root.cancels.half))) : "–"],
                    [qsTr("Speed flip cancels"), root.cancels.speed.length > 0 ? qsTr("%1 · %2 ms, %3° from forward").arg(root.cancels.speed.length).arg(Math.round(root.average(root.cancels.speed))).arg(Math.round(root.average(root.cancels.speedAngle))) : "–"]
                ]
                delegate: ColumnLayout {
                    required property var modelData
                    spacing: 0
                    Label { text: modelData[0]; color: "#8b8b93"; font.pixelSize: 12 }
                    Label { text: modelData[1]; color: "#e4e4e7"; font.pixelSize: 16; font.weight: Font.DemiBold }
                }
            }
        }

        // What the numbers suggest, only when they say something.
        ColumnLayout {
            visible: root.stats.count >= 5
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: {
                    const notes = []
                    const s = root.stats
                    if (s.near / s.count >= 0.2)
                        notes.push(qsTr("%1 of your last %2 jumps had the stick within a hair of your Dodge Deadzone (%3), where a flip and a double jump are one twitch apart. Push further for flips, or move Dodge Deadzone in the game away from where your thumb tends to rest.").arg(s.near).arg(s.count).arg(root.dodge.toLocaleString(Qt.locale(), 'f', 2)))
                    if (s.diagonals >= 3 && s.offDiagonal > 6)
                        notes.push(qsTr("Diagonal flips were %1° off the diagonal on average, which tips half-flips and wavedashes toward one side. The precision trainer's Flick drill practises exactly this.").arg(s.offDiagonal.toLocaleString(Qt.locale(), 'f', 1)))
                    if (s.diagonals >= 3 && s.axes < 0.9)
                        notes.push(qsTr("On diagonal flips each axis only reached %1% on average. Diagonal reach at 100% on the left stick lets a diagonal send full on both.").arg(Math.round(s.axes * 100)))
                    return notes
                }
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 10
                    Rectangle { Layout.alignment: Qt.AlignTop; Layout.topMargin: 4; width: 10; height: 10; radius: 5; color: theme.accent }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#d4d4d8"; text: modelData }
                }
            }
        }

        // The last few, newest first.
        ColumnLayout {
            visible: backend.flips.length > 1
            Layout.fillWidth: true
            spacing: 2
            Repeater {
                model: backend.flips.slice(1, 9)
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { Layout.preferredWidth: 260; text: root.describe(modelData, true); color: "#c9c9cf"; font.pixelSize: 13; elide: Text.ElideRight }
                    Label { Layout.preferredWidth: 80; text: qsTr("%1 ms").arg(Math.round(modelData.gap)); color: "#8b8b93"; font.pixelSize: 13 }
                    Label {
                        visible: modelData.directional
                        text: modelData.diagonal ? qsTr("%1% tilt, %2° off").arg(Math.round(modelData.tilt * 100)).arg(modelData.offDiagonal.toLocaleString(Qt.locale(), 'f', 1))
                                                 : qsTr("%1% tilt").arg(Math.round(modelData.tilt * 100))
                        color: "#8b8b93"
                        font.pixelSize: 13
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}
