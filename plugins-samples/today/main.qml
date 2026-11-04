import QtQuick
import NeonBoard

// R16-05：颜色一律 Theme.xxx 令牌 —— 示例是教学材料，不能教硬编码反模式。
// 令牌随主题切换自动渐变，与宿主看板视觉一致。
Rectangle {
    id: root
    color: "transparent"

    property int todoCount: 0
    property int doingCount: 0
    property int doneTodayCount: 0
    property int overdueCount: 0

    function refresh() {
        if (!pluginHost.active)
            return

        var list = pluginHost.tasks()
        var now = Date.now()
        var start = new Date()
        start.setHours(0, 0, 0, 0)

        var todo = 0
        var doing = 0
        var doneToday = 0
        var overdue = 0

        for (var i = 0; i < list.length; ++i) {
            var task = list[i]
            if (task.status === "todo")
                ++todo
            else if (task.status === "doing")
                ++doing
            else if (task.status === "done"
                     && Number(task.completedAt) >= start.getTime())
                ++doneToday

            if (task.status !== "done"
                    && Number(task.dueAt) > 0
                    && Number(task.dueAt) < now)
                ++overdue
        }

        todoCount = todo
        doingCount = doing
        doneTodayCount = doneToday
        overdueCount = overdue
    }

    Component.onCompleted: {
        refresh()
        // R16-01：面板侧代理补了 eventReceived 转发，订阅任务域后统计随看板自动刷新。
        // subscribe 返回 false（未授权 events 等）时静默退化，手动刷新按钮仍然可用。
        if (pluginHost.subscribe(["task.created", "task.updated", "task.moved",
                                  "task.completed", "task.removed"]))
            pluginHost.eventReceived.connect(function() { refresh() })
    }

    component StatCard: Rectangle {
        property string label: ""
        property int value: 0
        property color accent: Theme.cyan

        width: 160
        height: 72
        radius: 10
        color: Theme.card
        border.width: 1
        border.color: Qt.rgba(accent.r, accent.g, accent.b, 0.35)

        Column {
            anchors.centerIn: parent
            spacing: 3

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: value
                color: accent
                font.pixelSize: 26
                font.bold: true
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: label
                color: Theme.muted
                font.pixelSize: 10
            }
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 340)
        spacing: 14

        Column {
            width: parent.width
            spacing: 3

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "今日概览"
                color: Theme.text
                font.pixelSize: 18
                font.bold: true
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "TODAY / OVERVIEW"
                color: Theme.cyan
                font.pixelSize: 9
                font.letterSpacing: 2
            }
        }

        Grid {
            width: parent.width
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            StatCard {
                width: (parent.width - parent.columnSpacing) / 2
                label: "待办"
                value: root.todoCount
                accent: Theme.cyan
            }

            StatCard {
                width: (parent.width - parent.columnSpacing) / 2
                label: "进行中"
                value: root.doingCount
                accent: Theme.violet
            }

            StatCard {
                width: (parent.width - parent.columnSpacing) / 2
                label: "今日完成"
                value: root.doneTodayCount
                accent: Theme.magenta
            }

            StatCard {
                width: (parent.width - parent.columnSpacing) / 2
                label: "逾期"
                value: root.overdueCount
                accent: root.overdueCount > 0 ? Theme.magenta : Theme.muted
            }
        }

        Rectangle {
            width: parent.width
            height: 34
            radius: 8
            color: refreshArea.pressed ? Theme.cardHover : Theme.card
            border.width: 1
            border.color: refreshArea.containsMouse ? Theme.cyan : Theme.border

            Text {
                anchors.centerIn: parent
                text: "刷新概览"
                color: Theme.text
                font.pixelSize: 12
            }

            MouseArea {
                id: refreshArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.refresh()
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "数据来自本地 SQLite · 看板变动自动刷新（R16-01）"
            color: Theme.muted
            font.pixelSize: 9
        }
    }
}
