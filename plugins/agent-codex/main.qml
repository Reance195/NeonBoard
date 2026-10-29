import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NeonBoard

// Agent provider 名片。这个 agent 是唯一一个「声明了但默认不启用」的 ——
// 参数实测过，整轮 exec 没跑通，面板必须把这件事说清楚，
// 否则用户会以为「这个插件坏了」而不是「这条 profile 还没验收」。
Rectangle {
    id: root

    color: "transparent"

    readonly property bool granted: pluginHost ? pluginHost.hasPermission("exec.agentProfiles") : false

    readonly property string exeLine: "codex exec --json <prompt>   （标准输入接 NUL）"
    readonly property string readonlyLine: "codex exec --json -s read-only <prompt>"

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
                text: "Codex CLI"
                color: Theme.text
                font.pixelSize: 13
                font.bold: true
            }

            Rectangle {
                width: enabledLabel.implicitWidth + 12
                height: 16
                radius: 8
                color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.14)
                border.width: 1
                border.color: Theme.warning

                Text {
                    id: enabledLabel
                    anchors.centerIn: parent
                    text: "默认未启用"
                    color: Theme.warning
                    font.pixelSize: 9
                }
            }

            Item { Layout.fillWidth: true }
        }

        Text {
            Layout.fillWidth: true
            text: "为什么默认不启用：参数（exec --json / -s read-only）是实机看过的，"
                  + "但整轮 exec 至今没能在超时内跑到 turn.completed —— 本机走本地代理，"
                  + "config.toml 还把 provider 指到了自建网关。"
                  + "按 docs/agent-profile.md 的规矩，未跑通整轮的 profile 不进可选列表。"
            color: Theme.warning
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        KeyValue { k: "执行命令"; v: root.exeLine }
        KeyValue { k: "只读命令"; v: root.readonlyLine }
        KeyValue { k: "输出解析"; v: "json（事件流，末条回复要用 -o 另收，执行器未接管）" }
        KeyValue { k: "超时"; v: "30 分钟（可在设置页逐条覆盖）" }

        Text {
            Layout.fillWidth: true
            text: "stdin 必须接 NUL：不接的话 codex 会停在"
                  + "「Reading additional input from stdin...」直到超时，症状像模型卡住。"
            color: Theme.muted
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
            text: "本机装法：Microsoft Store 版，CLI 本体不在 PATH，"
                  + "藏在 %ProgramFiles%/WindowsApps/OpenAI.Codex_<版本>/app/resources/codex.exe。"
                  + "上面声明的 exe 是裸命令 codex —— 要用就先把它加进 PATH，"
                  + "或在设置页把这条改成绝对路径（版本号随升级变化）。"
            color: Theme.muted
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: "启用条件：先手工跑通一次以 turn.completed 结束的 exec，"
                  + "再到设置页把这条改成启用。没跑通就启用，失败会记成「超时」，"
                  + "而归因线索只在这段说明里。"
            color: Theme.cyan
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: root.granted
                  ? "已获「声明 Agent」权限。"
                  : "尚未获得「声明 Agent」权限：声明会被静默忽略，请到插件授权里开启。"
            color: root.granted ? Theme.muted : Theme.warning
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }
    }
}
