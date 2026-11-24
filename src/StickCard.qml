import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One stick: its live preview and the four values that shape it.
Rectangle {
    id: root

    property string side: "left"
    property string title
    property point raw
    property point out

    readonly property bool radial: { backend.revision; return backend.setting(side + ".radial") >= 0.5 }

    color: "#17171a"
    radius: theme.radius
    border.color: "#232328"
    implicitHeight: content.implicitHeight + 32

    function percent(p) { return Math.round(Math.min(1, Math.hypot(p.x, p.y)) * 100) }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.title
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#e4e4e7"
                Layout.fillWidth: true
            }
            Repeater {
                model: [{ label: qsTr("Radial"), radial: true }, { label: qsTr("Axial"), radial: false }]
                delegate: Button {
                    required property var modelData
                    text: modelData.label
                    checkable: true
                    checked: root.radial === modelData.radial
                    focusPolicy: Qt.NoFocus
                    contentItem: Label {
                        text: parent.text
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: parent.checked ? theme.accentForeground : "#c9c9cf"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: parent.checked ? theme.accent : parent.hovered ? "#26262c" : "transparent"
                    }
                    implicitHeight: 32
                    leftPadding: 12
                    rightPadding: 12
                    onClicked: backend.change(root.side + ".radial", modelData.radial ? 1 : 0)
                }
            }
        }

        StickPreview {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 190
            Layout.preferredHeight: 190
            raw: root.raw
            out: root.out
            accent: theme.accent
            radial: root.radial
            inner: { backend.revision; return backend.setting(root.side + ".inner") }
            outer: { backend.revision; return backend.setting(root.side + ".outer") }
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("In %1%   Out %2%").arg(root.percent(root.raw)).arg(root.percent(root.out))
            color: "#8b8b93"
            font.pixelSize: 12
            font.features: { "tnum": 1 }
        }

        SettingSlider {
            Layout.fillWidth: true
            key: root.side + ".inner"
            label: qsTr("Deadzone")
            to: 0.5
        }
        SettingSlider {
            Layout.fillWidth: true
            key: root.side + ".outer"
            label: qsTr("Outer edge")
            from: 0.5
        }
        SettingSlider {
            Layout.fillWidth: true
            key: root.side + ".anti"
            label: qsTr("Anti-deadzone")
            to: 0.5
        }
        SettingSlider {
            Layout.fillWidth: true
            key: root.side + ".curve"
            label: qsTr("Response curve")
            from: 0.2
            to: 4
            stepSize: 0.05
            format: function (v) {
                return Math.abs(v - 1) < 0.001 ? qsTr("Linear") : "×" + v.toLocaleString(Qt.locale(), 'f', 2)
            }
        }
    }
}
