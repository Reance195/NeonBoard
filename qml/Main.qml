import QtQuick
import QtQml
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
    // 窗口标题带 agent 标签：多个预览实例并行时能一眼分清谁是谁
    title: previewHub.instanceLabel
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
    // 预览中心：仅主线实例有入口，面板同样按需加载
    property bool previewPending: false
    property bool previewOpen: false

    // 专注模式：倒计时环 + 背景压暗 + 托盘剩余时间（R2-01）
    property bool focusMode: false
    property int focusTotal: 0
    property int focusRemaining: 0
    property bool focusPaused: false

    // 首启动引导：仅在「从未完成引导」且看板为空时自动出现
    property bool onboardVisible: false

    function finishOnboarding() {
        window.onboardVisible = false
        uiSettings.onboardingDone = true
    }

    // 示例卡：让新用户一上手就能试拖拽，随时可删
    function addSampleTasks() {
        taskStore.addTask("试用：按 Ctrl+N 快速记一条任务", "todo", 1, "引导", 0)
        taskStore.addTask("把这张卡拖到「进行中」", "todo", 2, "引导", 0)
        taskStore.addTask("完成后拖到「已办结」", "todo", 1, "引导", 0)
    }

    function startFocus(minutes) {
        window.focusTotal = Math.max(1, minutes) * 60
        window.focusRemaining = window.focusTotal
        window.focusPaused = false
        window.focusMode = true
        tray.setFocusRemaining(window.focusRemaining)
    }

    function stopFocus(completed) {
        window.focusMode = false
        window.focusPaused = false
        window.focusRemaining = 0
        window.focusTotal = 0
        tray.setFocusRemaining(0)
        if (completed) {
            background.burst()
            tray.showMessage("专注结束", "这一轮已完成，起身活动一下")
        }
    }

    function toggleFocusPause() {
        window.focusPaused = !window.focusPaused
    }

    // 快捷键与命令面板：运行中则退出，未运行直接开 25 分钟
    function toggleFocusMode() {
        if (window.focusMode)
            window.stopFocus(false)
        else
            window.startFocus(25)
    }

    Timer {
        id: focusTimer
        interval: 1000
        repeat: true
        running: window.focusMode && !window.focusPaused
        onTriggered: {
            if (window.focusRemaining > 0) {
                window.focusRemaining -= 1
                tray.setFocusRemaining(window.focusRemaining)
            }
            if (window.focusRemaining <= 0)
                window.stopFocus(true)
        }
    }

    // 键盘导航：把焦点交给指定列并选中首张卡（左右方向键跨列）
    function focusColumn(targetIndex) {
        var cols = [todoColumn, doingColumn, doneColumn]
        var i = Math.max(0, Math.min(cols.length - 1, targetIndex))
        var target = cols[i]
        if (!target)
            return
        target.focusList()
        target.selectFirst()
    }

    // 面板互斥：同一时刻只保留一个浮层，避免外观/插件/设置面板叠在一起
    function closeAllPanels() {
        themePicker.open = false
        commandPalette.close()
        if (pluginPanelLoader.item)
            pluginPanelLoader.item.close()
        if (settingsLoader.item)
            settingsLoader.item.close()
        if (previewLoader.item)
            previewLoader.item.close()
        window.previewOpen = false
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

    // 预览中心：列出各 agent 工作区，可一键构建并启动隔离的预览实例
    function openPreview() {
        show()
        raise()
        requestActivate()
        closeAllPanels()
        previewOpen = true
        previewHub.refresh()
        if (previewLoader.item) {
            previewLoader.item.open()
        } else {
            previewPending = true
            previewLoader.active = true
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

    // 运行插件命令：插件在跑就直接投递；不在跑就打开插件面板，面板加载后补投递
    function runPluginCommand(pluginId, commandId) {
        if (!pluginRuntime.invokeCommand(pluginId, commandId)) {
            window.closeAllPanels()
            window.openPluginById(pluginId)
        }
    }

    // 权限授权弹窗：按需加载，首次弹出时才构造
    function presentPermissionPrompt(pluginId) {
        if (permissionDialogLoader.item) {
            permissionDialogLoader.item.present(pluginId)
            return
        }
        permissionDialogLoader.pendingPluginId = pluginId
        permissionDialogLoader.active = true
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

    // 打开命令面板（供命令行 --palette 诊断用）
    function openPalette(query) {
        show()
        raise()
        requestActivate()
        closeAllPanels()
        commandPalette.open()
        if (query)
            commandPalette.setQuery(query)
    }

    function statusLabel(status) {
        if (status === "doing")
            return "进行中"
        if (status === "done")
            return "已办结"
        return "待办"
    }

    // 命令面板数据源：固定命令 + 任务跳转
    function buildCommands(query) {
        var q = (query || "").trim().toLowerCase()
        var cmds = []

        function add(title, category, subtitle, fn) {
            if (q.length > 0 && title.toLowerCase().indexOf(q) < 0 && subtitle.toLowerCase().indexOf(q) < 0)
                return
            cmds.push({ title: title, category: category, subtitle: subtitle, run: fn })
        }

        add("新建任务", "操作", "聚焦快速添加输入框", function() { window.focusNewTask() })
        add("打开插件面板", "面板", "当前插件内容", function() { window.openPlugins() })
        add("打开插件库", "面板", "已装 / 可装插件", function() { window.openPluginLibrary() })
        add("打开设置面板", "面板", "热键 / 数据路径 / 开机自启", function() { window.openSettings() })
        add("打开外观面板", "面板", "主题预设 / 画质 / 减弱动效", function() { window.openAppearance() })
        add("切换专注模式", "视图", window.focusMode ? "结束本轮专注" : "开始 25 分钟专注", function() { window.toggleFocusMode() })
        add("专注 45 分钟", "视图", "压暗背景 + 倒计时环 + 托盘剩余时间", function() { window.startFocus(45) })
        add("专注 60 分钟", "视图", "深度工作一轮", function() { window.startFocus(60) })
        add("查看全部阶段", "视图", "清除阶段过滤", function() { window.setStageFilter("") })
        add("只看 R0 阶段", "视图", "炫酷地基", function() { window.setStageFilter("R0") })
        add("只看 R1 阶段", "视图", "日常底座", function() { window.setStageFilter("R1") })
        add("只看 R2 阶段", "视图", "记忆点", function() { window.setStageFilter("R2") })
        add("只看 R3 阶段", "视图", "生态", function() { window.setStageFilter("R3") })

        for (var i = 0; i < Theme.presets.length; ++i) {
            (function(p) {
                add("切换主题：" + p.name, "主题", p.desc, function() { Theme.applyPresetByKey(p.key, true) })
            })(Theme.presets[i])
        }

        if (q.length > 0) {
            var tasks = taskStore.allTasks()
            for (var j = 0; j < tasks.length; ++j) {
                (function(t) {
                    var title = t.title ? t.title : ""
                    if (title.toLowerCase().indexOf(q) < 0)
                        return
                    cmds.push({
                        title: "跳转到：" + title,
                        category: "任务",
                        subtitle: window.statusLabel(t.status),
                        run: function() { window.jumpToTask(t.id) }
                    })
                })(tasks[j])
            }
        }

        // 插件命令：manifest 的 contributes.commands，由宿主统一聚合
        var pluginCommands = pluginRuntime.pluginCommands
        for (var p = 0; p < pluginCommands.length; ++p) {
            (function(cmd) {
                var label = cmd.title && cmd.title.length > 0 ? cmd.title : cmd.id
                var subtitle = cmd.description && cmd.description.length > 0
                        ? cmd.description : cmd.pluginName
                if (cmd.shortcut && cmd.shortcut.length > 0)
                    subtitle += " · " + cmd.shortcut
                if (cmd.conflict)
                    subtitle += " · 快捷键冲突"
                add(label, cmd.category && cmd.category.length > 0 ? cmd.category : "插件",
                    subtitle, function() { window.runPluginCommand(cmd.pluginId, cmd.id) })
            })(pluginCommands[p])
        }

        return cmds
    }

    // 跳转到指定任务：清除过滤 → 定位到所在列 → 高亮闪烁
    function jumpToTask(taskId) {
        searchField.text = ""
        window.setStageFilter("")
        var tasks = taskStore.allTasks()
        var status = "todo"
        for (var i = 0; i < tasks.length; ++i) {
            if (tasks[i].id === taskId) {
                status = tasks[i].status
                break
            }
        }
        var column = todoColumn
        if (status === "doing")
            column = doingColumn
        else if (status === "done")
            column = doneColumn
        column.flashTask(taskId)
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
            if (taskUndo.moveTaskBefore(dragTaskId, column.status, drop.beforeId))
                column.ripple()
        }
        clearDropMarks()
        dragActive = false
        dragOverStatus = ""
        dragTaskId = ""
        dragTaskTitle = ""
        dragTaskStatus = ""
    }

    // 所有可撤销操作统一走 taskUndo（删除 / 移动 / 完成 / 编辑）
    function deleteTaskWithUndo(id) {
        taskUndo.deleteTask(id)
    }

    function undoLastAction() {
        if (taskUndo.undo()) {
            undoToast.visible = false
            undoToastTimer.stop()
        }
    }

    Shortcut {
        sequence: "Ctrl+Z"
        onActivated: window.undoLastAction()
    }

    property string undoToastText: "任务已删除"

    // 撤销栈事件驱动提示条：文案随操作类型变化
    Connections {
        target: taskUndo
        function onActionPerformed(description) {
            window.undoToastText = description
            undoToast.visible = true
            undoToastTimer.restart()
        }
        function onUndone(description) {
            window.undoToastText = "已撤销：" + description
            undoToast.visible = true
            undoToastTimer.restart()
        }
    }

    Shortcut {
        sequence: "Ctrl+N"
        onActivated: window.focusNewTask()
    }

    Shortcut {
        sequence: "Ctrl+,"
        onActivated: window.openSettings()
    }

    Shortcut {
        sequence: "Ctrl+K"
        onActivated: {
            window.closeAllPanels()
            commandPalette.open()
        }
    }

    Shortcut {
        sequence: "Ctrl+Shift+F"
        onActivated: window.toggleFocusMode()
    }

    // 键盘导航入口：Alt+1/2/3 聚焦对应列，随后上下选卡、左右跨列
    Shortcut {
        sequence: "Alt+1"
        onActivated: window.focusColumn(0)
    }

    Shortcut {
        sequence: "Alt+2"
        onActivated: window.focusColumn(1)
    }

    Shortcut {
        sequence: "Alt+3"
        onActivated: window.focusColumn(2)
    }

    // 插件快捷键：manifest 声明，宿主统一仲裁；冲突的已被插件运行时置空
    Instantiator {
        model: pluginRuntime.pluginCommands

        delegate: Shortcut {
            required property var modelData

            sequence: modelData.shortcut ? modelData.shortcut : ""
            enabled: sequence.length > 0
            onActivated: window.runPluginCommand(modelData.pluginId, modelData.id)
        }
    }

    // 阶段统计：任务数据变化时重算里程碑完成度
    Connections {
        target: taskStore
        function onCountsChanged() {
            window.stageStats = taskStore.stageStats()
        }
        // 完成任务时背景爆发一次能量脉冲
        function onTaskCompleted(id) {
            background.burst()
        }
    }

    Component.onCompleted: {
        stageStats = taskStore.stageStats()
        setStageFilter(initialStage)
        pluginLoader.scan()
        // 老用户（已有卡片）不弹引导；空板 + 未完成过引导才弹
        window.onboardVisible = !uiSettings.onboardingDone && taskStore.count === 0
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
                iState: Math.min(1, taskStore.doingCount / 6)
                iFocus: window.focusMode ? 1 : 0
            }

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(Theme.bg.r, Theme.bg.g, Theme.bg.b, 0.28)
            }
        }

        // 专注模式提示：背景已压暗成追光，告知退出方式
        Rectangle {
            id: focusBadge

            anchors.top: parent.top
            anchors.topMargin: 48
            anchors.horizontalCenter: parent.horizontalCenter
            z: 30
            visible: window.focusMode
            width: focusBadgeText.implicitWidth + 30
            height: 28
            radius: 14
            color: Qt.rgba(Theme.bg.r, Theme.bg.g, Theme.bg.b, 0.72)
            border.width: 1
            border.color: Theme.violet

            Text {
                id: focusBadgeText

                anchors.centerIn: parent
                text: "专注模式 · Ctrl+Shift+F 退出"
                color: Theme.violet
                font.pixelSize: 11
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
            onPressed: function(mouse) {
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
                        // 预览中心只在主线实例出现：agent 沙箱实例里再嵌套预览入口只会混淆
                        model: previewHub.hostInstance
                               ? ["看板", "插件", "设置", "预览"]
                               : ["看板", "插件", "设置"]

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: 34
                            radius: 8
                            color: (index === 0 || (index === 3 && window.previewOpen))
                                   ? Theme.accentSoft
                                   : (navMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.04)
                                                             : "transparent")
                            border.width: (index === 0 || (index === 3 && window.previewOpen)) ? 1 : 0
                            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.22)

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData
                                color: (index === 0 || (index === 3 && window.previewOpen))
                                       ? Theme.cyan : Theme.muted
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
                                    } else if (index === 3) {
                                        window.openPreview()
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    PluginSlot {
                        slotName: "sidebar"
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                    }

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

                    Rectangle {
                        Layout.preferredWidth: 78
                        Layout.preferredHeight: 36
                        radius: 8
                        color: focusBtnMouse.containsMouse || window.focusMode
                               ? Theme.accentSoft
                               : Theme.input
                        border.width: 1
                        border.color: window.focusMode
                                      ? Qt.rgba(Theme.magenta.r, Theme.magenta.g, Theme.magenta.b, 0.45)
                                      : Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.22)

                        Text {
                            anchors.centerIn: parent
                            text: window.focusMode ? "专注中" : "专注"
                            color: window.focusMode ? Theme.magenta : Theme.cyan
                            font.pixelSize: 11
                        }

                        MouseArea {
                            id: focusBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (window.focusMode)
                                    window.toggleFocusPause()
                                else
                                    focusMenu.open()
                            }
                        }
                    }
                }

                Popup {
                    id: focusMenu
                    parent: window.contentItem
                    x: Math.max(20, window.width - width - 40)
                    y: 96
                    width: 160
                    padding: 8
                    modal: false
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                    background: Rectangle {
                        radius: 10
                        color: Theme.panel
                        border.width: 1
                        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.25)
                    }

                    Column {
                        spacing: 4
                        width: parent.availableWidth

                        Text {
                            text: "选择专注时长"
                            color: Theme.muted
                            font.pixelSize: 9
                            leftPadding: 4
                            bottomPadding: 2
                        }

                        Repeater {
                            model: [25, 45, 60]

                            delegate: Rectangle {
                                required property int modelData
                                width: parent.width
                                height: 28
                                radius: 6
                                color: pickMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : "transparent"

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData + " 分钟"
                                    color: Theme.text
                                    font.pixelSize: 11
                                }

                                MouseArea {
                                    id: pickMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        focusMenu.close()
                                        window.startFocus(modelData)
                                    }
                                }
                            }
                        }
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

                // 统计横幅：热力图 + 连续天数 + 周吞吐（数据来自 taskStats，只读任务表）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.preferredHeight: 84
                    radius: 12
                    color: Theme.panel
                    border.width: 1
                    border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.12)

                    ActivityHeatmap {
                        anchors.fill: parent
                        anchors.margins: 9
                        cells: taskStats.heatmap
                        weekly: taskStats.weekly
                        streak: taskStats.streakDays
                        totalDone: taskStats.totalDone
                        bestDay: taskStats.bestDay
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
                        columnIndex: 0
                        onColumnMoveRequested: function(targetIndex) { window.focusColumn(targetIndex) }
                        title: "待办"
                        accent: Theme.cyan
                        status: "todo"
                        taskModel: todoModel
                        searchQuery: searchField.text
                        stageFilter: window.stageFilter
                        dragSpace: root
                        dropTarget: window.dragOverStatus === status
                        onEditRequested: function(taskId) { window.openTaskEditor(taskId) }
                        onStartRequested: function(taskId) { taskUndo.moveTask(taskId, "doing", -1) }
                        onCompleteRequested: function(taskId) { taskUndo.moveTask(taskId, "done", -1) }
                        onRestoreRequested: function(taskId) { taskUndo.moveTask(taskId, "todo", -1) }
                        onDeleteRequested: function(taskId) { window.deleteTaskWithUndo(taskId) }
                        onDragStarted: function(taskId, title, status, x, y) { window.beginTaskDrag(taskId, title, status, x, y) }
                        onDragMoved: function(x, y) { window.updateTaskDrag(x, y) }
                        onDragEnded: function(x, y) { window.endTaskDrag(x, y) }
                    }

                    BoardColumn {
                        title: "进行中"
                        id: doingColumn
                        columnIndex: 1
                        onColumnMoveRequested: function(targetIndex) { window.focusColumn(targetIndex) }
                        accent: Theme.violet
                        status: "doing"
                        taskModel: doingModel
                        searchQuery: searchField.text
                        stageFilter: window.stageFilter
                        dragSpace: root
                        dropTarget: window.dragOverStatus === status
                        onEditRequested: function(taskId) { window.openTaskEditor(taskId) }
                        onStartRequested: function(taskId) { taskUndo.moveTask(taskId, "doing", -1) }
                        onCompleteRequested: function(taskId) { taskUndo.moveTask(taskId, "done", -1) }
                        onRestoreRequested: function(taskId) { taskUndo.moveTask(taskId, "todo", -1) }
                        onDeleteRequested: function(taskId) { window.deleteTaskWithUndo(taskId) }
                        onDragStarted: function(taskId, title, status, x, y) { window.beginTaskDrag(taskId, title, status, x, y) }
                        onDragMoved: function(x, y) { window.updateTaskDrag(x, y) }
                        onDragEnded: function(x, y) { window.endTaskDrag(x, y) }
                    }

                    BoardColumn {
                        title: "已办结"
                        id: doneColumn
                        columnIndex: 2
                        onColumnMoveRequested: function(targetIndex) { window.focusColumn(targetIndex) }
                        accent: Theme.magenta
                        status: "done"
                        taskModel: doneModel
                        searchQuery: searchField.text
                        stageFilter: window.stageFilter
                        dragSpace: root
                        dropTarget: window.dragOverStatus === status
                        onEditRequested: function(taskId) { window.openTaskEditor(taskId) }
                        onStartRequested: function(taskId) { taskUndo.moveTask(taskId, "doing", -1) }
                        onCompleteRequested: function(taskId) { taskUndo.moveTask(taskId, "done", -1) }
                        onRestoreRequested: function(taskId) { taskUndo.moveTask(taskId, "todo", -1) }
                        onDeleteRequested: function(taskId) { window.deleteTaskWithUndo(taskId) }
                        onDragStarted: function(taskId, title, status, x, y) { window.beginTaskDrag(taskId, title, status, x, y) }
                        onDragMoved: function(x, y) { window.updateTaskDrag(x, y) }
                        onDragEnded: function(x, y) { window.endTaskDrag(x, y) }
                    }
                }

                // 底部插件槽位：插件声明的底部面板
                PluginSlot {
                    slotName: "bottom"
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.bottomMargin: 12
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
                item.permissionRequested.connect(function(pluginId) {
                    window.presentPermissionPrompt(pluginId)
                })
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

        // 预览中心面板：按需加载（仅主线实例有入口）
        Loader {
            id: previewLoader
            anchors.fill: parent
            active: false
            source: "PreviewPanel.qml"
            z: 145

            onLoaded: {
                if (window.previewPending) {
                    window.previewPending = false
                    item.open()
                }
            }
        }

        // 首启动引导：录任务 → 拖列 → 装插件（可用「跳过」直接收尾）
        OnboardingOverlay {
            anchors.fill: parent
            z: 500
            visible: window.onboardVisible
            canSeed: taskStore.count === 0
            onFinished: window.finishOnboarding()
            onSampleRequested: window.addSampleTasks()
        }

        // 专注倒计时环：悬浮右下，结束后自动收起
        FocusRing {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 24
            anchors.bottomMargin: 24
            visible: window.focusMode
            z: 300
            total: window.focusTotal
            remaining: window.focusRemaining
            paused: window.focusPaused
            onStopRequested: window.stopFocus(false)
            onPauseToggleRequested: window.toggleFocusPause()
        }

        // 命令面板：Ctrl+K 呼出，按需构造
        CommandPalette {
            id: commandPalette
            anchors.fill: parent
            commandProvider: window.buildCommands
            z: 400
        }

        // 插件权限授权弹窗：安装插件后或首次调用高危能力时弹出
        Loader {
            id: permissionDialogLoader
            anchors.fill: parent
            active: false
            source: "PluginPermissionDialog.qml"
            z: 350

            property string pendingPluginId: ""

            onLoaded: {
                if (pendingPluginId.length > 0) {
                    item.present(pendingPluginId)
                    pendingPluginId = ""
                }
            }
        }

        Connections {
            target: pluginRuntime
            function onPermissionRequested(pluginId, permission) {
                window.presentPermissionPrompt(pluginId)
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
                text: window.undoToastText
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
                    onClicked: window.undoLastAction()
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
            const extra = taskExtra.extraOf(id)
            remindField.text = formatRemind(extra ? extra.remindAt : 0)
            rruleField.text = extra ? extra.rrule : ""
            open()
            titleField.forceActiveFocus()
        }

        // 提醒时刻 → "YYYY-MM-DD HH:mm"（本地时区）
        function formatRemind(epoch) {
            if (!epoch || epoch <= 0)
                return ""
            const d = new Date(epoch * 1000)
            function p(n) { return n < 10 ? "0" + n : "" + n }
            return d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" + p(d.getDate())
                   + " " + p(d.getHours()) + ":" + p(d.getMinutes())
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

                Text {
                    text: "提醒"
                    color: Theme.muted
                    font.pixelSize: 12
                }

                TextField {
                    id: remindField
                    Layout.fillWidth: true
                    placeholderText: "提醒时间 YYYY-MM-DD HH:mm（留空则逾期时提醒一次）"
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

                Text {
                    text: "重复"
                    color: Theme.muted
                    font.pixelSize: 12
                }

                Repeater {
                    model: [["每天", "FREQ=DAILY"], ["每周", "FREQ=WEEKLY"], ["每月", "FREQ=MONTHLY"]]
                    delegate: Rectangle {
                        width: 44
                        height: 28
                        radius: 8
                        color: rruleField.text === modelData[1]
                               ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.48)
                               : Theme.input
                        border.width: 1
                        border.color: rruleField.text === modelData[1] ? Theme.cyan : Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: modelData[0]
                            color: rruleField.text === modelData[1] ? Theme.text : Theme.muted
                            font.pixelSize: 11
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: rruleField.text = (rruleField.text === modelData[1]) ? "" : modelData[1]
                        }
                    }
                }

                TextField {
                    id: rruleField
                    Layout.fillWidth: true
                    placeholderText: "重复规则（RRULE，如 FREQ=DAILY;INTERVAL=2）"
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
                        taskUndo.updateTask(editor.taskId, {
                            title: title,
                            note: noteField.text,
                            priority: editor.priorityValue,
                            dueAt: dueAt
                        })
                        // 提醒走侧表 task_extra（不进撤销栈，与插件写入/快照同步同口径）
                        let remindAt = 0
                        const rawRemind = remindField.text.trim()
                        if (rawRemind.length > 0) {
                            const rd = new Date(rawRemind.replace(" ", "T"))
                            if (!isNaN(rd.getTime()))
                                remindAt = Math.floor(rd.getTime() / 1000)
                        }
                        taskExtra.setReminder(editor.taskId, remindAt)
                        // 重复规则走侧表 task_extra（RRULE 原样存，不自造格式）
                        taskExtra.setRrule(editor.taskId, rruleField.text.trim())
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

    // 提醒辉光：提醒触发时窗口边缘一次性脉冲（ReminderScheduler::fired）
    Rectangle {
        id: reminderGlow
        z: 100
        anchors.fill: parent
        anchors.margins: 1
        radius: 12
        color: "transparent"
        border.width: 3
        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, glowLevel)
        property real glowLevel: 0

        NumberAnimation on glowLevel {
            id: glowFade
            from: 0.85
            to: 0
            duration: 1400
            easing.type: Easing.OutCubic
            running: false
        }

        function pulse() {
            glowLevel = 0.85
            glowFade.restart()
        }
    }

    Connections {
        target: reminderScheduler
        function onFired(taskId, title) {
            reminderGlow.pulse()
        }
    }
}
