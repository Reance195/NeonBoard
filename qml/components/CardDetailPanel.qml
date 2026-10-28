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
Dialog {
    id: root

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

    parent: hostWindow ? hostWindow.contentItem : null
    anchors.centerIn: parent

    modal: true
    focus: true
    padding: 20
    width: Math.min(620, hostWindow ? hostWindow.width - 80 : 620)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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

    function metaRows() {
        const s = root.snapshot
        const rows = []
        if (s.stage && String(s.stage).length > 0 && String(s.stage) !== "OTHER")
            rows.push({ label: "阶段", value: s.stage, mono: true })
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

    background: Rectangle {
        color: Theme.panelSolid
        radius: 16
        border.width: 1
        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
    }

    // 抽屉里的输入框统一长这样：省得每个字段各写一遍 background 而写歪
    component Field: TextField {
        Layout.fillWidth: true
        color: Theme.text
        font.pixelSize: 12
        placeholderTextColor: Theme.muted
        background: Rectangle {
            color: Theme.elevated
            radius: 8
            border.width: 1
            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                radius: 8
                width: 44
                height: 22
                color: Qt.rgba(root.statusTint.r, root.statusTint.g, root.statusTint.b, 0.24)
                border.width: 1
                border.color: root.statusTint

                Text {
                    anchors.centerIn: parent
                    text: root.statusLabel(root.snapshot.status)
                    color: root.statusTint
                    font.pixelSize: 11
                }
            }

            Rectangle {
                radius: 8
                width: 40
                height: 22
                visible: !root.editing
                color: Qt.rgba(Theme.violet.r, Theme.violet.g, Theme.violet.b, 0.22)
                border.width: 1
                border.color: Qt.rgba(Theme.violet.r, Theme.violet.g, Theme.violet.b, 0.45)

                Text {
                    anchors.centerIn: parent
                    text: root.priorityLabel(root.snapshot.priority)
                    color: Theme.violet
                    font.pixelSize: 11
                }
            }

            Text {
                text: root.editing ? "编辑卡片" : "卡片详情"
                color: Theme.muted
                font.pixelSize: 12
            }

            Item { Layout.fillWidth: true }

            Button {
                visible: !root.editing
                text: "编辑"
                Layout.preferredHeight: 26
                Layout.preferredWidth: 52
                padding: 0
                onClicked: root.beginEdit()
                background: Rectangle {
                    radius: 8
                    color: Theme.input
                    border.width: 1
                    border.color: Theme.border
                }
                contentItem: Text {
                    text: "编辑"
                    color: Theme.muted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                text: "关闭"
                Layout.preferredHeight: 26
                Layout.preferredWidth: 52
                padding: 0
                onClicked: {
                    if (root.editing)
                        root.cancelEdit()
                    root.close()
                }
                background: Rectangle {
                    radius: 8
                    color: Theme.input
                    border.width: 1
                    border.color: Theme.border
                }
                contentItem: Text {
                    text: "关闭"
                    color: Theme.muted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // 标题：看的时候是大字，改的时候是输入框
        Text {
            Layout.fillWidth: true
            visible: !root.editing
            text: root.snapshot.title || "（无标题）"
            color: Theme.text
            font.pixelSize: 20
            font.bold: true
            wrapMode: Text.Wrap
        }

        Field {
            visible: root.editing
            text: root.draftTitle
            placeholderText: "标题（不能为空）"
            font.pixelSize: 15
            onTextChanged: root.draftTitle = text
        }

        Text {
            Layout.fillWidth: true
            visible: root.editing && String(root.draftTitle).trim().length === 0
            text: "标题不能为空，否则保存会被拒绝"
            color: Theme.warning
            font.pixelSize: 11
        }

        // 标签：看的时候是胶囊，改的时候是逗号分隔的输入框
        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: !root.editing

            Repeater {
                model: root.tagList()
                delegate: Rectangle {
                    radius: 7
                    width: tagText.width + 16
                    height: 20
                    color: Qt.rgba(Theme.magenta.r, Theme.magenta.g, Theme.magenta.b, 0.16)
                    border.width: 1
                    border.color: Qt.rgba(Theme.magenta.r, Theme.magenta.g, Theme.magenta.b, 0.36)

                    Text {
                        id: tagText
                        anchors.centerIn: parent
                        text: "#" + modelData
                        color: Theme.magenta
                        font.pixelSize: 11
                    }
                }
            }

            Text {
                visible: root.tagList().length === 0
                text: "无标签"
                color: Theme.muted
                font.pixelSize: 11
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
                font.pixelSize: 12
            }

            Repeater {
                model: [["无", 0], ["低", 1], ["中", 2], ["高", 3]]
                delegate: Rectangle {
                    width: 46
                    height: 26
                    radius: 7
                    color: root.draftPriority === modelData[1]
                           ? Qt.rgba(Theme.violet.r, Theme.violet.g, Theme.violet.b, 0.30)
                           : Theme.input
                    border.width: 1
                    border.color: root.draftPriority === modelData[1]
                                  ? Qt.rgba(Theme.violet.r, Theme.violet.g, Theme.violet.b, 0.60)
                                  : Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: modelData[0]
                        color: root.draftPriority === modelData[1] ? Theme.text : Theme.muted
                        font.pixelSize: 11
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
                    font.pixelSize: 11
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
                    font.pixelSize: 12
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
                    font.pixelSize: 12
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
                    font.pixelSize: 12
                    Layout.preferredWidth: 32
                }

                Repeater {
                    model: [["每天", "FREQ=DAILY"], ["每周", "FREQ=WEEKLY"], ["每月", "FREQ=MONTHLY"]]
                    delegate: Rectangle {
                        width: 44
                        height: 26
                        radius: 7
                        color: root.draftRrule === modelData[1]
                               ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
                               : Theme.input
                        border.width: 1
                        border.color: root.draftRrule === modelData[1] ? Theme.cyan : Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: modelData[0]
                            color: root.draftRrule === modelData[1] ? Theme.text : Theme.muted
                            font.pixelSize: 11
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
                font.pixelSize: 14
                font.bold: true
            }

            Text {
                visible: root.editing
                text: "支持 Markdown"
                color: Theme.muted
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            Text {
                visible: root.editing && root.draftDirty
                text: "有未保存的改动"
                color: Theme.warning
                font.pixelSize: 11
            }
        }

        // 预览：Markdown 渲染。空备注给一句提示，避免整块留白看着像坏了。
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(120, Math.min(260, noteView.contentHeight + 20))
            visible: !root.editing
            radius: 10
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
                    font.pixelSize: 12
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
            font.pixelSize: 12
            wrapMode: TextArea.Wrap
            background: Rectangle {
                color: Theme.elevated
                radius: 10
                border.width: 1
                border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                visible: !root.editing
                text: "打开编辑器"
                Layout.preferredHeight: 28
                padding: 0
                Layout.preferredWidth: 92
                onClicked: {
                    const id = root.cardId
                    root.silentClose = true
                    root.close()
                    root.openEditor(id)
                }
                background: Rectangle {
                    radius: 8
                    color: Theme.input
                    border.width: 1
                    border.color: Theme.border
                }
                contentItem: Text {
                    text: "打开编辑器"
                    color: Theme.muted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                visible: root.editing
                text: "取消"
                Layout.preferredHeight: 28
                Layout.preferredWidth: 64
                padding: 0
                onClicked: root.cancelEdit()
                background: Rectangle {
                    radius: 8
                    color: Theme.input
                    border.width: 1
                    border.color: Theme.border
                }
                contentItem: Text {
                    text: "取消"
                    color: Theme.muted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                visible: root.editing
                text: "保存"
                Layout.preferredHeight: 28
                Layout.preferredWidth: 64
                padding: 0
                enabled: root.draftDirty
                onClicked: root.saveAll()
                background: Rectangle {
                    radius: 8
                    color: root.draftDirty
                           ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
                           : Theme.input
                    border.width: 1
                    border.color: root.draftDirty ? Theme.cyan : Theme.border
                }
                contentItem: Text {
                    text: "保存"
                    color: root.draftDirty ? Theme.text : Theme.muted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
