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

    // R11-8：活跃热力横幅默认收起（回顾性数据不该常驻吃掉顶区），点「展开」再铺开
    property bool heatmapCollapsed: true

    // 首启动引导：仅在「从未完成引导」且看板为空时自动出现
    property bool onboardVisible: false
    // 「重看新手引导」开出来的：不受自动判定影响，关掉即止
    property bool onboardManual: false

    // 引导该不该出现，必须跟着任务数重算，而不是启动瞬间算一次就固化。
    // 启动期 TaskModel::load() 会被调两次（第二次来自 RoadmapSync::importFrom 末尾）：
    // 空库首启实测 0 条 → 426 ms 后 139 条。若只在 Component.onCompleted 判定一次，
    // 一旦那一刻还没就绪，「尚未就绪」就被当成「空板」，把三步说明塞给有数据的用户，
    // 而且数据后来填回来也不会收起。
    function refreshOnboard(verbose) {
        if (window.onboardManual)
            return
        const next = uiSettings.shouldOnboard(taskStore.count)
        if (next !== window.onboardVisible || verbose)
            console.info("ONBOARD", next ? "show" : "skip", "tasks", taskStore.count,
                         "done", uiSettings.onboardingDone)
        window.onboardVisible = next
    }

    function finishOnboarding() {
        window.onboardVisible = false
        window.onboardManual = false
        uiSettings.onboardingDone = true
    }

    // 命令面板里的「重看新手引导」：不改完成标记，只是把浮层再开一次
    function replayOnboarding() {
        onboarding.restart()
        window.closeAllPanels()
        window.onboardManual = true
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
            neonBgLayer.burst()
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
    function columnItems() { return boardChrome.columnItems() }

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
    //
    // R11-10：外观面板从标题栏下方的浮层改成主区右侧的抽屉，于是拆成两层：
    //   closeOtherPanels —— 关掉除外观抽屉以外的浮层（打开外观抽屉时用）
    //   closeAllPanels   —— 全关（含外观抽屉）
    function closeOtherPanels() {
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
        // 详情抽屉（R8-03）与主题市场（R8-02）：与其它浮层同一套互斥纪律
        if (cardDetailLoader.item)
            cardDetailLoader.item.close()
        if (themeMarketLoader.item)
            themeMarketLoader.item.close()
    }

    function closeAppearancePanel() {
        boardChrome.closeThemePicker()
        if (appearanceLoader.item)
            appearanceLoader.item.visible = false
    }

    function closeAllPanels() {
        closeOtherPanels()
        closeAppearancePanel()
    }

    // 外观抽屉开合同步：标题栏的主题三点按钮发 appearanceRequested 过来，
    // 这里按 themePickerOpen 的值开合抽屉（抽屉自己关掉时也会走这里）。
    function syncAppearancePanel() {
        if (!boardChrome.themePickerOpen) {
            if (appearanceLoader.item)
                appearanceLoader.item.visible = false
            return
        }
        closeOtherPanels()
        appearanceLoader.active = true
        if (appearanceLoader.item)
            appearanceLoader.item.visible = true
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
        boardChrome.openThemePicker()
        syncAppearancePanel()
    }

    // 打开主题市场（R8-02）：浏览 / 安装 / 卸载主题包
    function openThemeMarket() {
        show()
        raise()
        requestActivate()
        closeAllPanels()
        themeMarketLoader.active = true
        if (themeMarketLoader.item) {
            themeMarketLoader.item.hostWindow = window
            themeMarketLoader.item.open()
        }
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
        add("打开主题市场", "面板", "浏览 / 安装 / 卸载主题包", function() { window.openThemeMarket() })
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
        boardChrome.clearSearch()
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
    // 拖拽被 Esc 取消：鼠标还按着、但这次拖拽已经作废。
    // 之后所有 dragMoved / dragEnded 都直接丢弃 —— 松手不能落库，也不能进撤销栈。
    property bool dragAborted: false
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

    // 一键清除筛选：搜索词、里程碑、项目范围三处一起复位
    function clearFilters() { boardChrome.clearSearch(); setStageFilter(""); setProjectFilter("") }

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

    // 键盘跨列移动（R8-06）：选中 → Ctrl+←/→ → 卡落到相邻列的同一相对位置。
    //
    // 落位规则与鼠标拖拽共用一套：KeyMove 只算「去第几列 / 落第几位」，
    // 真正写库仍然走 taskUndo，所以 Ctrl+Z 的行为和拖拽完全一致。
    // 边界（已经在最左/最右）**不移动也不进撤销栈** —— 否则按一下 Ctrl+Z 像没反应。
    function moveCardAcrossColumns(taskId, delta) {
        if (!taskId || taskId.length === 0)
            return false
        var task = taskStore.taskById(taskId)
        if (!task || !task.id)
            return false
        var cols = columnItems()
        var current = -1
        for (var i = 0; i < cols.length; ++i) {
            if (cols[i].columnId === task.columnId) {
                current = i
                break
            }
        }
        if (current < 0)
            return false
        var target = keyMove.stepFor(current, delta, cols.length)
        if (target < 0)
            return false
        var fromIds = cols[current].taskIds()
        var fromIndex = fromIds.indexOf(taskId)
        var targetCol = cols[target]
        var targetIds = targetCol.taskIds()
        var index = keyMove.insertIndexFor(fromIndex, targetIds.length)
        var beforeId = index < targetIds.length ? targetIds[index] : ""
        if (!taskUndo.moveTaskBeforeColumn(taskId, targetCol.columnId, beforeId))
            return false
        // 选中跟着卡走：连按两次 Ctrl+→ 能一路搬到第三列，不用重新选
        if (cols[current] !== targetCol) {
            cols[current].selectedId = ""
            cols[current].selectionChanged("")
        }
        targetCol.selectId(taskId)
        targetCol.focusList()
        // 读屏播报：键盘移动没有指针位置可循，结果必须主动念出来
        keyMove.announce("已移到「" + targetCol.title + "」第 " + (index + 1) + " 位，Ctrl+Z 可撤销")
        return true
    }

    function beginTaskDrag(id, title, status, x, y) {
        dragTaskId = id
        dragTaskTitle = title
        dragTaskStatus = status
        dragX = x
        dragY = y
        dragSnapY = -1
        dragSettling = false
        dragAborted = false
        dragOverColumnId = boardColumns.columnIdForStatus(status)
        dragActive = true
        dragTrail.reset()
        dragTrail.feed(x, y)
    }

    // Esc 取消拖拽（R8-06）：卡回原位、拖影消失、**一条撤销记录都不留**。
    // 拖拽期间数据库一个字节都没动过（只在松手时写），所以「取消」等价于
    // 把界面状态丢掉 —— 这也正是「取消不该可撤销」的实现方式：没有可回滚的东西。
    function cancelTaskDrag() {
        if (!dragActive)
            return false
        dragAborted = true
        dragActive = false
        dragSettling = false
        dragSnapY = -1
        dragOverColumnId = ""
        dragTaskId = ""
        dragTaskTitle = ""
        dragTaskStatus = ""
        dragTrail.reset()
        clearDropMarks()
        keyMove.announce("已取消拖拽，卡片回到原位")
        return true
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
        // 取消之后鼠标可能还按着继续移动：丢弃，别把指示线又画出来
        if (dragAborted)
            return
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
        if (dragAborted) {
            // Esc 已经取消过：松手只是把鼠标放开，不许落库、不许进撤销栈
            dragAborted = false
            return
        }
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

    // Esc 取消拖拽（R8-06）：鼠标按着也能按，卡回原位且不留撤销记录。
    // 只在拖拽中启用 —— 弹层有自己的 CloseOnEscape，不能被这条抢掉。
    Shortcut {
        sequence: "Escape"
        enabled: window.dragActive
        onActivated: window.cancelTaskDrag()
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
            neonBgLayer.burst()
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

    // 任务数一变就重算引导的显隐（R13-01）：路线图同步会在启动后把模型整体
    // 重建一次，count 从 0 跳到实际条数；不重算的话，「还没就绪」会被当成「空板」。
    Connections {
        target: taskStore
        function onCountsChanged() {
            window.refreshOnboard(false)
        }
    }

    Component.onCompleted: {
        refreshStageData()
        setStageFilter(initialStage)
        pluginLoader.scan()
        // 判定收在 UiSettings::shouldOnboard（未完成过 且 空板）：老用户换机重装
        // 也该直接看到自己的看板，而不是被三步说明挡在门外。
        // 这里只是第一次算；之后每次 countsChanged 都会重算（见下方 Connections）。
        window.refreshOnboard(true)
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

        NeonBackgroundLayer {
            id: neonBgLayer
            anchors.fill: parent
            hostWin: window
        }

        // 专注模式提示：背景已压暗成追光，告知退出方式
        FocusBadge {
            focusMode: window.focusMode
        }

        // 外观抽屉展开时，点击标题栏即关闭（抽屉本体在主区，不受这条影响）
        MouseArea {
            anchors.fill: parent
            z: 5
            visible: boardChrome.themePickerOpen
            onClicked: window.closeAppearancePanel()
        }

        BoardChrome {
            id: boardChrome
            anchors.fill: parent

            hostWin: window
            columnManagerRef: columnManager
            dragSpaceRef: root
        }

        // 插件面板：按需加载。R11-10：位置统一为右侧抽屉，
        // 尺寸与定位由 DrawerShell 负责（Loader 只给承载区）。
        Loader {
            id: pluginPanelLoader
            anchors.fill: parent
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
                // R11-09：弹窗壳要挂在窗口的 overlay 上，宿主窗口由这里注入
                item.hostWindow = window
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
                // R11-09：弹窗壳要挂在窗口的 overlay 上，宿主窗口由这里注入
                item.hostWindow = window
                if (window.settingsPending) {
                    window.settingsPending = false
                    item.open()
                }
                item.openLibraryRequested.connect(function() {
                    window.openPluginLibrary()
                })
            }
        }

        // 外观抽屉（R11-10）：原先由标题栏的 ThemePicker 内部 Loader 承载，
        // 位置锚在标题栏下方；现在统一为主区右侧的抽屉。
        // 开合由抽屉根的 visible 控制（AppearancePanel 用 visible 触发入场动画），
        // 抽屉自己关掉时发 closeRequested，这里把 visible 收回去。
        Loader {
            id: appearanceLoader
            anchors.fill: parent
            active: false
            source: "AppearancePanel.qml"
            z: 155

            onLoaded: {
                item.closeRequested.connect(function() {
                    window.closeAppearancePanel()
                })
                item.visible = boardChrome.themePickerOpen
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
                // R11-09：弹窗壳要挂在窗口的 overlay 上，宿主窗口由这里注入
                item.hostWindow = window
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
        // R11-10：改用 DialogShell 后是 Popup，不再铺满整块区域
        OnboardingOverlay {
            id: onboarding
            hostWindow: window
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
        // R11-10：改用 DialogShell 后是 Popup，不再铺满整块区域
        CommandPalette {
            id: commandPalette
            hostWindow: window
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
                // R11-09：弹窗壳要挂在窗口的 overlay 上，宿主窗口由这里注入
                item.hostWindow = window
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

    // 任务编辑器（R6）：整迁至 qml/components/TaskEditorDialog.qml（MOD-17），按需加载语义不变
    Component {
        id: taskEditorComponent
        TaskEditorDialog {}
    }

    Loader {
        id: taskEditorLoader
        active: false
        sourceComponent: taskEditorComponent
        onLoaded: item.hostWin = window
    }

    Loader {
        id: themeMarketLoader
        active: false
        sourceComponent: Component { ThemeMarketPanel {} }
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

    ExecConfirmDialog {
        id: execConfirmDialog
        hostWin: window
        editorLoader: taskEditorLoader
    }
    ExecPanelDialog {
        id: execPanelDialog
        hostWin: window
    }

    NightBatchDialog {
        id: nightBatchDialog
        hostWin: window
    }

    // 执行队列（R7-02）：让用户看见「谁在排队、谁在跑、还要多久」并直接操作。
    //
    // 硬约束（与 R7-01 的分工一致）：
    //   - 只读 scheduler.queueView / estimateRemainingSec，操作全部转成 Scheduler 的
    //     pause / resume / reorder / skip —— 界面不碰数据库、不碰进程。
    //   - 队列是运行态，不是看板状态：调序 / 跳过都不改卡片所在列（沿用 ADR-0002）。
    //   - 只用 Theme.xxx 令牌，不写死颜色（AGENTS.md §10.1）。
    ExecQueueDialog { id: execQueueDialog }

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
