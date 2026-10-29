import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 看板单列：标题 + 任务列表 + 快速添加。
// 只负责呈现与局部交互，任务变更通过信号抛给上层处理。
Rectangle {
    id: column

    property string title: ""
    property color accent: Theme.cyan
    property var taskModel
    // 列 id（schema v2）。非空时列内排序 / 快速录入 / 落点计算都按列走，
    // 空串表示退化模式（列 id 对不上），退回按 status 处理。
    property string columnId: ""
    property string status: "todo"
    // 搜索关键字（由上层注入，用于空态文案判断）
    property string searchQuery: ""
    // 里程碑过滤（由上层注入，用于空态文案判断）
    property string stageFilter: ""
    // 项目范围（由上层注入）：空串 = 全局。非空时列里只剩该项目的卡，
    // 卡片也不再挂项目角标（整列同项目，标了是噪音）。
    property string projectFilter: ""
    // 是否在卡片上显示项目角标：全局视图下才需要
    property bool showProjectBadge: projectFilter === ""
    // 拖拽坐标基准（与卡片保持同一坐标系）
    property Item dragSpace: null
    // 是否为当前拖拽悬停的列（边框呼吸）
    property bool dropTarget: false
    property real dropPulse: 0
    // 拖拽悬停时的插入落点：beforeId 为空表示落到列尾
    property string dropBeforeId: ""
    // 插入指示线锚定的卡片 id（空表示画在列尾）
    property string dropAnchorId: ""
    // 指示线画在锚定卡片的下方（true）还是上方（false）
    property bool dropAnchorBelow: false
    // 落点已磁吸（鼠标贴上了插入线）：指示线加粗一档，给「吸住了、松手就落这儿」的反馈
    property bool dropSnapped: false
    // 快速录入解析预览：实时显示识别出的到期时间、优先级与标签
    property string parseDue: ""
    property int parsePriority: -1
    property var parseTags: []
    property bool parseHasText: false

    // 插位磁吸半径：鼠标离插入线小于这个距离就算「贴上」，拖影纵向吸过去。
    // 6px 是试出来的：再小则要精确瞄准，再大则两行之间会来回跳。
    property real snapDistance: 6
    // 落位涟漪的落点（列内 y）；负数表示没给，退回落在本列底部
    property real rippleY: -1

    property string highlightId: ""

    // 键盘导航：当前列内选中的任务 id（空表示未选中）
    property string selectedId: ""
    // 列序号（Main.qml 注入，左右键切换列时使用）
    property int columnIndex: 0

    signal selectionChanged(string taskId)
    signal columnMoveRequested(int targetIndex)

    function taskIds() {
        if (column.columnId.length > 0)
            return taskStore.taskIdsInColumn(column.columnId)
        return taskStore.taskIdsInStatus(column.status)
    }

    function focusList() {
        list.forceActiveFocus()
    }

    function selectId(taskId) {
        if (column.selectedId === taskId)
            return
        column.selectedId = taskId
        column.selectionChanged(taskId)
        var ids = column.taskIds()
        var idx = ids.indexOf(taskId)
        if (idx >= 0)
            list.positionViewAtIndex(idx, ListView.Contain)
    }

    function selectFirst() {
        var ids = column.taskIds()
        if (ids.length === 0) {
            column.selectedId = ""
            column.selectionChanged("")
            return
        }
        var target = (column.selectedId.length > 0 && ids.indexOf(column.selectedId) >= 0)
                   ? column.selectedId : ids[0]
        column.selectId(target)
    }

    function selectByOffset(delta) {
        var ids = column.taskIds()
        if (ids.length === 0) {
            column.selectedId = ""
            column.selectionChanged("")
            return
        }
        var idx = ids.indexOf(column.selectedId)
        if (idx < 0)
            idx = delta > 0 ? -1 : 0
        idx = Math.max(0, Math.min(ids.length - 1, idx + delta))
        column.selectId(ids[idx])
    }

    // 列内键盘：上下选卡、回车编辑、空格完成/恢复、Delete 删除、Esc 取消选中
    function handleKey(event) {
        if (column.selectedId.length === 0 && event.key === Qt.Key_Down) {
            column.selectFirst()
            event.accepted = true
            return
        }
        switch (event.key) {
        case Qt.Key_Down:
            column.selectByOffset(1)
            event.accepted = true
            break
        case Qt.Key_Up:
            column.selectByOffset(-1)
            event.accepted = true
            break
        case Qt.Key_Left:
            column.columnMoveRequested(column.columnIndex - 1)
            event.accepted = true
            break
        case Qt.Key_Right:
            column.columnMoveRequested(column.columnIndex + 1)
            event.accepted = true
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (column.selectedId.length > 0) {
                column.editRequested(column.selectedId)
                event.accepted = true
            }
            break
        case Qt.Key_Space:
            if (column.selectedId.length > 0) {
                if (column.status === "done")
                    column.restoreRequested(column.selectedId)
                else
                    column.completeRequested(column.selectedId)
                event.accepted = true
            }
            break
        case Qt.Key_Delete:
            if (column.selectedId.length > 0) {
                column.deleteRequested(column.selectedId)
                event.accepted = true
            }
            break
        case Qt.Key_Escape:
            if (column.selectedId.length > 0) {
                column.selectedId = ""
                column.selectionChanged("")
                event.accepted = true
            }
            break
        }
    }

    function flashTask(taskId) {
        highlightId = taskId
        for (var i = 0; i < list.count; i++) {
            var item = list.itemAtIndex(i)
            if (item && item.taskId === taskId) {
                list.positionViewAtIndex(i, ListView.Center)
                break
            }
        }
        highlightTimer.restart()
    }

    signal editRequested(string taskId)
    signal startRequested(string taskId)
    signal completeRequested(string taskId)
    signal restoreRequested(string taskId)
    signal deleteRequested(string taskId)
    signal dragStarted(string taskId, string title, string status, real x, real y)
    signal dragMoved(real x, real y)
    signal dragEnded(real x, real y)

    function focusQuickAdd() {
        quickAdd.forceActiveFocus()
    }

    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.minimumWidth: 150
    Layout.preferredWidth: 340
    radius: 16
    color: Theme.panel
    border.width: 1
    border.color: column.dropTarget
               ? Qt.rgba(accent.r, accent.g, accent.b, 0.45 + 0.35 * column.dropPulse)
               : Qt.rgba(accent.r, accent.g, accent.b, 0.20)

    Behavior on border.color {
        ColorAnimation { duration: Motion.base }
    }

    // 拖入氛围：整列底色浅浅浸一层目标色。
    // 只靠描边呼吸不够 —— 三列并排时边框本来就带色，用户很难一眼看出鼠标停在哪列。
    // 声明在这里（ColumnLayout 之前）是为了压在内容下面，不挡卡片与快速录入。
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: column.radius - 1
        color: Qt.rgba(column.accent.r, column.accent.g, column.accent.b,
                       0.045 + 0.055 * column.dropPulse)
        opacity: column.dropTarget ? 1 : 0
        visible: opacity > 0.01

        Behavior on opacity {
            NumberAnimation { duration: Motion.base; easing.type: Easing.OutCubic }
        }
    }

    onDropAnchorIdChanged: refreshDropLine()
    onDropAnchorBelowChanged: refreshDropLine()

    Timer {
        id: highlightTimer
        interval: 1800
        onTriggered: column.highlightId = ""
    }

    // 选中卡片被删除或移出本列后，清空选中态，避免指示条悬空
    Connections {
        target: column.taskModel
        function onCountChanged() {
            if (column.selectedId.length > 0 && column.taskIds().indexOf(column.selectedId) < 0) {
                column.selectedId = ""
                column.selectionChanged("")
            }
        }
    }

    // 拖入呼吸：仅在被拖拽悬停时循环
    NumberAnimation on dropPulse {
        running: column.dropTarget
        loops: Animation.Infinite
        from: 0
        to: 1
        duration: 900
        easing.type: Easing.InOutSine
    }

    // 落位涟漪：从「松手的那一点」扩散出去。
    // 之前固定在列底部，跨列拖到底部才放得出一圈 —— 明明是插在第三行，
    // 涟漪却从下面 200px 处冒出来，看着像没落准。
    function ripple(atY) {
        column.rippleY = (typeof atY === "number" && atY >= 0) ? atY : -1
        rippleRing.y = column.rippleY >= 0
                ? Math.max(4, Math.min(list.height - rippleRing.height - 4,
                                       column.rippleY - rippleRing.height / 2))
                : list.height - rippleRing.height
        rippleAnimation.restart()
    }

    // 当前已实例化的卡片代理（按纵向位置排序）
    function visibleDelegates() {
        var out = []
        if (!list.contentItem)
            return out
        var children = list.contentItem.children
        for (var i = 0; i < children.length; ++i) {
            var it = children[i]
            if (!it || it.taskId === undefined || it.y === undefined)
                continue
            out.push({ id: it.taskId, index: out.length, y: it.y, height: it.height })
        }
        out.sort(function(a, b) { return a.y - b.y })
        return out
    }

    // 解析拖拽落点：yInColumn 为列内坐标，返回 { beforeId, anchorId, below, lineY, viewY, onLine }
    // beforeId 为空串表示插到列尾；anchorId 为空串表示没有卡可锚（空列）
    //
    // 几何本身在 src/DragFeel.cpp（纯函数、单测覆盖）：这里只做 QML 独有的两件事 ——
    // 把视口 y 换算成内容 y，以及把线的位置换算回视口给绘制用。
    function resolveDrop(yInColumn) {
        var pt = list.mapFromItem(column, 0, yInColumn)
        var contentY = pt.y + list.contentY
        var place = dragFeel.resolveDrop(column.visibleDelegates(), column.taskIds(), contentY,
                                         list.spacing, column.snapDistance)
        place.viewY = list.y + place.lineY - list.contentY
        return place
    }

    // 在列表中找到锚定卡片的 delegate（遍历 contentItem 的子项，按 taskId 匹配）
    function anchorDelegate(anchorId) {
        if (!anchorId || !list || !list.contentItem)
            return null
        var children = list.contentItem.children
        for (var i = 0; i < children.length; ++i) {
            var it = children[i]
            if (it && it.taskId !== undefined && it.taskId === anchorId)
                return it
        }
        return null
    }

    // 刷新插入指示线的位置（列表滚动、delegate 实例化后都要重算）
    function refreshDropLine() {
        if (!dropLine)
            return
        var d = column.anchorDelegate(column.dropAnchorId)
        if (!d) {
            dropLine.visible = false
            return
        }
        // d.y 是 delegate 在 contentItem 中的坐标，换算到 viewport：减掉 contentY
        var contentY = d.y + (column.dropAnchorBelow ? d.height : 0)
        dropLine.y = list.y + contentY - list.contentY + (column.dropAnchorBelow ? list.spacing / 2 : -list.spacing / 2)
        dropLine.visible = true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: column.accent
            }

            Text {
                text: column.title
                color: Theme.text
                font.pixelSize: 14
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 28
                height: 20
                radius: 10
                color: Qt.rgba(column.accent.r, column.accent.g, column.accent.b, 0.12)
                border.width: 1
                border.color: Qt.rgba(column.accent.r, column.accent.g, column.accent.b, 0.24)

                Text {
                    anchors.centerIn: parent
                    text: column.taskModel ? column.taskModel.count : 0
                    color: column.accent
                    font.pixelSize: 10
                    font.bold: true
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                spacing: 8
                boundsBehavior: Flickable.StopAtBounds
                model: column.taskModel
                activeFocusOnTab: true
                Keys.onPressed: function(event) { column.handleKey(event) }

                Accessible.role: Accessible.List
                Accessible.name: column.title + "列，共 " + (column.taskModel ? column.taskModel.count : 0) + " 个任务"
                Accessible.description: "用上下方向键选择任务，回车编辑，空格完成，Delete 删除"

                delegate: TaskCard {
                    width: list.width
                    title: model.title
                    highlight: column.highlightId === model.id
                    selected: column.selectedId === model.id
                    tag: model.tag
                    priority: model.priority
                    due: model.due
                    taskId: model.id
                    taskStatus: model.status
                    showProjectBadge: column.showProjectBadge
                    dragSpace: column.dragSpace
                    onSelectionRequested: column.selectId(model.id)
                    onEditRequested: column.editRequested(model.id)
                    onStartRequested: column.startRequested(model.id)
                    onCompleteRequested: column.completeRequested(model.id)
                    onRestoreRequested: column.restoreRequested(model.id)
                    onDeleteRequested: column.deleteRequested(model.id)
                    onDragStarted: function(taskId, title, status, x, y) {
                        column.dragStarted(taskId, title, status, x, y)
                    }
                    onDragMoved: function(x, y) { column.dragMoved(x, y) }
                    onDragEnded: function(x, y) { column.dragEnded(x, y) }

                    Component.onCompleted: column.refreshDropLine()
                }

                onContentYChanged: column.refreshDropLine()
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: column.taskModel && column.taskModel.count === 0
                text: column.searchQuery.length > 0
                      ? "没有匹配的任务"
                      : (column.stageFilter.length > 0
                         ? ("该阶段暂无任务\n" + column.stageFilter + " · 切回「全部」查看其它阶段")
                         : (column.status === "done"
                            ? "完成的任务会出现在这里"
                            : "暂无任务\n在下方输入，回车添加"))
                color: Theme.muted
                opacity: 0.75
                font.pixelSize: 12
                lineHeight: 1.5
                horizontalAlignment: Text.AlignHCenter
            }

            // 插入指示线：拖拽时用霓虹横线标出即将落下的位置
            // 作为覆盖层放在列表区域之上，位置由 refreshDropLine() 按需计算
            Rectangle {
                id: dropLine
                z: 6
                visible: false
                x: list.x + 6
                width: Math.max(0, list.width - 12)
                height: column.dropSnapped ? 4 : 2
                radius: 2
                color: column.accent
                opacity: visible ? (column.dropSnapped ? 1 : 0.72) : 0

                Behavior on opacity {
                    NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
                }
                Behavior on height {
                    NumberAnimation { duration: Motion.instant; easing.type: Easing.OutCubic }
                }

                // 外扩辉光
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -4
                    radius: 7
                    color: Qt.rgba(column.accent.r, column.accent.g, column.accent.b, 0.28)
                }
            }

            // 落位涟漪
            Rectangle {
                id: rippleRing
                width: 24
                height: 24
                radius: 12
                x: (list.width - width) / 2
                // y 由 ripple(atY) 按落点设置，不再绑定列底
                y: list.height - height
                color: "transparent"
                border.width: 2
                border.color: column.accent
                opacity: 0
                visible: opacity > 0.01
                z: 5

                SequentialAnimation {
                    id: rippleAnimation
                    NumberAnimation { target: rippleRing; property: "width"; from: 24; to: 240; duration: Motion.slow; easing.type: Easing.OutCubic }
                    NumberAnimation { target: rippleRing; property: "height"; from: 24; to: 240; duration: Motion.slow; easing.type: Easing.OutCubic }
                    NumberAnimation { target: rippleRing; property: "radius"; from: 12; to: 120; duration: Motion.slow; easing.type: Easing.OutCubic }
                    NumberAnimation { target: rippleRing; property: "opacity"; from: 0.85; to: 0; duration: Motion.slow }
                }
            }
        }

        // 快速录入解析预览：输入时实时显示识别出的日期、优先级与标签
        RowLayout {
            id: parsePreview
            Layout.fillWidth: true
            Layout.preferredHeight: 18
            spacing: 6

            readonly property bool hasInfo: column.parseDue.length > 0
                                           || column.parsePriority >= 0
                                           || (column.parseTags && column.parseTags.length > 0)

            Text {
                visible: !parsePreview.hasInfo
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: column.parseHasText ? "支持 明天 15:00 #标签 !3" : " "
                color: Theme.muted
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            Rectangle {
                id: dueChip
                visible: column.parseDue.length > 0
                Layout.alignment: Qt.AlignVCenter
                height: 18
                width: dueLabel.implicitWidth + 14
                radius: 9
                color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.12)
                border.width: 1
                border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.35)

                Text {
                    id: dueLabel
                    anchors.centerIn: parent
                    text: "⏰ " + column.parseDue
                    color: Theme.cyan
                    font.pixelSize: 10
                }
            }

            Rectangle {
                id: priorityChip
                visible: column.parsePriority >= 0
                Layout.alignment: Qt.AlignVCenter
                height: 18
                width: priorityLabel.implicitWidth + 14
                radius: 9
                color: Qt.rgba(priorityChip.accent.r, priorityChip.accent.g, priorityChip.accent.b, 0.12)
                border.width: 1
                border.color: Qt.rgba(priorityChip.accent.r, priorityChip.accent.g, priorityChip.accent.b, 0.35)

                readonly property color accent: column.parsePriority >= 3 ? Theme.magenta
                                                  : column.parsePriority === 2 ? Theme.violet
                                                  : Theme.cyan

                Text {
                    id: priorityLabel
                    anchors.centerIn: parent
                    text: column.parsePriority >= 3 ? "高优先"
                        : column.parsePriority === 2 ? "中优先" : "低优先"
                    color: priorityChip.accent
                    font.pixelSize: 10
                }
            }

            Repeater {
                model: column.parseTags
                delegate: Rectangle {
                    required property var modelData
                    Layout.alignment: Qt.AlignVCenter
                    height: 18
                    width: tagLabel.implicitWidth + 12
                    radius: 9
                    color: Theme.ghost
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        id: tagLabel
                        anchors.centerIn: parent
                        text: "#" + modelData
                        color: Theme.muted
                        font.pixelSize: 10
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 34
            radius: 8
            color: Theme.input
            border.width: 1
            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.14)

            Behavior on border.color {
                ColorAnimation { duration: Motion.fast }
            }

            TextField {
                id: quickAdd
                anchors.fill: parent
                leftPadding: 10
                rightPadding: 10
                placeholderText: "＋ 添加任务，回车"
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: 12
                background: null
                onTextChanged: {
                    column.parseHasText = text.trim().length > 0;
                    if (!column.taskModel) {
                        column.parseDue = "";
                        column.parsePriority = -1;
                        column.parseTags = [];
                        return;
                    }
                    const info = column.taskModel.previewInput(text);
                    column.parseDue = info.dueText || "";
                    column.parsePriority = info.priority !== undefined ? info.priority : -1;
                    column.parseTags = info.tags || [];
                }
                onAccepted: {
                    if (column.taskModel && text.trim().length > 0) {
                        column.taskModel.addTask(text.trim(), column.status, 1, "", 0,
                                                 column.columnId)
                        text = ""
                    }
                }
            }
        }
    }
}
