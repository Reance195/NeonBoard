import QtQuick
import QtQuick.Layouts

// 统计小挂件：侧栏/标题区的数字徽标。
// 数值变化时做「数字滚动 + 跳动闪光」，让状态变化被看见。
Rectangle {
    id: chip

    property string label: ""
    property real value: 0
    property color accent: Theme.cyan

    property real shownValue: 0
    property real pulse: 1

    Layout.preferredWidth: 110
    height: 48
    radius: 10
    color: Theme.input
    border.width: 1
    border.color: Qt.rgba(accent.r, accent.g, accent.b, 0.28)

    Behavior on color {
        ColorAnimation { duration: Motion.fast }
    }
    Behavior on border.color {
        ColorAnimation { duration: Motion.fast }
    }

    onValueChanged: {
        rollAnimation.from = shownValue
        rollAnimation.to = value
        rollAnimation.restart()
        pulseAnimation.restart()
    }

    NumberAnimation {
        id: rollAnimation
        target: chip
        property: "shownValue"
        duration: Motion.base
        easing.type: Easing.OutCubic
    }

    NumberAnimation {
        id: pulseAnimation
        target: chip
        property: "pulse"
        from: 1.28
        to: 1.0
        duration: Motion.base
        easing.type: Easing.OutCubic
    }

    Column {
        anchors.centerIn: parent
        spacing: 2

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Math.round(chip.shownValue)
            color: chip.accent
            font.pixelSize: 18
            font.bold: true
            scale: chip.pulse
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: chip.label
            color: Theme.muted
            font.pixelSize: 9
        }
    }
}
