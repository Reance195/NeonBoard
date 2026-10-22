import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NeonBoard

// Agent 任务桥面板：展示后台入口的运行状态与说明。
// 解析 / 映射 / 流转策略都在 background.qml（它们不该有两个实现）；
// 这里只读 background 的状态可见的 meta 之外的事实：
// 通过共享 meta 键 agentbridge.status 把后台最新摘要带过来。
Rectangle {
    id: root

    color: "transparent"

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Text {
            text: "Agent 任务桥"
            color: Theme.text
            font.pixelSize: 13
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            text: "监听事件域 agent.task（agent-task-event v1 协议）。"
                  + "AI Agent 的适配器（如 opencode 的 neonboard-todo-bridge）"
                  + "把任务清单投递进来，插件自动新建与流转对应卡片。"
            color: Theme.muted
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "最近批次"
                color: Theme.muted
                font.pixelSize: 10
                Layout.preferredWidth: 60
            }

            Text {
                Layout.fillWidth: true
                text: backgroundState.summary
                color: Theme.text
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "累计事件"
                color: Theme.muted
                font.pixelSize: 10
                Layout.preferredWidth: 60
            }

            Text {
                text: backgroundState.count > 0 ? String(backgroundState.count) : "0"
                color: Theme.cyan
                font.pixelSize: 10
            }

            Item { Layout.fillWidth: true }
        }

        Text {
            Layout.fillWidth: true
            text: "映射关系保存在 meta 键 agentbridge.<agent>.<sessionId>.<todoId>，"
                  + "卡片标题不带短号；任务删除后下次投递会自动重建。"
            color: Theme.muted
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }
    }

    // 板面通过共享 meta 读取后台状态（后台每次处理批次后写入）
    QtObject {
        id: backgroundState

        property string summary: ""
        property int count: 0
    }

    Component.onCompleted: {
        backgroundState.summary = pluginHost.metaValue("agentbridge.status", "尚未收到事件")
        var n = pluginHost.metaValue("agentbridge.handled", "0")
        backgroundState.count = parseInt(n, 10) || 0
    }
}
