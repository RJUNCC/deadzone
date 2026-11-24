import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1040
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
    property bool showDescriptions: false
    // Single-key shortcuts stay out of the way while a text field has focus.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: win.helpVisible || backend.calibrating.length > 0; onActivated: { win.helpVisible = false; backend.cancelCalibration() } }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.active = !backend.active }
    Shortcut { sequence: "D"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.selectNext() }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.selectProfile((backend.profileIndex + 1) % backend.profileNames.length) }
    Shortcut { sequence: "I"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.showDescriptions = !win.showDescriptions }
    Shortcut { sequence: "L"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.lightbar.mode = (backend.lightbar.mode + 1) % 4 }
    Shortcut { sequence: "H"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.hideFromGames = !backend.hideFromGames }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.resetProfile() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.redo() }

    readonly property var shortcuts: [
        ["Space", qsTr("Turn remapping on or off")],
        ["P", qsTr("Switch to the next profile")],
        ["D", qsTr("Show the next controller")],
        ["I", qsTr("Show every description under its setting")],
        ["H", qsTr("Hide the real controller from games")],
        ["L", qsTr("Next lightbar mode")],
        ["R", qsTr("Reset the profile to its preset")],
        ["Esc", qsTr("Stop measuring")],
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
                              : backend.remapping ? qsTr("Games see a virtual Xbox 360 pad")
                              : backend.active ? qsTr("Waiting for Rocket League to start")
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

                SplitScreenCard {
                    visible: win.hasController && (backend.exposed || (backend.hideFromGames && backend.rawHeldBy.length > 0))
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                }

                Rectangle {
                    visible: backend.calibrating.length === 0 && backend.calibrationMessage.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    implicitHeight: noticeRow.implicitHeight + 20
                    radius: theme.radius
                    color: "#17171a"
                    border.color: "#2a2a30"
                    RowLayout {
                        id: noticeRow
                        anchors.fill: parent
                        anchors.margins: 10
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: "#e4e4e7"
                            text: backend.calibrationMessage
                        }
                    }
                }

                RocketLeagueCard {
                    visible: win.hasController
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                }

                SessionCard {
                    visible: win.hasController
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                }

                ProfileBar {
                    visible: win.hasController
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    onRenameRequested: { renameField.text = backend.profileNames[backend.profileIndex]; renameDialog.open() }
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
                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        side: "left"
                        title: qsTr("Left stick")
                        dodge: { backend.revision; backend.gameRevision; return backend.travelFor("left", backend.gameSetting("dodge")) }
                        raw: backend.leftRaw
                        out: backend.leftOut
                    }
                    StickCard {
                        Layout.alignment: Qt.AlignTop
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
                                    description: qsTr("How far you press before games see anything, so a resting finger never counts.")
                                    to: 0.5
                                }
                                SettingSlider {
                                    Layout.fillWidth: true
                                    key: modelData.key + ".outer"
                                    label: qsTr("Outer edge")
                                    description: qsTr("Where games see a full press. Lower means full throttle sooner.")
                                    from: 0.5
                                }
                                SettingSlider {
                                    Layout.fillWidth: true
                                    key: modelData.key + ".anti"
                                    label: qsTr("Anti-deadzone")
                                    description: qsTr("Where the output starts once pressed. Cancels a game's own trigger deadzone; Rocket League has 10%.")
                                    to: 0.5
                                }
                            }
                        }
                    }
                }

                LightbarCard {
                    visible: win.hasController
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 20
                    Layout.topMargin: 0
                    spacing: 10

                    Switch {
                        id: hideSwitch
                        checked: backend.hideFromGames
                        enabled: !backend.hideBusy
                        focusPolicy: Qt.NoFocus
                        onToggled: { backend.hideFromGames = checked; checked = Qt.binding(() => backend.hideFromGames) }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: "#8b8b93"
                        font.pixelSize: 13
                        text: backend.hideFromGames ? qsTr("Hidden from games: they only see the virtual pad. Turn this off to give Steam its own access back.")
                                                    : qsTr("Hide the real controller from games, so they only see the virtual pad")
                    }
                    Label {
                        text: qsTr("Press ? for shortcuts")
                        color: "#6b6b73"
                        font.pixelSize: 13
                    }
                }
            }
        }

        Dialog {
            id: renameDialog
            anchors.centerIn: parent
            modal: true
            title: qsTr("Rename profile")
            standardButtons: Dialog.Ok | Dialog.Cancel
            Material.roundedScale: theme.radius
            onAccepted: backend.renameProfile(renameField.text)
            onOpened: renameField.forceActiveFocus()
            TextField {
                id: renameField
                width: 300
                Material.roundedScale: theme.radius
                onAccepted: renameDialog.accept()
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
