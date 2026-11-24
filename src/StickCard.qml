import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One stick: its live preview, its response graph, and the values that
// shape it, each with what it does.
Rectangle {
    id: root

    property string side: "left"
    property string title
    property point raw
    property point out

    readonly property int shape: { backend.revision; return Math.round(backend.setting(side + ".shape")) }
    readonly property bool custom: { backend.revision; return backend.setting(side + ".custom") >= 0.5 }
    readonly property bool measuring: backend.calibrating.startsWith(side + ".")
    readonly property bool descriptions: ApplicationWindow.window.showDescriptions

    color: "#17171a"
    radius: theme.radius
    border.color: "#232328"
    implicitHeight: content.implicitHeight + 32

    function percent(p) { return Math.round(Math.min(1, Math.hypot(p.x, p.y)) * 100) }

    readonly property var shapeDescriptions: [
        qsTr("Round deadzone: every direction needs the same push to leave it. The most natural feel for steering and aiming."),
        qsTr("Square deadzone: reaches further toward the diagonals, so small diagonal wobbles are ignored while straight pushes respond as soon as they leave the edge."),
        qsTr("Each axis has its own deadzone, so pushing straight up never leaks a little sideways. Good for racing, menus, and anything that wants clean straight lines.")
    ]

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.title
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#e4e4e7"
                Layout.fillWidth: true
            }
            Segmented {
                options: [qsTr("Circle"), qsTr("Square"), qsTr("Cross")]
                current: root.shape
                onPicked: index => backend.change(root.side + ".shape", index)
            }
        }

        Label {
            visible: root.descriptions
            Layout.fillWidth: true
            text: root.shapeDescriptions[root.shape]
            wrapMode: Text.Wrap
            color: "#76767e"
            font.pixelSize: 12
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                StickPreview {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 180
                    Layout.preferredHeight: 180
                    raw: root.raw
                    out: root.out
                    accent: theme.accent
                    shape: root.shape
                    inner: { backend.revision; return backend.setting(root.side + ".inner") }
                    outer: { backend.revision; return backend.setting(root.side + ".outer") }
                    square: { backend.revision; return backend.setting(root.side + ".square") }
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("In %1%   Out %2%").arg(root.percent(root.raw)).arg(root.percent(root.out))
                    color: "#8b8b93"
                    font.pixelSize: 12
                    font.features: { "tnum": 1 }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                CurveEditor {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 180
                    Layout.preferredHeight: 180
                    side: root.side
                    accent: theme.accent
                    live: Math.hypot(root.raw.x, root.raw.y)
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Travel → output")
                    color: "#8b8b93"
                    font.pixelSize: 12
                }
            }
        }

        // Measuring fits the deadzone and outer edge to this stick.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                text: qsTr("Measure drift")
                flat: true
                focusPolicy: Qt.NoFocus
                enabled: backend.calibrating.length === 0
                Material.roundedScale: theme.radius
                onClicked: backend.calibrate(root.side, "drift")
            }
            Button {
                text: qsTr("Measure edge")
                flat: true
                focusPolicy: Qt.NoFocus
                enabled: backend.calibrating.length === 0
                Material.roundedScale: theme.radius
                onClicked: backend.calibrate(root.side, "edge")
            }
            Item { Layout.fillWidth: true }
            ProgressBar {
                visible: root.measuring
                Layout.preferredWidth: 90
                value: backend.calibrationProgress
            }
        }
        Label {
            visible: root.measuring || (root.descriptions && backend.calibrating.length === 0)
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: root.measuring ? theme.accent : "#76767e"
            text: root.measuring ? backend.calibrationMessage
                                 : qsTr("Measure drift: let go of the stick for three seconds and the deadzone is set just above its wobble. Measure edge: roll the stick around the rim for five seconds and the outer edge is set where every direction still reaches.")
        }

        SettingSlider {
            Layout.fillWidth: true
            key: root.side + ".inner"
            label: qsTr("Deadzone")
            description: qsTr("How far the stick can move before games see anything. Set it just above your stick's drift.")
            to: 0.5
        }
        SettingSlider {
            Layout.fillWidth: true
            key: root.side + ".outer"
            label: qsTr("Outer edge")
            description: qsTr("How far you push before games see full tilt. Lower it if your stick never quite reaches 100% in some directions.")
            from: 0.5
        }
        SettingSlider {
            Layout.fillWidth: true
            key: root.side + ".anti"
            label: qsTr("Anti-deadzone")
            description: qsTr("Where the output starts once you leave the deadzone. Set it to a game's own deadzone so the two do not add up.")
            to: 0.5
        }
        SettingSlider {
            Layout.fillWidth: true
            key: root.side + ".square"
            label: qsTr("Square output")
            description: qsTr("Lets diagonals reach full tilt on both axes, like an Xbox stick. A round stick like the DualShock 4 otherwise tops out near 71% on each axis at a diagonal.")
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Response")
                color: "#c9c9cf"
                font.pixelSize: 13
                Layout.fillWidth: true
            }
            Button {
                visible: root.custom
                text: qsTr("Straighten")
                flat: true
                focusPolicy: Qt.NoFocus
                implicitHeight: 30
                Material.roundedScale: theme.radius
                onClicked: backend.resetCurve(root.side)
            }
            Segmented {
                options: [qsTr("Curve"), qsTr("Custom")]
                current: root.custom ? 1 : 0
                onPicked: index => backend.change(root.side + ".custom", index)
            }
        }
        Label {
            visible: root.descriptions && root.custom
            Layout.fillWidth: true
            Layout.topMargin: -6
            text: qsTr("Drag the points on the graph to draw your own response. Flat stretches ignore that part of the travel; steep ones make it more sensitive.")
            wrapMode: Text.Wrap
            color: "#76767e"
            font.pixelSize: 12
        }
        SettingSlider {
            visible: !root.custom
            Layout.fillWidth: true
            Layout.topMargin: -6
            key: root.side + ".curve"
            label: qsTr("Response curve")
            description: qsTr("Bends the middle of the range. Above 1 is finer near the center and faster at the rim; below 1 is the opposite.")
            from: 0.2
            to: 4
            stepSize: 0.05
            format: function (v) {
                return Math.abs(v - 1) < 0.001 ? qsTr("Linear") : "×" + v.toLocaleString(Qt.locale(), 'f', 2)
            }
        }
    }
}
