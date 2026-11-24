import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The OBS overlay: turning it on, and building the address with its options.
Rectangle {
    id: root

    property bool compact: false
    property bool raw: false
    property bool profileName: false
    property bool xbox: false
    property real scale: 1.0

    readonly property string url: {
        const params = []
        if (compact) params.push("layout=compact")
        if (raw) params.push("raw=1")
        if (profileName) params.push("profile=1")
        if (xbox) params.push("buttons=xbox")
        if (Math.abs(scale - 1) > 0.01) params.push("scale=" + scale.toFixed(2))
        params.push("accent=" + String(theme.accent).replace("#", ""))
        return backend.overlayUrl + "?" + params.join("&")
    }
    readonly property int pageWidth: Math.round((compact ? 440 : 600) * scale)
    readonly property int pageHeight: Math.round((compact ? 270 : 390) * scale)

    color: "#17171a"
    radius: theme.radius
    border.color: "#232328"
    implicitHeight: column.implicitHeight + 32

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: qsTr("OBS overlay")
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#e4e4e7"
            }
            InfoIcon {
                text: qsTr("Shows your controller live on stream or in recordings: sticks, triggers, and buttons, as the game receives them. Deadzone serves it on this computer only, and it works with the window closed when deadzone runs in the background.")
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: backend.overlayEnabled && backend.overlayError.length === 0
                text: backend.overlayClients > 0 ? qsTr("%n overlay(s) connected", "", backend.overlayClients) : qsTr("Waiting for OBS")
                color: backend.overlayClients > 0 ? "#4ade80" : "#8b8b93"
                font.pixelSize: 13
            }
            Switch {
                checked: backend.overlayEnabled
                focusPolicy: Qt.NoFocus
                onToggled: backend.overlayEnabled = checked
            }
        }

        Label {
            visible: backend.overlayEnabled && backend.overlayError.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#ffb4b4"
            text: qsTr("Could not start the overlay on port %1: %2. Pick another port below.").arg(backend.overlayPort).arg(backend.overlayError)
        }

        ColumnLayout {
            visible: backend.overlayEnabled
            Layout.fillWidth: true
            spacing: 12

            // The address, ready to paste into OBS.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 38
                    radius: Math.min(4, theme.radius)
                    color: "#121215"
                    border.color: "#2a2a30"
                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                        text: root.url
                        color: "#e4e4e7"
                        font.family: "monospace"
                        font.pixelSize: 13
                    }
                }
                Button {
                    id: copyButton
                    property bool copied: false
                    text: copied ? qsTr("Copied") : qsTr("Copy")
                    focusPolicy: Qt.NoFocus
                    Material.foreground: theme.accentForeground
                    background: Rectangle {
                        implicitHeight: 36
                        implicitWidth: 80
                        radius: Math.min(4, theme.radius)
                        color: parent.down ? Qt.darker(theme.accent, 1.2) : theme.accent
                    }
                    onClicked: { clipboard.setText(root.url); copied = true; copiedTimer.restart() }
                    Timer { id: copiedTimer; interval: 1500; onTriggered: copyButton.copied = false }
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#8b8b93"
                font.pixelSize: 12
                text: qsTr("In OBS: Sources, +, Browser. Paste the address as the URL, and set Width to %1 and Height to %2.").arg(root.pageWidth).arg(root.pageHeight)
            }

            Flow {
                Layout.fillWidth: true
                spacing: 16
                RowLayout {
                    spacing: 6
                    Label { text: qsTr("Layout"); color: "#c9c9cf"; font.pixelSize: 13 }
                    Segmented {
                        options: [qsTr("Full controller"), qsTr("Sticks and triggers")]
                        current: root.compact ? 1 : 0
                        onPicked: index => root.compact = index === 1
                    }
                }
                RowLayout {
                    spacing: 6
                    Label { text: qsTr("Buttons"); color: "#c9c9cf"; font.pixelSize: 13 }
                    Segmented {
                        options: ["PlayStation", "Xbox"]
                        current: root.xbox ? 1 : 0
                        onPicked: index => root.xbox = index === 1
                    }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 16
                RowLayout {
                    spacing: 4
                    CheckBox { checked: root.raw; focusPolicy: Qt.NoFocus; onToggled: root.raw = checked }
                    Label { text: qsTr("Show the physical stick too"); color: "#c9c9cf"; font.pixelSize: 13 }
                }
                RowLayout {
                    spacing: 4
                    CheckBox { checked: root.profileName; focusPolicy: Qt.NoFocus; onToggled: root.profileName = checked }
                    Label { text: qsTr("Show the profile name"); color: "#c9c9cf"; font.pixelSize: 13 }
                }
                RowLayout {
                    spacing: 8
                    Label { text: qsTr("Size"); color: "#c9c9cf"; font.pixelSize: 13 }
                    Slider {
                        Layout.preferredWidth: 140
                        from: 0.5
                        to: 2
                        stepSize: 0.05
                        value: root.scale
                        onMoved: root.scale = value
                    }
                    Label { text: Math.round(root.scale * 100) + "%"; color: "#8b8b93"; font.pixelSize: 13 }
                }
                RowLayout {
                    spacing: 6
                    Label { text: qsTr("Port"); color: "#c9c9cf"; font.pixelSize: 13 }
                    TextField {
                        Layout.preferredWidth: 80
                        horizontalAlignment: Text.AlignHCenter
                        topPadding: 6
                        bottomPadding: 6
                        Material.containerStyle: Material.Outlined
                        Material.roundedScale: theme.radius
                        validator: IntValidator { bottom: 1024; top: 65535 }
                        text: backend.overlayPort
                        onEditingFinished: backend.overlayPort = parseInt(text)
                    }
                }
            }
        }
    }
}
