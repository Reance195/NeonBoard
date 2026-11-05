import QtQuick
import QtQuick.Layouts

// 统一空态（R11-07）：列表 / 筛选没有内容时给人看的四段结构 ——
// 图标 + 主文案 + 副文案 + 可选行动按钮。
//
// 以前每个地方的空态各写一句灰字：用户看到「暂无任务」既不知道是「该继续输入」
// 还是「该换个条件」，只能自己猜。现在把「现在是什么状态」和「下一步做什么」
// 说清楚，并把出口做成一个能点的按钮。
Rectangle {
    id: root

    // 图标：一个字符即可，不引入图片资源
    property string icon: "◌"
    // 主文案：一句话说清现在是什么状态
    property string title: ""
    // 副文案：说清下一步怎么做
    property string subtitle: ""
    // 行动按钮文案；空串表示这次没有可给的动作
    property string actionText: ""
    // 强调色：跟随当前主题（检索用青、异常用危险色）
    property color accent: Theme.cyan

    signal actionTriggered()

    implicitWidth: 360
    implicitHeight: content.implicitHeight + Theme.spacingXl * 2
    radius: Theme.radiusPanel
    color: Theme.panel
    border.width: 1
    border.color: Theme.hairline

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
            text: root.title
            visible: root.title.length > 0
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: root.subtitle
            visible: root.subtitle.length > 0
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
            visible: root.actionText.length > 0
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
