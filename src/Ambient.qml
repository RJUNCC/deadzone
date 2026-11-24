import QtQuick
import QtQuick.Particles

// Slow motes of the accent drifting up behind the cards. About eighty at a
// time, drawn by the GPU in one batch. They stop while the window is in the
// background, so they never take a frame from the game.
Item {
    id: root
    property bool running: true

    visible: theme.particles
    ParticleSystem {
        id: system
        running: theme.particles
        paused: !root.running
    }
    ImageParticle {
        system: system
        source: "qrc:///particleresources/glowdot.png"
        color: theme.accent
        colorVariation: 0.08
        alpha: theme.light ? 0.3 : 0.45
        entryEffect: ImageParticle.Fade
    }
    Emitter {
        system: system
        anchors.fill: parent
        emitRate: 7
        lifeSpan: 11000
        lifeSpanVariation: 4000
        size: 26
        sizeVariation: 18
        endSize: 8
        velocity: AngleDirection { angle: 270; angleVariation: 40; magnitude: 14; magnitudeVariation: 8 }
    }
    Wander {
        system: system
        xVariance: 30
        pace: 20
        affectedParameter: Wander.Velocity
    }
}
