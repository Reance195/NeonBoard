import QtQuick
import QtQuick.Layouts

// 统计状态带（R11-01）：顶栏的一行紧凑指标。
//
// 原先是三块 110×48 的统计盒（待办 / 进行中 / 今日完成），为了放三个数字占掉
// 330 px 宽、48 px 高，顶区垂直占用全花在盒子上。现在压成一行：数字 + 标签并列、
// 组间一条细分隔线，零值降饱和 —— 一眼能看出「哪一项真的有事」。
//
// 逾期（R11-03 接入）用危险色：它是看板最该被看见的状态，之前只在卡片上有呼吸描边。
RowLayout {
    id: band

    spacing: 0

    property real todo: 0
    property real doing: 0
    property real doneToday: 0
    property real overdue: 0

    // 指标顺序即展示顺序；标签固定，数值随看板变化
    readonly property var labels: ["待办", "进行中", "今日完成", "逾期"]
    readonly property var values: [todo, doing, doneToday, overdue]
    readonly property var accents: [Theme.cyan, Theme.violet, Theme.magenta, Theme.danger]

    implicitHeight: 22

    Repeater {
        model: band.labels.length

        delegate: RowLayout {
            id: metric
            required property int index

            spacing: 10

            StatChip {
                Layout.alignment: Qt.AlignVCenter
                label: band.labels[metric.index]
                value: band.values[metric.index]
                accent: band.accents[metric.index]
            }

            // 组间分隔线：最后一项不带
            Rectangle {
                visible: metric.index < band.labels.length - 1
                Layout.preferredWidth: 1
                Layout.preferredHeight: 12
                color: Theme.divider
            }
        }
    }
}
