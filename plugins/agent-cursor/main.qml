import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NeonBoard

// Agent provider 名片：内容全部在 plugin.json 的 contributes.agentProfiles 里。
// 这个 agent 的特殊之处不在参数，而在「装法」—— exe 能不能拉起与 isAvailable 是两件事，
// 面板必须把这一点讲明白，否则界面一片绿、派活却失败。
Rectangle {
    id: root

    color: "transparent"

    readonly property bool granted: pluginHost ? pluginHost.hasPermission("exec.agentProfiles") : false

    readonly property string exeLine: "cursor-agent -p --output-format stream-json <prompt>"
    readonly property string readonlyLine: "cursor-agent -p --output-format stream-json --plan <prompt>"
    readonly property string adapterCmd: "pwsh -File scripts/install-agent-adapters.ps1 -Install -Agents cursor"

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
                text: "Cursor Agent"
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
            text: "装法提醒：本机 PATH 上的 cursor-agent 是个 .cmd 包装器，"
                  + "真身是 %LOCALAPPDATA%/cursor-agent/versions/<版本>/node.exe 加 index.js，"
                  + "外层 .cmd 只负责转调 powershell 脚本。"
            color: Theme.muted
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: "「可用」只代表 exe 解析得到，不代表一定拉得起来。"
                  + "执行器以 exe 直接拉起进程、不经过 shell，遇到 .cmd 包装器时若报"
                  + "「进程启动失败」，请按上面的真实路径排查。"
            color: Theme.warning
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        Text {
            Layout.fillWidth: true
            text: "入站方向（agent 的待办 → 看板卡片）由 ~/.cursor/hooks.json 的适配器负责："
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

        Item { Layout.fillHeight: true }
    }
}
