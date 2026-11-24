import QtQuick
import QtQuick.Particles

// Short bursts of light for moments worth a flourish: a flip, the stick
// snapping across, remapping switching on. The system sits paused, drawing
// nothing and costing nothing, until a burst is fired, and pauses again once
// the last spark has faded. Particles off in Appearance means none at all.
Item {
    id: root

    property color color: theme.accent
    property int lifeSpan: 700
    property real speed: 120
    property real size: 10
    property real spread: 360
    property real angle: 0
    property real gravity: 0
    property url source: "qrc:///particleresources/glowdot.png"

    function fire(x, y, count) {
        if (!theme.particles || count < 1)
            return
        system.paused = false
        emitter.burst(Math.round(count), x, y)
        rest.restart()
    }

    ParticleSystem {
        id: system
        paused: true
    }
    ImageParticle {
        system: system
        source: root.source
        color: root.color
        colorVariation: 0.12
        alpha: 0.9
        entryEffect: ImageParticle.Scale
    }
    Emitter {
        id: emitter
        anchors.fill: parent
        system: system
        enabled: false
        lifeSpan: root.lifeSpan
        lifeSpanVariation: root.lifeSpan / 3
        size: root.size
        sizeVariation: root.size / 2
        endSize: 0
        velocity: AngleDirection {
            angle: root.angle
            angleVariation: root.spread / 2
            magnitude: root.speed
            magnitudeVariation: root.speed / 2
        }
        acceleration: PointDirection { y: root.gravity }
    }
    Timer {
        id: rest
        interval: root.lifeSpan * 1.5
        onTriggered: system.paused = true
    }
}
