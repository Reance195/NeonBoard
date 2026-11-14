import QtQuick
import QtQml
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 任务编辑器（R6-01/03/07，MOD-17 自 Main.qml 内联组件原样迁入）：
// hostWin = 宿主窗口（原 window，6 处引用接口化）。
// 根层 FolderDialog execCwdDialog（MOD-17 自 Main.qml 随迁）：parent 显式绑宿主 contentItem，
// 等价保留原「放在根层，避免嵌套弹窗时 parent window 解析问题」的约束。
Dialog {
    id: editor

    property var hostWin
    parent: hostWin ? hostWin.contentItem : null
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 20
        width: Math.min(520, hostWin.width - 80)
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
                placeholderText: "边界：每行一条路径 glob（如 src/task/TaskModel.cpp）"
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
                            hostWin.openSettings()
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
                    onClicked: hostWin.openExecPanel(editor.taskId)
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
                        hostWin.openExecConfirm({
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
                        hostWin.deleteTaskWithUndo(editor.taskId)
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

    // 执行页签的工作目录选择（MOD-17 自 Main.qml 根层随迁）：随编辑器同源加载，
    // 窗口经编辑器父链解析（qml.exe offscreen 探针验证无告警）。
    FolderDialog {
        id: execCwdDialog
        title: "选择工作目录"
        onAccepted: {
            editor.execCwd = editor.localPath(selectedFolder)
        }
    }
}
