import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NeonBoard

// Agent provider 名片：「声明」这件事的全部内容都在 plugin.json 的 contributes.agentProfiles 里。
// 面板只解释参数含义与站外适配器怎么装 —— 不做第二份数据源，否则两边必然漂移。
Rectangle {
    id: root

    color: "transparent"

    readonly property bool granted: pluginHost ? pluginHost.hasPermission("exec.agentProfiles") : false

    // 与 plugin.json 保持同一份事实。测试会校验这两处与内置 profile 三者一致。
    readonly property string exeLine: "claude -p --output-format stream-json <prompt>"
    readonly property string readonlyLine: "claude --permission-mode plan -p --output-format stream-json <prompt>"
    readonly property string adapterCmd: "pwsh -File scripts/install-agent-adapters.ps1 -Install -Agent claude"

    component KeyValue: RowLayout {
        property string k: ""
        property string v: ""
        Layout.fillWidth: true
        spacing: 8

        Text {
            text: k
            color: Theme.muted
            font.pixelSize: 10
            Layout.preferredWidth: 64
        }

        Text {
            Layout.fillWidth: true
            text: v
            color: Theme.text
            font.pixelSize: 10
            font.family: "Consolas"
            wrapMode: Text.WrapAnywhere
            selectByMouse: true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "Claude Code"
                color: Theme.text
                font.pixelSize: 13
                font.bold: true
            }

            Rectangle {
                width: grantedLabel.implicitWidth + 12
                height: 16
                radius: 8
                color: root.granted ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.14)
                                    : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.14)
                border.width: 1
                border.color: root.granted ? Theme.success : Theme.warning

                Text {
                    id: grantedLabel
                    anchors.centerIn: parent
                    text: root.granted ? "已授权" : "待授权"
                    color: root.granted ? Theme.success : Theme.warning
                    font.pixelSize: 9
                }
            }

            Item { Layout.fillWidth: true }
        }

        Text {
            Layout.fillWidth: true
            text: root.granted
                  ? "已获「声明 Agent」权限：本插件声明的 profile 会出现在执行器与卡片派活的 agent 列表里，来源标为 plugin。"
                  : "尚未获得「声明 Agent」权限：声明被静默忽略（权限决定拉起哪个进程，不是提示项）。请到插件授权里开启后再试。"
            color: root.granted ? Theme.muted : Theme.warning
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        KeyValue { k: "执行命令"; v: root.exeLine }
        KeyValue { k: "只读命令"; v: root.readonlyLine }
        KeyValue { k: "输出解析"; v: "stream-json（逐事件流式写进运行日志）" }
        KeyValue { k: "超时"; v: "30 分钟（可在设置页逐条覆盖）" }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        Text {
            Layout.fillWidth: true
            text: "入站方向（agent 的待办 → 看板卡片）由站外适配器负责，装一次即可："
            color: Theme.muted
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: root.adapterCmd
            color: Theme.cyan
            font.pixelSize: 10
            font.family: "Consolas"
            wrapMode: Text.WrapAnywhere
            selectByMouse: true
        }

        Text {
            Layout.fillWidth: true
            text: "适配器写进 ~/.claude/settings.json 的 hooks，只追加不覆盖已有条目。"
                  + "出站（卡片派活）由本插件的声明负责，两条链路互不依赖。"
            color: Theme.muted
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }
    }
}
