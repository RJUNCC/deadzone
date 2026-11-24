import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One profile value; what it does sits behind the info icon. A drag is a
// single undo step; arrow keys on a focused slider are one step each.
ColumnLayout {
    id: root

    property string key
    property string label
    property string description
    property real from: 0
    property real to: 1
    property real stepSize: 0.01
    property var format: function (v) { return Math.round(v * 100) + "%" }

    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Label {
            text: root.label
            color: "#c9c9cf"
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        InfoIcon { text: root.description }
        Item { Layout.fillWidth: true }
        Label {
            text: root.format(slider.value)
            color: "#8b8b93"
            font.pixelSize: 13
            font.features: { "tnum": 1 }
        }
    }

    Label {
        visible: root.description.length > 0 && ApplicationWindow.window.showDescriptions
        Layout.fillWidth: true
        Layout.topMargin: 2
        text: root.description
        wrapMode: Text.Wrap
        color: "#76767e"
        font.pixelSize: 12
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        Layout.topMargin: -6
        Layout.bottomMargin: -6
        Material.roundedScale: theme.radius
        from: root.from
        to: root.to
        stepSize: root.stepSize
        value: { backend.revision; return backend.setting(root.key) }

        onPressedChanged: pressed ? backend.beginEdit() : backend.endEdit()
        onMoved: pressed ? backend.setSetting(root.key, value) : backend.change(root.key, value)
    }
}
