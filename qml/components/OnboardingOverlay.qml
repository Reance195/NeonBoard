import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 组件行为：Bound —— 委托里的 root 才能拿到根对象实例的属性。
// 根对象是 DialogShell 的实例，不加这句的话委托里拿到的是「类型」，属性全是 undefined。
pragma ComponentBehavior: Bound

// 首启动引导（R11-10）：迁到统一弹窗壳 DialogShell —— md 560 档。
//
// 规格 §3.4：副标题位放「第 1 / 3 步」进度，不再自己画标题条；底部固定「跳过 / 下一步」。
//
// 显示由宿主驱动（Main.qml 用 visible 绑定 onboardVisible），所以本组件内部
// **不调用 open() / close()**：内部只改 step 并发信号，finished 交给宿主去改 visible。
// Esc / 点遮罩由壳的 closePolicy 关闭 → onClosed 里发 finished（等同原来的「跳过」）。
DialogShell {
    id: root

    titleText: "欢迎使用 NeonBoard"
    subtitleText: "第 " + (step + 1) + " / " + steps.length + " 步"
    sizeMode: "md"

    property int step: 0
    property bool canSeed: true

    signal finished()
    signal sampleRequested()
    // 点模板：带上预填标题直接开新建面板（空板上「不知道写什么」的那一步）
    signal templateRequested(string title)

    readonly property var templates: [
        "周报：本周完成 / 下周计划",
        "会议记录：议题 / 结论 / 行动项",
        "Bug：现象 / 复现步骤 / 期望"
    ]

    readonly property var steps: [
        {
            "title": "先记一条任务",
            "body": "按 Ctrl+N 聚焦顶部输入框，直接回车即建卡。\n支持自然语言：「明天 10:00 写周报 #工作 !3」。",
            "keys": "Ctrl+N  ·  Enter"
        },
        {
            "title": "把任务拖动起来",
            "body": "在卡片上按住拖动，从「待办」拖到「进行中」，\n完成后拖到「已办结」——完成时间会自动进入统计热力图。",
            "keys": "拖拽  ·  ←→ 换列"
        },
        {
            "title": "按需装个插件",
            "body": "侧栏「插件」里有插件库；计划钩子能把 Git 提交里的\n#T12=done 自动流转到看板。不需要就跳过。",
            "keys": "Ctrl+K  ·  命令面板"
        }
    ]

    // 是否已经表态过（跳过 / 收尾）：避免「点了按钮 → 壳关闭 → onClosed」重复发 finished
    property bool finishedEmitted: false

    // 从命令面板「重看新手引导」进来时用：回到第一页，标记状态不动
    function restart() {
        step = 0
        finishedEmitted = false
    }

    // 结束引导：只发信号，visible 由宿主改（组件内部不碰 open / close）
    function finish() {
        if (finishedEmitted)
            return
        finishedEmitted = true
        finished()
    }

    // 打开时：复位表态标记，并把焦点交给壳（Esc 要能被壳收到）
    onOpened: {
        finishedEmitted = false
        forceActiveFocus()
    }

    // Esc / 点遮罩 → 壳自己关闭 → 等同「跳过」
    onClosed: finish()

    // 右上角关闭键：同样只是结束引导
    onCloseRequested: finish()

    // 底部：主操作 = 下一步 / 开始使用；次操作 = 上一步
    primaryText: step === steps.length - 1 ? "开始使用" : "下一步"
    secondaryText: step > 0 ? "上一步" : ""

    onPrimaryTriggered: {
        if (step === steps.length - 1)
            finish()
        else
            step += 1
    }

    onSecondaryTriggered: step -= 1

    // ---------- 正文 ----------
    ColumnLayout {
        id: content
        Layout.fillWidth: true
        spacing: Theme.spacingMd

        // 无障碍：整个浮层当作一个对话框播报，读屏用户至少知道当前停在哪一步
        Accessible.role: Accessible.Dialog
        Accessible.name: root.steps[root.step].title
        Accessible.description: "首启动引导，第 " + (root.step + 1) + " / "
                                + root.steps.length + " 步，Esc 可跳过"

        // 进度点
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            Item { Layout.fillWidth: true }

            Repeater {
                model: root.steps.length

                delegate: Rectangle {
                    required property int index

                    width: 22
                    height: 4
                    radius: Theme.radiusControl
                    color: index <= root.step ? Theme.cyan : Theme.alpha(Theme.text, 0.14)
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.steps[root.step].title
            color: Theme.text
            font.pixelSize: Theme.fontDisplay
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            text: root.steps[root.step].body
            color: Theme.muted
            font.pixelSize: Theme.fontBody
            lineHeight: 1.45
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            radius: Theme.radiusControl
            color: Theme.input
            border.width: 1
            border.color: Theme.border

            Text {
                anchors.centerIn: parent
                text: root.steps[root.step].keys
                color: Theme.accent2
                font.pixelSize: Theme.fontCaption
                font.family: "Consolas"
            }
        }

        // 任务模板：空板上最常见的三个「不知道写什么」，点一下预填
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            visible: root.step === 0

            Repeater {
                model: root.templates

                delegate: Rectangle {
                    required property string modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    radius: Theme.radiusControl
                    color: tplMouse.containsMouse ? Theme.cardHover : Theme.input
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - Theme.spacingMd
                        text: modelData.split("：")[0]
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                    }

                    MouseArea {
                        id: tplMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.templateRequested(modelData)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            radius: Theme.radiusControl
            visible: root.step === 0 && root.canSeed
            color: seedMouse.containsMouse ? Theme.cardHover : Theme.card
            border.width: 1
            border.color: Theme.border

            Text {
                anchors.centerIn: parent
                text: "载入三张示例卡（可随时删除）"
                color: Theme.cyan
                font.pixelSize: Theme.fontCaption
            }

            MouseArea {
                id: seedMouse
                anchors.fill: parent
                hoverEnabled: true
                // 载入示例卡后直接进入下一步：用户手上已经有牌可拖，
                // 引导该讲「怎么拖」而不是停在「先建一条」
                onClicked: {
                    root.sampleRequested()
                    root.step = Math.min(root.step + 1, root.steps.length - 1)
                }
            }
        }
    }

    // ---------- 底栏左侧：跳过 ----------
    footerExtra: Component {
        TopToolButton {
            text: "跳过"
            description: "跳过新手引导"
            accent: Theme.muted
            onClicked: root.finish()
        }
    }
}
