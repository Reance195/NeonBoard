import QtQuick
import QtQuick.Layouts

// 统计指标项（R11-01 改为紧凑形态）。
//
// 原先是 110×48 的大盒子：为了放两个数字（18 px 数字 + 9 px 标签），
// 顶栏一排就占掉 330 px 宽、48 px 高。现在压成「数字 + 标签」并列的一项，
// 由 TopStatBar 负责排布与分组分隔线。
//
// 数值变化时仍做「数字滚动 + 跳动闪光」，让状态变化被看见；
// 数值为 0 时降饱和（灰色），一眼能看出「这一项现在没事」。
Item {
    id: chip

    property string label: ""
    property real value: 0
    property color accent: Theme.accent
    // 零值时用灰色而不是强调色
    property bool dimWhenZero: true

    property real shownValue: 0
    property real pulse: 1

    readonly property bool isZero: Math.round(value) === 0

    implicitWidth: row.implicitWidth
    implicitHeight: 22

    RowLayout {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        Text {
            id: numberText
            Layout.alignment: Qt.AlignBaseline
            text: Math.round(chip.shownValue)
            color: (chip.dimWhenZero && chip.isZero) ? Theme.muted : chip.accent
            font.pixelSize: 15
            font.bold: true
            scale: chip.pulse

            Behavior on color {
                ColorAnimation { duration: Motion.fast }
            }
        }

        Text {
            Layout.alignment: Qt.AlignBaseline
            text: chip.label
            color: Theme.muted
            font.pixelSize: 10
        }
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

    Accessible.role: Accessible.StaticText
    Accessible.name: chip.label + " " + Math.round(chip.value)
}
