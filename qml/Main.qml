import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 600
    visible: false
    title: "NeonBoard"
    color: Theme.bg
    flags: Qt.FramelessWindowHint | Qt.Window | Qt.NoDropShadowWindowHint

    onClosing: function(close) {
        if (!windowController.quitting) {
            close.accepted = false
            window.hide()
        }
    }

    function openTaskEditor(id) {
        taskEditorLoader.active = true
        if (taskEditorLoader.item)
            taskEditorLoader.item.openFor(id)
    }

    function focusNewTask() {
        show()
        raise()
        requestActivate()
        todoColumn.focusQuickAdd()
    }

    // 设置 / 插件面板均按需加载：首次打开时才构造，避免首帧为低频界面付出代价
    property bool settingsPending: false
    property int pendingPluginIndex: -1
    property bool libraryPending: false

    // 面板互斥：同一时刻只保留一个浮层，避免外观/插件/设置面板叠在一起
    function closeAllPanels() {
        themePicker.open = false
        if (pluginPanelLoader.item)
            pluginPanelLoader.item.close()
        if (settingsLoader.item)
            settingsLoader.item.close()
    }

    function openSettings() {
        show()
        raise()
        requestActivate()
        closeAllPanels()
        if (settingsLoader.item) {
            settingsLoader.item.open()
        } else {
            settingsPending = true
            settingsLoader.active = true
        }
    }

    function openPluginAt(index) {
        closeAllPanels()
        if (pluginPanelLoader.item) {
            pluginPanelLoader.item.openPlugin(index)
        } else {
            pendingPluginIndex = index
            pluginPanelLoader.active = true
        }
    }

    function openPlugins() {
        show()
        raise()
        requestActivate()
        openPluginAt(0)
    }

    // 按插件 id 打开插件面板（插件库用）
    function openPluginById(pluginId) {
        var list = pluginLoader.plugins
        for (var i = 0; i < list.length; ++i) {
            if (list[i].id === pluginId) {
                openPluginAt(i)
                return true
            }
        }
        return false
    }

    // 打开插件库
    function openPluginLibrary() {
        show()
        raise()
        requestActivate()
        if (pluginLibraryLoader.item) {
            pluginLibraryLoader.item.open()
        } else {
            libraryPending = true
            pluginLibraryLoader.active = true
        }
    }

    // 打开外观面板（供命令行 --appearance 诊断用）
    function openAppearance() {
        show()
        raise()
        requestActivate()
        closeAllPanels()
        themePicker.open = true
    }

    property bool dragActive: false
    property string dragTaskId: ""
    property string dragTaskTitle: ""
    property string dragTaskStatus: ""
    property real dragX: 0
    property real dragY: 0
    // 当前悬停的目标列状态（todo/doing/done），用于目标列呼吸高亮
    property string dragOverStatus: ""

    // 里程碑过滤：空串表示全部；阶段来自卡片首个标签（R0/R1/R2/R3）
    property string stageFilter: ""
    property var stageStats: ({})

    function setStageFilter(stage) {
        stageFilter = stage
        todoModel.stage = stage
        doingModel.stage = stage
        doneModel.stage = stage
    }

    function buildStageList() {
        var order = ["R0", "R1", "R2", "R3", "OTHER"]
        var labels = { "R0": "R0", "R1": "R1", "R2": "R2", "R3": "R3", "OTHER": "其他" }
        var list = []
        for (var i = 0; i < order.length; ++i) {
            var key = order[i]
            var stat = stageStats[key]
            if (!stat)
                continue
            if (key === "OTHER" && stat.total === 0)
                continue
            list.push({
                key: key,
                label: labels[key],
                total: stat.total || 0,
                done: stat.done || 0,
                doing: stat.doing || 0,
                todo: stat.todo || 0
            })
        }
        return list
    }

    // 路线图（仓库 progress.json）与看板的双向同步状态
    property string roadmapSyncText: roadmap.enabled
          ? (roadmap.conflicts > 0
             ? "路线图已连接 · " + roadmap.conflicts + " 条外部改动"
             : "路线图已连接 · 已对齐")
          : "路线图同步未启用"
    property bool roadmapSyncOk: roadmap.enabled && roadmap.conflicts === 0

    // 命中测试：把 root 坐标换算成所在列
    function columnAt(x, y) {
        var columns = [todoColumn, doingColumn, doneColumn]
        for (var i = 0; i < columns.length; ++i) {
            var column = columns[i]
            var origin = column.mapToItem(root, 0, 0)
            if (x >= origin.x && x <= origin.x + column.width
                    && y >= origin.y && y <= origin.y + column.height)
                return column
        }
        return null
    }

    function beginTaskDrag(id, title, status, x, y) {
        dragTaskId = id
        dragTaskTitle = title
        dragTaskStatus = status
        dragX = x
        dragY = y
        dragOverStatus = status
        dragActive = true
    }

    function clearDropMarks() {
        var columns = [todoColumn, doingColumn, doneColumn]
        for (var i = 0; i < columns.length; ++i) {
            columns[i].dropBeforeId = ""
            columns[i].dropAnchorId = ""
            columns[i].dropAnchorBelow = false
        }
    }

    function updateTaskDrag(x, y) {
        dragX = x
        dragY = y
        var column = columnAt(x, y)
        if (!column) {
            clearDropMarks()
            dragOverStatus = ""
            return
        }
        var origin = column.mapToItem(root, 0, 0)
        var drop = column.resolveDrop(y - origin.y)
        var columns = [todoColumn, doingColumn, doneColumn]
        for (var i = 0; i < columns.length; ++i) {
            columns[i].dropBeforeId = (columns[i] === column) ? drop.beforeId : ""
            columns[i].dropAnchorId = (columns[i] === column) ? drop.anchorId : ""
            columns[i].dropAnchorBelow = (columns[i] === column) ? drop.below : false
        }
        dragOverStatus = column.status
    }

    function endTaskDrag(x, y) {
        if (!dragActive)
            return
        dragX = x
        dragY = y
        var column = columnAt(x, y)
        if (column) {
            var origin = column.mapToItem(root, 0, 0)
            var drop = column.resolveDrop(y - origin.y)
            // 精确插位：插到落点卡片之前，beforeId 为空则落到列尾
            if (taskStore.moveTaskBefore(dragTaskId, column.status, drop.beforeId))
                column.ripple()
        }
        clearDropMarks()
        dragActive = false
        dragOverStatus = ""
        dragTaskId = ""
        dragTaskTitle = ""
        dragTaskStatus = ""
    }

    property var lastDeletedTask: null

    function deleteTaskWithUndo(id) {
        lastDeletedTask = taskStore.removeTask(id)
        if (lastDeletedTask && lastDeletedTask.id) {
            undoToast.visible = true
            undoToastTimer.restart()
        }
    }

    function restoreDeleted() {
        if (!lastDeletedTask)
            return
        taskStore.restoreTask(lastDeletedTask)
        lastDeletedTask = null
        undoToast.visible = false
        undoToastTimer.stop()
    }

    Shortcut {
        sequence: "Ctrl+Z"
        onActivated: window.restoreDeleted()
    }

    Shortcut {
        sequence: "Ctrl+N"
        onActivated: window.focusNewTask()
    }

    Shortcut {
        sequence: "Ctrl+,"
        onActivated: window.openSettings()
    }

    // 阶段统计：任务数据变化时重算里程碑完成度
    Connections {
        target: taskStore
        function onCountsChanged() {
            window.stageStats = taskStore.stageStats()
        }
    }

    Component.onCompleted: {
        stageStats = taskStore.stageStats()
        setStageFilter(initialStage)
        pluginLoader.scan()
    }


    Rectangle {
        id: root
        anchors.fill: parent
        radius: 16
        color: Theme.bg
        border.width: 1
        border.color: Theme.border
        clip: true

        Item {
            anchors.fill: parent

            NeonBackground {
                id: background
                anchors.fill: parent
                active: window.active
                visible: !noBackground
            }

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(Theme.bg.r, Theme.bg.g, Theme.bg.b, 0.28)
            }
        }

        // 外观面板展开时，点击面板之外区域即关闭（z 低于标题栏，面板本体不受影响）
        MouseArea {
            anchors.fill: parent
            z: 5
            visible: themePicker.open
            onClicked: themePicker.close()
        }

        Rectangle {
            id: titleBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 40
            color: "transparent"
            z: 10

MouseArea {
            anchors.fill: parent
            onPressed: {
                if (mouse.button === Qt.LeftButton)
                    window.startSystemMove()
            }
            onDoubleClicked: windowController.toggleMaximize()
        }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 9

                Image {
                    width: 20
                    height: 20
                    source: "qrc:/icons/neonboard.svg"
                    sourceSize: Qt.size(40, 40)
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "NEONBOARD"
                    color: Theme.text
                    font.pixelSize: 12
                    font.bold: true
                    font.letterSpacing: 2.2
                }
            }

            Row {
                id: windowButtons
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                WinButton {
                    glyph: "—"
                    onClicked: windowController.minimize()
                }

                WinButton {
                    glyph: windowController.maximized ? "❐" : "□"
                    onClicked: windowController.toggleMaximize()
                }

                WinButton {
                    glyph: "✕"
                    danger: true
                    onClicked: window.hide()
                }
            }

            ThemePicker {
                id: themePicker
                anchors.right: windowButtons.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        RowLayout {
            anchors.top: titleBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 0

            Rectangle {
                // 窄屏时侧栏按比例收缩，保证三列看板不被挤出可视区
                Layout.preferredWidth: Math.min(220, Math.max(170, window.width * 0.24))
                Layout.fillHeight: true
                color: Theme.panel
                border.width: 0

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8

                    Text {
                        text: "工作台"
                        color: Theme.text
                        font.pixelSize: 20
                        font.bold: true
                    }

                    Text {
                        text: "PERSONAL WORKBENCH"
                        color: Theme.muted
                        font.pixelSize: 9
                        font.letterSpacing: 1.4
                    }

                    Item { Layout.preferredHeight: 8 }

                    Repeater {
                        model: ["看板", "插件", "设置"]

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: 34
                            radius: 8
                            color: index === 0
                                   ? Theme.accentSoft
                                   : (navMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.04)
                                                             : "transparent")
                            border.width: index === 0 ? 1 : 0
                            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.22)

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData
                                color: index === 0 ? Theme.cyan : Theme.muted
                                font.pixelSize: 13
                            }

                            MouseArea {
                                id: navMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (index === 1) {
                                        window.openPluginLibrary()
                                    } else if (index === 2) {
                                        window.openSettings()
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 210
                        radius: 12
                        color: Theme.panel
                        border.width: 1
                        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.12)

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: "插件库"
                                    color: Theme.text
                                    font.pixelSize: 11
                                    font.bold: true
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: pluginLibrary.entries.length
                                    color: Theme.muted
                                    font.pixelSize: 10
                                }
                            }

                            ListView {
                                id: libraryList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 2
                                model: pluginLibrary.entries

                                delegate: Rectangle {
                                    width: libraryList.width
                                    height: 24
                                    radius: 6
                                    color: libraryRowMouse.containsMouse
                                           ? Qt.rgba(1, 1, 1, 0.05)
                                           : "transparent"

                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 4
                                        anchors.right: libraryTag.left
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.name
                                        color: modelData.installed ? Theme.text : Theme.accent2
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        id: libraryTag
                                        anchors.right: parent.right
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.installed
                                              ? (modelData.enabled ? "已装" : "禁用")
                                              : "可装"
                                        color: Theme.muted
                                        font.pixelSize: 9
                                    }

                                    MouseArea {
                                        id: libraryRowMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            if (modelData.installed)
                                                window.openPluginById(modelData.id)
                                            else
                                                pluginLibrary.install(modelData.id)
                                        }
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.preferredHeight: visible ? implicitHeight : 0
                                visible: pluginLibrary.entries.length === 0
                                text: "插件库为空"
                                color: Theme.muted
                                font.pixelSize: 10
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Text {
                                    text: "刷新市场"
                                    color: marketBtnMouse.containsMouse ? Theme.cyan : Theme.muted
                                    font.pixelSize: 9
                                    MouseArea {
                                        id: marketBtnMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: pluginMarket.refresh()
                                    }
                                }

                                Text {
                                    text: "重新扫描"
                                    color: rescanBtnMouse.containsMouse ? Theme.cyan : Theme.muted
                                    font.pixelSize: 9
                                    MouseArea {
                                        id: rescanBtnMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            pluginRuntime.reload()
                                            pluginLibrary.refresh()
                                        }
                                    }
                                }

                                Text {
                                    text: "打开目录"
                                    color: dirBtnMouse.containsMouse ? Theme.cyan : Theme.muted
                                    font.pixelSize: 9
                                    MouseArea {
                                        id: dirBtnMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: pluginLibrary.openPluginDir()
                                    }
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: "详情"
                                    color: detailBtnMouse.containsMouse ? Theme.cyan : Theme.text
                                    font.pixelSize: 9
                                    MouseArea {
                                        id: detailBtnMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: window.openPluginLibrary()
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        text: "SQLite · " + database.taskCount + " 条任务"
                        color: Theme.muted
                        font.pixelSize: 9
                    }

                    Text {
                        text: hotkey.registered
                              ? "HOTKEY  Ctrl+Shift+Space"
                              : "HOTKEY  OFF"
                        color: hotkey.registered ? Theme.cyan : Theme.magenta
                        font.pixelSize: 9
                        font.letterSpacing: 0.6
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 18
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20

                    ColumnLayout {
                        spacing: 2

                        Text {
                            text: "工作台"
                            color: Theme.text
                            font.pixelSize: 24
                            font.bold: true
                        }

                        Text {
                            text: "专注当下，逐项推进"
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StatChip {
                        label: "待办"
                        value: todoModel.count
                        accent: Theme.cyan
                    }

                    StatChip {
                        label: "进行中"
                        value: doingModel.count
                        accent: Theme.violet
                    }

                    StatChip {
                        label: "今日完成"
                        value: taskStore.doneTodayCount
                        accent: Theme.magenta
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    spacing: 12

                    StageBar {
                        id: stageBar
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        stages: window.buildStageList()
                        activeStage: window.stageFilter
                        syncText: window.roadmapSyncText
                        syncOk: window.roadmapSyncOk
                        onStageSelected: function(stage) { window.setStageFilter(stage) }
                    }

                    Rectangle {
                        Layout.preferredWidth: 340
                        height: 34
                        radius: 8
                        color: Theme.input
                        border.width: 1
                        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.18)

                        TextField {
                            id: searchField
                            anchors.fill: parent
                            leftPadding: 10
                            rightPadding: 10
                            placeholderText: "搜索任务"
                            placeholderTextColor: Theme.muted
                            color: Theme.text
                            font.pixelSize: 12
                            background: null
                            onTextChanged: {
                                todoModel.search = text
                                doingModel.search = text
                                doneModel.search = text
                            }
                            Keys.onEscapePressed: text = ""
                        }
                    }
                }

                RowLayout {
                    id: boardRow
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.bottomMargin: 20
                    spacing: 12

                    BoardColumn {
                        id: todoColumn
                        title: "待办"
                        accent: Theme.cyan
                        status: "todo"
                        taskModel: todoModel
                        searchQuery: searchField.text
                        stageFilter: window.stageFilter
                        dragSpace: root
                        dropTarget: window.dragOverStatus === status
                        onEditRequested: function(taskId) { window.openTaskEditor(taskId) }
                        onStartRequested: function(taskId) { taskStore.moveTask(taskId, "doing", -1) }
                        onCompleteRequested: function(taskId) { taskStore.moveTask(taskId, "done", -1) }
                        onRestoreRequested: function(taskId) { taskStore.moveTask(taskId, "todo", -1) }
                        onDeleteRequested: function(taskId) { window.deleteTaskWithUndo(taskId) }
                        onDragStarted: function(taskId, title, status, x, y) { window.beginTaskDrag(taskId, title, status, x, y) }
                        onDragMoved: function(x, y) { window.updateTaskDrag(x, y) }
                        onDragEnded: function(x, y) { window.endTaskDrag(x, y) }
                    }

                    BoardColumn {
                        title: "进行中"
                        id: doingColumn
                        accent: Theme.violet
                        status: "doing"
                        taskModel: doingModel
                        searchQuery: searchField.text
                        stageFilter: window.stageFilter
                        dragSpace: root
                        dropTarget: window.dragOverStatus === status
                        onEditRequested: function(taskId) { window.openTaskEditor(taskId) }
                        onStartRequested: function(taskId) { taskStore.moveTask(taskId, "doing", -1) }
                        onCompleteRequested: function(taskId) { taskStore.moveTask(taskId, "done", -1) }
                        onRestoreRequested: function(taskId) { taskStore.moveTask(taskId, "todo", -1) }
                        onDeleteRequested: function(taskId) { window.deleteTaskWithUndo(taskId) }
                        onDragStarted: function(taskId, title, status, x, y) { window.beginTaskDrag(taskId, title, status, x, y) }
                        onDragMoved: function(x, y) { window.updateTaskDrag(x, y) }
                        onDragEnded: function(x, y) { window.endTaskDrag(x, y) }
                    }

                    BoardColumn {
                        title: "已办结"
                        id: doneColumn
                        accent: Theme.magenta
                        status: "done"
                        taskModel: doneModel
                        searchQuery: searchField.text
                        stageFilter: window.stageFilter
                        dragSpace: root
                        dropTarget: window.dragOverStatus === status
                        onEditRequested: function(taskId) { window.openTaskEditor(taskId) }
                        onStartRequested: function(taskId) { taskStore.moveTask(taskId, "doing", -1) }
                        onCompleteRequested: function(taskId) { taskStore.moveTask(taskId, "done", -1) }
                        onRestoreRequested: function(taskId) { taskStore.moveTask(taskId, "todo", -1) }
                        onDeleteRequested: function(taskId) { window.deleteTaskWithUndo(taskId) }
                        onDragStarted: function(taskId, title, status, x, y) { window.beginTaskDrag(taskId, title, status, x, y) }
                        onDragMoved: function(x, y) { window.updateTaskDrag(x, y) }
                        onDragEnded: function(x, y) { window.endTaskDrag(x, y) }
                    }
                }
            }
        }

        // 插件面板：按需加载。首次点开插件时才构造面板与插件内容
        // 尺寸与定位由 Loader 负责（QQuickLoader 会把 Loader 的尺寸强加给被加载项）
        Loader {
            id: pluginPanelLoader
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 24
            anchors.bottomMargin: 24
            width: Math.min(380, root.width - 48)
            height: Math.min(420, root.height - 140)
            active: false
            source: "PluginPanel.qml"
            z: 100

            onLoaded: {
                if (window.pendingPluginIndex >= 0) {
                    var index = window.pendingPluginIndex
                    window.pendingPluginIndex = -1
                    item.openPlugin(index)
                }
            }
        }

        // 插件库面板：按需加载
        Loader {
            id: pluginLibraryLoader
            anchors.fill: parent
            active: false
            source: "PluginLibraryPanel.qml"
            z: 140

            onLoaded: {
                if (window.libraryPending) {
                    window.libraryPending = false
                    item.open()
                }
            }
        }

        // 插件库请求打开某个插件 → 转到插件面板
        Connections {
            target: pluginLibrary
            function onOpenRequested(pluginId) {
                window.openPluginById(pluginId)
            }
        }

        // 设置面板：按需加载
        Loader {
            id: settingsLoader
            anchors.fill: parent
            active: false
            source: "SettingsPanel.qml"
            z: 150

            onLoaded: {
                if (window.settingsPending) {
                    window.settingsPending = false
                    item.open()
                }
                item.openLibraryRequested.connect(function() {
                    window.openPluginLibrary()
                })
            }
        }

        Item {
            anchors.fill: parent
            z: 200

            MouseArea {
                width: 6
                height: parent.height
                anchors.left: parent.left
                cursorShape: Qt.SizeHorCursor
                onPressed: window.startSystemResize(Qt.LeftEdge)
            }

            MouseArea {
                width: 6
                height: parent.height
                anchors.right: parent.right
                cursorShape: Qt.SizeHorCursor
                onPressed: window.startSystemResize(Qt.RightEdge)
            }

            MouseArea {
                width: parent.width
                height: 6
                anchors.top: parent.top
                cursorShape: Qt.SizeVerCursor
                onPressed: window.startSystemResize(Qt.TopEdge)
            }

            MouseArea {
                width: parent.width
                height: 6
                anchors.bottom: parent.bottom
                cursorShape: Qt.SizeVerCursor
                onPressed: window.startSystemResize(Qt.BottomEdge)
            }

            MouseArea {
                width: 10
                height: 10
                anchors.left: parent.left
                anchors.top: parent.top
                cursorShape: Qt.SizeFDiagCursor
                onPressed: window.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
            }

            MouseArea {
                width: 10
                height: 10
                anchors.right: parent.right
                anchors.top: parent.top
                cursorShape: Qt.SizeBDiagCursor
                onPressed: window.startSystemResize(Qt.RightEdge | Qt.TopEdge)
            }

            MouseArea {
                width: 10
                height: 10
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                cursorShape: Qt.SizeBDiagCursor
                onPressed: window.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
            }

            MouseArea {
                width: 10
                height: 10
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                cursorShape: Qt.SizeFDiagCursor
                onPressed: window.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
            }
        }

        Rectangle {
            id: undoToast
            visible: false
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 22
            width: undoLabel.implicitWidth + 96
            height: 38
            radius: 10
            color: Theme.elevated
            border.width: 1
            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.30)
            z: 300

            Text {
                id: undoLabel
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: "任务已删除"
                color: Theme.text
                font.pixelSize: 11
            }

            Rectangle {
                width: 48
                height: 24
                radius: 6
                anchors.right: parent.right
                anchors.rightMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                color: undoMouse.containsMouse ? Theme.accentSoft
                                               : Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.25)

                Text {
                    anchors.centerIn: parent
                    text: "撤销"
                    color: Theme.cyan
                    font.pixelSize: 11
                    font.bold: true
                }

                MouseArea {
                    id: undoMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: window.restoreDeleted()
                }
            }
        }

        Timer {
            id: undoToastTimer
            interval: 5000
            onTriggered: undoToast.visible = false
        }

    }

    component TaskEditor: Dialog {
        id: editor
        parent: window.contentItem
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 20
        width: Math.min(520, window.width - 80)
        closePolicy: Popup.CloseOnEscape
        property string taskId: ""
        property int priorityValue: 1

        function openFor(id) {
            const task = taskStore.taskById(id)
            if (!task || !task.id)
                return
            taskId = id
            titleField.text = task.title
            noteField.text = task.note
            priorityValue = Math.max(1, Math.min(3, Number(task.priority) || 1))
            dueField.text = task.due || ""
            open()
            titleField.forceActiveFocus()
        }

        background: Rectangle {
            color: Theme.panelSolid
            radius: 16
            border.width: 1
            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                text: "编辑任务"
                color: Theme.text
                font.pixelSize: 18
                font.bold: true
            }

            TextField {
                id: titleField
                Layout.fillWidth: true
                placeholderText: "任务标题"
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: 14
                background: Rectangle {
                    color: Theme.elevated
                    radius: 8
                    border.width: 1
                    border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
                }
            }

            TextArea {
                id: noteField
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                placeholderText: "备注"
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: 12
                wrapMode: TextArea.Wrap
                background: Rectangle {
                    color: Theme.elevated
                    radius: 8
                    border.width: 1
                    border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Row {
                    spacing: 6
                    Repeater {
                        model: ["低", "中", "高"]
                        delegate: Rectangle {
                            width: 58
                            height: 32
                            radius: 8
                            color: editor.priorityValue === index + 1
                                   ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.48)
                                   : Theme.input
                            border.width: 1
                            border.color: editor.priorityValue === index + 1
                                          ? Theme.cyan
                                          : Theme.border

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: editor.priorityValue === index + 1 ? Theme.text : Theme.muted
                                font.pixelSize: 11
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: editor.priorityValue = index + 1
                            }
                        }
                    }
                }

                TextField {
                    id: dueField
                    Layout.fillWidth: true
                    placeholderText: "截止日期 YYYY-MM-DD"
                    placeholderTextColor: Theme.muted
                    color: Theme.text
                    font.pixelSize: 12
                    background: Rectangle {
                        color: Theme.elevated
                        radius: 8
                        border.width: 1
                        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: "删除"
                    onClicked: {
                        window.deleteTaskWithUndo(editor.taskId)
                        editor.close()
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "取消"
                    onClicked: editor.close()
                }

                Button {
                    text: "保存"
                    onClicked: {
                        const title = titleField.text.trim()
                        if (title.length === 0)
                            return
                        let dueAt = 0
                        const raw = dueField.text.trim()
                        if (raw.length > 0) {
                            const date = new Date(raw + "T23:59:59")
                            if (!isNaN(date.getTime()))
                                dueAt = Math.floor(date.getTime() / 1000)
                        }
                        taskStore.updateTask(editor.taskId, {
                            title: title,
                            note: noteField.text,
                            priority: editor.priorityValue,
                            dueAt: dueAt
                        })
                        editor.close()
                    }
                }
            }
        }
    }

    Component {
        id: taskEditorComponent
        TaskEditor {}
    }

    Loader {
        id: taskEditorLoader
        active: false
        sourceComponent: taskEditorComponent
    }

    Rectangle {
        id: dragGhost
        visible: window.dragActive
        width: Math.min(320, dragGhostText.implicitWidth + 40)
        height: 54
        radius: 12
        color: Theme.ghost
        border.width: 1
        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.75)
        z: 300
        x: window.dragX - width / 2
        y: window.dragY - height / 2
        // 拖起放大：从列表里被拎起来的一瞬间
        scale: 1.04
        transformOrigin: Item.Center

        Behavior on scale {
            NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: 14
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.28)
        }

        Text {
            id: dragGhostText
            width: parent.width - 24
            anchors.centerIn: parent
            text: window.dragTaskTitle
            color: Theme.text
            font.pixelSize: 12
            font.bold: true
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
