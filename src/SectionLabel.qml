import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A small caps heading that groups the settings under it.
Label {
    Layout.fillWidth: true
    Layout.topMargin: 6
    font.pixelSize: 11
    font.weight: Font.DemiBold
    font.letterSpacing: 1.2
    font.capitalization: Font.AllUppercase
    color: theme.accent
}
