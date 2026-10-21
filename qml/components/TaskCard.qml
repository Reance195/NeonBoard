import QtQuick

// 任务卡片：看板三列的最小信息单元。
// 交互基线：悬停浮起 + 描边发光，按下微沉，动作按钮渐显，进场淡入。
Rectangle {
    id: card

    property string title: ""
    property string tag: ""
    property int priority: 1
    property string due: ""
    property string taskId: ""
    property string taskStatus: "todo"

    // 拖拽坐标基准（由外部注入主内容区域）
    property Item dragSpace: null

    signal editRequested()
    signal completeRequested()
    signal restoreRequested()
    signal startRequested()
    signal deleteRequested()
    signal dragStarted(string taskId, string title, string status, real x, real y)
    signal dragMoved(real x, real y)
    signal dragEnded(real x, real y)

    height: 82
    radius: 12
    color: cardMouse.containsMouse ? Theme.cardHover : Theme.card
    border.width: 1
    border.color: cardMouse.containsMouse ? Theme.borderStrong : Theme.border
    scale: cardMouse.pressed ? 0.985 : (cardMouse.containsMouse ? 1.012 : 1.0)
    opacity: 0

    Behavior on scale {
        NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
    }
    Behavior on color {
        ColorAnimation { duration: Motion.fast }
    }
    Behavior on border.color {
        ColorAnimation { duration: Motion.fast }
    }
    Behavior on opacity {
        NumberAnimation { duration: Motion.enter; easing.type: Easing.OutCubic }
    }

    Component.onCompleted: opacity = 1

    function priorityColor(p) {
        if (p >= 3)
            return Theme.magenta
        if (p === 2)
            return Theme.violet
        return Theme.cyan
    }

    // 优先级光条
    Rectangle {
        id: priorityBar
        width: 3
        height: parent.height - 18
        radius: 2
        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        color: card.priorityColor(card.priority)
        opacity: cardMouse.containsMouse ? 1.0 : 0.7

        Behavior on opacity {
            NumberAnimation { duration: Motion.fast }
        }
    }

    Column {
        anchors.left: priorityBar.right
        anchors.leftMargin: 11
        anchors.right: parent.right
        anchors.rightMargin: 82
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7

        Text {
            width: parent.width
            text: card.title
            color: Theme.text
            font.pixelSize: 13
            font.bold: true
            elide: Text.ElideRight
        }

        Row {
            spacing: 6

            Rectangle {
                visible: card.tag.length > 0
                height: 18
                width: tagText.implicitWidth + 12
                radius: 5
                color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.16)
                border.width: 1
                border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.30)

                Text {
                    id: tagText
                    anchors.centerIn: parent
                    text: card.tag
                    color: Theme.cyan
                    font.pixelSize: 10
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: card.due.length > 0
                text: card.due
                color: Theme.muted
                font.pixelSize: 10
            }
        }
    }

    // 拖拽 / 双击编辑
    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor
        onDoubleClicked: card.editRequested()

        property point pressPoint
        property bool dragging: false

        onPressed: function(mouse) {
            pressPoint = Qt.point(mouse.x, mouse.y)
            dragging = false
        }

        onPositionChanged: function(mouse) {
            if (!cardMouse.pressed)
                return
            var delta = Math.abs(mouse.x - pressPoint.x) + Math.abs(mouse.y - pressPoint.y)
            if (!dragging && delta > 8) {
                dragging = true
                var p = cardMouse.mapToItem(card.dragSpace, mouse.x, mouse.y)
                card.dragStarted(card.taskId, card.title, card.taskStatus, p.x, p.y)
            }
            if (dragging) {
                var q = cardMouse.mapToItem(card.dragSpace, mouse.x, mouse.y)
                card.dragMoved(q.x, q.y)
            }
        }

        onReleased: function(mouse) {
            if (dragging) {
                var p = cardMouse.mapToItem(card.dragSpace, mouse.x, mouse.y)
                card.dragEnded(p.x, p.y)
            }
            dragging = false
        }
    }

    // 快捷动作：悬停时渐显，不再硬切
    Row {
        id: actions
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        spacing: 4
        z: 3
        opacity: cardMouse.containsMouse ? 1.0 : 0.0
        visible: opacity > 0.01

        Behavior on opacity {
            NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
        }

        Rectangle {
            visible: card.taskStatus === "todo"
            width: 22
            height: 22
            radius: 6
            color: Qt.rgba(0.12, 0.35, 0.70, 0.55)
            border.width: 1
            border.color: Qt.rgba(0.35, 0.70, 1.00, 0.35)

            Text {
                anchors.centerIn: parent
                text: "▶"
                color: "#8FD3FF"
                font.pixelSize: 10
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: card.startRequested()
            }
        }

        Rectangle {
            width: 22
            height: 22
            radius: 6
            color: Qt.rgba(0.10, 0.50, 0.45, 0.55)
            border.width: 1
            border.color: Qt.rgba(0.30, 0.95, 0.80, 0.35)

            Text {
                anchors.centerIn: parent
                text: card.taskStatus === "done" ? "↩" : "✓"
                color: "#7CFFE8"
                font.pixelSize: 12
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: card.taskStatus === "done" ? card.restoreRequested()
                                                     : card.completeRequested()
            }
        }

        Rectangle {
            width: 22
            height: 22
            radius: 6
            color: Qt.rgba(0.55, 0.10, 0.22, 0.55)
            border.width: 1
            border.color: Qt.rgba(1.00, 0.35, 0.55, 0.35)

            Text {
                anchors.centerIn: parent
                text: "✕"
                color: "#FF8FB0"
                font.pixelSize: 11
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: card.deleteRequested()
            }
        }
    }
}
