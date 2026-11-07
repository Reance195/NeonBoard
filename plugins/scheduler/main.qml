import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NeonBoard
import "rules.js" as Rules

// 定时任务面板：规则编辑 + 手动执行 + 执行记录。
// 规则存插件 storage 的 scheduler.rules 键（与后台常驻入口共用同一份），
// 保存后最迟 30 秒内生效（后台每 30 秒重读，无需重启）。
// 编辑表单不使用属性绑定（同 plan-hook 的做法）：打开时程序赋值、
// 收集时读控件，避免 QML 用户输入断绑定的坑。
Rectangle {
    id: root

    color: "transparent"

    property var rules: []
    property var records: []
    property int editingIndex: -1      // -1 = 新建；>=0 = 编辑 rules[editingIndex]
    property var draftActions: []      // 编辑中的动作列表（deep copy，保存时写回）

    function reload() {
        rules = Rules.readRules(pluginHost)
        records = Rules.readRecords(pluginHost)
    }

    // 星期勾选框自带 day 属性（1=周一 … 7=周日），遍历子项时按 day 识别，
    // 不按位置——RowLayout 里还排着 Text 与 Repeater 占位
    function collectWeekdays() {
        var days = []
        for (var i = 0; i < weekdayRow.children.length; i++) {
            var box = weekdayRow.children[i]
            if (box.day !== undefined && box.checked)
                days.push(box.day)
        }
        return days
    }

    function populateWeekdays(days) {
        for (var i = 0; i < weekdayRow.children.length; i++) {
            var box = weekdayRow.children[i]
            if (box.day !== undefined)
                box.checked = days.indexOf(box.day) >= 0
        }
    }

    function startAdd() {
        editingIndex = -1
        labelField.text = ""
        kindBox.currentIndex = 0
        timeField.text = "09:00"
        populateWeekdays([1])
        onceField.text = ""
        draftActions = [Rules.defaultAction()]
        actionsRepeater.model = draftActions
        editorTitle.text = "新建规则"
        draft.visible = true
    }

    function startEdit(index) {
        var rule = rules[index]
        editingIndex = index
        labelField.text = rule.label
        kindBox.currentIndex = rule.kind === "weekly" ? 1 : (rule.kind === "once" ? 2 : 0)
        timeField.text = rule.time
        populateWeekdays(rule.weekdays)
        onceField.text = rule.onceAt > 0 ? Rules.formatDateTime(rule.onceAt) : ""
        draftActions = JSON.parse(JSON.stringify(rule.actions))
        if (draftActions.length === 0)
            draftActions = [Rules.defaultAction()]
        actionsRepeater.model = draftActions
        editorTitle.text = "编辑规则"
        draft.visible = true
    }

    function closeEditor() {
        draft.visible = false
        editingIndex = -1
    }

    // 收集表单 → 归一化规则；返回 null 表示校验失败（statusText 已提示）
    function collectDraft() {
        var rule = Rules.normalizeRule({
            "id": editingIndex >= 0 ? rules[editingIndex].id : "",
            "enabled": editingIndex >= 0 ? rules[editingIndex].enabled : true,
            "label": labelField.text,
            "kind": kindBox.currentIndex === 1 ? "weekly" : (kindBox.currentIndex === 2 ? "once" : "daily"),
            "time": timeField.text,
            "weekdays": collectWeekdays(),
            "onceAt": Rules.parseLocalDateTime(onceField.text),
            "actions": draftActions
        })
        var problems = Rules.ruleProblems(rule)
        if (problems.length > 0) {
            statusText.text = problems[0]
            return null
        }
        return rule
    }

    function saveDraft() {
        var rule = collectDraft()
        if (rule === null)
            return
        if (editingIndex >= 0)
            rules[editingIndex] = rule
        else
            rules.push(rule)
        if (!Rules.writeRules(pluginHost, rules)) {
            statusText.text = "保存失败：缺少 storage 权限"
            return
        }
        closeEditor()
        reload()
        statusText.text = "规则已保存：" + rule.label
    }

    function removeRule(index) {
        rules.splice(index, 1)
        if (!Rules.writeRules(pluginHost, rules)) {
            statusText.text = "删除失败：缺少 storage 权限"
            return
        }
        reload()
        statusText.text = "已删除规则"
    }

    // 手动执行与后台到点触发共用 runActions（Z5）；不推进槽位状态
    function runNow(index) {
        var rule = rules[index]
        var result = Rules.runActions(rule, pluginHost)
        Rules.appendRecord(pluginHost, {
            "time": Rules.formatDateTime(Date.now()),
            "label": rule.label,
            "source": "manual",
            "created": result.created,
            "moved": result.moved,
            "notified": result.notified,
            "error": result.errors.length > 0 ? result.errors[0] : ""
        })
        records = Rules.readRecords(pluginHost)
        statusText.text = rule.label + " · " + Rules.resultText(result)
    }

    Component.onCompleted: {
        reload()
    }

    // 面板内复用的小按钮（描边胶囊，随邻 plan-hook 的自绘按钮风格）
    component PillButton: Rectangle {
        id: pill

        property string label: ""
        property color fg: Theme.cyan
        signal clicked

        width: pillLabel.implicitWidth + 16
        height: 22
        radius: 8
        color: pillArea.pressed ? Theme.elevated : Theme.card
        border.width: 1
        border.color: Theme.border

        Text {
            id: pillLabel
            anchors.centerIn: parent
            text: pill.label
            color: pill.fg
            font.pixelSize: 10
        }

        MouseArea {
            id: pillArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.clicked()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 10

                // ---------- 规则列表 ----------
                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "规则"
                        color: Theme.text
                        font.pixelSize: 12
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    PillButton {
                        label: "＋ 新建规则"
                        visible: !draft.visible
                        onClicked: root.startAdd()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.rules.length === 0 && !draft.visible
                    text: "还没有规则。新建一条，例如：每天 09:00 建卡「晨会」到待办。"
                    color: Theme.muted
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: root.rules

                    delegate: Rectangle {
                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        height: ruleCol.implicitHeight + 16
                        radius: 8
                        color: Theme.card
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: ruleCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    color: Theme.text
                                    font.pixelSize: 11
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Switch {
                                    checked: modelData.enabled
                                    onToggled: {
                                        root.rules[index].enabled = checked
                                        if (!Rules.writeRules(pluginHost, root.rules))
                                            return
                                        root.reload()
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: Rules.scheduleText(modelData)
                                      + " · 下次：" + Rules.nextFireText(modelData, new Date())
                                color: Theme.cyan
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }

                            Repeater {
                                model: modelData.actions

                                Text {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: "· " + Rules.actionText(modelData)
                                    color: Theme.muted
                                    font.pixelSize: 9
                                    elide: Text.ElideRight
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                Item { Layout.fillWidth: true }

                                PillButton {
                                    label: "立即执行"
                                    onClicked: root.runNow(index)
                                }

                                PillButton {
                                    label: "编辑"
                                    onClicked: root.startEdit(index)
                                }

                                PillButton {
                                    label: "删除"
                                    fg: Theme.danger
                                    onClicked: root.removeRule(index)
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.border
                    visible: !draft.visible
                }

                // ---------- 编辑表单 ----------
                ColumnLayout {
                    id: draft
                    Layout.fillWidth: true
                    visible: false
                    spacing: 8

                    Text {
                        id: editorTitle
                        text: "新建规则"
                        color: Theme.text
                        font.pixelSize: 12
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "名称"
                            color: Theme.muted
                            font.pixelSize: 10
                            Layout.preferredWidth: 60
                        }

                        TextField {
                            id: labelField
                            Layout.fillWidth: true
                            placeholderText: "例如：每日晨会卡"
                            font.pixelSize: 10
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "计划"
                            color: Theme.muted
                            font.pixelSize: 10
                            Layout.preferredWidth: 60
                        }

                        ComboBox {
                            id: kindBox
                            Layout.preferredWidth: 70
                            model: ["每天", "每周", "一次性"]
                            font.pixelSize: 10
                        }

                        TextField {
                            id: timeField
                            Layout.preferredWidth: 64
                            placeholderText: "09:00"
                            font.pixelSize: 10
                            visible: kindBox.currentIndex < 2
                        }

                        TextField {
                            id: onceField
                            Layout.fillWidth: true
                            placeholderText: "2026-12-31 20:00"
                            font.pixelSize: 10
                            visible: kindBox.currentIndex === 2
                        }
                    }

                    RowLayout {
                        id: weekdayRow
                        Layout.fillWidth: true
                        spacing: 2
                        visible: kindBox.currentIndex === 1

                        Text {
                            text: "星期"
                            color: Theme.muted
                            font.pixelSize: 10
                            Layout.preferredWidth: 60
                        }

                        Repeater {
                            model: ["一", "二", "三", "四", "五", "六", "日"]

                            delegate: CheckBox {
                                required property string modelData
                                required property int index
                                property int day: index + 1
                                text: modelData
                                font.pixelSize: 9
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "动作"
                            color: Theme.text
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        PillButton {
                            label: "＋ 添加动作"
                            onClicked: {
                                root.draftActions.push(Rules.defaultAction())
                                root.draftActions = root.draftActions.slice()
                                actionsRepeater.model = root.draftActions
                            }
                        }
                    }

                    Repeater {
                        id: actionsRepeater

                        delegate: Rectangle {
                            id: actionCard

                            required property var modelData
                            required property int index

                            Layout.fillWidth: true
                            height: actionCol.implicitHeight + 16
                            radius: 8
                            color: Theme.input
                            border.width: 1
                            border.color: Theme.border

                            function replaceWithType(type) {
                                root.draftActions[index] = Rules.defaultAction(type)
                                root.draftActions = root.draftActions.slice()
                                actionsRepeater.model = root.draftActions
                            }

                            ColumnLayout {
                                id: actionCol
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true

                                    ComboBox {
                                        Layout.preferredWidth: 76
                                        model: ["建卡", "流转", "通知"]
                                        font.pixelSize: 10
                                        currentIndex: actionCard.modelData.type === "move" ? 1
                                                      : (actionCard.modelData.type === "notify" ? 2 : 0)
                                        onActivated: actionCard.replaceWithType(
                                                         currentIndex === 1 ? "move"
                                                         : (currentIndex === 2 ? "notify" : "create"))
                                    }

                                    Item { Layout.fillWidth: true }

                                    PillButton {
                                        label: "移除"
                                        fg: Theme.danger
                                        onClicked: {
                                            root.draftActions.splice(actionCard.index, 1)
                                            if (root.draftActions.length === 0)
                                                root.draftActions.push(Rules.defaultAction())
                                            root.draftActions = root.draftActions.slice()
                                            actionsRepeater.model = root.draftActions
                                        }
                                    }
                                }

                                // ---- 建卡字段 ----
                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: actionCard.modelData.type === "create"
                                    spacing: 6

                                    TextField {
                                        id: createTitle
                                        Layout.fillWidth: true
                                        placeholderText: "卡片标题"
                                        font.pixelSize: 10
                                        text: actionCard.modelData.title
                                        onTextEdited: actionCard.modelData.title = text
                                    }

                                    ComboBox {
                                        id: createStatus
                                        Layout.preferredWidth: 72
                                        model: ["todo", "doing", "done"]
                                        font.pixelSize: 10
                                        currentIndex: ["todo", "doing", "done"].indexOf(actionCard.modelData.status)
                                        onActivated: actionCard.modelData.status = currentText
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: actionCard.modelData.type === "create"
                                    spacing: 6

                                    TextField {
                                        Layout.fillWidth: true
                                        placeholderText: "标签（可空）"
                                        font.pixelSize: 10
                                        text: actionCard.modelData.tags
                                        onTextEdited: actionCard.modelData.tags = text
                                    }

                                    TextField {
                                        Layout.preferredWidth: 56
                                        placeholderText: "0 天后到期"
                                        font.pixelSize: 10
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                        text: actionCard.modelData.dueOffsetDays > 0 ? String(actionCard.modelData.dueOffsetDays) : ""
                                        onTextEdited: actionCard.modelData.dueOffsetDays = parseInt(text) || 0
                                    }
                                }

                                // ---- 流转字段 ----
                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: actionCard.modelData.type === "move"
                                    spacing: 6

                                    TextField {
                                        Layout.fillWidth: true
                                        placeholderText: "标题含（可空）"
                                        font.pixelSize: 10
                                        text: actionCard.modelData.keyword
                                        onTextEdited: actionCard.modelData.keyword = text
                                    }

                                    TextField {
                                        Layout.fillWidth: true
                                        placeholderText: "标签等于（可空）"
                                        font.pixelSize: 10
                                        text: actionCard.modelData.tag
                                        onTextEdited: actionCard.modelData.tag = text
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: actionCard.modelData.type === "move"
                                    spacing: 6

                                    Text {
                                        text: "来源列"
                                        color: Theme.muted
                                        font.pixelSize: 9
                                    }

                                    ComboBox {
                                        Layout.preferredWidth: 76
                                        model: ["", "todo", "doing", "done"]
                                        font.pixelSize: 10
                                        currentIndex: ["", "todo", "doing", "done"].indexOf(actionCard.modelData.from)
                                        onActivated: actionCard.modelData.from = currentText
                                    }

                                    Text {
                                        text: "→"
                                        color: Theme.muted
                                        font.pixelSize: 9
                                    }

                                    ComboBox {
                                        Layout.preferredWidth: 76
                                        model: ["todo", "doing", "done"]
                                        font.pixelSize: 10
                                        currentIndex: ["todo", "doing", "done"].indexOf(actionCard.modelData.to)
                                        onActivated: actionCard.modelData.to = currentText
                                    }
                                }

                                // ---- 通知字段 ----
                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: actionCard.modelData.type === "notify"
                                    spacing: 6

                                    TextField {
                                        Layout.fillWidth: true
                                        placeholderText: "通知标题（空则用规则名）"
                                        font.pixelSize: 10
                                        text: actionCard.modelData.title
                                        onTextEdited: actionCard.modelData.title = text
                                    }

                                    TextField {
                                        Layout.fillWidth: true
                                        placeholderText: "通知正文（可空）"
                                        font.pixelSize: 10
                                        text: actionCard.modelData.body
                                        onTextEdited: actionCard.modelData.body = text
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Item { Layout.fillWidth: true }

                        PillButton {
                            label: "取消"
                            fg: Theme.muted
                            onClicked: root.closeEditor()
                        }

                        PillButton {
                            label: "保存"
                            onClicked: root.saveDraft()
                        }
                    }
                }

                // ---------- 执行记录 ----------
                Text {
                    text: "执行记录"
                    color: Theme.text
                    font.pixelSize: 12
                    font.bold: true
                }

                Repeater {
                    model: root.records

                    Text {
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.time + " · " + (modelData.source === "manual" ? "手动" : "自动")
                              + " · " + modelData.label + " · 建 " + modelData.created
                              + " / 转 " + modelData.moved + " / 知 " + modelData.notified
                              + (modelData.error.length > 0 ? " · ⚠ " + modelData.error : "")
                        color: modelData.error.length > 0 ? Theme.warning : Theme.muted
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                }

                Text {
                    id: statusText
                    Layout.fillWidth: true
                    text: ""
                    color: Theme.muted
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
