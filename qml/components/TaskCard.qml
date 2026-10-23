import QtQuick

// 任务卡片：看板三列的最小信息单元。
// 动效链：进场滑入 → 悬停浮起 → 按下微沉 → 拖起放大 → 勾选光扫 → 删除故障 → 撤销滑回。
Rectangle {
    id: card

    property string title: ""
    property string tag: ""
    property int priority: 1
    property string due: ""
    property string taskId: ""
    property string taskStatus: "todo"

    // 主任务 / 子任务关联（数据来自 taskTree 门面：独立边表 task_link）
    readonly property string parentTitle: taskTree.parentTitle(taskId)
    readonly property var childStats: {
        var rev = taskTree.revision   // 建立依赖：关联或子任务状态变化时重算
        return taskTree.childStats(taskId)
    }
    readonly property bool hasParent: parentTitle.length > 0

    // 重复规则（数据来自 taskExtra 门面：独立侧表 task_extra）
    readonly property string rrule: {
        var rev = taskExtra.revision   // 建立依赖：侧表变化时重算
        return taskExtra.extraOf(taskId).rrule
    }
    readonly property bool hasChildren: childStats.total > 0

    // 拖拽坐标基准（由外部注入主内容区域）
    property Item dragSpace: null

    // 卡片槽位（插件 contributes.slots.card）展开后占用的额外高度
    readonly property real cardSlotExtra: cardSlot.implicitHeight > 0 ? cardSlot.implicitHeight + 8 : 0

    // 逾期：due 形如 "MM-dd"，同年按字典序比较即可
    readonly property bool overdue: {
        if (due.length !== 5)
            return false
        var now = new Date()
        var today = ("0" + (now.getMonth() + 1)).slice(-2) + "-" + ("0" + now.getDate()).slice(-2)
        return due < today
    }

    // 内部状态
    property bool busy: false        // 正在播放完成/删除动效，屏蔽重复点击
    property bool glitching: false
    property real offsetX: 0         // 进场位移 / 故障抖动用
    property real overduePulse: 0
    property real enterOpacity: 0    // 进场淡入，拖起时再整体压暗
    property bool highlight: false   // 命令面板跳转定位时高亮

    signal editRequested()
    signal completeRequested()
    signal restoreRequested()
    signal startRequested()
    signal deleteRequested()
    signal dragStarted(string taskId, string title, string status, real x, real y)
    signal dragMoved(real x, real y)
    signal dragEnded(real x, real y)

    height: 82 + card.cardSlotExtra
    radius: 12
    color: cardMouse.containsMouse ? Theme.cardHover : Theme.card
    border.width: card.highlight ? 2 : 1
    border.color: {
        if (card.highlight)
            return Theme.cyan
        if (card.glitching)
            return Theme.danger
        if (card.overdue)
            return Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.25 + 0.45 * card.overduePulse)
        return cardMouse.containsMouse ? Theme.borderStrong : Theme.border
    }
    scale: cardMouse.pressed ? 0.985 : (cardMouse.dragging ? 1.04 : (cardMouse.containsMouse ? 1.012 : 1.0))
    // 拖起时压暗成「虚位」，告诉用户这张卡已经离手
    opacity: card.enterOpacity * (cardMouse.dragging ? 0.35 : 1)
    clip: true

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
    Behavior on enterOpacity {
        NumberAnimation { duration: Motion.enter; easing.type: Easing.OutCubic }
    }

    Component.onCompleted: card.enterOpacity = 1

    // 进场：从右侧滑入并淡入（新建与撤销恢复共用）
    NumberAnimation on offsetX {
        from: 26
        to: 0
        duration: Motion.enter
        easing.type: Easing.OutCubic
    }

    // 逾期呼吸：低频、不刺眼
    NumberAnimation on overduePulse {
        running: card.overdue
        loops: Animation.Infinite
        from: 0
        to: 1
        duration: 1400
        easing.type: Easing.InOutSine
    }

    function priorityColor(p) {
        if (p >= 3)
            return Theme.magenta
        if (p === 2)
            return Theme.violet
        return Theme.cyan
    }

    // ---------- 完成光扫 ----------
    function playSweep() {
        if (card.busy)
            return
        card.busy = true
        sweepBand.sweepAnimation.restart()
        sweepDelay.restart()
    }

    Timer {
        id: sweepDelay
        interval: Motion.enter
        onTriggered: {
            card.busy = false
            card.completeRequested()
        }
    }

    // ---------- 删除故障 ----------
    function playGlitch() {
        if (card.busy)
            return
        card.busy = true
        card.glitching = true
        glitchAnimation.restart()
        glitchDelay.restart()
    }

    SequentialAnimation {
        id: glitchAnimation
        NumberAnimation { target: card; property: "offsetX"; to: -5; duration: 32 }
        NumberAnimation { target: card; property: "offsetX"; to: 5; duration: 32 }
        NumberAnimation { target: card; property: "offsetX"; to: -3; duration: 32 }
        NumberAnimation { target: card; property: "offsetX"; to: 0; duration: 42 }
    }

    Timer {
        id: glitchDelay
        interval: 220
        onTriggered: {
            card.glitching = false
            card.busy = false
            card.deleteRequested()
        }
    }

    // 卡片内容（整体做位移，承载进场与故障抖动）
    Item {
        id: inner
        width: parent.width
        height: 82
        anchors.top: parent.top
        x: card.offsetX

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

        // 归属竖线：有父任务时贴在左侧，提示「这张属于上面那张」
        Rectangle {
            visible: card.hasParent
            width: 1.5
            height: parent.height - 22
            radius: 1
            anchors.left: parent.left
            anchors.leftMargin: 3
            anchors.verticalCenter: parent.verticalCenter
            color: Qt.rgba(Theme.accent3.r, Theme.accent3.g, Theme.accent3.b, 0.45)
        }

        Column {
            anchors.left: priorityBar.right
            anchors.leftMargin: 11 + (card.hasParent ? 13 : 0)
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
                    visible: card.hasParent
                    height: 18
                    width: parentChipText.width + 12
                    radius: 5
                    color: Qt.rgba(Theme.accent3.r, Theme.accent3.g, Theme.accent3.b, 0.14)
                    border.width: 1
                    border.color: Qt.rgba(Theme.accent3.r, Theme.accent3.g, Theme.accent3.b, 0.28)

                    Text {
                        id: parentChipText
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, 190)
                        text: "↳ " + card.parentTitle
                        color: Theme.accent3
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }

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
                    visible: card.rrule.length > 0
                    text: "↻"
                    color: Theme.muted
                    font.pixelSize: 10
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: card.due.length > 0
                    text: card.due
                    color: card.overdue ? Theme.danger : Theme.muted
                    font.pixelSize: 10
                }
            }
        }

        // 快捷动作：悬停时渐显
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
                color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.24)
                border.width: 1
                border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.35)

                Text {
                    anchors.centerIn: parent
                    text: "▶"
                    color: Theme.cyan
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
                color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.24)
                border.width: 1
                border.color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.35)

                Text {
                    anchors.centerIn: parent
                    text: card.taskStatus === "done" ? "↩" : "✓"
                    color: Theme.success
                    font.pixelSize: 12
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: card.taskStatus === "done" ? card.restoreRequested()
                                                         : card.playSweep()
                }
            }

            Rectangle {
                width: 22
                height: 22
                radius: 6
                color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.24)
                border.width: 1
                border.color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.35)

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    color: Theme.danger
                    font.pixelSize: 11
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: card.playGlitch()
                }
            }
        }

        // 子任务进度环：父卡右下角显示「已完成/总数」
        Item {
            id: subtaskProgress
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 10
            anchors.bottomMargin: 8
            height: 22
            width: progressRing.width + 5 + progressText.width
            visible: card.hasChildren
            onVisibleChanged: progressRing.requestPaint()

            Canvas {
                id: progressRing
                width: 20
                height: 20
                anchors.verticalCenter: parent.verticalCenter
                // 关联或子任务状态变化时重绘
                property int statsRev: taskTree.revision
                onStatsRevChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    var cx = width / 2
                    var cy = height / 2
                    var r = width / 2 - 2.5
                    ctx.lineWidth = 2.5
                    ctx.strokeStyle = Theme.border
                    ctx.beginPath()
                    ctx.arc(cx, cy, r, 0, Math.PI * 2)
                    ctx.stroke()
                    if (card.childStats.total > 0) {
                        ctx.strokeStyle = Theme.accent
                        ctx.beginPath()
                        ctx.arc(cx, cy, r, -Math.PI / 2,
                                -Math.PI / 2 + Math.PI * 2 * (card.childStats.done / card.childStats.total))
                        ctx.stroke()
                    }
                }
            }

            Text {
                id: progressText
                anchors.left: progressRing.right
                anchors.leftMargin: 5
                anchors.verticalCenter: parent.verticalCenter
                text: card.childStats.done + "/" + card.childStats.total
                color: Theme.muted
                font.pixelSize: 10
            }
        }
    }

    // 勾选光扫：一道光带掠过卡片
    Rectangle {
        id: sweepBand
        width: card.width * 0.7
        height: card.height * 1.8
        rotation: 18
        x: -card.width
        opacity: 0
        visible: opacity > 0.01
        z: 4
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.glow }
            GradientStop { position: 1.0; color: "transparent" }
        }

        SequentialAnimation {
            id: sweepAnimation
            NumberAnimation { target: sweepBand; property: "x"; from: -sweepBand.width; to: card.width; duration: Motion.enter; easing.type: Easing.InOutSine }
            NumberAnimation { target: sweepBand; property: "opacity"; from: 0.9; to: 0; duration: Motion.enter }
        }
    }

    // 拖拽 / 双击编辑
    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: !card.busy
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

    // 卡片插件槽位：插件可往卡片底部追加自己的字段
    PluginSlot {
        id: cardSlot
        slotName: "card"
        taskId: card.taskId
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.bottomMargin: 8
    }
}
