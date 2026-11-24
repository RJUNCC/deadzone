import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One key in a legend: a dot, a dash, or a dashed ring, and what it means.
RowLayout {
    id: root
    property color color: "#8b8b93"
    property string text
    property string mark: "dot"   // dot, line, or ring
    spacing: 5

    Item {
        implicitWidth: 14
        implicitHeight: 12
        Rectangle {
            visible: root.mark === "dot"
            anchors.centerIn: parent
            width: 9; height: 9; radius: 4.5
            color: root.color
        }
        Rectangle {
            visible: root.mark === "line"
            anchors.centerIn: parent
            width: 14; height: 2
            color: root.color
        }
        Row {
            visible: root.mark === "ring"
            anchors.centerIn: parent
            spacing: 2
            Repeater {
                model: 3
                Rectangle { width: 3; height: 2; color: root.color }
            }
        }
    }
    Label {
        text: root.text
        color: "#8b8b93"
        font.pixelSize: 11
    }
}
