import QtQuick

// Pixel art from rows of characters, one color per character ('.' is
// empty). Runs of the same color in a row become one rectangle, so a whole
// sprite is a few dozen items, built once.
Item {
    id: root

    property var rows: []
    property var colors: ({})
    property int px: 3

    readonly property var runs: {
        const out = []
        for (let y = 0; y < rows.length; ++y) {
            const row = rows[y]
            let x = 0
            while (x < row.length) {
                const c = row[x]
                let end = x + 1
                while (end < row.length && row[end] === c)
                    ++end
                if (c !== ".")
                    out.push({ x: x, y: y, w: end - x, c: c })
                x = end
            }
        }
        return out
    }

    implicitWidth: (rows.length ? rows[0].length : 0) * px
    implicitHeight: rows.length * px

    Repeater {
        model: root.runs
        delegate: Rectangle {
            required property var modelData
            x: modelData.x * root.px
            y: modelData.y * root.px
            width: modelData.w * root.px
            height: root.px
            color: root.colors[modelData.c] || "magenta"
            antialiasing: false
        }
    }
}
