import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One stick: its live preview, its response graph, and the values that
// shape it, grouped by the center, the rim, and the response between.
Rectangle {
    id: root

    property string side: "left"
    property string title
    property point raw
    property point out
    property real dodge: -1

    readonly property int shape: { backend.revision; return Math.round(backend.setting(side + ".shape")) }
    readonly property bool split: { backend.revision; return backend.setting(side + ".split") >= 0.5 }
    readonly property bool custom: { backend.revision; return backend.setting(side + ".custom") >= 0.5 }
    readonly property bool splitCurve: { backend.revision; return backend.setting(side + ".splitCurve") >= 0.5 }
    readonly property bool measuring: backend.calibrating.startsWith(side + ".")

    color: "#17171a"
    radius: theme.radius
    border.color: "#232328"
    implicitHeight: content.implicitHeight + 32

    readonly property var shapeDescriptions: [
        qsTr("Round deadzone: every direction needs the same push to leave it. The most natural feel for steering and aiming."),
        qsTr("Square deadzone: reaches further toward the diagonals, so small diagonal wobbles are ignored while straight pushes respond as soon as they leave the edge."),
        qsTr("Each axis has its own deadzone, so pushing straight up never leaks a little sideways."),
        qsTr("Drag the eight handles on the stick picture to set how far the deadzone reaches in each direction. It starts as the shape you had.")
    ]

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Label {
            text: root.title
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: "#e4e4e7"
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
                    side: root.side
                    raw: root.raw
                    out: root.out
                    dodge: root.dodge
                    accent: theme.accent
                    outer: { backend.revision; return backend.setting(root.side + ".outer") }
                    square: { backend.revision; return backend.setting(root.side + ".square") }
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Out  X %1%  Y %2%").arg(Math.round(Math.abs(root.out.x) * 100)).arg(Math.round(Math.abs(root.out.y) * 100))
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

        // What each mark on the two pictures means.
        Flow {
            Layout.fillWidth: true
            spacing: 14
            LegendItem { color: "#8b8b93"; text: qsTr("Your stick") }
            LegendItem { color: theme.accent; text: qsTr("Sent to the game") }
            LegendItem { visible: root.dodge > 0; color: "#60a5fa"; mark: "ring"; text: qsTr("Flip threshold") }
            LegendItem { visible: root.splitCurve; color: theme.accent; mark: "line"; text: qsTr("Horizontal curve") }
            LegendItem { visible: root.splitCurve; color: "#60a5fa"; mark: "line"; text: qsTr("Vertical curve") }
        }

        // Measuring fits the deadzone and outer edge to this stick.
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
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
            InfoIcon {
                text: qsTr("Measure drift: let go of the stick for three seconds and the deadzone is set just above its wobble. Measure edge: roll the stick around the rim for five seconds and the outer edge is set where every direction still reaches.")
            }
            Item { Layout.fillWidth: true }
            ProgressBar {
                visible: root.measuring
                Layout.preferredWidth: 90
                value: backend.calibrationProgress
            }
        }
        Label {
            visible: root.measuring
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: theme.accent
            text: backend.calibrationMessage
        }

        SectionLabel { text: qsTr("Center") }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("Shape")
                color: "#c9c9cf"
                font.pixelSize: 13
            }
            InfoIcon { text: root.shapeDescriptions[root.shape] }
            Item { Layout.fillWidth: true }
            Segmented {
                options: [qsTr("Circle"), qsTr("Square"), qsTr("Cross"), qsTr("Custom")]
                current: root.shape
                onPicked: index => backend.change(root.side + ".shape", index)
            }
        }
        RowLayout {
            visible: root.shape !== 3
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("Separate horizontal and vertical")
                color: "#c9c9cf"
                font.pixelSize: 13
            }
            InfoIcon {
                text: qsTr("Gives the deadzone its own width and height: an oval for Circle, a rectangle for Square, two bands for Cross. A narrow, tall deadzone lets steering respond instantly while ignoring small up and down wobble.")
            }
            Item { Layout.fillWidth: true }
            Switch {
                checked: root.split
                focusPolicy: Qt.NoFocus
                onToggled: backend.change(root.side + ".split", checked ? 1 : 0)
            }
        }
        SettingSlider {
            visible: root.shape !== 3
            Layout.fillWidth: true
            key: root.side + ".inner"
            label: root.split ? qsTr("Horizontal deadzone") : qsTr("Deadzone")
            description: root.split ? qsTr("How far the stick can move left or right before games see anything.")
                                    : qsTr("How far the stick can move before games see anything. Set it just above your stick's drift.")
        }
        SettingSlider {
            visible: root.split && root.shape !== 3
            Layout.fillWidth: true
            key: root.side + ".innerY"
            label: qsTr("Vertical deadzone")
            description: qsTr("How far the stick can move up or down before games see anything.")
        }
        SettingSlider {
            Layout.fillWidth: true
            key: root.side + ".anti"
            label: qsTr("Anti-deadzone")
            description: qsTr("Where the output starts once you leave the deadzone. Set it to a game's own deadzone so the two do not add up.")
            to: 0.5
        }

        SectionLabel { text: qsTr("Edge") }
        SettingSlider {
            Layout.fillWidth: true
            key: root.side + ".outer"
            label: qsTr("Outer edge")
            description: qsTr("How far you push before games see full tilt. Lower it if your stick never quite reaches 100% in some directions.")
            from: 0.5
        }
        SettingSlider {
            Layout.fillWidth: true
            key: root.side + ".square"
            label: qsTr("Diagonal reach")
            description: qsTr("Works with any deadzone shape; it changes the rim, not the center. A round stick like the DualShock 4 only sends about 71% on each axis when pushed fully diagonal. At 100% that diagonal sends 100% on both, like an Xbox stick, which the Out X and Y readout shows. The dashed outline is the reach.")
        }

        SectionLabel { text: qsTr("Response") }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: backend.curvePresetIds()
                delegate: Button {
                    required property var modelData
                    text: backend.curvePresetName(modelData)
                    focusPolicy: Qt.NoFocus
                    implicitHeight: 30
                    leftPadding: 10
                    rightPadding: 10
                    contentItem: Label {
                        text: parent.text
                        font.pixelSize: 12
                        color: "#c9c9cf"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: parent.down ? "#34343c" : parent.hovered ? "#26262c" : "#1e1e23"
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: ({
                        linear: qsTr("What you push is what the game gets."),
                        soft: qsTr("Gentler near the center for small corrections, full speed at the rim."),
                        precise: qsTr("Much finer near the center; for careful aerials and dribbles."),
                        quick: qsTr("Responds strongly to small pushes; for fast turns."),
                        scurve: qsTr("Gentle at both ends and quick through the middle."),
                        dual: qsTr("Fine control for the first half of the travel, then quick to full.")
                    })[modelData]
                    onClicked: backend.applyCurvePreset(root.side, modelData)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("Shape of the response")
                color: "#c9c9cf"
                font.pixelSize: 13
            }
            InfoIcon {
                text: root.custom ? qsTr("Drag the points on the graph to draw your own response. Flat stretches ignore that part of the travel; steep ones make it more sensitive.")
                                  : qsTr("One number bends the whole range. Switch to Custom to draw it point by point.")
            }
            Item { Layout.fillWidth: true }
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
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("Separate horizontal and vertical curves")
                color: "#c9c9cf"
                font.pixelSize: 13
            }
            InfoIcon {
                text: qsTr("Gives up and down its own curve. In Rocket League horizontal is steering on the ground and yaw or roll in the air, while vertical is pitch in the air, so the two can feel different. The graph draws vertical in blue. Horizontal keeps the presets and the drawn curve; vertical uses one number.")
            }
            Item { Layout.fillWidth: true }
            Switch {
                checked: root.splitCurve
                focusPolicy: Qt.NoFocus
                onToggled: backend.change(root.side + ".splitCurve", checked ? 1 : 0)
            }
        }
        SettingSlider {
            visible: !root.custom
            Layout.fillWidth: true
            key: root.side + ".curve"
            label: root.splitCurve ? qsTr("Horizontal curve") : qsTr("Response curve")
            description: qsTr("Bends the middle of the range. Above 1 is finer near the center and faster at the rim; below 1 is the opposite.")
            from: 0.2
            to: 4
            stepSize: 0.05
            format: function (v) {
                return Math.abs(v - 1) < 0.001 ? qsTr("Linear") : "×" + v.toLocaleString(Qt.locale(), 'f', 2)
            }
        }
        SettingSlider {
            visible: root.splitCurve
            Layout.fillWidth: true
            key: root.side + ".curveY"
            label: qsTr("Vertical curve")
            description: qsTr("Bends up and down movement only. Above 1 is finer near the center; below 1 responds sooner.")
            from: 0.2
            to: 4
            stepSize: 0.05
            format: function (v) {
                return Math.abs(v - 1) < 0.001 ? qsTr("Linear") : "×" + v.toLocaleString(Qt.locale(), 'f', 2)
            }
        }
    }
}
