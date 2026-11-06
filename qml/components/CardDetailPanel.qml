import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 卡片详情抽屉（R8-03）。
//
// 与「编辑任务」对话框的分工：抽屉管**看明白**，编辑器管执行定义那半张表
// （目标 / 验收 / 边界 / agent / workflow / cwd）。抽屉自己带一轮就地编辑：
// 标题 / 优先级 / 标签 / 到期 / 提醒 / 重复 / 备注一次改完，点「保存」才落库。
//
// 三条硬约束（对应 R8-03 的验收）：
//   1. Esc / 点外部 / 取消：一个字都不写库 —— 草稿只在 QML 里活着
//   2. 没有实际改动时「保存」置灰；即便绕过界面直调 saveFields，后端也会挡掉（零写入）
//   3. 备注按 Text.MarkdownText 渲染，编辑时回落纯文本（渲染后的文本不能反过来当源码改）
DialogShell {
    id: root

    // R11-09：统一弹窗壳 —— 头固定（标题 + 关闭键）、体滚动、底固定（编辑 / 取消 / 保存）。
    titleText: root.editing ? "编辑卡片" : "卡片详情"
    sizeMode: "md"
    maxBodyHeight: 520
    secondaryText: root.editing ? "取消" : ""
    primaryText: root.editing ? "保存" : ""
    primaryEnabled: root.draftDirty

    onSecondaryTriggered: root.cancelEdit()
    onPrimaryTriggered: root.saveAll()
    onCloseRequested: {
        // 关闭即丢弃草稿：取消语义只看「有没有写库」
        if (root.editing)
            root.cancelEdit()
        root.close()
    }

    // 底栏左侧的动作：看详情时才有（编辑 / 打开编辑器），编辑态留给右边的取消 / 保存
    footerExtra: Component {
        RowLayout {
            spacing: Theme.spacingSm

            Button {
                visible: !root.editing
                text: "编辑"
                Layout.preferredHeight: 26
                Layout.preferredWidth: 52
                padding: 0
                onClicked: root.beginEdit()
                background: Rectangle {
                    radius: Theme.radiusControl
                    color: Theme.input
                    border.width: 1
                    border.color: Theme.border
                }
                contentItem: Text {
                    text: "编辑"
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                visible: !root.editing
                text: "打开编辑器"
                Layout.preferredHeight: 26
                padding: 0
                Layout.preferredWidth: 92
                onClicked: {
                    const id = root.cardId
                    root.silentClose = true
                    root.close()
                    root.openEditor(id)
                }
                background: Rectangle {
                    radius: Theme.radiusControl
                    color: Theme.input
                    border.width: 1
                    border.color: Theme.border
                }
                contentItem: Text {
                    text: "打开编辑器"
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    property string cardId: ""
    property var snapshot: ({})
    property bool editing: false

    // 草稿：只在点「编辑」时从快照拷一份，取消即整体丢弃
    property string draftTitle: ""
    property string draftTags: ""
    property string draftDue: ""
    property string draftRemind: ""
    property string draftRrule: ""
    property string draftNote: ""
    property int draftPriority: 0

    // 「保存」是否可用：唯一判据交给后端（cardDetail.hasChanges），QML 不自造比较逻辑
    readonly property bool draftDirty: cardDetail.hasChanges(root.cardId, root.patchOf())
    // 状态色只算一次：标题左侧胶囊的底色 / 描边 / 文字都取它，三处各写一遍必然漂移
    readonly property color statusTint: statusColor(root.snapshot.status)

    property bool silentClose: false

    // 面板是从 Loader 里实例化的跨文件组件，拿不到 Main.qml 的 window id（QML 的 id 不跨文件），
    // 宽度与归属只能由宿主显式注入。
    // 类型必须是 var 而不是 Item：宿主传进来的是 ApplicationWindow（QQuickWindow 不是 QQuickItem），
    // 声明成 Item 会在赋值时报 "Cannot assign QObject* to QQuickItem*"，parent 变 null 后
    // 抽屉就成了没有宿主的游离窗口。真正的挂载点取它的 contentItem（见下）。
    property var hostWindow: null

    signal openEditor(string taskId)

    onClosed: {
        // 关掉即丢弃草稿：取消语义只看「有没有写库」，草稿留在内存里没有意义，
        // 下次打开必须回到库里的事实
        if (!root.silentClose)
            root.editing = false
    }

    function openFor(id) {
        if (!id || id.length === 0)
            return
        root.cardId = id
        root.silentClose = false
        root.editing = false
        root.refresh()
        open()
    }

    function refresh() {
        if (root.cardId.length === 0)
            return
        root.snapshot = taskStore.taskById(root.cardId)
        const extra = taskExtra.extraOf(root.cardId)
        // extraOf 返回的 map 不能直接扩写（只读），拷一份再塞派生字段
        const merged = {}
        for (const key in root.snapshot)
            merged[key] = root.snapshot[key]
        merged.remindAt = extra ? (extra.remindAt || 0) : 0
        merged.rrule = extra ? (extra.rrule || "") : ""
        root.snapshot = merged
        root.resetDrafts()
    }

    onOpened: {
        // 重开抽屉时把项目下拉复位到库里的实际归属：
        // currentIndex 的绑定在用户交互后可能已失效、不会自动重建。
        // 放 onOpened 而不是 refresh()：打开前 contentItem 不保证已实例化
        projectCombo.currentIndex = projectCombo.projectIndexInDb
    }

    function resetDrafts() {
        const s = root.snapshot
        root.draftTitle = s.title || ""
        root.draftPriority = Number(s.priority || 0)
        root.draftTags = root.tagList().join(", ")
        root.draftDue = datePart(s.dueAt)
        root.draftRemind = i18nDate(s.remindAt)
        root.draftRrule = s.rrule || ""
        root.draftNote = s.note || ""
    }

    function beginEdit() {
        root.resetDrafts()
        root.editing = true
    }

    function cancelEdit() {
        root.resetDrafts()
        root.editing = false
    }

    // 草稿 → 后端补丁。日期在这里解析成 epoch：界面只认字符串，落库只认时间戳，
    // 解析失败就当没填（0），与编辑器表单同一口径。
    function patchOf() {
        const patch = {
            title: String(root.draftTitle || "").trim(),
            priority: root.draftPriority,
            tags: String(root.draftTags || ""),
            dueAt: epochOfDate(root.draftDue),
            remindAt: epochOfDateTime(root.draftRemind),
            rrule: String(root.draftRrule || "").trim(),
            note: String(root.draftNote || "")
        }
        return patch
    }

    function saveAll() {
        if (root.cardId.length === 0)
            return false
        const ok = cardDetail.saveFields(root.cardId, root.patchOf())
        if (!ok && root.draftDirty)
            return false
        // 保存成功，或者根本没有改动：两种情况都该退出编辑态
        root.editing = false
        root.refresh()
        return ok
    }

    function epochOfDate(text) {
        const raw = String(text || "").trim()
        if (raw.length === 0)
            return 0
        const d = new Date(raw + "T23:59:59")
        return isNaN(d.getTime()) ? 0 : Math.floor(d.getTime() / 1000)
    }

    function epochOfDateTime(text) {
        const raw = String(text || "").trim()
        if (raw.length === 0)
            return 0
        const d = new Date(raw.replace(" ", "T"))
        return isNaN(d.getTime()) ? 0 : Math.floor(d.getTime() / 1000)
    }

    function datePart(epoch) {
        const n = Number(epoch || 0)
        if (!n || n <= 0)
            return ""
        const d = new Date(n * 1000)
        function p(v) { return v < 10 ? "0" + v : "" + v }
        return d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" + p(d.getDate())
    }

    function i18nDate(epoch) {
        const n = Number(epoch || 0)
        if (!n || n <= 0)
            return ""
        const d = new Date(n * 1000)
        function p(v) { return v < 10 ? "0" + v : "" + v }
        return d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" + p(d.getDate())
               + " " + p(d.getHours()) + ":" + p(d.getMinutes())
    }

    function priorityLabel(level) {
        const n = Number(level || 0)
        if (n >= 3) return "高"
        if (n === 2) return "中"
        if (n === 1) return "低"
        return "未设"
    }

    function statusLabel(status) {
        if (status === "doing") return "进行中"
        if (status === "done") return "已办结"
        return "待办"
    }

    function statusColor(status) {
        if (status === "doing") return Theme.cyan
        if (status === "done") return Theme.success
        return Theme.muted
    }

    function tagList() {
        // tagsFull 在模型里是**存库原文**（JSON 数组字符串），不是数组：
        // 按 Array.isArray 判断会永远落空，只显示第一个标签。这里先按 JSON 解，
        // 解不出来（历史数据是逗号串）再按逗号切。
        const raw = root.snapshot.tagsFull
        if (Array.isArray(raw))
            return raw.map(function(t) { return String(t) }).filter(function(t) { return t.length > 0 })
        if (typeof raw === "string" && raw.length > 0) {
            try {
                const parsed = JSON.parse(raw)
                if (Array.isArray(parsed))
                    return parsed.map(function(t) { return String(t) })
                            .filter(function(t) { return t.length > 0 })
            } catch (e) {
                // 不是合法 JSON：退回逗号分隔
            }
            return raw.split(",").map(function(t) { return t.trim() })
                    .filter(function(t) { return t.length > 0 })
        }
        const single = root.snapshot.tags
        return (single && String(single).length > 0) ? [String(single)] : []
    }

    function draftTagList() {
        return String(root.draftTags || "").split(",").map(function(t) { return t.trim() })
                .filter(function(t) { return t.length > 0 })
    }

    // RRULE → 人话。只覆盖编辑器里会给到的那几种（DAILY/WEEKLY/MONTHLY/YEARLY
    // + INTERVAL + BYDAY，见 TaskExtra.h 的 v1 子集），认不出的原样回显，
    // 不猜也不吞：用户至少还能看到自己填的是什么。
    function rruleText(rule) {
        const raw = String(rule || "").trim()
        if (raw.length === 0)
            return "不重复"
        const parts = raw.toUpperCase().split(";")
        const map = {}
        for (let i = 0; i < parts.length; ++i) {
            const eq = parts[i].indexOf("=")
            if (eq > 0)
                map[parts[i].slice(0, eq)] = parts[i].slice(eq + 1)
        }
        const freq = map.FREQ || ""
        const base = { DAILY: "每天", WEEKLY: "每周", MONTHLY: "每月", YEARLY: "每年" }[freq]
        if (!base)
            return raw
        const interval = Number(map.INTERVAL || 1)
        if (!isFinite(interval) || interval <= 1)
            return base
        const unit = { DAILY: "天", WEEKLY: "周", MONTHLY: "个月", YEARLY: "年" }[freq]
        return "每 " + interval + " " + unit
    }

    // 项目归属（R18-01）：task_project 侧表，与草稿保存是两条独立写路径 ——
    // 下拉一选立即落库（同设置面板里项目操作的口径），不进 patchOf / hasChanges 的草稿流程。
    // projectName 引用 revision 是为了让 metaRows 的绑定随归属变化重算。
    function projectName() {
        const rev = projectStore.revision
        const info = projectStore.taskProject(root.cardId)
        return (info && info.name && String(info.name).length > 0) ? String(info.name) : "未归属"
    }

    // 名字 / id 平铺成两个数组：ComboBox 用字符串 model 最省事；
    // map 模型得自己实现 labelRole（SettingsPanel 的 NeonComboBox 就是为此存在的，这里不值得再抄一份组件）。
    function projectOptions() {
        const rev = projectStore.revision
        const list = projectStore.projects()
        const names = ["（未归属）"]
        const ids = [""]
        for (let i = 0; i < list.length; ++i) {
            names.push(String(list[i].name))
            ids.push(String(list[i].id))
        }
        return { names: names, ids: ids }
    }

    function metaRows() {
        const s = root.snapshot
        const rows = []
        if (s.stage && String(s.stage).length > 0 && String(s.stage) !== "OTHER")
            rows.push({ label: "阶段", value: s.stage, mono: true })
        rows.push({ label: "项目", value: root.projectName(), mono: false })
        rows.push({ label: "所在列", value: columnName(s.columnId, s.status), mono: false })
        rows.push({ label: "到期", value: s.due || "未设置", mono: false })
        rows.push({ label: "提醒", value: i18nDate(s.remindAt) || "未设置", mono: false })
        rows.push({ label: "重复", value: rruleText(s.rrule), mono: false })
        rows.push({ label: "创建于", value: i18nDate(s.createdAt) || "—", mono: true })
        rows.push({ label: "更新于", value: i18nDate(s.updatedAt) || "—", mono: true })
        if (Number(s.completedAt || 0) > 0)
            rows.push({ label: "完成于", value: i18nDate(s.completedAt), mono: true })
        return rows
    }

    function columnName(columnId, status) {
        const name = boardColumns.titleOf(String(columnId || ""))
        return (name && name.length > 0) ? name : statusLabel(status)
    }

    // ---------------- R8-04：子任务与时间线 ----------------
    // 子任务进度：跟卡片上的进度环同源（taskTree 的关联边表），不另存一份计数
    readonly property var subtaskStats: {
        const rev = taskTree.revision
        return taskTree.childStats(root.cardId)
    }

    // 最多列 5 张：抽屉是「看明白这张卡」，不是子任务管理页
    function subtaskRows() {
        const rev = taskTree.revision
        const ids = taskTree.childIds(root.cardId)
        const out = []
        for (let i = 0; i < ids.length && out.length < 5; ++i) {
            const t = taskStore.taskById(ids[i])
            if (!t || !t.id)
                continue
            out.push({ id: t.id, title: t.title, status: t.status })
        }
        return out
    }

    // 事件按自增 id 倒序（同一秒内的两条也能稳定排序），这里只取最近 8 条
    readonly property var events: {
        const rev = taskAudit.revision
        return taskAudit.eventsOf(root.cardId, 8)
    }

    function eventTime(epoch) {
        const d = new Date(Number(epoch) * 1000)
        function p(v) { return v < 10 ? "0" + v : "" + v }
        return p(d.getMonth() + 1) + "-" + p(d.getDate()) + " " + p(d.getHours()) + ":" + p(d.getMinutes())
    }

    function eventColor(kind) {
        if (kind === "completed")
            return Theme.success
        if (kind === "removed")
            return Theme.danger
        if (kind === "created")
            return Theme.cyan
        return Theme.muted
    }

    // 抽屉里的输入框统一长这样：省得每个字段各写一遍 background 而写歪
    component Field: TextField {
        Layout.fillWidth: true
        color: Theme.text
        font.pixelSize: Theme.fontBody
        placeholderTextColor: Theme.muted
        background: Rectangle {
            color: Theme.elevated
            radius: Theme.radiusControl
            border.width: 1
            border.color: Theme.alpha(Theme.cyan, 0.20)
        }
    }

    // 内容区放进 ScrollView：R8-04 之后多了子任务与时间线两段，固定高度会溢出屏幕。
    // 高度取「内容自然高度」与「窗口高度减 80」中较小者；
    // implicitHeight 在某些布局下会是 NaN（> 0 为假），此时直接退回上限，
    // 否则 Math.min(NaN, x) = NaN，弹窗会按内容一路长到屏幕外面去。
    // 内容落在壳的体滚动区（头 / 底固定）
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12


            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    radius: Theme.radiusControl
                    width: 44
                    height: 22
                    color: Theme.alpha(root.statusTint, 0.24)
                    border.width: 1
                    border.color: root.statusTint

                    Text {
                        anchors.centerIn: parent
                        text: root.statusLabel(root.snapshot.status)
                        color: root.statusTint
                        font.pixelSize: Theme.fontCaption
                    }
                }

                Rectangle {
                    radius: Theme.radiusControl
                    width: 40
                    height: 22
                    visible: !root.editing
                    color: Theme.alpha(Theme.violet, 0.22)
                    border.width: 1
                    border.color: Theme.alpha(Theme.violet, 0.45)

                    Text {
                        anchors.centerIn: parent
                        text: root.priorityLabel(root.snapshot.priority)
                        color: Theme.violet
                        font.pixelSize: Theme.fontCaption
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // 标题：看的时候是大字，改的时候是输入框
            Text {
                Layout.fillWidth: true
                visible: !root.editing
                text: root.snapshot.title || "（无标题）"
                color: Theme.text
                font.pixelSize: Theme.fontDisplay
                font.bold: true
                wrapMode: Text.Wrap
            }

            Field {
                visible: root.editing
                text: root.draftTitle
                placeholderText: "标题（不能为空）"
                font.pixelSize: Theme.fontTitle
                onTextChanged: root.draftTitle = text
            }

            Text {
                Layout.fillWidth: true
                visible: root.editing && String(root.draftTitle).trim().length === 0
                text: "标题不能为空，否则保存会被拒绝"
                color: Theme.warning
                font.pixelSize: Theme.fontCaption
            }

            // 标签：看的时候是胶囊，改的时候是逗号分隔的输入框
            Flow {
                Layout.fillWidth: true
                spacing: 6
                visible: !root.editing

                Repeater {
                    model: root.tagList()
                    delegate: Rectangle {
                        radius: Theme.radiusControl
                        width: tagText.width + 16
                        height: 20
                        color: Theme.alpha(Theme.magenta, 0.16)
                        border.width: 1
                        border.color: Theme.alpha(Theme.magenta, 0.36)

                        Text {
                            id: tagText
                            anchors.centerIn: parent
                            text: "#" + modelData
                            color: Theme.magenta
                            font.pixelSize: Theme.fontCaption
                        }
                    }
                }

                Text {
                    visible: root.tagList().length === 0
                    text: "无标签"
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                }
            }

            Field {
                visible: root.editing
                text: root.draftTags
                placeholderText: "标签，逗号分隔（第一个标签决定阶段，如 R8, 记忆点）"
                onTextChanged: root.draftTags = text
            }

            // 优先级：只读时在标题右侧的胶囊里，编辑时给三个按钮
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: root.editing

                Text {
                    text: "优先级"
                    color: Theme.muted
                    font.pixelSize: Theme.fontBody
                }

                Repeater {
                    model: [["无", 0], ["低", 1], ["中", 2], ["高", 3]]
                    delegate: Rectangle {
                        width: 46
                        height: 26
                        radius: Theme.radiusControl
                        color: root.draftPriority === modelData[1]
                               ? Theme.alpha(Theme.violet, 0.30)
                               : Theme.input
                        border.width: 1
                        border.color: root.draftPriority === modelData[1]
                                      ? Theme.alpha(Theme.violet, 0.60)
                                      : Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: modelData[0]
                            color: root.draftPriority === modelData[1] ? Theme.text : Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.draftPriority = modelData[1]
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // 项目（R18-01）：编辑态可见；只读态已在 metaRows 网格里有「项目」行。
            // 选即落库（侧表写入），不参与「有未保存的改动」判定。
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.editing

                Text {
                    text: "项目"
                    color: Theme.muted
                    font.pixelSize: Theme.fontBody
                    Layout.preferredWidth: 32
                }

                ComboBox {
                    id: projectCombo
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    model: root.projectOptions().names
                    font.pixelSize: Theme.fontBody

                    // 库里当前归属在下拉里的下标；-1 显示「（未归属）」
                    readonly property int projectIndexInDb: {
                        const rev = projectStore.revision + root.cardId
                        const current = projectStore.projectOfTask(root.cardId)
                        return root.projectOptions().ids.indexOf(current)
                    }

                    // 显式引用 revision 与 cardId：归属变化、切卡查看时下拉要跟上库里的状态
                    currentIndex: projectCombo.projectIndexInDb

                    contentItem: Text {
                        leftPadding: 8
                        rightPadding: 20
                        text: projectCombo.currentIndex >= 0
                               ? String(projectCombo.model[projectCombo.currentIndex])
                               : "（未归属）"
                        color: Theme.text
                        font: projectCombo.font
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    background: Rectangle {
                        color: Theme.elevated
                        radius: Theme.radiusControl
                        border.width: 1
                        border.color: Theme.alpha(Theme.cyan, 0.20)
                    }

                    delegate: ItemDelegate {
                        width: projectCombo.width
                        height: 28
                        contentItem: Text {
                            leftPadding: 8
                            text: modelData
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            color: parent.highlighted ? Theme.accentSoft : "transparent"
                        }
                    }

                    popup: Popup {
                        y: projectCombo.height + 2
                        width: projectCombo.width
                        implicitHeight: contentItem.implicitHeight
                        padding: 1
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: projectCombo.popup.visible ? projectCombo.delegateModel : null
                            currentIndex: projectCombo.highlightedIndex
                            ScrollIndicator.vertical: ScrollIndicator {}
                        }
                        background: Rectangle {
                            radius: Theme.radiusControl
                            color: Theme.panelSolid
                            border.width: 1
                            border.color: Theme.border
                        }
                    }

                    onActivated: function(index) {
                        const ids = root.projectOptions().ids
                        if (index >= 0 && index < ids.length)
                            projectStore.assignTask(root.cardId, ids[index])
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10
                rowSpacing: 4
                visible: !root.editing

                Repeater {
                    model: root.metaRows()
                    delegate: Text {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: modelData.label + "：" + modelData.value
                        color: Theme.muted
                        font.pixelSize: Theme.fontCaption
                        font.family: modelData.mono ? "Consolas" : Qt.application.font.family
                        elide: Text.ElideRight
                    }
                }
            }

            // 编辑态的日期 / 提醒：与编辑器表单同一口径（到期只认日期，23:59:59 截止）
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: root.editing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: "到期"
                        color: Theme.muted
                        font.pixelSize: Theme.fontBody
                        Layout.preferredWidth: 32
                    }
                    Field {
                        text: root.draftDue
                        placeholderText: "YYYY-MM-DD（留空表示不设）"
                        onTextChanged: root.draftDue = text
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: "提醒"
                        color: Theme.muted
                        font.pixelSize: Theme.fontBody
                        Layout.preferredWidth: 32
                    }
                    Field {
                        text: root.draftRemind
                        placeholderText: "YYYY-MM-DD HH:mm（留空表示不提醒）"
                        onTextChanged: root.draftRemind = text
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: "重复"
                        color: Theme.muted
                        font.pixelSize: Theme.fontBody
                        Layout.preferredWidth: 32
                    }

                    Repeater {
                        model: [["每天", "FREQ=DAILY"], ["每周", "FREQ=WEEKLY"], ["每月", "FREQ=MONTHLY"]]
                        delegate: Rectangle {
                            width: 44
                            height: 26
                            radius: Theme.radiusControl
                            color: root.draftRrule === modelData[1]
                                   ? Theme.alpha(Theme.cyan, 0.32)
                                   : Theme.input
                            border.width: 1
                            border.color: root.draftRrule === modelData[1] ? Theme.cyan : Theme.border

                            Text {
                                anchors.centerIn: parent
                                text: modelData[0]
                                color: root.draftRrule === modelData[1] ? Theme.text : Theme.muted
                                font.pixelSize: Theme.fontCaption
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.draftRrule = (root.draftRrule === modelData[1]) ? "" : modelData[1]
                            }
                        }
                    }

                    Field {
                        text: root.draftRrule
                        placeholderText: "RRULE，如 FREQ=DAILY;INTERVAL=2"
                        onTextChanged: root.draftRrule = text
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "备注"
                    color: Theme.text
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                }

                Text {
                    visible: root.editing
                    text: "支持 Markdown"
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    visible: root.editing && root.draftDirty
                    text: "有未保存的改动"
                    color: Theme.warning
                    font.pixelSize: Theme.fontCaption
                }
            }

            // 预览：Markdown 渲染。空备注给一句提示，避免整块留白看着像坏了。
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(120, Math.min(260, noteView.contentHeight + 20))
                visible: !root.editing
                radius: Theme.radiusCard
                color: Theme.input
                border.width: 1
                border.color: Theme.border

                Flickable {
                    id: noteFlick
                    anchors.fill: parent
                    anchors.margins: 10
                    contentWidth: width
                    contentHeight: noteView.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Text {
                        id: noteView
                        width: noteFlick.width
                        text: (root.snapshot.note && root.snapshot.note.length > 0)
                              ? root.snapshot.note : "还没有备注。点右上角「编辑」开始写，支持 Markdown。"
                        color: root.snapshot.note && root.snapshot.note.length > 0 ? Theme.text : Theme.muted
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.Wrap
                        textFormat: Text.MarkdownText
                        onLinkActivated: function(link) { Qt.openUrlExternally(link) }
                    }
                }
            }

            // 编辑：纯文本。这里刻意不做所见即所得 —— 渲染后的文本不可反过来当源码编辑。
            TextArea {
                id: noteEdit
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                visible: root.editing
                text: root.draftNote
                onTextChanged: root.draftNote = text
                placeholderText: "支持 Markdown：# 标题 / - 列表 / **加粗** / `代码`"
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: Theme.fontBody
                wrapMode: TextArea.Wrap
                background: Rectangle {
                    color: Theme.elevated
                    radius: Theme.radiusCard
                    border.width: 1
                    border.color: Theme.alpha(Theme.cyan, 0.20)
                }
            }

            // ---------------- 子任务（R8-04）----------------
            // 只有真挂了子任务才出现：空着一块「暂无子任务」只是噪音
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: root.subtaskStats.total > 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Canvas {
                        id: subtaskRing
                        width: 24
                        height: 24
                        // 依赖 revision：关联或子任务状态变了就重绘
                        property int statsRev: taskTree.revision
                        property real ratio: root.subtaskStats.total > 0
                                            ? root.subtaskStats.done / root.subtaskStats.total : 0
                        onStatsRevChanged: requestPaint()
                        onRatioChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            const cx = width / 2
                            const cy = height / 2
                            const r = width / 2 - 2.5
                            ctx.lineWidth = 3
                            ctx.strokeStyle = Theme.border
                            ctx.beginPath()
                            ctx.arc(cx, cy, r, 0, Math.PI * 2)
                            ctx.stroke()
                            if (ratio > 0) {
                                ctx.strokeStyle = Theme.success
                                ctx.beginPath()
                                ctx.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * ratio)
                                ctx.stroke()
                            }
                        }
                    }

                    Text {
                        text: "子任务"
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                    }

                    Text {
                        text: root.subtaskStats.done + " / " + root.subtaskStats.total + " 已完成"
                        color: Theme.muted
                        font.pixelSize: Theme.fontCaption
                    }

                    Item { Layout.fillWidth: true }
                }

                Repeater {
                    model: root.subtaskRows()
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            width: 6
                            height: 6
                            radius: Theme.radiusControl
                            color: modelData.status === "done" ? Theme.success
                                 : modelData.status === "doing" ? Theme.cyan : Theme.muted
                            opacity: modelData.status === "done" ? 0.9 : 0.6
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.title
                            color: modelData.status === "done" ? Theme.muted : Theme.text
                            font.pixelSize: Theme.fontBody
                            font.strikeout: modelData.status === "done"
                            elide: Text.ElideRight
                        }

                        Text {
                            text: root.statusLabel(modelData.status)
                            color: Theme.muted
                            font.pixelSize: Theme.fontMicro
                        }
                    }
                }
            }

            // ---------------- 时间线（R8-04）----------------
            // 数据来自侧表 task_event（创建 / 移动 / 完成 / 恢复 / 删除）。
            // 刻意不记「编辑」：每次移动都会顺带一条，半屏都是它，真正的生命周期反而看不见。
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                visible: root.events.length > 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "时间线"
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                    }

                    Text {
                        text: "最近 " + root.events.length + " 条"
                        color: Theme.muted
                        font.pixelSize: Theme.fontCaption
                    }

                    Item { Layout.fillWidth: true }
                }

                Repeater {
                    model: root.events
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            width: 5
                            height: 5
                            radius: Theme.radiusControl
                            color: root.eventColor(modelData.kind)
                        }

                        Text {
                            text: modelData.label
                            color: root.eventColor(modelData.kind)
                            font.pixelSize: Theme.fontCaption
                            Layout.preferredWidth: 32
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.detail.length > 0 ? modelData.detail : " "
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                        }

                        Text {
                            text: root.eventTime(modelData.at)
                            color: Theme.muted
                            opacity: 0.8
                            font.pixelSize: Theme.fontMicro
                            font.family: "Consolas"
                        }
                    }
                }
            }
        }
}
