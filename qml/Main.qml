import QtQuick
import QtQml
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
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

    // 卡片详情抽屉（R8-03）：看明白一张卡（Markdown 备注 + 全部元信息），并就地改
    // 标题 / 优先级 / 标签 / 到期 / 提醒 / 重复；执行定义仍在编辑器里
    function openCardDetail(id) {
        if (!id || id.length === 0)
            return
        closeAllPanels()
        cardDetailLoader.active = true
        if (cardDetailLoader.item) {
            cardDetailLoader.item.hostWindow = window
            cardDetailLoader.item.openFor(id)
        }
    }

    // 统一新建入口：顶部按钮 / Ctrl+N / 命令面板 / 托盘共用同一弹窗
    function openTaskComposer(status, draftTitle) {
        show()
        raise()
        requestActivate()
        taskComposerLoader.active = true
        if (taskComposerLoader.item)
            taskComposerLoader.item.openComposer(status || "todo", draftTitle || "")
    }

    // 设置 / 插件面板均按需加载：首次打开时才构造，避免首帧为低频界面付出代价
    property bool settingsPending: false
    property int pendingPluginIndex: -1
    property bool libraryPending: false
    // 预览中心：仅主线实例有入口，面板同样按需加载
    property bool previewPending: false
    property bool previewOpen: false
    // 执行队列（R7-02）：队列是运行态，所有实例都能看自己本机的队列
    property bool execQueueOpen: false

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

    // 命令面板里的「重看新手引导」：不改完成标记，只是把浮层再开一次
    function replayOnboarding() {
        onboarding.restart()
        window.closeAllPanels()
        window.onboardVisible = true
    }

    // 示例卡：让新用户一上手就能试拖拽，随时可删
    // 都落到「待办」列：引导第二步讲的就是把它们往右拖
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

    // 已实例化的列（列清单来自数据库，顺序即界面顺序）。
    // 不用固定 id 引用：列是可以增删的，写死 todoColumn 之类的引用会在加列后失效。
    function columnItems() {
        var out = []
        for (var i = 0; i < columnRepeater.count; ++i) {
            var item = columnRepeater.itemAt(i)
            if (item)
                out.push(item)
        }
        return out
    }

    // 键盘导航：把焦点交给指定列并选中首张卡（左右方向键跨列）
    function focusColumn(targetIndex) {
        var cols = columnItems()
        if (cols.length === 0)
            return
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
        // 面板可能在启动早期被调用：那时列管理面板还没实例化，不能直接解引用
        if (columnManager)
            columnManager.close()
        commandPalette.close()
        if (pluginPanelLoader.item)
            pluginPanelLoader.item.close()
        if (settingsLoader.item)
            settingsLoader.item.close()
        if (previewLoader.item)
            previewLoader.item.close()
        projectMapLoader.active = false
        window.previewOpen = false
        execQueueDialog.close()
        // 详情抽屉（R8-03）：与其它浮层同一套互斥纪律
        if (cardDetailLoader.item)
            cardDetailLoader.item.close()
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

    // 项目全景侧边浮层入口
    function openProjectMap() {
        show()
        raise()
        requestActivate()
        closeAllPanels()
        projectMapLoader.active = true
    }

    // 执行前确认（R6-03）：只展示快照，真正的进程启动在 R6-04 接入
    function openExecConfirm(payload) {
        execConfirmDialog.taskId = payload.taskId || ""
        execConfirmDialog.agentId = payload.agentId || ""
        execConfirmDialog.agentName = payload.agentName || payload.agentId || ""
        execConfirmDialog.cwd = payload.cwd || ""
        execConfirmDialog.command = payload.command || ""
        execConfirmDialog.acceptance = payload.acceptance || []
        execConfirmDialog.timeoutMin = payload.timeoutMin || 30
        execConfirmDialog.maxAttempts = payload.maxAttempts || 1
        execConfirmDialog.cwdTrusted = agentProfile.isTrustedCwd(execConfirmDialog.cwd)
        execConfirmDialog.trustChecked = false
        execConfirmDialog.open()
    }

    // 执行面板（R6-06）：最近一次 run 的结论徽章 / 耗时 / 末尾日志 /
    // 查看完整日志 / 重试 / 标为已办结（走既有完成路径、可撤销）
    function openExecPanel(taskId) {
        if (!taskId || taskId.length === 0)
            return
        execPanelDialog.taskId = taskId
        execPanelDialog.refresh()
        execPanelDialog.open()
    }

    // 执行队列（R7-02）：排队中 / 在跑的 run、并发指示、预计完成时间，
    // 可在面板里暂停 / 恢复 / 调序 / 跳过。队列是运行态，不改卡片所在列。
    function openExecQueue() {
        show()
        raise()
        requestActivate()
        closeAllPanels()
        execQueueDialog.refresh()
        execQueueDialog.open()
    }

    // 夜间批量（R7-05）：勾选一批待办卡 → 一次入队 → 次日看结果。
    function openNightBatch() {
        show()
        raise()
        requestActivate()
        closeAllPanels()
        nightBatchDialog.refreshEstimate()
        nightBatchDialog.open()
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

        add("新建任务", "操作", "选列 / 挂父卡 / 预览解析", function() { window.openTaskComposer("todo") })
        add("打开插件面板", "面板", "当前插件内容", function() { window.openPlugins() })
        add("打开插件库", "面板", "已装 / 可装插件", function() { window.openPluginLibrary() })
        add("打开设置面板", "面板", "热键 / 数据路径 / 开机自启", function() { window.openSettings() })
        add("打开项目全景图", "面板", "里程碑进度 + 开发链路，点节点钻取细节", function() { window.openProjectMap() })
        add("打开执行队列", "面板", "排队中 / 在跑的 run，可暂停 · 调序 · 跳过", function() { window.openExecQueue() })
        add("夜间批量执行", "面板", "勾选多张待办卡 → 一次入队 → 次日看结果", function() { window.openNightBatch() })
        add("检查软件更新", "面板", "当前 " + updateManager.currentVersion + "，查看是否有新版本",
            function() { window.openSettings(); updateManager.checkForUpdates(false) })
        add("打开外观面板", "面板", "主题预设 / 画质 / 减弱动效", function() { window.openAppearance() })
        add("重看新手引导", "帮助", "三步说明：录任务 / 拖列 / 装插件", function() { window.replayOnboarding() })
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
                    cmds.push({
                        title: "卡片详情：" + title,
                        category: "任务",
                        subtitle: "看备注与全部元信息（Markdown 渲染）",
                        run: function() { window.openCardDetail(t.id) }
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

    // 按列 id 找已实例化的列；列 id 对不上（退化模式 / 老数据）时退回按状态找第一列
    function columnFor(columnId, status) {
        var cols = columnItems()
        if (columnId && columnId.length > 0) {
            for (var i = 0; i < cols.length; ++i) {
                if (cols[i].columnId === columnId)
                    return cols[i]
            }
        }
        for (var j = 0; j < cols.length; ++j) {
            if (cols[j].status === status)
                return cols[j]
        }
        return null
    }

    // 跳转到指定任务：清除过滤 → 定位到所在列 → 高亮闪烁
    function jumpToTask(taskId) {
        searchField.text = ""
        window.setStageFilter("")
        // 项目范围也要清：目标卡若属于别的项目，留在项目视图里它就被过滤掉了，
        // flashTask 找不到行，跳转会静默失败。
        window.setProjectFilter("")
        var task = taskStore.taskById(taskId)
        if (!task || !task.id)
            return
        var column = columnFor(task.columnId, task.status)
        if (column)
            column.flashTask(taskId)
    }

    property bool dragActive: false
    // 松手后的收尾：拖影还要淡出 settleMs 毫秒才彻底消失。
    // 数据在 endTaskDrag 里已经写完了，这段时间纯粹是视觉收尾，绝不阻塞落库。
    property bool dragSettling: false
    property string dragTaskId: ""
    property string dragTaskTitle: ""
    property string dragTaskStatus: ""
    property real dragX: 0
    property real dragY: 0
    // 磁吸后的拖影纵向位置（root 坐标）；-1 表示没吸附，拖影跟手
    property real dragSnapY: -1
    // 当前悬停的目标列 id，用于目标列呼吸高亮；空串表示不在任何列上。
    // 用列而不是状态：同状态的多个列必须各自高亮，串味会让用户以为拖到了隔壁。
    property string dragOverColumnId: ""

    // 里程碑过滤：空串表示全部；阶段来自卡片首个标签（R0/R1/R2/R3…）
    property string stageFilter: ""
    // 项目范围过滤：空串表示全局；非空只显示该项目的卡（R10-02）
    property string projectFilter: ""
    // 阶段条数据由 C++ 侧 TaskModel::stageList() 给出：集合与顺序取自路线图 §9 行序，
    // §9 之外的阶段折进「其他」并给出可对账的计数。QML 只负责画，
    // 不再自己写死一份阶段清单 —— 否则 §9 加了行、界面还得跟着改（DX-04）。
    property var stageList: []
    // §9 解析失败已告警过（避免编辑器连写时每次 change 都刷一行日志）
    property bool roadmapStageWarned: false

    function setStageFilter(stage) {
        stageFilter = stage
        boardColumns.stage = stage
    }

    // 项目范围：空串 = 全局。与阶段过滤相互独立，叠加生效（先按项目收窄，再按里程碑筛）。
    // 只改视图状态，不写库 —— 退出重开回到全局，符合「范围不是数据」这条约定。
    function setProjectFilter(projectId) {
        projectFilter = projectId
        boardColumns.project = projectId
    }

    function refreshStageData() {
        // 阶段顺序由 projectMap 给出（读路线图 §9 的行序，只读一份 md、不跑 git）；
        // 模型层保持零依赖，测试里也能直接传一组顺序进来断言。
        var order = projectMap.stageOrder()
        // 路线图被写坏时 stageOrder() 返回空列表（读不到文件 / §9 表格缺列都会走这条）。
        // 空列表会把阶段条清空 —— 比「停在上一份有效集合」糟得多，所以这里保留旧值并告警一次。
        // 编辑器半截写入也是合法中间态，下一秒补全后 projectMap.onChanged 会再调进来，届时自然恢复。
        if (order.length === 0 && stageList.length > 0) {
            if (!roadmapStageWarned) {
                roadmapStageWarned = true
                console.warn("STAGE", "路线图 §9 解析为空，保留上次阶段集合",
                             stageList.length, "项")
            }
            return
        }
        roadmapStageWarned = false
        stageList = taskStore.stageList(order)
        // 一行诊断：§9 改动后靠它能直接从日志确认「watcher 真的触发了、阶段集合换成了新的」
        // （DX-08 验收要求「1 秒内自动更新」，没有这行就只能靠肉眼看界面）。
        console.info("STAGE", "refresh", order.length, "阶段")
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
        var columns = columnItems()
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
        dragSnapY = -1
        dragSettling = false
        dragOverColumnId = boardColumns.columnIdForStatus(status)
        dragActive = true
        dragTrail.reset()
        dragTrail.feed(x, y)
    }

    function clearDropMarks() {
        var columns = columnItems()
        for (var i = 0; i < columns.length; ++i) {
            columns[i].dropBeforeId = ""
            columns[i].dropAnchorId = ""
            columns[i].dropAnchorBelow = false
            columns[i].dropSnapped = false
        }
    }

    function updateTaskDrag(x, y) {
        dragX = x
        dragY = y
        dragTrail.feed(x, y)
        var column = columnAt(x, y)
        if (!column) {
            clearDropMarks()
            dragOverColumnId = ""
            dragSnapY = -1
            return
        }
        var origin = column.mapToItem(root, 0, 0)
        var drop = column.resolveDrop(y - origin.y)
        var columns = columnItems()
        for (var i = 0; i < columns.length; ++i) {
            columns[i].dropBeforeId = (columns[i] === column) ? drop.beforeId : ""
            columns[i].dropAnchorId = (columns[i] === column) ? drop.anchorId : ""
            columns[i].dropAnchorBelow = (columns[i] === column) ? drop.below : false
            columns[i].dropSnapped = (columns[i] === column) ? drop.onLine : false
        }
        dragOverColumnId = column.columnId
        // 磁吸：贴上插入线时，拖影纵向吸到线上（横向仍然跟手）。
        // 只吸纵向 —— 用户判断的是「插进哪一行」，横向吸附只会让人觉得鼠标漂了。
        dragSnapY = drop.onLine ? (origin.y + drop.viewY) : -1
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
            // 精确插位：插到落点卡片之前，beforeId 为空则落到列尾。
            // 按列移动（同状态多列各排各的）；列 id 为空说明是退化模式，回退按状态。
            var moved = column.columnId.length > 0
                    ? taskUndo.moveTaskBeforeColumn(dragTaskId, column.columnId, drop.beforeId)
                    : taskUndo.moveTaskBefore(dragTaskId, column.status, drop.beforeId)
            if (moved)
                column.ripple(drop.viewY)
        }
        clearDropMarks()
        dragTrail.reset()
        dragActive = false
        dragSettling = true
        dragSettleTimer.restart()
        dragOverColumnId = ""
        dragSnapY = -1
        dragTaskId = ""
        dragTaskTitle = ""
        dragTaskStatus = ""
    }

    // 拖影收尾计时：reduced 时 0ms（下一拍就消失），常规 90ms。
    // 时长由 C++ 单测钉住（tst_dragfeel：不许超过 100ms，否则拖完像卡了一下）。
    Timer {
        id: dragSettleTimer
        interval: dragFeel.settleDuration(Motion.reduced)
        onTriggered: window.dragSettling = false
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
        onActivated: window.openTaskComposer("todo")
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
            window.refreshStageData()
        }
        // 完成任务时背景爆发一次能量脉冲
        function onTaskCompleted(id) {
            background.burst()
        }
    }

    // 阶段条跟随路线图（DX-08 / #11）：§9 表格行改动后无需重启。
    // 复用 ProjectMap 已有的「文件监听 + 700ms 防抖」—— 它 watch 了 docs/iteration-roadmap.md，
    // 落盘后 refresh() 再 emit changed()，阶段条与面板因此共用同一份变更信号（不引入轮询）。
    // 原先阶段条只挂在 taskStore.onCountsChanged 上，改 §9 必须重启才可见（DX-04 遗留）。
    Connections {
        target: projectMap
        function onChanged() {
            window.refreshStageData()
        }
    }

    Component.onCompleted: {
        refreshStageData()
        setStageFilter(initialStage)
        pluginLoader.scan()
        // 判定收在 UiSettings::shouldOnboard（未完成过 且 空板）：老用户换机重装
        // 也该直接看到自己的看板，而不是被三步说明挡在门外
        window.onboardVisible = uiSettings.shouldOnboard(taskStore.count)
        // 一行诊断：出问题时能直接从日志看出「该弹没弹 / 不该弹却弹了」
        console.info("ONBOARD", window.onboardVisible ? "show" : "skip",
                     "tasks", taskStore.count, "done", uiSettings.onboardingDone)
        // --exec-queue：启动即打开队列面板（截图 / 排障用，与 --map / --appearance 同类）
        if (initialExecQueue)
            window.openExecQueue()
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
        FocusBadge {
            focusMode: window.focusMode
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

            WindowButtons {
                id: windowButtons
                onCloseRequested: window.hide()
            }

            ThemePicker {
                id: themePicker
                anchors.right: windowButtons.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
            }

            // R11-02：标题栏按钮统一走 TopToolButton —— 原先「全景」「＋ 新建」
            // 是同一套样式各抄一遍（圆角 / 边框 alpha / 悬浮底色硬编码），改主题不跟随。
            TopToolButton {
                id: mapButton
                anchors.right: themePicker.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: "全景"
                description: "打开项目全景面板"
                onClicked: window.openProjectMap()
            }

            TopToolButton {
                id: newTaskButton
                anchors.right: mapButton.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "＋ 新建"
                emphasis: true
                description: "新建任务"
                onClicked: window.openTaskComposer("todo")
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
                        // 「队列」所有实例都有（队列是本机运行态，沙箱实例同样要能看自己的队列）；
                        // 「预览」只在主线实例出现：agent 沙箱实例里再嵌套预览入口只会混淆。
                        model: previewHub.hostInstance
                               ? ["看板", "插件", "设置", "队列", "预览"]
                               : ["看板", "插件", "设置", "队列"]

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: 34
                            radius: 8
                            color: (index === 0 || (index === 3 && window.execQueueOpen)
                                    || (index === 4 && window.previewOpen))
                                   ? Theme.accentSoft
                                   : (navMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.04)
                                                             : "transparent")
                            border.width: (index === 0 || (index === 3 && window.execQueueOpen)
                                           || (index === 4 && window.previewOpen)) ? 1 : 0
                            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.22)

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData
                                color: (index === 0 || (index === 3 && window.execQueueOpen)
                                        || (index === 4 && window.previewOpen))
                                       ? Theme.cyan : Theme.muted
                                font.pixelSize: 13
                            }

                            // 队列徽章：有 run 在排队 / 在跑时给出数量，不用点进去也知道
                            Rectangle {
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                visible: index === 3 && (scheduler.queueLength + scheduler.running) > 0
                                height: 16
                                width: queueCountText.implicitWidth + 10
                                radius: 5
                                color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.18)

                                Text {
                                    id: queueCountText
                                    anchors.centerIn: parent
                                    text: scheduler.running + "/" + (scheduler.queueLength + scheduler.running)
                                    color: Theme.cyan
                                    font.pixelSize: 9
                                }
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
                                        window.openExecQueue()
                                    } else if (index === 4) {
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
                spacing: 8

                // R11-01：顶区信息层级重排。
                // 原先这里是一行 48 px 高的「大标题 + 三块统计盒」，顶区垂直占用近 190 px，
                // 看板被压扁。现在标题降到 18 px 且与副标题同行，统计改成单行状态带，
                // 整行高度从 48 px 降到 28 px —— 省下的高度直接还给三列看板。
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    spacing: 12

                    RowLayout {
                        spacing: 8

                        Text {
                            text: "工作台"
                            color: Theme.text
                            font.pixelSize: 18
                            font.bold: true
                        }

                        Text {
                            text: "专注当下，逐项推进"
                            color: Theme.muted
                            font.pixelSize: 10
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // 统计状态带：待办 / 进行中 / 今日完成 / 逾期
                    TopStatBar {
                        Layout.alignment: Qt.AlignVCenter
                        todo: boardColumns.todoCount
                        doing: boardColumns.doingCount
                        doneToday: taskStore.doneTodayCount
                        overdue: taskStats.overdueCount
                    }

                    // 专注按钮：与标题栏按钮同一套控件语言（圆角 / 高度 / 边框层级）
                    Rectangle {
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 28
                        radius: Theme.radiusControl
                        color: focusBtnMouse.containsMouse || window.focusMode
                               ? Theme.accentSoft
                               : Theme.input
                        border.width: 1
                        border.color: window.focusMode
                                      ? Qt.rgba(Theme.magenta.r, Theme.magenta.g, Theme.magenta.b, 0.45)
                                      : Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: window.focusMode ? "专注中" : "专注"
                            color: window.focusMode ? Theme.magenta : Theme.accent
                            font.pixelSize: 11
                        }

                        MouseArea {
                            id: focusBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
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

                    // 两级看板的第一级：项目范围（全局 / 单项目）。
                    // 放在阶段条左侧，是因为「看谁的事」比「看到哪一步」更外层。
                    ProjectSwitcher {
                        id: projectSwitcher
                        Layout.preferredWidth: 230
                        Layout.preferredHeight: 40
                        activeProject: window.projectFilter
                        onProjectSelected: function(projectId) { window.setProjectFilter(projectId) }
                    }

                    StageBar {
                        id: stageBar
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        stages: window.stageList
                        activeStage: window.stageFilter
                        syncText: window.roadmapSyncText
                        syncOk: window.roadmapSyncOk
                        onStageSelected: function(stage) { window.setStageFilter(stage) }
                    }

                    Rectangle {
                        Layout.preferredWidth: 300
                        height: 28
                        radius: Theme.radiusControl
                        color: Theme.input
                        border.width: 1
                        border.color: Theme.border

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
                            onTextChanged: boardColumns.search = text
                            Keys.onEscapePressed: text = ""
                        }
                    }
                }

                // 统计横幅：热力图 + 连续天数 + 周吞吐（数据来自 taskStats，只读任务表）
                // R11-01：高度 84 → 72（热力格 8 → 7 px，横幅内边距 9 → 6），
                // 顶区总高度由此再省 12 px 给看板。
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.preferredHeight: 72
                    radius: Theme.radiusPanel
                    color: Theme.panel
                    border.width: 1
                    border.color: Theme.hairline

                    ActivityHeatmap {
                        anchors.fill: parent
                        anchors.margins: 6
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

                    // 列由数据库驱动：加列 / 改名 / 调序只改 board_column，
                    // 界面按 order_key 顺序自动出列，不再写死三列。
                    Repeater {
                        id: columnRepeater
                        model: boardColumns

                        delegate: BoardColumn {
                            required property string boardColumnId
                            required property string boardColumnTitle
                            required property string boardColumnStatus
                            required property var boardColumnModel
                            required property int boardColumnIndex

                            columnId: boardColumnId
                            columnIndex: boardColumnIndex
                            title: boardColumnTitle
                            status: boardColumnStatus
                            taskModel: boardColumnModel
                            // 三态沿用原有配色：待办青、进行中紫、已办结洋红
                            accent: boardColumnStatus === "done" ? Theme.magenta
                                  : boardColumnStatus === "doing" ? Theme.violet
                                  : Theme.cyan
                            searchQuery: searchField.text
                            stageFilter: window.stageFilter
                            projectFilter: window.projectFilter
                            dragSpace: root
                            dropTarget: window.dragOverColumnId === boardColumnId
                            onColumnMoveRequested: function(targetIndex) { window.focusColumn(targetIndex) }
                            // 双击 / 回车 / 无障碍「按下」都走详情抽屉（R8-03）：
                            // 先让人看清楚再决定改什么，编辑器从抽屉里一键进入
                            onEditRequested: function(taskId) { window.openCardDetail(taskId) }
                            onStartRequested: function(taskId) { taskUndo.moveTask(taskId, "doing", -1) }
                            onCompleteRequested: function(taskId) { taskUndo.moveTask(taskId, "done", -1) }
                            onRestoreRequested: function(taskId) { taskUndo.moveTask(taskId, "todo", -1) }
                            onDeleteRequested: function(taskId) { window.deleteTaskWithUndo(taskId) }
                            onDragStarted: function(taskId, title, status, x, y) { window.beginTaskDrag(taskId, title, status, x, y) }
                            onDragMoved: function(x, y) { window.updateTaskDrag(x, y) }
                            onDragEnded: function(x, y) { window.endTaskDrag(x, y) }
                        }
                    }

                    // 列管理入口：列由数据库驱动，加列 / 改名 / 调序都在这里
                    Rectangle {
                        Layout.preferredWidth: 46
                        Layout.fillHeight: true
                        radius: 16
                        color: addColumnMouse.containsMouse ? Theme.cardHover : Theme.card
                        border.width: 1
                        border.color: addColumnMouse.containsMouse ? Theme.borderStrong : Theme.border

                        Column {
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "＋"
                                color: addColumnMouse.containsMouse ? Theme.cyan : Theme.muted
                                font.pixelSize: 18
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "列"
                                color: Theme.muted
                                font.pixelSize: 10
                            }
                        }

                        MouseArea {
                            id: addColumnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                window.closeAllPanels()
                                columnManager.open = true
                            }
                        }
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

        // 执行队列面板（R7-02）内联在文件末尾的 Dialog 里（不是 Loader）：
        // 它只在点开时才 refresh，构造成本可忽略，没必要为它多一个按需加载层。

        // 项目全景侧边浮层：按需加载（右锚定，z 高层级）
        Loader {
            id: projectMapLoader
            anchors.fill: parent
            active: false
            source: "ProjectMapPanel.qml"
            z: 160

            onLoaded: {
                item.requestClose.connect(function() {
                    projectMapLoader.active = false
                })
            }
        }

        // 首启动引导：录任务 → 拖列 → 装插件（可用「跳过」直接收尾）
        OnboardingOverlay {
            id: onboarding
            anchors.fill: parent
            z: 500
            visible: window.onboardVisible
            // 已经有卡就不再提供「载入示例卡」：那是空板才需要的脚手架
            canSeed: taskStore.count === 0
            onFinished: window.finishOnboarding()
            onSampleRequested: window.addSampleTasks()
            // 点模板 = 结束引导 + 直接开新建面板（文案已预填，改完回车即可）
            onTemplateRequested: function(title) {
                window.finishOnboarding()
                window.openTaskComposer("todo", title)
            }
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

        // 列管理面板：把列结构交给用户（增 / 改名 / 调序 / 删）
        ColumnManager {
            id: columnManager
            anchors.fill: parent
            z: 380
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
        // 执行定义（M0，侧表 task_exec；与任务字段分开保存）
        property string execAgent: ""
        property string execWorkflow: ""
        // 页签：0 任务 / 1 执行
        property int tabIndex: 0
        property string execCwd: ""
        property string execNotice: ""
        // Triage 草案（R6-07）：只读分析结果，采纳后才回填表单
        property var triageDraft: ({})
        property bool triageRunning: false
        // 「执行」按钮的最小集：目标 + 验收 + 工作目录
        readonly property string execBlocker: {
            if (goalField.text.trim().length === 0)
                return "缺少执行目标"
            if (linesOf(acceptanceField.text).length === 0)
                return "缺少验收项"
            if (execCwd.trim().length === 0)
                return "未选择工作目录"
            // R6-07：草案置信度 < 0.5 时禁止一键执行（先拆分或先澄清）
            if (triageDraft && triageDraft.status === "ok"
                    && triageDraft.draft && triageDraft.draft.executable === false)
                return "分析置信度低于 0.5：请先拆分任务或回答草案里的问题"
            return ""
        }

        // 多行文本 → 字符串数组（去空行、去首尾空白）
        function linesOf(text) {
            return String(text || "").split("\n")
                .map(function (s) { return s.trim() })
                .filter(function (s) { return s.length > 0 })
        }

        function intOf(text, fallback) {
            const n = parseInt(String(text || ""), 10)
            return isNaN(n) ? fallback : n
        }

        // file:///E:/x → E:/x（FolderDialog 给的是 URL）
        function localPath(url) {
            let s = String(url || "")
            if (s.indexOf("file:///") === 0)
                s = s.substring(8)
            return decodeURIComponent(s)
        }

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
            // 执行定义（侧表 task_exec）：缺省时清空，避免串上一张卡的内容
            const exec = taskExec.execOf(id)
            goalField.text = exec ? exec.goal : ""
            acceptanceField.text = exec ? exec.acceptance.join("\n") : ""
            scopeField.text = exec ? exec.scope.join("\n") : ""
            execAgent = exec ? exec.agent : ""
            execWorkflow = exec ? exec.workflow : ""
            agentField.text = execAgent
            execCwd = exec ? exec.cwd : ""
            timeoutField.text = String(exec ? exec.timeoutMin : 30)
            attemptsField.text = String(exec ? exec.maxAttempts : 1)
            tabIndex = 0
            execNotice = ""
            // 已有草案（上次分析）直接回显
            triageDraft = triageRunner.draftOf(id) || ({})
            triageRunning = false
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

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "编辑任务"
                    color: Theme.text
                    font.pixelSize: 18
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: taskTab
                    text: "任务"
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 28
                    padding: 0
                    onClicked: {
                        editor.tabIndex = 0
                        editor.execNotice = ""
                    }
                    background: Rectangle {
                        radius: 8
                        color: editor.tabIndex === 0
                               ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
                               : Theme.input
                        border.width: 1
                        border.color: editor.tabIndex === 0 ? Theme.cyan : Theme.border
                    }
                    contentItem: Text {
                        text: taskTab.text
                        color: editor.tabIndex === 0 ? Theme.text : Theme.muted
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    id: execTab
                    text: "执行"
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 28
                    padding: 0
                    onClicked: {
                        editor.tabIndex = 1
                        editor.execNotice = ""
                    }
                    background: Rectangle {
                        radius: 8
                        color: editor.tabIndex === 1
                               ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
                               : Theme.input
                        border.width: 1
                        border.color: editor.tabIndex === 1 ? Theme.cyan : Theme.border
                    }
                    contentItem: Text {
                        text: execTab.text
                        color: editor.tabIndex === 1 ? Theme.text : Theme.muted
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            TextField {
                id: titleField
                Layout.fillWidth: true
                visible: editor.tabIndex === 0
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
                visible: editor.tabIndex === 0
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
                visible: editor.tabIndex === 0

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
                visible: editor.tabIndex === 0

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
                visible: editor.tabIndex === 0

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

            TextField {
                id: goalField
                Layout.fillWidth: true
                visible: editor.tabIndex === 1
                placeholderText: "目标：一句话说明要达成什么"
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

            TextArea {
                id: acceptanceField
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                visible: editor.tabIndex === 1
                placeholderText: "验收：每行一条（如 pwsh -File scripts/verify.ps1）"
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: 11
                wrapMode: TextArea.Wrap
                background: Rectangle {
                    color: Theme.elevated
                    radius: 8
                    border.width: 1
                    border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
                }
            }

            TextArea {
                id: scopeField
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                visible: editor.tabIndex === 1
                placeholderText: "边界：每行一条路径 glob（如 src/TaskModel.cpp）"
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: 11
                wrapMode: TextArea.Wrap
                background: Rectangle {
                    color: Theme.elevated
                    radius: 8
                    border.width: 1
                    border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
                }
            }

            // Triage 只读分析（R6-07）：先分析 → 草案预览 → 采纳回填表单
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: editor.tabIndex === 1

                Button {
                    text: editor.triageRunning ? "分析中…" : "先分析"
                    enabled: !editor.triageRunning
                             && agentProfile.executorEnabled
                             && editor.execCwd.trim().length > 0
                    onClicked: {
                        // 分析用当前表单里的 agent / cwd / workflow：先把它们落进
                        // task_exec（只写这三个键，不影响其它字段），TriageRunner
                        // 从执行定义读参数。
                        taskExec.setExec(editor.taskId, {
                            agent: agentField.text.trim() || agentProfile.defaultAgent,
                            cwd: editor.execCwd,
                            workflow: editor.execWorkflow
                        })
                        editor.triageDraft = {}
                        editor.triageRunning = true
                        const r = triageRunner.start(editor.taskId)
                        if (!r || !r.ok) {
                            editor.triageRunning = false
                            editor.execNotice = (r && r.message) ? r.message : "分析启动失败"
                        }
                    }
                }

                // 台账单向导入（R6-09，ADR-0001）：按标题内嵌短号只补空值，
                // 冲突只报告不覆盖；没有「写入台账」的反向入口。
                Button {
                    text: "台账导入"
                    property string code: {
                        var m = String(titleField.text || "").match(/#([A-Za-z]{1,4}\d*-\d+|M\d+)\b/)
                        return m ? m[1].toUpperCase() : ""
                    }
                    visible: code.length > 0
                    onClicked: {
                        const r = ledgerImport.importCard(code)
                        if (!r || !r.ok) {
                            editor.execNotice = "台账导入失败：" + ((r && r.error) || "未知错误")
                            return
                        }
                        if (r.result === "imported") {
                            // 重新加载执行定义，把补进来的字段显示到表单
                            const exec = taskExec.execOf(editor.taskId)
                            goalField.text = exec ? exec.goal : goalField.text
                            acceptanceField.text = exec && exec.acceptance.length > 0
                                ? exec.acceptance.join("\n") : acceptanceField.text
                            scopeField.text = exec && exec.scope.length > 0
                                ? exec.scope.join("\n") : scopeField.text
                            editor.execNotice = "已从台账导入 " + r.filled + " 项（只补空值）"
                        } else if (r.result === "conflict") {
                            var c = (r.conflicts || [])[0] || {}
                            editor.execNotice = "台账与看板定义冲突（" + (c.field || "")
                                + "）：不覆盖，请人工决定"
                        } else if (r.result === "missing") {
                            editor.execNotice = "看板里找不到短号 " + code + " 对应的卡"
                        } else {
                            editor.execNotice = "台账与看板一致，无需导入"
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: {
                        var d = editor.triageDraft || {}
                        if (!d.status)
                            return "只读分析：agent 用只读参数看仓库，产出执行草案，人采纳后回填"
                        if (d.status === "ok")
                            return "分析完成：置信度 " + Number(d.draft.confidence).toFixed(2)
                        if (d.status === "draft")
                            return "分析失败（保留原始输出）：" + (d.message || "")
                        if (d.status === "readonly-violation")
                            return "只读失效：" + (d.message || "")
                        return d.message || ""
                    }
                    color: (editor.triageDraft && editor.triageDraft.status === "ok")
                           ? Theme.cyan : Theme.muted
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
            }

            // 草案预览：理解复述 / scope / 验收 / 问题 / 采纳按钮
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: triageDraftCol.implicitHeight + 16
                visible: editor.tabIndex === 1
                         && editor.triageDraft
                         && editor.triageDraft.status === "ok"
                radius: 8
                color: Theme.card
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: triageDraftCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: "理解：" + ((editor.triageDraft.draft || {}).understanding || "")
                        color: Theme.text
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: (((editor.triageDraft.draft || {}).affected || {}).risk || "").length > 0
                        text: "风险：" + ((editor.triageDraft.draft || {}).affected || {}).risk
                        color: Theme.warning
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "scope：" + (((editor.triageDraft.draft || {}).scope_draft || []).join("  "))
                        color: Theme.muted
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "验收：" + (((editor.triageDraft.draft || {}).acceptance_draft || []).join("  "))
                        color: Theme.muted
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: ((editor.triageDraft.draft || {}).questions || [])
                        Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: "？ " + modelData
                            color: Theme.cyan
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            visible: (editor.triageDraft.problems || []).length > 0
                            text: (editor.triageDraft.problems || []).join("；")
                            color: Theme.warning
                            font.pixelSize: 9
                            wrapMode: Text.Wrap
                        }

                        Button {
                            text: "采纳草案"
                            // confidence < 0.5 时不给一键回填执行参数（先拆分或澄清）
                            enabled: ((editor.triageDraft.draft || {}).executable !== false)
                            onClicked: {
                                const d = editor.triageDraft.draft || {}
                                if ((d.scope_draft || []).length > 0)
                                    scopeField.text = d.scope_draft.join("\n")
                                if ((d.acceptance_draft || []).length > 0)
                                    acceptanceField.text = d.acceptance_draft.join("\n")
                                if (d.understanding && goalField.text.trim().length === 0)
                                    goalField.text = d.understanding
                                editor.execNotice = "已采纳草案：保存后生效"
                            }
                        }

                        Button {
                            text: "忽略"
                            onClicked: {
                                triageRunner.clearDraft(editor.taskId)
                                editor.triageDraft = {}
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: editor.tabIndex === 1

                TextField {
                    id: agentField
                    Layout.fillWidth: true
                    placeholderText: "agent（如 opencode / claude）"
                    placeholderTextColor: Theme.muted
                    color: Theme.text
                    font.pixelSize: 11
                    background: Rectangle {
                        color: Theme.elevated
                        radius: 8
                        border.width: 1
                        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
                    }
                }

                Repeater {
                    model: ["fix", "feature", "hotfix", "docs", "review", "delivery"]
                    delegate: Rectangle {
                        width: 52
                        height: 26
                        radius: 8
                        color: editor.execWorkflow === modelData
                               ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.48)
                               : Theme.input
                        border.width: 1
                        border.color: editor.execWorkflow === modelData ? Theme.cyan : Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: editor.execWorkflow === modelData ? Theme.text : Theme.muted
                            font.pixelSize: 10
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: editor.execWorkflow = (editor.execWorkflow === modelData) ? "" : modelData
                        }
                    }
                }
            }

            // cwd：只读展示 + 目录选择；「执行」要求显式目录，绝不在未知目录里跑 agent
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: editor.tabIndex === 1

                TextField {
                    id: cwdField
                    Layout.fillWidth: true
                    text: editor.execCwd
                    readOnly: true
                    placeholderText: "工作目录（执行必填）"
                    placeholderTextColor: Theme.muted
                    color: Theme.text
                    font.pixelSize: 11
                    background: Rectangle {
                        color: Theme.elevated
                        radius: 8
                        border.width: 1
                        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
                    }
                }

                Button {
                    text: "选择…"
                    onClicked: execCwdDialog.open()
                }

                Button {
                    text: "清除"
                    enabled: editor.execCwd.length > 0
                    onClicked: editor.execCwd = ""
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: editor.tabIndex === 1

                Text {
                    text: "超时"
                    color: Theme.muted
                    font.pixelSize: 12
                }

                TextField {
                    id: timeoutField
                    Layout.preferredWidth: 56
                    text: "30"
                    horizontalAlignment: TextInput.AlignHCenter
                    color: Theme.text
                    font.pixelSize: 11
                    validator: IntValidator { bottom: 1; top: 480 }
                    background: Rectangle {
                        color: Theme.elevated
                        radius: 8
                        border.width: 1
                        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
                    }
                }

                Text {
                    text: "分钟"
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "轮数"
                    color: Theme.muted
                    font.pixelSize: 12
                }

                TextField {
                    id: attemptsField
                    Layout.preferredWidth: 48
                    text: "1"
                    horizontalAlignment: TextInput.AlignHCenter
                    color: Theme.text
                    font.pixelSize: 11
                    validator: IntValidator { bottom: 1; top: 5 }
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
                visible: editor.tabIndex === 1

                Text {
                    Layout.fillWidth: true
                    text: editor.execBlocker.length > 0
                          ? "执行不可用：" + editor.execBlocker
                          : (editor.execNotice.length > 0
                             ? editor.execNotice
                             : (agentProfile.executorEnabled
                                ? "最小集已满足：执行前会弹出确认框"
                                : "执行器未启用：请在 设置 → 任务执行 打开总开关"))
                    color: editor.execBlocker.length > 0
                           ? Theme.muted
                           : (agentProfile.executorEnabled ? Theme.cyan : Theme.warning)
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }

                Text {
                    visible: !agentProfile.executorEnabled
                    text: "去设置"
                    color: execSettingsMouse.containsMouse ? Theme.cyan : Theme.text
                    font.pixelSize: 10

                    MouseArea {
                        id: execSettingsMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            editor.close()
                            window.openSettings()
                        }
                    }
                }

                Button {
                    text: "执行态"
                    // 有任何 run 记录才显示（revision 建立依赖，run 变化时重算）
                    property var latest: {
                        var rev = taskRuns.revision
                        return taskRuns.latestRun(editor.taskId)
                    }
                    visible: Object.keys(latest || {}).length > 0
                    onClicked: window.openExecPanel(editor.taskId)
                }

                Button {
                    text: "执行"
                    enabled: editor.execBlocker.length === 0
                    onClicked: {
                        if (!agentProfile.executorEnabled) {
                            editor.execNotice = "执行器未启用：请到 设置 → 任务执行 打开总开关"
                            return
                        }
                        const requested = agentField.text.trim()
                        const agentId = requested.length > 0 ? requested : agentProfile.defaultAgent
                        if (agentId.length === 0) {
                            editor.execNotice = "未指定 agent：请填写 agent，或在设置里选默认 agent"
                            return
                        }
                        const profile = agentProfile.profileOf(agentId)
                        if (!profile || !profile.exe) {
                            editor.execNotice = "未知 agent：" + agentId + "（请先在设置 → 任务执行 里配置）"
                            return
                        }
                        window.openExecConfirm({
                            taskId: editor.taskId,
                            agentId: agentId,
                            agentName: profile.displayName || agentId,
                            cwd: editor.execCwd,
                            command: agentProfile.commandLine(agentId, false, goalField.text.trim()),
                            acceptance: editor.linesOf(acceptanceField.text),
                            timeoutMin: editor.intOf(timeoutField.text, 30),
                            maxAttempts: editor.intOf(attemptsField.text, 1)
                        })
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
                        // 执行定义走侧表 task_exec：目标 / 验收 / 边界 / agent / workflow / cwd / 超时 / 轮数
                        const execSaved = taskExec.setExec(editor.taskId, {
                            goal: goalField.text.trim(),
                            acceptance: editor.linesOf(acceptanceField.text),
                            scope: editor.linesOf(scopeField.text),
                            agent: agentField.text.trim(),
                            workflow: editor.execWorkflow,
                            cwd: editor.execCwd,
                            timeoutMin: editor.intOf(timeoutField.text, 30),
                            maxAttempts: editor.intOf(attemptsField.text, 1)
                        })
                        if (!execSaved) {
                            editor.tabIndex = 1
                            editor.execNotice = "执行定义未保存：请检查工作目录"
                            return
                        }
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

    Loader {
        id: cardDetailLoader
        active: false
        sourceComponent: Component {
            CardDetailPanel {
                // 详情抽屉里的「打开编辑器」：先关抽屉再开编辑器，避免两个浮层并存
                onOpenEditor: function(taskId) { window.openTaskEditor(taskId) }
            }
        }
    }

    // 执行页签的工作目录选择：放在根层，避免嵌套弹窗时 parent window 解析问题
    FolderDialog {
        id: execCwdDialog
        title: "选择工作目录"
        onAccepted: {
            if (taskEditorLoader.item)
                taskEditorLoader.item.execCwd = taskEditorLoader.item.localPath(selectedFolder)
        }
    }

    // 执行前确认（R6-03）：agent / cwd / 完整命令行 / 验收项 / 超时，
    // 首次使用某个 cwd 必须在勾选「信任本目录」后才能确认；进程启动在 R6-04 接入。
    Dialog {
        id: execConfirmDialog
        parent: window.contentItem
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 20
        width: Math.min(560, window.width - 80)
        closePolicy: Popup.CloseOnEscape

        property string taskId: ""
        property string agentId: ""
        property string agentName: ""
        property string cwd: ""
        property string command: ""
        property var acceptance: []
        property int timeoutMin: 30
        property int maxAttempts: 1
        property bool cwdTrusted: false
        property bool trustChecked: false
        property string errorText: ""

        background: Rectangle {
            color: Theme.panelSolid
            radius: 16
            border.width: 1
            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
        }

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                text: "执行前确认"
                color: Theme.text
                font.pixelSize: 18
                font.bold: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10
                rowSpacing: 6

                Text {
                    text: "agent"
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Text {
                    Layout.fillWidth: true
                    text: execConfirmDialog.agentName + "（" + execConfirmDialog.agentId + "）"
                    color: Theme.text
                    font.pixelSize: 11
                }

                Text {
                    text: "工作目录"
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Text {
                    Layout.fillWidth: true
                    text: execConfirmDialog.cwd
                    color: execConfirmDialog.cwdTrusted ? Theme.text : Theme.warning
                    font.family: "Consolas"
                    font.pixelSize: 10
                    wrapMode: Text.WrapAnywhere
                }

                Text {
                    text: "预计耗时"
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Text {
                    Layout.fillWidth: true
                    text: "最多 " + execConfirmDialog.timeoutMin + " 分钟 · "
                          + execConfirmDialog.maxAttempts + " 轮"
                    color: Theme.text
                    font.pixelSize: 11
                }
            }

            Text {
                Layout.fillWidth: true
                text: "命令行（只读；执行时按 argv 传参，不经过 shell）"
                color: Theme.muted
                font.pixelSize: 10
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                radius: 8
                color: Theme.input
                border.width: 1
                border.color: Theme.border

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AsNeeded

                    TextArea {
                        id: execCommandText
                        readOnly: true
                        selectByMouse: true
                        text: execConfirmDialog.command
                        color: Theme.cyan
                        font.family: "Consolas"
                        font.pixelSize: 10
                        wrapMode: TextArea.WrapAnywhere
                        background: null
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: "验收项（执行后逐条判定）"
                color: Theme.muted
                font.pixelSize: 10
            }

            Column {
                Layout.fillWidth: true
                spacing: 2

                Repeater {
                    model: execConfirmDialog.acceptance

                    Text {
                        required property var modelData
                        width: parent.width
                        text: "· " + modelData
                        color: Theme.text
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: trustRow.implicitHeight + 16
                radius: 8
                color: Theme.card
                border.width: 1
                border.color: execConfirmDialog.cwdTrusted ? Theme.border : Theme.borderStrong

                RowLayout {
                    id: trustRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 8
                    spacing: 8

                    CheckBox {
                        id: trustCwdCheck
                        text: execConfirmDialog.cwdTrusted
                              ? "本目录已在信任白名单"
                              : "信任本目录（写入 cwd 白名单）"
                        checked: execConfirmDialog.cwdTrusted || execConfirmDialog.trustChecked
                        enabled: !execConfirmDialog.cwdTrusted
                        onToggled: execConfirmDialog.trustChecked = checked

                        indicator: Rectangle {
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 4
                            color: trustCwdCheck.checked
                                   ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
                                   : Theme.input
                            border.width: 1
                            border.color: trustCwdCheck.hovered ? Theme.cyan : Theme.border

                            Text {
                                anchors.centerIn: parent
                                visible: trustCwdCheck.checked
                                text: "✓"
                                color: Theme.text
                                font.pixelSize: 10
                            }
                        }

                        contentItem: Text {
                            leftPadding: 6
                            text: trustCwdCheck.text
                            color: trustCwdCheck.enabled ? Theme.text : Theme.muted
                            font.pixelSize: 10
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "信任后该目录不再重复确认"
                        color: Theme.muted
                        font.pixelSize: 9
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: execConfirmDialog.errorText.length > 0
                text: execConfirmDialog.errorText
                color: Theme.danger
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: "确认后在本机启动 agent；执行完最多留下本地改动，不自动推送。"
                    color: Theme.muted
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                }

                Button {
                    text: "取消"
                    onClicked: execConfirmDialog.close()
                }

                Button {
                    text: "确认执行"
                    enabled: execConfirmDialog.cwdTrusted || execConfirmDialog.trustChecked
                    onClicked: {
                        if (!execConfirmDialog.cwdTrusted
                                && !agentProfile.trustCwd(execConfirmDialog.cwd)) {
                            execConfirmDialog.errorText =
                                "无法信任该目录：请检查目录是否存在"
                            return
                        }
                        // R6-06：真正拉起执行器。start 按执行定义取 agent/cwd/prompt，
                        // 同卡已有活跃 run 会被 busy 拒绝（M1 不排队）。
                        const r = taskRunner.start(execConfirmDialog.taskId)
                        if (r && r.ok) {
                            if (taskEditorLoader.item)
                                taskEditorLoader.item.execNotice =
                                    "已启动执行（run " + String(r.runId).substring(0, 8) + "…），结果见下方执行态"
                            execConfirmDialog.close()
                            window.openExecPanel(execConfirmDialog.taskId)
                        } else {
                            execConfirmDialog.errorText =
                                (r && r.message) ? r.message : "执行启动失败"
                        }
                    }
                }
            }
        }
    }

    // 执行面板（R6-06）：run 终态回显 + 人工决策入口。
    // 硬约束：程序不自动改看板列、不自动 push ——「标为已办结」是人点了才走
    // taskUndo.moveTask 的既有完成路径（保留撤销）；重试 = 新建一条 run（attempt+1）。
    Dialog {
        id: execPanelDialog
        parent: window.contentItem
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 20
        width: Math.min(620, window.width - 80)
        closePolicy: Popup.CloseOnEscape

        property string taskId: ""
        property var summary: ({})
        // R7-06 成本账：本卡累计运行耗时 + 执行次数
        property var costTotalMs: 0
        property int costRuns: 0

        function refresh() {
            summary = taskRuns.summaryOf(taskId, 30)
            var cost = taskRuns.costSummary(taskId)
            costTotalMs = cost ? (cost.totalCostMs || 0) : 0
            costRuns = cost ? (cost.runs || 0) : 0
        }

        // ms → 「3 分 20 秒」/「45 秒」/「1 小时 5 分」
        function humanCost(ms) {
            if (ms <= 0)
                return "—"
            var s = Math.round(ms / 1000)
            if (s < 60)
                return s + " 秒"
            var m = Math.floor(s / 60)
            if (m < 60)
                return s % 60 > 0 ? m + " 分 " + (s % 60) + " 秒" : m + " 分"
            return Math.floor(m / 60) + " 小时 " + (m % 60) + " 分"
        }

        onOpened: refresh()

        background: Rectangle {
            color: Theme.panelSolid
            radius: 16
            border.width: 1
            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
        }

        contentItem: ColumnLayout {
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "执行面板"
                    color: Theme.text
                    font.pixelSize: 18
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                // 结论徽章：通过 / 失败或受阻 / 取消 / 执行中
                Rectangle {
                    visible: execPanelDialog.summary.hasRun === true
                    height: 22
                    width: execBadgeText.implicitWidth + 16
                    radius: 6
                    color: Qt.rgba(execBadgeText.color.r, execBadgeText.color.g,
                                   execBadgeText.color.b, 0.14)
                    border.width: 1
                    border.color: Qt.rgba(execBadgeText.color.r, execBadgeText.color.g,
                                          execBadgeText.color.b, 0.34)

                    Text {
                        id: execBadgeText
                        anchors.centerIn: parent
                        text: {
                            var s = execPanelDialog.summary.status || ""
                            if (s === "passed") return "✔ 通过"
                            if (s === "blocked") return "✘ 失败/受阻"
                            if (s === "cancelled") return "○ 已取消"
                            return "◌ 执行中"
                        }
                        color: {
                            var s = execPanelDialog.summary.status || ""
                            if (s === "passed") return Theme.success
                            if (s === "blocked") return Theme.danger
                            if (s === "cancelled") return Theme.muted
                            return Theme.cyan
                        }
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: execPanelDialog.summary.hasRun !== true
                text: "这张卡还没有执行记录。到 编辑 → 执行 页签发起一次执行。"
                color: Theme.muted
                font.pixelSize: 11
            }

            GridLayout {
                Layout.fillWidth: true
                visible: execPanelDialog.summary.hasRun === true
                columns: 2
                columnSpacing: 10
                rowSpacing: 4

                Text { text: "轮次"; color: Theme.muted; font.pixelSize: 11 }
                Text {
                    Layout.fillWidth: true
                    text: "attempt " + (execPanelDialog.summary.attempt || 1)
                    color: Theme.text
                    font.pixelSize: 11
                }

                Text { text: "耗时"; color: Theme.muted; font.pixelSize: 11 }
                Text {
                    Layout.fillWidth: true
                    text: (execPanelDialog.summary.durationSec || 0) + " 秒"
                          + (execPanelDialog.summary.exitCode !== undefined
                             && execPanelDialog.summary.exitCode !== null
                             ? " · 退出码 " + execPanelDialog.summary.exitCode : "")
                    color: Theme.text
                    font.pixelSize: 11
                }

                // R7-06 成本账：本卡累计耗时（所有已结算 run 之和）
                Text {
                    text: "本卡累计"
                    color: Theme.muted
                    font.pixelSize: 11
                    visible: execPanelDialog.costTotalMs > 0
                }
                Text {
                    Layout.fillWidth: true
                    visible: execPanelDialog.costTotalMs > 0
                    text: execPanelDialog.humanCost(execPanelDialog.costTotalMs)
                          + (execPanelDialog.costRuns > 0
                             ? "（" + execPanelDialog.costRuns + " 次执行）" : "")
                    color: Theme.text
                    font.pixelSize: 11
                }

                Text {
                    text: "失败原因"
                    color: Theme.muted
                    font.pixelSize: 11
                    visible: (execPanelDialog.summary.lastError || "").length > 0
                }
                Text {
                    Layout.fillWidth: true
                    visible: (execPanelDialog.summary.lastError || "").length > 0
                    text: execPanelDialog.summary.lastError || ""
                    color: Theme.danger
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }

                Text {
                    text: "越界改动"
                    color: Theme.muted
                    font.pixelSize: 11
                    visible: (execPanelDialog.summary.scopeViolation || "").length > 0
                }
                Text {
                    Layout.fillWidth: true
                    visible: (execPanelDialog.summary.scopeViolation || "").length > 0
                    text: execPanelDialog.summary.scopeViolation || ""
                    color: Theme.warning
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                }
            }

            Text {
                Layout.fillWidth: true
                visible: (execPanelDialog.summary.logTail || "").length > 0
                text: "日志末尾"
                color: Theme.muted
                font.pixelSize: 10
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                visible: (execPanelDialog.summary.logTail || "").length > 0
                radius: 8
                color: Theme.input
                border.width: 1
                border.color: Theme.border

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true

                    TextArea {
                        readOnly: true
                        selectByMouse: true
                        text: execPanelDialog.summary.logTail || ""
                        color: Theme.text
                        font.family: "Consolas"
                        font.pixelSize: 10
                        wrapMode: TextArea.WrapAnywhere
                        background: null
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: "查看完整日志"
                    enabled: (execPanelDialog.summary.logPath || "").length > 0
                    onClicked: Qt.openUrlExternally("file:///" + execPanelDialog.summary.logPath)
                }

                // R7-02：从执行面板直接跳到队列（排队 / 在跑的都在这看）
                Button {
                    text: "执行队列"
                    visible: execPanelDialog.summary.hasRun === true
                    onClicked: {
                        execPanelDialog.close()
                        window.openExecQueue()
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "重试"
                    enabled: execPanelDialog.summary.hasRun === true
                             && execPanelDialog.summary.status !== "running"
                             && execPanelDialog.summary.status !== "queued"
                    onClicked: {
                        const r = taskRunner.start(execPanelDialog.taskId)
                        if (r && r.ok) {
                            execPanelDialog.refresh()
                        } else {
                            execNoticeText.text = (r && r.message) ? r.message : "重试启动失败"
                        }
                    }
                }

                Button {
                    text: "标为已办结"
                    enabled: execPanelDialog.summary.hasRun === true
                             && execPanelDialog.summary.status === "passed"
                    onClicked: {
                        // 人点了才走既有完成路径（taskUndo，可 Ctrl+Z 撤销）；
                        // 程序绝不自动改列。
                        taskUndo.moveTask(execPanelDialog.taskId, "done", -1)
                        execPanelDialog.close()
                    }
                }

                Button {
                    text: "关闭"
                    onClicked: execPanelDialog.close()
                }
            }

            Text {
                id: execNoticeText
                Layout.fillWidth: true
                visible: text.length > 0
                color: Theme.warning
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }
        }

        // run 结束 / 状态变化时刷新面板（面板开着才刷）
        Connections {
            target: taskRunner
            function onRunFinished(runId, detail) {
                if (execPanelDialog.opened)
                    execPanelDialog.refresh()
            }
        }
    }

    // 夜间批量（R7-05）：勾选多张待办卡一次性入队，给出「预计总时长 / 约几点完成」。
    //
    // 硬约束（与 R7-01 / R7-02 的分工一致）：
    //   - 入队只是把卡放进 R7-01 的 FIFO 队列（同卡已在队列中会被拒绝，不重复）；
    //   - 不改卡片所在列（沿用 ADR-0002）；单卡失败只把该 run 结算成 blocked，不阻断其余；
    //   - 只用 Theme.xxx 令牌，不写死颜色。
    // 注：卡面 Ctrl/Shift 直接多选需要改 BoardColumn/TaskCard（不在本卡 scope），
    // 这里用「勾选清单」实现「勾选多卡 → 排队」。
    Dialog {
        id: nightBatchDialog
        parent: window.contentItem
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 20
        width: Math.min(680, window.width - 80)
        closePolicy: Popup.CloseOnEscape

        // id -> true 的选中集合；estimate 是 scheduler.nightBatchEstimate 的结果
        property var selected: ({})
        property var estimate: ({})
        property string notice: ""

        function selectedIds() {
            var ids = []
            for (var k in selected)
                if (selected[k])
                    ids.push(k)
            return ids
        }

        function refreshEstimate() {
            estimate = scheduler.nightBatchEstimate(selectedIds(), 300)
        }

        function isSelected(id) {
            return selected[id] === true
        }

        function toggle(id) {
            var s = {}
            for (var k in selected)
                if (selected[k])
                    s[k] = true
            if (s[id])
                delete s[id]
            else
                s[id] = true
            selected = s
            refreshEstimate()
        }

        function humanDuration(sec) {
            if (sec <= 0)
                return "—"
            var m = Math.floor(sec / 60)
            if (m >= 60)
                return Math.floor(m / 60) + " 小时 " + (m % 60) + " 分"
            var s = Math.round(sec % 60)
            if (m <= 0)
                return s + " 秒"
            return s > 0 ? m + " 分 " + s + " 秒" : m + " 分"
        }

        function finishClock(sec) {
            if (sec <= 0)
                return ""
            var d = new Date(Date.now() + sec * 1000)
            return ("0" + d.getHours()).slice(-2) + ":" + ("0" + d.getMinutes()).slice(-2)
        }

        onOpened: {
            selected = ({})
            notice = ""
            refreshEstimate()
        }

        background: Rectangle {
            color: Theme.panelSolid
            radius: 16
            border.width: 1
            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
        }

        contentItem: ColumnLayout {
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "夜间批量执行"
                    color: Theme.text
                    font.pixelSize: 18
                    font.bold: true
                }

                Text {
                    text: "NIGHT BATCH"
                    color: Theme.muted
                    font.pixelSize: 10
                    font.letterSpacing: 1.2
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "已选 " + nightBatchDialog.selectedIds().length + " 张"
                    color: Theme.cyan
                    font.pixelSize: 11
                }
            }

            Text {
                Layout.fillWidth: true
                text: "勾选待办卡（每张卡会按它的 workflow 排队执行）；队列按 FIFO 顺序推进，"
                      + "并发受设置约束。单张失败只标 blocked，不影响其余。"
                color: Theme.muted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            // 待办卡清单：点整行切换选中
            ListView {
                id: batchList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(320, contentHeight)
                clip: true
                spacing: 2
                model: taskStore
                delegate: Rectangle {
                    width: batchList.width
                    height: (model.status === "todo") ? 32 : 0
                    visible: height > 0
                    color: rowMouse.containsMouse ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.08)
                                                  : "transparent"
                    radius: 6

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: nightBatchDialog.toggle(model.id)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 8

                        Rectangle {
                            width: 16
                            height: 16
                            radius: 4
                            color: nightBatchDialog.isSelected(model.id)
                                   ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.25) : "transparent"
                            border.width: 1
                            border.color: nightBatchDialog.isSelected(model.id) ? Theme.cyan : Theme.muted

                            Text {
                                anchors.centerIn: parent
                                visible: nightBatchDialog.isSelected(model.id)
                                text: "✓"
                                color: Theme.cyan
                                font.pixelSize: 10
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: model.title
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // 预计总时长：按各卡历史平均耗时（无历史则用全局均值 / 兜底）求和，按并发折算
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    text: "预计总时长：" + nightBatchDialog.humanDuration(
                              nightBatchDialog.estimate.totalSec || 0)
                    color: Theme.text
                    font.pixelSize: 11
                }

                Text {
                    visible: (nightBatchDialog.estimate.totalSec || 0) > 0
                    text: "约 " + nightBatchDialog.finishClock(nightBatchDialog.estimate.totalSec || 0) + " 完成"
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "并发 " + (nightBatchDialog.estimate.concurrency || scheduler.concurrency)
                    color: Theme.muted
                    font.pixelSize: 11
                }
            }

            Text {
                Layout.fillWidth: true
                visible: nightBatchDialog.notice.length > 0
                text: nightBatchDialog.notice
                color: Theme.warning
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: "清空选择"
                    enabled: nightBatchDialog.selectedIds().length > 0
                    onClicked: {
                        nightBatchDialog.selected = ({})
                        nightBatchDialog.refreshEstimate()
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "关闭"
                    onClicked: nightBatchDialog.close()
                }

                Button {
                    text: "加入夜间队列"
                    enabled: nightBatchDialog.selectedIds().length > 0
                    onClicked: {
                        var res = scheduler.enqueueNightBatch(nightBatchDialog.selectedIds(), {})
                        var queued = (res.queued || []).length
                        var rejected = (res.rejected || []).length
                        var msg = "已入队 " + queued + " 张"
                        if (rejected > 0)
                            msg += "，跳过 " + rejected + " 张（已在队列中或参数缺失）"
                        nightBatchDialog.notice = msg + "。可在「执行队列」面板查看进度。"
                        nightBatchDialog.selected = ({})
                        nightBatchDialog.refreshEstimate()
                    }
                }
            }
        }
    }

    // 执行队列（R7-02）：让用户看见「谁在排队、谁在跑、还要多久」并直接操作。
    //
    // 硬约束（与 R7-01 的分工一致）：
    //   - 只读 scheduler.queueView / estimateRemainingSec，操作全部转成 Scheduler 的
    //     pause / resume / reorder / skip —— 界面不碰数据库、不碰进程。
    //   - 队列是运行态，不是看板状态：调序 / 跳过都不改卡片所在列（沿用 ADR-0002）。
    //   - 只用 Theme.xxx 令牌，不写死颜色（AGENTS.md §10.1）。
    Dialog {
        id: execQueueDialog
        parent: window.contentItem
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 20
        width: Math.min(760, window.width - 80)
        closePolicy: Popup.CloseOnEscape

        property var rows: []
        property int concurrencyCount: 1
        property bool queuePaused: false
        property int estimateSec: 0
        // 最近一次操作的提示（已跳过 / 已到队首等），避免静默失败
        property string notice: ""

        function refresh() {
            rows = scheduler.queueView(100)
            concurrencyCount = scheduler.concurrency
            queuePaused = scheduler.paused
            estimateSec = scheduler.estimateRemainingSec(300)
        }

        // 秒 → 「12 分 30 秒」/「约 01:20 完成」
        function humanDuration(sec) {
            if (sec <= 0)
                return "—"
            if (sec < 60)
                return sec + " 秒"
            var m = Math.floor(sec / 60)
            var s = Math.round(sec % 60)
            if (m >= 60)
                return Math.floor(m / 60) + " 小时 " + (m % 60) + " 分"
            return s > 0 ? m + " 分 " + s + " 秒" : m + " 分"
        }

        function finishClock(sec) {
            if (sec <= 0)
                return ""
            var d = new Date(Date.now() + sec * 1000)
            return ("0" + d.getHours()).slice(-2) + ":" + ("0" + d.getMinutes()).slice(-2)
        }

        // 已运行时长（秒）：started_at 为空表示还没开始跑
        function runDuration(row) {
            if (!row || !row.startedAt)
                return 0
            return Math.max(0, Math.floor((Date.now() / 1000) - row.startedAt))
        }

        function stateLabel(row) {
            var q = row.queueStatus || ""
            if (q === "running")
                return "执行中"
            if (q === "dispatching")
                return "启动中"
            return "排队中"
        }

        function stateColor(row) {
            var q = row.queueStatus || ""
            if (q === "running")
                return Theme.cyan
            if (q === "dispatching")
                return Theme.warning
            return Theme.muted
        }

        function waitingCount() {
            var n = 0
            for (var i = 0; i < rows.length; ++i) {
                if ((rows[i].queueStatus || "") === "waiting")
                    ++n
            }
            return n
        }

        onOpened: refresh()

        background: Rectangle {
            color: Theme.panelSolid
            radius: 16
            border.width: 1
            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
        }

        contentItem: ColumnLayout {
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "执行队列"
                    color: Theme.text
                    font.pixelSize: 18
                    font.bold: true
                }

                Text {
                    text: "EXEC QUEUE"
                    color: Theme.muted
                    font.pixelSize: 10
                    font.letterSpacing: 1.2
                }

                Item { Layout.fillWidth: true }

                // 暂停 / 恢复：只停派发，不杀在跑的 run
                Rectangle {
                    width: 62
                    height: 22
                    radius: 6
                    color: execQueueDialog.queuePaused
                           ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.18)
                           : Theme.accentSoft
                    border.width: 1
                    border.color: execQueueDialog.queuePaused ? Theme.warning : Theme.borderStrong

                    Text {
                        anchors.centerIn: parent
                        text: execQueueDialog.queuePaused ? "▶ 恢复" : "⏸ 暂停"
                        color: execQueueDialog.queuePaused ? Theme.warning : Theme.cyan
                        font.pixelSize: 10
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (execQueueDialog.queuePaused)
                                scheduler.resumeQueue()
                            else
                                scheduler.pauseQueue()
                            execQueueDialog.refresh()
                        }
                    }
                }
            }

            // 概览：并发 / 排队数 / 预计完成
            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                Text {
                    text: "并发 " + scheduler.running + "/" + execQueueDialog.concurrencyCount
                    color: Theme.text
                    font.pixelSize: 11
                }

                Text {
                    text: "排队 " + execQueueDialog.waitingCount()
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Text {
                    visible: execQueueDialog.queuePaused
                    text: "⏸ 队列已暂停，不会派发新的 run"
                    color: Theme.warning
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: execQueueDialog.estimateSec > 0
                          ? "预计 " + execQueueDialog.humanDuration(execQueueDialog.estimateSec)
                            + "，约 " + execQueueDialog.finishClock(execQueueDialog.estimateSec) + " 完成"
                          : ""
                    color: Theme.muted
                    font.pixelSize: 11
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }

            Text {
                Layout.fillWidth: true
                visible: execQueueDialog.rows.length === 0
                text: "队列为空。在卡片上点「执行」，排队中的 run 会出现在这里。"
                color: Theme.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            // 队列列表：waiting / dispatching / running
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(320, execQueueDialog.rows.length * 62 + 16)
                visible: execQueueDialog.rows.length > 0
                radius: 10
                color: Theme.panel
                border.width: 1
                border.color: Theme.border

                ListView {
                    id: execQueueList
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    clip: true
                    model: execQueueDialog.rows

                    ScrollBar.vertical: ScrollBar {
                        width: 6
                        policy: execQueueList.contentHeight > execQueueList.height
                                ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    }

                    delegate: Rectangle {
                        required property var modelData

                        width: execQueueList.width
                        height: 56
                        radius: 8
                        color: (modelData.queueStatus || "") === "running"
                               ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.10)
                               : Theme.card
                        border.width: 1
                        border.color: (modelData.queueStatus || "") === "running"
                                      ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.42)
                                      : Theme.border

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            // 状态点：只有 running 才脉冲（脉冲语义 = 正在跑）
                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: execQueueDialog.stateColor(modelData)
                                opacity: (modelData.queueStatus || "") === "running"
                                         ? 0.4 + 0.6 * itemPulse.value : 1.0

                                SequentialAnimation {
                                    running: (modelData.queueStatus || "") === "running"
                                    loops: Animation.Infinite
                                    NumberAnimation {
                                        target: itemPulse; property: "value"
                                        from: 0; to: 1; duration: 700
                                        easing.type: Easing.InOutSine
                                    }
                                    NumberAnimation {
                                        target: itemPulse; property: "value"
                                        from: 1; to: 0; duration: 700
                                        easing.type: Easing.InOutSine
                                    }
                                }

                                QtObject {
                                    id: itemPulse
                                    property real value: 0
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.title || modelData.taskId
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        var parts = [execQueueDialog.stateLabel(modelData)]
                                        if ((modelData.queueStatus || "") === "running")
                                            parts.push("已运行 " + execQueueDialog.humanDuration(
                                                           execQueueDialog.runDuration(modelData)))
                                        if ((modelData.role || "").length > 0)
                                            parts.push("角色 " + modelData.role)
                                        if (modelData.agent)
                                            parts.push(modelData.agent)
                                        if ((modelData.lastError || "").length > 0)
                                            parts.push(modelData.lastError)
                                        return parts.join(" · ")
                                    }
                                    color: Theme.muted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }

                            // 调序 / 跳过：只对 waiting 生效（running 的不许被打断）
                            Text {
                                visible: (modelData.queueStatus || "") === "waiting"
                                text: "↑"
                                color: upMouse.containsMouse ? Theme.cyan : Theme.muted
                                font.pixelSize: 13
                                MouseArea {
                                    id: upMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (!scheduler.reorder(modelData.id, -1))
                                            execQueueDialog.notice = "已经是最前面一条"
                                        execQueueDialog.refresh()
                                    }
                                }
                            }

                            Text {
                                visible: (modelData.queueStatus || "") === "waiting"
                                text: "↓"
                                color: downMouse.containsMouse ? Theme.cyan : Theme.muted
                                font.pixelSize: 13
                                MouseArea {
                                    id: downMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (!scheduler.reorder(modelData.id, 1))
                                            execQueueDialog.notice = "已经是最后面一条"
                                        execQueueDialog.refresh()
                                    }
                                }
                            }

                            Text {
                                visible: (modelData.queueStatus || "") === "waiting"
                                text: "跳过"
                                color: skipMouse.containsMouse ? Theme.danger : Theme.muted
                                font.pixelSize: 10
                                MouseArea {
                                    id: skipMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (!scheduler.skip(modelData.id))
                                            execQueueDialog.notice = "该条已不在等待状态"
                                        else
                                            execQueueDialog.notice =
                                                "已跳过：" + (modelData.title || "")
                                        execQueueDialog.refresh()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: execQueueDialog.notice.length > 0
                text: execQueueDialog.notice
                color: Theme.warning
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }

            // 底部：说明 + 关闭
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: "队列顺序只影响运行态，不改卡片所在列；跳过 / 失败都不会自动把卡标为已办结。"
                    color: Theme.muted
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }

                Button {
                    text: "关闭"
                    onClicked: execQueueDialog.close()
                }
            }
        }

        // 队列变化（入队 / 派发 / 结束 / 暂停）时刷新；面板开着才刷
        Connections {
            target: scheduler
            function onChanged() {
                if (execQueueDialog.opened)
                    execQueueDialog.refresh()
            }
        }

        // 每秒刷一次「已运行时长」：不依赖 revision，避免心跳把整个面板重建
        Timer {
            interval: 1000
            repeat: true
            running: execQueueDialog.opened
            onTriggered: {
                if (execQueueDialog.opened)
                    execQueueDialog.refresh()
            }
        }
    }

    Component {
        id: taskComposerComponent
        TaskComposer {}
    }

    Loader {
        id: taskComposerLoader
        active: false
        sourceComponent: taskComposerComponent
    }

    // 光轨：拖影后面拖一条渐隐的霓虹尾巴。
    // 用单个 Canvas 而不是一串 Rectangle —— 连续拖 20 次也不会新增任何动画对象，
    // 采样点存在 JS 数组里，超出上限就丢最旧的（这正是「无残留」那条验收的做法）。
    Canvas {
        id: dragTrail
        z: 299
        anchors.fill: parent
        // 减弱动效 / 低画质档不画：这两档下任何拖尾都只是噪声
        visible: (window.dragActive || window.dragSettling) && !Motion.reduced && Theme.quality > 0
        opacity: window.dragActive ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: dragFeel.settleDuration(Motion.reduced) }
        }

        property var points: []

        function feed(x, y) {
            var next = points.slice()
            next.unshift({ x: x, y: y })
            if (next.length > 10)
                next.length = 10
            points = next
            requestPaint()
        }

        function reset() {
            if (points.length === 0)
                return
            points = []
            requestPaint()
        }

        onPaint: {
            var pts = points
            if (pts.length < 2)
                return
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.lineCap = "round"
            var n = pts.length
            for (var i = 0; i < n - 1; ++i) {
                var k = 1 - i / n
                ctx.beginPath()
                ctx.moveTo(pts[i].x, pts[i].y)
                ctx.lineTo(pts[i + 1].x, pts[i + 1].y)
                ctx.lineWidth = 9 * k
                ctx.strokeStyle = Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20 * k)
                ctx.stroke()
            }
        }
    }

    Rectangle {
        id: dragGhost
        visible: window.dragActive || window.dragSettling
        width: Math.min(320, dragGhostText.implicitWidth + 40)
        height: 54
        radius: 12
        color: Theme.ghost
        border.width: 1
        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b,
                              window.dragSnapY >= 0 ? 1.0 : 0.75)
        z: 300
        x: window.dragX - width / 2
        // 磁吸：贴上插入线就纵向吸过去；没贴上时严格跟手（Behavior 关掉，否则会橡皮筋）
        y: (window.dragSnapY >= 0 ? window.dragSnapY : window.dragY) - height / 2
        opacity: window.dragActive ? 1 : 0
        // 拖起放大：从列表里被拎起来的一瞬间
        scale: 1.04
        transformOrigin: Item.Center

        Behavior on scale {
            NumberAnimation { duration: Motion.dragLift; easing.type: Easing.OutCubic }
        }
        Behavior on opacity {
            NumberAnimation { duration: dragFeel.settleDuration(Motion.reduced) }
        }
        Behavior on y {
            enabled: window.dragSnapY >= 0 || !window.dragActive
            NumberAnimation { duration: Motion.dragSnap; easing.type: Easing.OutCubic }
        }
        Behavior on border.color {
            ColorAnimation { duration: Motion.instant }
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

    // Triage 完成（R6-07）：编辑面板开着且是同一张卡时刷新草案回显
    Connections {
        target: triageRunner
        function onTriageFinished(taskId, result) {
            if (taskEditorLoader.item) {
                taskEditorLoader.item.triageRunning = false
                if (taskEditorLoader.item.taskId === taskId)
                    taskEditorLoader.item.triageDraft =
                        triageRunner.draftOf(taskId) || ({})
            }
        }
    }
}
