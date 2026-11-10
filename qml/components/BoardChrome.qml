import QtQuick
import QtQml
import QtQuick.Controls
import QtQuick.Layouts

// 标题栏 + 主看板区（MOD-15 自 Main.qml 原样迁入）：
// hostWin = 宿主窗口（原 window）；columnManagerRef = 原 columnManager 面板；
// dragSpaceRef = 原 root Rectangle（卡片拖拽坐标空间）。
// MOD-25 收敛：Main 不再摸内部元素——搜索清空 / 列项扫描 / 外观抽屉开关
// 走下方语义接口（原 searchField / columnRepeater / themePicker alias 已删）。
Item {
    property var hostWin
    property var columnManagerRef
    property Item dragSpaceRef

    // ---- 语义接口（原手工 alias，MOD-25）----

    // 清空搜索框（原 Main 直接 searchField.text = ""）
    function clearSearch() {
        searchField.text = ""
    }

    // 已实例化的列项（原 Main 遍历 columnRepeater.itemAt；顺序即界面顺序）
    function columnItems() {
        var out = []
        for (var i = 0; i < columnRepeater.count; ++i) {
            var item = columnRepeater.itemAt(i)
            if (item)
                out.push(item)
        }
        return out
    }

    // 外观抽屉开关（原 Main 读写 themePicker.open）
    readonly property bool themePickerOpen: themePicker.open
    function openThemePicker() {
        themePicker.open = true
    }
    function closeThemePicker() {
        themePicker.open = false
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
                hostWin.startSystemMove()
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
            onCloseRequested: hostWin.hide()
        }

        ThemePicker {
            id: themePicker
            anchors.right: windowButtons.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            // R11-8：设置入口从侧栏迁到标题栏右上角齿轮（Ctrl+, 不变）
            onSettingsRequested: hostWin.openSettings()
            // R11-10：外观面板从标题栏下方的浮层改成主区右侧抽屉，
            // 主题三点按钮只发意图，实际开合由 Main.qml 同步抽屉状态
            onAppearanceRequested: hostWin.syncAppearancePanel()
        }

        // R11-02：标题栏按钮统一走 TopToolButton —— 原先「全景」「＋ 新建」
        // 是同一套样式各抄一遍（圆角 / 边框 alpha / 悬浮底色硬编码），改主题不跟随。
        TopToolButton {
            id: mapButton
            anchors.right: focusButton.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: "全景"
            description: "打开项目全景面板"
            onClicked: hostWin.openProjectMap()
        }

        // R11-8：专注按钮从主区顶栏迁到标题栏 —— 它是全局动作（压暗背景 + 倒计时环），
        // 与设置、全景同级，而不是看板区专属。主区顶栏因此只剩「标题 + 副标题 + 统计带」。
        Rectangle {
            id: focusButton
            anchors.right: themePicker.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 52
            height: 26
            radius: Theme.radiusControl
            color: focusBtnMouse.containsMouse || hostWin.focusMode
                   ? Theme.accentSoft
                   : Theme.input
            border.width: 1
            border.color: hostWin.focusMode
                          ? Qt.rgba(Theme.magenta.r, Theme.magenta.g, Theme.magenta.b, 0.45)
                          : Theme.border

            Text {
                anchors.centerIn: parent
                text: hostWin.focusMode ? "专注中" : "专注"
                color: hostWin.focusMode ? Theme.magenta : Theme.accent
                font.pixelSize: 11
            }

            MouseArea {
                id: focusBtnMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (hostWin.focusMode)
                        hostWin.toggleFocusPause()
                    else
                        focusMenu.open()
                }
            }

            Accessible.role: Accessible.Button
            Accessible.name: "专注模式"
        }

        TopToolButton {
            id: newTaskButton
            anchors.right: mapButton.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: "＋ 新建"
            emphasis: true
            description: "新建任务"
            onClicked: hostWin.openTaskComposer("todo")
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
            // R11-8：插件库常驻卡片移除后侧栏只剩导航 + 槽位 + 状态行，220 → 200
            Layout.preferredWidth: Math.min(200, Math.max(160, hostWin.width * 0.24))
            Layout.fillHeight: true
            color: Theme.panel
            border.width: 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                // R11-8：侧栏顶部直接是导航 —— 原先这里有一块「工作台 / PERSONAL
                // WORKBENCH」标题，与主区顶栏的「工作台」重复且零信息量（约 60 px）。
                Repeater {
                    // 「队列」所有实例都有（队列是本机运行态，沙箱实例同样要能看自己的队列）；
                    // 「预览」只在主线实例出现：agent 沙箱实例里再嵌套预览入口只会混淆。
                    // R11-8：设置移出侧栏，改标题栏右上角齿轮按钮 —— 它是低频全局操作，
                    // 占侧栏一整行不划算；导航下标相应前移一位（插件 1 / 队列 2 / 预览 3）。
                    model: previewHub.hostInstance
                           ? ["看板", "插件", "队列", "预览"]
                           : ["看板", "插件", "队列"]

                    delegate: Rectangle {
                        Layout.fillWidth: true
                        height: 34
                        radius: 8
                        color: (index === 0 || (index === 2 && hostWin.execQueueOpen)
                                || (index === 3 && hostWin.previewOpen))
                               ? Theme.accentSoft
                               : (navMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.04)
                                                         : "transparent")
                        border.width: (index === 0 || (index === 2 && hostWin.execQueueOpen)
                                       || (index === 3 && hostWin.previewOpen)) ? 1 : 0
                        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.22)

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            color: (index === 0 || (index === 2 && hostWin.execQueueOpen)
                                    || (index === 3 && hostWin.previewOpen))
                                   ? Theme.cyan : Theme.muted
                            font.pixelSize: 13
                        }

                        // 队列徽章：有 run 在排队 / 在跑时给出数量，不用点进去也知道
                        Rectangle {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            visible: index === 2 && (scheduler.queueLength + scheduler.running) > 0
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
                                    hostWin.openPluginLibrary()
                                } else if (index === 2) {
                                    hostWin.openExecQueue()
                                } else if (index === 3) {
                                    hostWin.openPreview()
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

                // R11-8：侧栏不再常驻插件库卡片 —— 这块「插件库 8 + 已装列表 +
                // 刷新市场 / 重新扫描 / 打开目录 / 详情」与「插件」导航点开的插件库
                // 面板是同一份信息出现两处。浏览 / 安装 / 卸载归插件库面板，
                // 插件目录 / 市场源 / 重新扫描归设置 → 插件（见 R11-9）。

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
            // R11-8：8 → 6，顶区（标题行 / 工具行 / 横幅）三段各省 2 px
            spacing: 6

            // R11-01：顶区信息层级重排。
            // 原先这里是一行 48 px 高的「大标题 + 三块统计盒」，顶区垂直占用近 190 px，
            // 看板被压扁。现在标题降到 18 px 且与副标题同行，统计改成单行状态带，
            // 整行高度从 48 px 降到 28 px —— 省下的高度直接还给三列看板。
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 12

                RowLayout {
                    spacing: 8

                    Text {
                        text: "工作台"
                        color: Theme.text
                        font.pixelSize: Theme.fontDisplay
                        font.bold: true
                    }

                    Text {
                        text: "专注当下，逐项推进"
                        color: Theme.muted
                        font.pixelSize: Theme.fontCaption
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
            }

            // R11-8：专注按钮迁到标题栏后，时长菜单跟着从标题栏下方弹出
            Popup {
                id: focusMenu
                parent: hostWin.contentItem
                x: Math.max(20, focusButton.mapToItem(hostWin.contentItem, focusButton.width, 0).x - width)
                y: titleBar.height + 6
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
                                    hostWin.startFocus(modelData)
                                }
                            }
                        }
                    }
                }
            }

            // R11-8：工具行三合一 —— 项目切换 / 里程碑条 / 搜索框并成一行，
            // 顶区竖直占用因此从 264 px 收到 147 px（热力图收起态）。
            //
            // R10-05 的教训仍然成立：阶段条不能被挤成 0 宽（那次实测三个项目 chip
            // + 搜索框 + 阶段条在 980 px 里放不下，只剩「全部」一个胶囊）。这里的做法是
            // 给三者都留下限宽度，阶段条拿剩余空间、窄窗口下横向滚动（R11-03 的
            // Flickable），而不是被压扁变形。
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 12

                // 两级看板的第一级：项目范围（全局 / 单项目）。
                // 「看谁的事」比「看到哪一步」更外层，所以它排在最左。
                ProjectSwitcher {
                    id: projectSwitcher
                    // 宽度由内容决定（R10-05）：写死 230 会把第三个项目裁掉。
                    // 上限由组件自己的 maxWidth 兜底，项目再多也是滚动而不是挤压别的控件。
                    // R11-8：与阶段条同排后上限收窄到 320，给阶段条留出可用的宽度。
                    Layout.maximumWidth: Math.min(projectSwitcher.maxWidth, 320)
                    Layout.minimumWidth: 140
                    Layout.preferredHeight: 36
                    activeProject: hostWin.projectFilter
                    onProjectSelected: function(projectId) { hostWin.setProjectFilter(projectId) }
                }

                // 阶段条：横向滚动（13 个阶段），拿整行剩余宽度
                StageBar {
                    id: stageBar
                    Layout.fillWidth: true
                    Layout.minimumWidth: 120
                    Layout.preferredHeight: 36
                    stages: hostWin.stageList
                    activeStage: hostWin.stageFilter
                    syncText: hostWin.roadmapSyncText
                    syncOk: hostWin.roadmapSyncOk
                    onStageSelected: function(stage) { hostWin.setStageFilter(stage) }
                }

                Rectangle {
                    Layout.preferredWidth: 232
                    Layout.minimumWidth: 120
                    Layout.preferredHeight: 28
                    Layout.alignment: Qt.AlignVCenter
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
                        font.pixelSize: Theme.fontBody
                        background: null
                        onTextChanged: boardColumns.search = text
                        Keys.onEscapePressed: text = ""
                    }

                    // Ctrl K 提示：命令面板的入口此前完全不可见
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !searchField.activeFocus && searchField.text.length === 0
                        text: "Ctrl K"
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                    }
                }
            }

            // 活跃横幅：热力图 + 连续天数 + 周吞吐（数据来自 taskStats，只读任务表）
            // R11-01：高度 84 → 72（热力格 8 → 7 px，横幅内边距 9 → 6），
            // 顶区总高度由此再省 12 px 给看板。
            // R11-8：可收起 —— 回顾性数据默认收起成 26 px 摘要条，想看时点「展开」。
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.preferredHeight: hostWin.heatmapCollapsed ? 26 : 72
                radius: Theme.radiusPanel
                color: Theme.panel
                border.width: 1
                border.color: Theme.hairline

                ActivityHeatmap {
                    id: heatmap
                    anchors.fill: parent
                    anchors.margins: hostWin.heatmapCollapsed ? 4 : 6
                    collapsed: hostWin.heatmapCollapsed
                    cells: taskStats.heatmap
                    weekly: taskStats.weekly
                    streak: taskStats.streakDays
                    totalDone: taskStats.totalDone
                    bestDay: taskStats.bestDay
                    onToggleRequested: hostWin.heatmapCollapsed = !hostWin.heatmapCollapsed
                }
            }

            // R11-13：筛选态空态要浮在看板正中间，但 EmptyState 原本挂在窗口级容器
            // （本 Rectangle）上，而 boardRow 是它的三层后代 —— QML 只允许锚到父级或兄弟，
            // anchors.centerIn: boardRow 必然失效：引擎每次启动打一条告警并忽略这条锚定，
            // 空态面板停在窗口左上角 (0,0)，而不是看板正中。
            // 这里给看板区包一层普通 Item，让 EmptyState 与 boardRow 变成同一父级下的兄弟，
            // 锚定恢复合法；列的边距 / 间距数值一律沿用原值，布局不变。
            Item {
                id: boardArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 20

                RowLayout {
                    id: boardRow
                    anchors.fill: parent
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
                            stageFilter: hostWin.stageFilter
                            projectFilter: hostWin.projectFilter
                            dragSpace: dragSpaceRef
                            dropTarget: hostWin.dragOverColumnId === boardColumnId
                            onColumnMoveRequested: function(targetIndex) { hostWin.focusColumn(targetIndex) }
                            // Ctrl+←/→：把选中卡搬到相邻列（落位规则与拖拽同一套）
                            onCardMoveRequested: function(taskId, delta) {
                                hostWin.moveCardAcrossColumns(taskId, delta)
                            }
                            // 双击 / 回车 / 无障碍「按下」都走详情抽屉（R8-03）：
                            // 先让人看清楚再决定改什么，编辑器从抽屉里一键进入
                            onEditRequested: function(taskId) { hostWin.openCardDetail(taskId) }
                            onStartRequested: function(taskId) { taskUndo.moveTask(taskId, "doing", -1) }
                            onCompleteRequested: function(taskId) { taskUndo.moveTask(taskId, "done", -1) }
                            onRestoreRequested: function(taskId) { taskUndo.moveTask(taskId, "todo", -1) }
                            onDeleteRequested: function(taskId) { hostWin.deleteTaskWithUndo(taskId) }
                            onDragStarted: function(taskId, title, status, x, y) { hostWin.beginTaskDrag(taskId, title, status, x, y) }
                            onDragMoved: function(x, y) { hostWin.updateTaskDrag(x, y) }
                            onDragEnded: function(x, y) { hostWin.endTaskDrag(x, y) }
                        }
                    }

                    // 列管理入口：列由数据库驱动，加列 / 改名 / 调序都在这里
                    AddColumnButton {
                        Layout.preferredWidth: 46
                        Layout.fillHeight: true
                        onClicked: { hostWin.closeAllPanels(); columnManagerRef.open = true }
                    }
                }

                // R11-13：空态与 boardRow 同级 → anchors.centerIn: boardRow 合法，
                // 空态真正落在看板正中（此前停在窗口左上角）。
                EmptyState {
                    anchors.centerIn: boardRow
                    width: Math.min(360, boardRow.width - Theme.spacingXl * 2)
                    query: searchField.text
                    stageFiltered: hostWin.stageFilter.length > 0
                    projectFiltered: hostWin.projectFilter.length > 0
                    visibleCount: boardColumns.todoCount + boardColumns.doingCount + boardColumns.doneCount
                    actionText: "清除筛选"
                    onActionTriggered: hostWin.clearFilters()
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
}
