import QtQuick

// A small friend that lives along the top of the page. It wanders, naps
// while the window is in the background, and plays along with the
// controller: it follows the left stick, hops on jump, flips with every
// flip, and the car boosts when you boost. Click it to say hi.
//
// It only moves while there is something to do, so it costs nothing while
// it sits still, and nothing at all while it sleeps.
Item {
    id: root

    property string kind: "car"
    property real minX: 0
    property real maxX: 400
    property bool awake: true

    readonly property bool isCar: kind === "car"
    readonly property int px: 4

    readonly property var art: ({
        car: {
            a: [
                "............aaaaaa......",
                "aa.........agggggga.....",
                "aa........agggggggga....",
                ".aaaaaaaaaaaaaaaaaaaaaa.",
                "raaaaaaaaaaaaaaaaaaaaaal",
                "addkkkkkdddddddddkkkkkda",
                "...kkhhkk.......kkhhkk..",
                "...kkhhkk.......kkhhkk..",
                "....kkkk.........kkkk..."
            ],
            b: [
                "............aaaaaa......",
                "aa.........agggggga.....",
                "aa........agggggggga....",
                ".aaaaaaaaaaaaaaaaaaaaaa.",
                "raaaaaaaaaaaaaaaaaaaaaal",
                "addkkkkkdddddddddkkkkkda",
                "...khkkhk.......khkkhk..",
                "...khkkhk.......khkkhk..",
                "....kkkk.........kkkk..."
            ]
        },
        cat: {
            a: [
                "............o...o.",
                "............oo.oo.",
                "............ooooo.",
                "o...........oeoeo.",
                "o...........owpwo.",
                ".o.....ooooooowo..",
                ".oossoossoosoooo..",
                "..ooooooooooooo...",
                "..owwwwwwwwwwoo...",
                "..oo.oo....oo.oo..",
                "..o...o....o...o.."
            ],
            b: [
                "............o...o.",
                "............oo.oo.",
                "............ooooo.",
                "o...........oeoeo.",
                ".o..........owpwo.",
                ".o.....ooooooowo..",
                ".oossoossoosoooo..",
                "..ooooooooooooo...",
                "..owwwwwwwwwwoo...",
                "...oo.oo..oo.oo...",
                "....oo.....oo....."
            ]
        },
        dog: {
            a: [
                "............cc..cc",
                "............cc..cc",
                "............cccccc",
                "c...........cecccc",
                "cc..........ccccwn",
                ".c..........cwwwt.",
                ".cccccccccccccww..",
                ".ccccccccccccww...",
                "..wwwwwwwwwwcc....",
                "..cc.cc....cc.cc..",
                "..c...c....c...c.."
            ],
            b: [
                "............cc..cc",
                "............cc..cc",
                "............cccccc",
                "............cecccc",
                "cc..........ccccwn",
                "cc..........cwwwt.",
                ".cccccccccccccww..",
                ".ccccccccccccww...",
                "..wwwwwwwwwwcc....",
                "...cc.cc..cc.cc...",
                "....cc.....cc....."
            ]
        }
    })
    readonly property var sprite: art[kind] || art.car
    readonly property var colors: ({
        a: theme.accent, d: Qt.darker(theme.accent, 1.6), g: "#cfe9ff", k: "#52525b", h: "#e4e4e7",
        l: "#fff7c2", r: "#ff4d4d",
        o: "#f2a65a", s: "#c8742f", e: "#27272a", p: "#ff8fa3", w: "#fff1e0",
        c: "#c98a4b", n: "#27272a", t: "#ff8fa3"
    })

    width: sprite.a[0].length * px
    height: sprite.a.length * px

    // Movement.
    property real facing: 1
    property real target: x
    property bool walking: false
    property int frame: 0
    readonly property real stick: awake ? backend.leftOut.x : 0
    readonly property bool steering: Math.abs(stick) > 0.35
    readonly property bool boosting: isCar && awake && (backend.liveButtons & 2) !== 0
    readonly property real speed: (isCar ? 150 : 55) * (boosting ? 2.2 : 1)
    readonly property bool moving: walking || steering || boosting

    // Hops and flips.
    property real hop: 0
    property real spin: 0

    function jump(height, turns) {
        if (!awake)
            return
        up.to = -height
        spinner.to = turns * 360 * root.facing
        air.restart()
    }

    FrameAnimation {
        running: root.visible && root.moving
        onTriggered: {
            const dt = Math.min(frameTime, 0.05)
            let dx = 0
            if (root.steering) {
                dx = root.stick * root.speed * 1.6 * dt
                root.walking = false
            } else if (root.boosting) {
                dx = root.facing * root.speed * dt
                root.walking = false
            } else {
                const left = root.target - root.x
                dx = Math.sign(left) * Math.min(Math.abs(left), root.speed * dt)
                if (Math.abs(left) < 0.5)
                    root.walking = false
            }
            if (dx !== 0)
                root.facing = dx > 0 ? 1 : -1
            const next = Math.max(root.minX, Math.min(root.maxX - root.width, root.x + dx))
            // Boosting into a wall turns the car around.
            if (root.boosting && next === root.x)
                root.facing = -root.facing
            root.x = next
        }
    }
    Timer {
        interval: root.isCar ? 70 : 150
        running: root.visible && root.moving
        repeat: true
        onTriggered: root.frame = 1 - root.frame
    }
    // Every few seconds: go somewhere, or stay put for a bit.
    Timer {
        id: brain
        interval: 3000
        running: root.visible && root.awake && !root.steering
        repeat: true
        onTriggered: {
            interval = 2500 + Math.random() * 4500
            if (Math.random() < 0.6) {
                root.target = root.minX + Math.random() * Math.max(0, root.maxX - root.minX - root.width)
                root.walking = true
            }
        }
    }
    Component.onCompleted: x = minX + Math.random() * Math.max(0, maxX - minX - width)

    // A shadow that shrinks as it leaves the ground.
    Rectangle {
        width: root.width * 0.7 * (1 + root.hop / 120)
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.height - 2
        height: 4
        radius: 2
        color: "black"
        opacity: 0.25 * (1 + root.hop / 80)
    }

    Item {
        id: body
        width: root.width
        height: root.height
        y: root.hop
        transform: [
            Scale { origin.x: body.width / 2; xScale: root.facing },
            Rotation { origin.x: body.width / 2; origin.y: body.height / 2; angle: root.spin }
        ]

        // Boost: a flickering flame out the back.
        Item {
            visible: root.boosting
            x: -flame.width + 2
            y: 4 * root.px
            width: 8 * root.px
            height: 2 * root.px
            Rectangle { id: flame; anchors.right: parent.right; height: parent.height; width: parent.width * (0.6 + 0.4 * root.frame); color: "#fb923c" }
            Rectangle { anchors.right: parent.right; height: parent.height; width: parent.width * (0.3 + 0.2 * (1 - root.frame)); color: "#fde047" }
        }

        PixelSprite { rows: root.sprite.a; colors: root.colors; px: root.px; visible: root.frame === 0 || !root.moving }
        PixelSprite { rows: root.sprite.b; colors: root.colors; px: root.px; visible: root.frame === 1 && root.moving }
    }

    // Asleep while the window is in the background.
    Row {
        visible: !root.awake
        x: root.facing > 0 ? root.width - 6 : -14
        y: -14
        spacing: 1
        Repeater {
            model: ["z", "z", "Z"]
            delegate: Text {
                required property var modelData
                required property int index
                text: modelData
                y: -index * 4
                color: theme.muted
                font.pixelSize: 9 + index * 2
                font.bold: true
            }
        }
    }

    SequentialAnimation {
        id: air
        ParallelAnimation {
            SequentialAnimation {
                NumberAnimation { id: up; target: root; property: "hop"; to: -40; duration: 260; easing.type: Easing.OutQuad }
                NumberAnimation { target: root; property: "hop"; to: 0; duration: 300; easing.type: Easing.InQuad }
            }
            NumberAnimation { id: spinner; target: root; property: "spin"; from: 0; duration: 540; easing.type: Easing.InOutQuad }
        }
        ScriptAction { script: root.spin = 0 }
    }

    // Jump hops; a second jump in time flips the way the stick points.
    property int lastButtons: 0
    readonly property int buttons: awake ? backend.liveButtons : 0
    onButtonsChanged: {
        const jumpBit = 1 << backend.jumpButton
        if ((buttons & jumpBit) && !(lastButtons & jumpBit) && !air.running)
            jump(24, 0)
        lastButtons = buttons
    }
    Connections {
        target: backend
        property real lastSeen: -1
        function onFlipsChanged() {
            const f = backend.flips.length > 0 ? backend.flips[0] : null
            if (!f || f.t === lastSeen)
                return
            lastSeen = f.t
            // Forward tips forward, back tips back; a double jump just goes higher.
            root.jump(48, f.directional ? (f.y > 0.3 ? -1 : 1) : 0)
            sparks.fire(root.width / 2, root.height / 2, f.directional ? 20 : 10)
        }
    }

    Sparks {
        id: sparks
        anchors.fill: parent
        source: "qrc:///particleresources/star.png"
        lifeSpan: 700
        speed: 110
        size: 10
        gravity: 140
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler {
        onTapped: {
            root.jump(44, 1)
            sparks.fire(root.width / 2, root.height / 2, 16)
        }
    }
}
