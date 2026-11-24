import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1000
    height: 1000
    minimumWidth: 760
    minimumHeight: 640
    visible: true
    title: "Deadzone"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.active = !backend.active }
    Shortcut { sequence: "D"; context: Qt.ApplicationShortcut; onActivated: backend.selectNext() }
    Shortcut { sequence: "H"; context: Qt.ApplicationShortcut; onActivated: backend.hideFromGames = !backend.hideFromGames }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; onActivated: backend.resetProfile() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; onActivated: backend.redo() }

    readonly property var shortcuts: [
        ["Space", qsTr("Turn remapping on or off")],
        ["D", qsTr("Show the next controller")],
        ["H", qsTr("Hide the real controller from games")],
        ["R", qsTr("Reset all settings")],
        ["Ctrl+Z", qsTr("Undo")],
        ["Ctrl+Shift+Z", qsTr("Redo")],
        ["?", qsTr("Show shortcuts")],
        ["Q", qsTr("Quit")]
    ]

    readonly property bool hasController: backend.controllers.length > 0

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 16

                // Header: which controller, and whether games get the shaped version.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 20
                    Layout.bottomMargin: 0
                    spacing: 14

                    Label {
                        text: "Deadzone"
                        font.pixelSize: 22
                        font.weight: Font.Bold
                        color: "#e4e4e7"
                    }
                    ComboBox {
                        Layout.preferredWidth: 340
                        enabled: win.hasController
                        focusPolicy: Qt.NoFocus
                        Material.roundedScale: theme.radius
                        model: win.hasController ? backend.controllers.map(c => c.name) : [qsTr("No controller connected")]
                        currentIndex: win.hasController ? backend.selected : 0
                        onActivated: index => backend.selected = index
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: !win.hasController ? ""
                              : backend.active ? qsTr("Games see a virtual Xbox 360 pad")
                                               : qsTr("Off: games see the controller as-is")
                        color: backend.active ? "#c9c9cf" : "#8b8b93"
                        font.pixelSize: 13
                    }
                    Switch {
                        checked: backend.active
                        focusPolicy: Qt.NoFocus
                        onToggled: backend.active = checked
                    }
                }

                // Problems are said in the window, never worked around silently.
                Rectangle {
                    visible: backend.error.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    implicitHeight: errorText.implicitHeight + 20
                    radius: theme.radius
                    color: "#3a1418"
                    border.color: "#6b2028"
                    Label {
                        id: errorText
                        anchors.fill: parent
                        anchors.margins: 10
                        text: backend.error
                        wrapMode: Text.Wrap
                        color: "#ffb4b4"
                    }
                }

                Rectangle {
                    visible: backend.exposed && !backend.hideFromGames
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    implicitHeight: exposedRow.implicitHeight + 20
                    radius: theme.radius
                    color: "#1d1a10"
                    border.color: Qt.darker(theme.accent, 2.2)
                    RowLayout {
                        id: exposedRow
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 12
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: "#e4e4e7"
                            text: qsTr("Steam and some games read this controller directly, skip your deadzones, and may see two controllers. Hiding it from games stops that; it asks for your password.")
                        }
                        Button {
                            text: qsTr("Hide from games")
                            focusPolicy: Qt.NoFocus
                            Material.foreground: theme.accentForeground
                            background: Rectangle {
                                implicitHeight: 36
                                radius: Math.min(4, theme.radius)
                                color: parent.down ? Qt.darker(theme.accent, 1.2) : theme.accent
                            }
                            onClicked: backend.hideFromGames = true
                        }
                    }
                }

                // Nothing to shape yet: say what to do.
                ColumnLayout {
                    visible: !win.hasController
                    Layout.fillWidth: true
                    Layout.topMargin: 60
                    spacing: 8
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Connect a controller")
                        font.pixelSize: 20
                        color: "#e4e4e7"
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Plug it in over USB or pair it over Bluetooth. It shows up here on its own.")
                        color: "#8b8b93"
                    }
                }

                RowLayout {
                    visible: win.hasController
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    spacing: 16

                    StickCard {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        side: "left"
                        title: qsTr("Left stick")
                        raw: backend.leftRaw
                        out: backend.leftOut
                    }
                    StickCard {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        side: "right"
                        title: qsTr("Right stick")
                        raw: backend.rightRaw
                        out: backend.rightOut
                    }
                }

                Rectangle {
                    visible: win.hasController
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    implicitHeight: triggers.implicitHeight + 32
                    color: "#17171a"
                    radius: theme.radius
                    border.color: "#232328"

                    RowLayout {
                        id: triggers
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 24

                        Repeater {
                            model: [
                                { key: "l2", title: qsTr("Left trigger") },
                                { key: "r2", title: qsTr("Right trigger") }
                            ]
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                spacing: 10

                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: modelData.title
                                        font.pixelSize: 16
                                        font.weight: Font.DemiBold
                                        color: "#e4e4e7"
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        readonly property real rawValue: modelData.key === "l2" ? backend.l2Raw : backend.r2Raw
                                        readonly property real outValue: modelData.key === "l2" ? backend.l2Out : backend.r2Out
                                        text: qsTr("In %1%   Out %2%").arg(Math.round(rawValue * 100)).arg(Math.round(outValue * 100))
                                        color: "#8b8b93"
                                        font.pixelSize: 12
                                        font.features: { "tnum": 1 }
                                    }
                                }
                                TriggerMeter {
                                    Layout.fillWidth: true
                                    accent: theme.accent
                                    raw: modelData.key === "l2" ? backend.l2Raw : backend.r2Raw
                                    out: modelData.key === "l2" ? backend.l2Out : backend.r2Out
                                    inner: { backend.revision; return backend.setting(modelData.key + ".inner") }
                                    outer: { backend.revision; return backend.setting(modelData.key + ".outer") }
                                }
                                SettingSlider {
                                    Layout.fillWidth: true
                                    key: modelData.key + ".inner"
                                    label: qsTr("Deadzone")
                                    to: 0.5
                                }
                                SettingSlider {
                                    Layout.fillWidth: true
                                    key: modelData.key + ".outer"
                                    label: qsTr("Outer edge")
                                    from: 0.5
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 20
                    Layout.topMargin: 0
                    spacing: 10

                    Switch {
                        id: hideSwitch
                        checked: backend.hideFromGames
                        focusPolicy: Qt.NoFocus
                        onToggled: backend.hideFromGames = checked
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: "#8b8b93"
                        font.pixelSize: 13
                        text: backend.hiding ? qsTr("Hidden from games: only the virtual pad is visible to them.")
                                             : qsTr("Hide the real controller from games while remapping")
                    }
                    Label {
                        text: qsTr("Press ? for shortcuts")
                        color: "#6b6b73"
                        font.pixelSize: 13
                    }
                }
            }
        }

        // Every shortcut, on ?.
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: "#cc0e0e10"

            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Rectangle {
                anchors.centerIn: parent
                width: helpColumn.implicitWidth + 48
                height: helpColumn.implicitHeight + 40
                radius: theme.radius
                color: "#17171a"
                border.color: "#2a2a30"

                ColumnLayout {
                    id: helpColumn
                    anchors.centerIn: parent
                    spacing: 10

                    Label {
                        text: qsTr("Shortcuts")
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: "#e4e4e7"
                        Layout.bottomMargin: 6
                    }
                    Repeater {
                        model: win.shortcuts
                        delegate: RowLayout {
                            required property var modelData
                            spacing: 16
                            Rectangle {
                                Layout.preferredWidth: 130
                                implicitHeight: keyLabel.implicitHeight + 8
                                radius: Math.min(4, theme.radius)
                                color: "#232328"
                                Label {
                                    id: keyLabel
                                    anchors.centerIn: parent
                                    text: modelData[0]
                                    color: theme.accent
                                    font.family: "monospace"
                                }
                            }
                            Label {
                                text: modelData[1]
                                color: "#c9c9cf"
                            }
                        }
                    }
                }
            }
        }
    }
}
