import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One tab's page. It scrolls smoothly with a notched mouse wheel (each notch
// glides instead of jumping), follows a touchpad exactly, and rises into
// place when its tab is picked.
Flickable {
    id: page

    readonly property bool current: StackLayout.isCurrentItem
    property real rise: 0

    clip: true
    contentWidth: width
    contentHeight: contentItem.childrenRect.height
    boundsBehavior: Flickable.StopAtBounds
    flickDeceleration: 4000
    maximumFlickVelocity: 6000
    pixelAligned: true
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    transform: Translate { y: page.rise }

    function clamp(y) { return Math.max(0, Math.min(y, contentHeight - height)) }

    NumberAnimation {
        id: glide
        target: page
        property: "contentY"
        duration: theme.animations ? 240 : 0
        easing.type: Easing.OutCubic
    }

    WheelHandler {
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            if (page.contentHeight <= page.height)
                return
            if (event.pixelDelta.y !== 0) {
                glide.stop()
                page.contentY = page.clamp(page.contentY - event.pixelDelta.y)
            } else {
                const from = glide.running ? glide.to : page.contentY
                glide.to = page.clamp(from - event.angleDelta.y / 120 * 120)
                glide.restart()
            }
        }
    }

    ParallelAnimation {
        id: enter
        NumberAnimation { target: page; property: "opacity"; from: 0; to: 1; duration: 220; easing.type: Easing.OutCubic }
        NumberAnimation { target: page; property: "rise"; from: 14; to: 0; duration: 320; easing.type: Easing.OutCubic }
    }
    onCurrentChanged: if (current && theme.animations) enter.restart()
}
