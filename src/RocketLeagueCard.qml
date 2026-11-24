import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Everything that ties deadzone to Rocket League: when it remaps, what the
// game's own settings add on top, and where flips and full steer begin.
Rectangle {
    id: root

    color: "#17171a"
    radius: theme.radius
    border.color: "#232328"
    implicitHeight: column.implicitHeight + 32

    // Re-evaluated whenever the profile or the game settings change.
    readonly property int tick: backend.revision + backend.gameRevision
    readonly property real gameDeadzone: { tick; return backend.gameSetting("deadzone") }
    readonly property real anti: { tick; return backend.setting("left.anti") }
    readonly property real dodgeTravel: { tick; return backend.travelFor("left", backend.gameSetting("dodge")) }
    readonly property real steerTravel: { tick; return backend.travelFor("left", Math.min(1, 1 / Math.max(0.01, backend.gameSetting("steering")))) }
    readonly property real aerialTravel: { tick; return backend.travelFor("left", Math.min(1, 1 / Math.max(0.01, backend.gameSetting("aerial")))) }
    readonly property real triggerGame: backend.gameTriggerDeadzone
    readonly property bool triggersMatch: { tick; return triggerGame < 0 || (Math.abs(backend.setting("l2.anti") - triggerGame) < 0.005 && Math.abs(backend.setting("r2.anti") - triggerGame) < 0.005) }

    function percent(v) { return v < 0 ? qsTr("never") : Math.round(v * 100) + "%" }

    component GameField: RowLayout {
        id: gameField
        property string key
        property string label
        property string help
        property real maximum: 1
        spacing: 6
        Label { text: gameField.label; color: "#c9c9cf"; font.pixelSize: 13 }
        InfoIcon { text: gameField.help }
        TextField {
            id: field
            Layout.preferredWidth: 72
            horizontalAlignment: Text.AlignHCenter
            Material.roundedScale: theme.radius
            Material.containerStyle: Material.Outlined
            topPadding: 6
            bottomPadding: 6
            validator: DoubleValidator { bottom: 0; top: gameField.maximum; decimals: 2; notation: DoubleValidator.StandardNotation }
            text: { root.tick; return backend.gameSetting(gameField.key).toLocaleString(Qt.locale(), 'f', 2) }
            onEditingFinished: backend.setGameSetting(gameField.key, Number.fromLocaleString(Qt.locale(), text))
        }
    }

    component Insight: RowLayout {
        id: insight
        property bool good: true
        property string text
        property string action
        signal acted()
        spacing: 10
        Layout.fillWidth: true
        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 3
            width: 10; height: 10; radius: 5
            color: insight.good ? "#4ade80" : theme.accent
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#d4d4d8"
            text: insight.text
        }
        Button {
            visible: insight.action.length > 0
            text: insight.action
            flat: true
            focusPolicy: Qt.NoFocus
            implicitHeight: 30
            Material.roundedScale: theme.radius
            onClicked: insight.acted()
        }
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label {
                text: "Rocket League"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#e4e4e7"
            }
            Rectangle {
                width: 8; height: 8; radius: 4
                color: backend.gameRunning ? "#4ade80" : "#55555d"
            }
            Label {
                text: backend.gameRunning ? qsTr("Running") : qsTr("Not running")
                color: "#8b8b93"
                font.pixelSize: 13
            }
            InfoIcon { text: { translator.language; backend.revision; return backend.presetTips(backend.profilePreset) } }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Switch {
                checked: backend.onlyWhileGame
                focusPolicy: Qt.NoFocus
                onToggled: backend.onlyWhileGame = checked
            }
            Label { text: qsTr("Remap only while Rocket League runs"); color: "#c9c9cf"; font.pixelSize: 13 }
            InfoIcon { text: qsTr("Outside the game the controller works as it normally does, with no virtual pad, for the desktop and other games. Deadzone takes over the moment Rocket League starts.") }
            Item { Layout.preferredWidth: 24 }
            Switch {
                checked: backend.background
                enabled: backend.backgroundAvailable
                focusPolicy: Qt.NoFocus
                onToggled: { backend.background = checked; checked = Qt.binding(() => backend.background) }
            }
            Label { text: qsTr("Keep running without the window"); color: backend.backgroundAvailable ? "#c9c9cf" : "#55555d"; font.pixelSize: 13 }
            InfoIcon {
                text: backend.backgroundAvailable
                      ? qsTr("Starts at login and keeps remapping after you close this window, with the settings saved here. Together with the switch on the left, you never have to open deadzone before playing.")
                      : qsTr("Install deadzone as a package (bin/install) to use this.")
            }
            Item { Layout.fillWidth: true }
        }

        SectionLabel { text: qsTr("Your in-game settings") }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: -6
            wrapMode: Text.Wrap
            color: "#8b8b93"
            font.pixelSize: 12
            text: qsTr("Copy these from Settings, Controls in Rocket League. The game keeps them to itself, so deadzone uses what you type here to show how the two combine.")
        }
        Flow {
            Layout.fillWidth: true
            spacing: 18
            GameField {
                key: "deadzone"; label: qsTr("Controller Deadzone")
                help: qsTr("The game's own stick deadzone. It applies on top of this app's.")
            }
            GameField {
                key: "dodge"; label: qsTr("Dodge Deadzone")
                help: qsTr("How far the game needs the stick pushed before a jump becomes a flip in that direction instead of straight up.")
            }
            GameField {
                key: "steering"; label: qsTr("Steering Sensitivity"); maximum: 10
                help: qsTr("The game multiplies stick input by this, so above 1 reaches full steer before the stick reaches the rim.")
            }
            GameField {
                key: "aerial"; label: qsTr("Aerial Sensitivity"); maximum: 10
                help: qsTr("The same multiplier, for pitch, yaw, and roll in the air.")
            }
        }

        SectionLabel { text: qsTr("How it all combines") }
        Insight {
            good: root.gameDeadzone < 0.005 || Math.abs(root.anti - root.gameDeadzone) < 0.005
            text: root.gameDeadzone < 0.005
                  ? qsTr("In-game deadzone is off, so only this app's deadzone applies.")
                  : Math.abs(root.anti - root.gameDeadzone) < 0.005
                    ? qsTr("Anti-deadzone matches the in-game deadzone, so the two cancel out.")
                    : qsTr("The in-game deadzone of %1 adds to this one, so the car ignores more of the stick than the deadzone here suggests. Set it to its lowest in the game, or cancel it with Anti-deadzone.")
                        .arg(root.gameDeadzone.toLocaleString(Qt.locale(), 'f', 2))
            action: good ? "" : qsTr("Cancel it")
            onActed: backend.change("left.anti", root.gameDeadzone)
        }
        Insight {
            text: qsTr("Flips go directional after about %1 of left stick travel: the blue ring on the left stick.").arg(root.percent(root.dodgeTravel))
        }
        Insight {
            good: root.steerTravel >= 0
            text: qsTr("Full steer arrives at about %1 of travel, and full aerial control at about %2.").arg(root.percent(root.steerTravel)).arg(root.percent(root.aerialTravel))
        }
        Insight {
            visible: root.triggerGame >= 0
            good: root.triggersMatch
            text: root.triggersMatch
                  ? qsTr("Rocket League adds a %1% deadzone to the triggers (TAInput.ini), and trigger Anti-deadzone cancels it.").arg(Math.round(root.triggerGame * 100))
                  : qsTr("Rocket League adds a %1% deadzone to the triggers (TAInput.ini). Matching trigger Anti-deadzone to it makes throttle respond from the first touch.").arg(Math.round(root.triggerGame * 100))
            action: good ? "" : qsTr("Match")
            onActed: { backend.beginEdit(); backend.setSetting("l2.anti", root.triggerGame); backend.setSetting("r2.anti", root.triggerGame); backend.endEdit() }
        }
    }
}
