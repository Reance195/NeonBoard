import QtQuick

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

    Component.onCompleted: refresh()

    component StatCard: Rectangle {
        property string label: ""
        property int value: 0
        property color accent: "#22D3EE"

        width: 160
        height: 72
        radius: 10
        color: "#0A1524"
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
                color: "#7C8BA1"
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
                color: "#E6F1FF"
                font.pixelSize: 18
                font.bold: true
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "TODAY / OVERVIEW"
                color: "#22D3EE"
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
                accent: "#22D3EE"
            }

            StatCard {
                width: (parent.width - parent.columnSpacing) / 2
                label: "进行中"
                value: root.doingCount
                accent: "#A78BFA"
            }

            StatCard {
                width: (parent.width - parent.columnSpacing) / 2
                label: "今日完成"
                value: root.doneTodayCount
                accent: "#F472B6"
            }

            StatCard {
                width: (parent.width - parent.columnSpacing) / 2
                label: "逾期"
                value: root.overdueCount
                accent: root.overdueCount > 0 ? "#F472B6" : "#4B6478"
            }
        }

        Rectangle {
            width: parent.width
            height: 34
            radius: 8
            color: refreshArea.pressed ? "#12303A" : "#0B1B26"
            border.width: 1
            border.color: refreshArea.containsMouse ? "#22D3EE" : "#1E4A57"

            Text {
                anchors.centerIn: parent
                text: "刷新概览"
                color: "#E6F1FF"
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
            text: "数据来自本地 SQLite"
            color: "#5B6B80"
            font.pixelSize: 9
        }
    }
}
