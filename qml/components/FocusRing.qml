import QtQuick
import QtQuick.Layouts

// 专注倒计时环：右上角悬浮，不遮挡看板列。
Rectangle {
    id: ringRoot

    property int total: 0
    property int remaining: 0
    property bool running: false
    property bool paused: false

    signal stopRequested()
    signal pauseToggleRequested()

    width: 148
    height: 172
    radius: 14
    color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.92)
    border.width: 1
    border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.28)

    readonly property real progress: total > 0 ? Math.max(0, Math.min(1, remaining / total)) : 0
    readonly property string clock: {
        const secs = Math.max(0, remaining)
        const m = Math.floor(secs / 60)
        const s = secs % 60
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
    }

    Canvas {
        id: dial
        anchors.horizontalCenter: parent.horizontalCenter
        y: 12
        width: 104
        height: 104
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const cx = width / 2
            const cy = height / 2
            const r = Math.min(cx, cy) - 6

            ctx.beginPath()
            ctx.arc(cx, cy, r, 0, Math.PI * 2)
            ctx.lineWidth = 6
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.07)
            ctx.stroke()

            const start = -Math.PI / 2
            const end = start + Math.PI * 2 * ringRoot.progress
            ctx.beginPath()
            ctx.arc(cx, cy, r, start, end)
            ctx.lineWidth = 6
            ctx.lineCap = "round"
            ctx.strokeStyle = ringRoot.paused ? Theme.muted : Theme.cyan
            ctx.stroke()
        }

        Connections {
            target: ringRoot
            function onProgressChanged() { dial.requestPaint() }
            function onPausedChanged() { dial.requestPaint() }
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 48
        text: ringRoot.clock
        color: ringRoot.paused ? Theme.muted : Theme.text
        font.pixelSize: 24
        font.bold: true
        font.letterSpacing: 1
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 78
        text: ringRoot.paused ? "已暂停" : "专注中"
        color: Theme.muted
        font.pixelSize: 9
    }

    RowLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 8

        Rectangle {
            width: 56
            height: 22
            radius: 6
            color: pauseMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Theme.input
            border.width: 1
            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.2)

            Text {
                anchors.centerIn: parent
                text: ringRoot.paused ? "继续" : "暂停"
                color: Theme.text
                font.pixelSize: 10
            }

            MouseArea {
                id: pauseMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: ringRoot.pauseToggleRequested()
            }
        }

        Rectangle {
            width: 56
            height: 22
            radius: 6
            color: stopMouse.containsMouse ? Qt.rgba(Theme.magenta.r, Theme.magenta.g, Theme.magenta.b, 0.18)
                                           : Theme.input
            border.width: 1
            border.color: Qt.rgba(Theme.magenta.r, Theme.magenta.g, Theme.magenta.b, 0.28)

            Text {
                anchors.centerIn: parent
                text: "结束"
                color: Theme.magenta
                font.pixelSize: 10
            }

            MouseArea {
                id: stopMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: ringRoot.stopRequested()
            }
        }
    }
}
