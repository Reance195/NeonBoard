import QtQuick
import QtQuick.Layouts

// 统一空态（R11-07）：列表 / 筛选没有内容时给人看的四段结构 ——
// 图标 + 主文案 + 副文案 + 可选行动按钮。
//
// 以前每个地方的空态各写一句灰字：用户看到「暂无任务」既不知道是「该继续输入」
// 还是「该换个条件」，只能自己猜。现在把「现在是什么状态」和「下一步做什么」
// 说清楚，并把出口做成一个能点的按钮。
//
// 筛选态（搜索 / 里程碑 / 项目范围把看板筛空）由组件自己判：
// 主界面只把「当前条件 + 可见任务数」递进来，不用在主界面里写一堆判据。
Rectangle {
    id: root

    // 图标：一个字符即可，不引入图片资源
    property string icon: "◌"
    // 主文案：一句话说清现在是什么状态（留空则按筛选状态自己说）
    property string title: ""
    // 副文案：说清下一步怎么做（留空则按筛选状态自己说）
    property string subtitle: ""
    // 行动按钮文案；空串表示这次没有可给的动作
    property string actionText: ""
    // 强调色：跟随当前主题（检索用青、异常用危险色）
    property color accent: Theme.cyan

    // ---- 筛选态判据 ----
    // 搜索关键词
    property string query: ""
    // 是否按里程碑 / 项目范围过滤
    property bool stageFiltered: false
    property bool projectFiltered: false
    // 可见任务数；-1 表示不启用筛选判据（当普通空态用）
    property int visibleCount: -1

    readonly property bool filterActive: query.length > 0 || stageFiltered || projectFiltered
    // 两条判据同时成立才算「被筛空」：少了后一条，空看板会被误报成「筛选没结果」
    readonly property bool filteredEmpty: visibleCount >= 0 && filterActive && visibleCount === 0
    // 该不该显示：显式给了文案就显示，否则只在「被筛空」时显示
    readonly property bool shown: title.length > 0 || filteredEmpty

    readonly property string effectiveTitle: title.length > 0 ? title : filterTitle
    readonly property string effectiveSubtitle: subtitle.length > 0 ? subtitle : filterSubtitle
    readonly property string filterTitle: {
        if (query.length > 0)
            return "没有匹配「" + query + "」的任务"
        if (stageFiltered)
            return "该阶段暂无任务"
        return "这个范围里还没有任务"
    }
    readonly property string filterSubtitle: {
        if (query.length > 0)
            return "换个关键词，或清除筛选查看全部任务"
        if (stageFiltered)
            return "切回「全部」看看其它阶段，或新建一张卡片"
        return "切回全局视图，或新建一张卡片"
    }

    signal actionTriggered()

    implicitWidth: 360
    implicitHeight: content.implicitHeight + Theme.spacingXl * 2
    radius: Theme.radiusPanel
    color: Theme.panel
    border.width: 1
    border.color: Theme.hairline
    visible: root.shown

    ColumnLayout {
        id: content
        anchors.centerIn: parent
        width: parent.width - Theme.spacingXl * 2
        spacing: Theme.spacingSm

        // 图标：描边圆环 + 符号，比一句灰字更容易被看见
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            radius: 22
            color: Theme.alpha(root.accent, 0.10)
            border.width: 1
            border.color: Theme.alpha(root.accent, 0.32)

            Text {
                anchors.centerIn: parent
                text: root.icon
                color: root.accent
                font.pixelSize: Theme.fontTitle
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.effectiveTitle
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: root.effectiveSubtitle
            visible: root.effectiveSubtitle.length > 0
            color: Theme.muted
            font.pixelSize: Theme.fontBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            lineHeight: 1.5
        }

        // 行动按钮：有文案才出现
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: actionLabel.implicitWidth + Theme.spacingLg * 2
            Layout.preferredHeight: 30
            visible: root.shown && root.actionText.length > 0
            radius: Theme.radiusControl
            color: actionMouse.containsMouse ? Theme.alpha(root.accent, 0.24)
                                            : Theme.alpha(root.accent, 0.12)
            border.width: 1
            border.color: Theme.alpha(root.accent, actionMouse.containsMouse ? 0.55 : 0.32)

            Behavior on color {
                ColorAnimation { duration: Motion.fast }
            }

            Text {
                id: actionLabel
                anchors.centerIn: parent
                text: root.actionText
                color: root.accent
                font.pixelSize: Theme.fontCaption
                font.bold: true
            }

            MouseArea {
                id: actionMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.Button
                Accessible.name: root.actionText
                Accessible.onPressAction: root.actionTriggered()
                onClicked: root.actionTriggered()
            }
        }
    }
}
