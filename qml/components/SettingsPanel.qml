import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 设置面板（R11-10）：迁到统一弹窗壳 DialogShell —— lg 720 档。
//
// 结构（规格 §3.4「设置 → lg 720，左侧 160 px 固定导航，底部按钮常驻」）：
//   头固定：标题「设置 / SETTINGS」+ 右上角统一关闭键
//   体：左侧 160 px 固定分页导航（不随正文滚动）+ 右侧滚动正文（6 个分页）
//   底固定：左侧说明 + 右侧「关闭」
//
// 默认落在「热键与自动化」，与原型 docs/design/dialogs.html 的 SETTINGS_PAGES 一致。
//
// 底栏没有「保存」：面板里每一项都是即时生效的（开关即改、导入导出当场执行），
// 放一个什么都不做的「保存」只会让人以为设置没存住。
//
// profile 编辑器与「绑定仓库」两个浮层留在文件末尾，不进分页、不改成壳：
// 它们是浮在设置面板之上的二级弹窗，拆进分页会把「编辑」变成「换页」。
DialogShell {
    id: panel

    // ---- 壳配置 ----
    titleText: "设置"
    subtitleText: "SETTINGS"
    sizeMode: "lg"
    maxBodyHeight: 520

    // 底栏常驻：只有「关闭」。没有「保存」——设置即时生效。
    secondaryText: "关闭"
    footerNote: "设置即时生效，改完即用，无需保存。"
    onSecondaryTriggered: panel.close()
    onCloseRequested: panel.close()

    // Main.qml 用它连到插件库面板
    signal openLibraryRequested()

    // 当前分页（键与左侧导航一一对应）
    property string page: "hotkey"

    // 正文可用宽度：壳宽 - 左导航 160 - 左右留白。
    //
    // 为什么不用 Layout.fillWidth：壳的体滚动区把列宽绑在 bodyFlick.availableWidth 上，
    // 而 availableWidth 是 Control 的属性、Flickable 没有（Qt 6.8 实测为 undefined），
    // 于是体列宽会塌成「当前页内容宽」——本面板的开关与快捷键是右对齐的，一塌就全挤到左边。
    // 壳归壳所有（不在本卡 scope），这里按壳的几何自己算准宽度。
    readonly property int bodyWidth: Math.max(240, panel.width - 160 - Theme.spacingMd * 2)

    // 左侧导航的 6 项（顺序即导航顺序）
    readonly property var pages: [
        { key: "hotkey", label: "热键与自动化" },
        { key: "data", label: "数据与备份" },
        { key: "appearance", label: "外观与动效" },
        { key: "exec", label: "任务执行" },
        { key: "plugin", label: "插件" },
        { key: "about", label: "关于" }
    ]

    // 只读快捷键清单：固定键位改不了，列出来省得用户到处找
    readonly property var shortcuts: [
        { label: "显示 / 隐藏窗口", keys: "Ctrl+Shift+Space" },
        { label: "呼出命令面板", keys: "Ctrl+K" },
        { label: "新建任务", keys: "Ctrl+N" },
        { label: "撤销上一步", keys: "Ctrl+Z" },
        { label: "专注一轮", keys: "Ctrl+Shift+F" },
        { label: "打开设置", keys: "Ctrl+," }
    ]

    // 主题预设当前名（只读展示；换主题在标题栏的主题按钮里）
    readonly property string currentThemeName: {
        const list = Theme.presets
        const preset = list[Theme.presetIndex]
        return preset && preset.name ? preset.name : "（未知主题）"
    }

    // 画质当前档（只读展示）
    readonly property string currentQualityName: Theme.qualityNames[Theme.quality] || ""

    // 面板内复用的小按钮
    component DataButton: Rectangle {
        id: btn
        property string label: ""
        property color accentColor: Theme.cyan
        signal triggered()

        height: 30
        radius: Theme.radiusControl
        color: btnMouse.pressed ? Theme.elevated : Theme.card
        border.width: 1
        border.color: btnMouse.containsMouse
                         ? Theme.alpha(accentColor, 0.55)
                         : Theme.border

        Text {
            anchors.centerIn: parent
            text: btn.label
            color: btn.accentColor
            font.pixelSize: Theme.fontCaption
        }

        MouseArea {
            id: btnMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.triggered()
        }
    }

    // 面板内复用的下拉框（Basic 样式默认外观与霓虹面板不搭，这里统一描边与配色）
    // labelRole 是自定义字段名：ComboBox.textRole 是 FINAL，不能覆盖。
    component NeonComboBox: ComboBox {
        id: combo
        property string labelRole: ""
        property string emptyText: ""
        Layout.preferredHeight: 30
        font.pixelSize: Theme.fontMicro

        // displayText 对 QVariantList<map> 会退化成空串，必须按 labelRole 自己取
        readonly property string currentLabel: {
            if (combo.currentIndex < 0 || combo.currentIndex >= combo.count)
                return ""
            const item = combo.model[combo.currentIndex]
            if (combo.labelRole.length > 0 && item)
                return item[combo.labelRole] !== undefined ? String(item[combo.labelRole]) : ""
            return combo.displayText
        }

        contentItem: Text {
            leftPadding: 8
            rightPadding: 20
            text: combo.currentIndex >= 0 ? combo.currentLabel : combo.emptyText
            color: combo.currentIndex >= 0 ? Theme.text : Theme.muted
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.input
            border.width: 1
            border.color: Theme.border
        }

        delegate: ItemDelegate {
            width: combo.width
            height: 28
            contentItem: Text {
                leftPadding: 8
                text: combo.labelRole.length > 0 ? modelData[combo.labelRole] : modelData
                color: Theme.text
                font.pixelSize: Theme.fontMicro
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: parent.highlighted ? Theme.accentSoft : "transparent"
            }
        }

        popup: Popup {
            y: combo.height + 2
            width: combo.width
            implicitHeight: contentItem.implicitHeight
            padding: 1
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            background: Rectangle {
                radius: Theme.radiusControl
                color: Theme.panelSolid
                border.width: 1
                border.color: Theme.border
            }
        }
    }

    // 面板内复用的单行输入框
    component NeonField: TextField {
        Layout.fillWidth: true
        Layout.preferredHeight: 28
        color: Theme.text
        font.pixelSize: Theme.fontMicro
        selectByMouse: true
        background: Rectangle {
            color: Theme.input
            radius: Theme.radiusControl
            border.width: 1
            border.color: Theme.border
        }
    }

    // profile 编辑框：map 为空表示新建
    function editProfile(map) {
        profileEditor.editingId = map && map.id ? map.id : ""
        profileEditor.baseEnv = map && map.env ? map.env : ({ "NO_COLOR": "1" })
        profileIdField.text = map && map.id ? map.id : ""
        profileNameField.text = map && map.displayName ? map.displayName : ""
        profileExeField.text = map && map.exe ? map.exe : ""
        profileArgsField.text = map && map.args ? map.args.join(" ") : ""
        profileReadonlyField.text = map && map.readonlyArgs ? map.readonlyArgs.join(" ") : ""
        profilePromptCombo.currentIndex = Math.max(0, profilePromptModes.indexOf(
            map && map.promptMode ? map.promptMode : "arg"))
        profileOutputCombo.currentIndex = Math.max(0, profileOutputModes.indexOf(
            map && map.output ? map.output : "text"))
        profileTimeoutField.text = String(map && map.defaultTimeoutMin ? map.defaultTimeoutMin : 30)
        profileEditor.errorText = ""
        profileEditor.open()
    }

    property var profilePromptModes: ["arg", "stdin"]
    property var profileOutputModes: ["stream-json", "json", "text"]

    function splitArgs(text) {
        const trimmed = String(text || "").trim()
        return trimmed.length === 0 ? [] : trimmed.split(/\s+/)
    }

    // 未归属卡数：函数调用不进依赖图，显式引用 revision 才会随归属变化刷新
    readonly property int unassignedTasks: {
        const rev = projectStore.revision
        return projectStore.unassignedCount()
    }

    // 项目新增（R18-01）：createProject 对空名返回空串，据此提示而不是静默吞掉
    function newProject() {
        const name = newProjectField.text.trim()
        if (name.length === 0) {
            projectSettingsHint.text = "项目名不能为空"
            return
        }
        const id = projectStore.createProject(name)
        if (id.length > 0) {
            newProjectField.text = ""
            projectSettingsHint.text = "已创建「" + name + "」"
        } else {
            projectSettingsHint.text = "创建失败：「" + name + "」"
        }
    }

    // 仓库绑定（R18-02 / R18-03）：目录选择 → fail-closed 路径校验 →
    // 确认对话框（无台账的仓库提供生成脚手架的开关，默认勾上）。
    // 目录选择走 QML FolderDialog（QtQuick.Dialogs），不引入 QtWidgets 依赖。
    property string bindProjectId: ""

    function bindProjectRepo(projectId) {
        panel.bindProjectId = projectId
        repoFolderDialog.open()
    }

    function onRepoPicked(path) {
        if (path.length === 0)
            return
        const invalid = projectStore.validateRepoPath(path)
        if (invalid.length > 0) {
            projectSettingsHint.text = invalid
            return
        }
        repoBindDialog.projectId = panel.bindProjectId
        repoBindDialog.repoPath = path
        repoBindDialog.scaffoldNeeded = !projectStore.repoHasScaffold(path)
        repoBindDialog.errorText = ""
        repoBindDialog.open()
    }

    // 导入来源：数据目录下已导出的文件 + 手动输入路径
    function refreshExports() {
        exportCombo.model = dataPort.listExports()
        if (exportCombo.model.length > 0 && exportCombo.currentIndex < 0)
            exportCombo.currentIndex = 0
    }

    // 每次打开回到第一页（与原型一致），同时刷新导入文件清单
    onOpened: {
        panel.page = "hotkey"
        panel.refreshExports()
    }

    // ---- 左侧固定分页导航（壳给的 160 px 一列，不参与滚动）----
    navRail: Component {
        Item {
            id: rail

            implicitWidth: 160
            implicitHeight: 240

            // 与正文之间的分隔线
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.hairline
            }

            Column {
                id: railColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingSm
                spacing: Theme.spacingXs

                Repeater {
                    model: panel.pages

                    delegate: Rectangle {
                        id: navRow
                        required property var modelData

                        width: railColumn.width
                        height: 30
                        radius: Theme.radiusControl
                        color: panel.page === navRow.modelData.key
                               ? Theme.accentSoft
                               : (navMouse.containsMouse ? Theme.card : "transparent")
                        border.width: 1
                        border.color: panel.page === navRow.modelData.key
                                      ? Theme.alpha(Theme.cyan, 0.45)
                                      : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSm
                            anchors.rightMargin: Theme.spacingSm
                            spacing: Theme.spacingXs

                            Text {
                                Layout.fillWidth: true
                                text: navRow.modelData.label
                                color: panel.page === navRow.modelData.key ? Theme.cyan : Theme.muted
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                            }

                            // 插件页角标：已装插件数
                            Rectangle {
                                visible: navRow.modelData.key === "plugin"
                                width: pluginBadge.implicitWidth + 8
                                height: 15
                                radius: Theme.radiusControl
                                color: Theme.alpha(Theme.cyan, 0.16)
                                border.width: 1
                                border.color: Theme.alpha(Theme.cyan, 0.40)

                                Text {
                                    id: pluginBadge
                                    anchors.centerIn: parent
                                    text: pluginLoader.plugins.length + ""
                                    color: Theme.cyan
                                    font.pixelSize: Theme.fontMicro
                                }
                            }
                        }

                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.page = navRow.modelData.key
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: navRow.modelData.label
                        Accessible.description: "切换到「" + navRow.modelData.label + "」页"
                    }
                }
            }
        }
    }

    // ================= 分页 1：热键与自动化 =================
    ColumnLayout {
        width: panel.bodyWidth
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
        visible: panel.page === "hotkey"
        spacing: Theme.spacingMd

        // ① 全局热键
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "全局热键"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 3

                    Text {
                        text: "全局热键"
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                    }

                    Text {
                        text: hotkey.registered ? "Ctrl + Shift + Space" : "注册失败（可能被占用）"
                        color: hotkey.registered ? Theme.muted : Theme.magenta
                        font.pixelSize: Theme.fontMicro
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: hotkey.registered ? "ACTIVE" : "OFF"
                    color: hotkey.registered ? Theme.cyan : Theme.magenta
                    font.family: "Cascadia Code"
                    font.pixelSize: Theme.fontMicro
                }
            }

            // 只读快捷键清单：固定键位不可改，列出来免得用户到处找
            Repeater {
                model: panel.shortcuts

                delegate: RowLayout {
                    id: shortcutRow
                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        Layout.fillWidth: true
                        text: shortcutRow.modelData.label
                        color: Theme.text
                        font.pixelSize: Theme.fontCaption
                    }

                    Text {
                        text: shortcutRow.modelData.keys
                        color: Theme.muted
                        font.family: "Cascadia Code"
                        font.pixelSize: Theme.fontMicro
                    }
                }
            }
        }

        // ② 开机与提醒
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "开机与提醒"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 3

                    Text {
                        text: "开机自启"
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                    }

                    Text {
                        text: "登录 Windows 后静默启动到托盘"
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                    }
                }

                Item { Layout.fillWidth: true }

                Switch {
                    checked: tray.isAutostart()
                    onToggled: tray.setAutostart(checked)
                }
            }
        }
    }

    // ================= 分页 2：数据与备份 =================
    ColumnLayout {
        width: panel.bodyWidth
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
        visible: panel.page === "data"
        spacing: Theme.spacingMd

        // ① 数据文件
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Text {
                text: "数据文件"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            Text {
                Layout.fillWidth: true
                text: database.path
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideMiddle
            }
        }

        // ② 导入导出与备份
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Text {
                text: "导入导出与备份"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            Text {
                Layout.fillWidth: true
                text: "备份目录 " + dataPort.backupDir
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideMiddle
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8

                DataButton {
                    label: "导出 JSON"
                    Layout.fillWidth: true
                    onTriggered: {
                        var path = dataPort.defaultPath("json")
                        var error = dataPort.exportTasks(path, "json")
                        dataHint.text = error.isEmpty() ? "已导出：" + path : "导出失败：" + error
                    }
                }

                DataButton {
                    label: "导出 Markdown"
                    Layout.fillWidth: true
                    onTriggered: {
                        var path = dataPort.defaultPath("markdown")
                        var error = dataPort.exportTasks(path, "markdown")
                        dataHint.text = error.isEmpty() ? "已导出：" + path : "导出失败：" + error
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ComboBox {
                    id: exportCombo
                    Layout.fillWidth: true
                    Layout.minimumWidth: 90
                    Layout.preferredHeight: 30
                    model: []
                    font.pixelSize: Theme.fontMicro
                    displayText: (currentIndex >= 0 && currentText.length > 0)
                                 ? currentText.split(/[\\/]/).pop()
                                 : "（数据目录下暂无导出文件）"

                    contentItem: Text {
                        leftPadding: 8
                        rightPadding: 20
                        text: exportCombo.displayText
                        color: Theme.text
                        font: exportCombo.font
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                    }

                    background: Rectangle {
                        radius: Theme.radiusControl
                        color: Theme.input
                        border.width: 1
                        border.color: Theme.border
                    }

                    delegate: ItemDelegate {
                        width: exportCombo.width
                        height: 28
                        contentItem: Text {
                            text: modelData
                            color: Theme.text
                            font.pixelSize: Theme.fontMicro
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideMiddle
                            leftPadding: 6
                        }
                        background: Rectangle {
                            color: parent.highlighted ? Theme.accentSoft : "transparent"
                        }
                    }

                    popup: Popup {
                        y: exportCombo.height + 2
                        width: exportCombo.width
                        implicitHeight: contentItem.implicitHeight
                        padding: 1
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: exportCombo.popup.visible ? exportCombo.delegateModel : null
                            currentIndex: exportCombo.highlightedIndex
                            ScrollIndicator.vertical: ScrollIndicator {}
                        }
                        background: Rectangle {
                            radius: Theme.radiusControl
                            color: Theme.panelSolid
                            border.width: 1
                            border.color: Theme.border
                        }
                    }
                }

                DataButton {
                    label: "导入所选"
                    Layout.preferredWidth: 84
                    onTriggered: {
                        if (exportCombo.currentIndex < 0) {
                            dataHint.text = "请先选择要导入的文件"
                            return
                        }
                        var path = exportCombo.currentText
                        var error = dataPort.importTasks(path, "auto")
                        dataHint.text = error.isEmpty() ? "导入完成：" + path : "导入失败：" + error
                    }
                }

                DataButton {
                    label: "立即备份"
                    accentColor: Theme.success
                    Layout.preferredWidth: 84
                    onTriggered: {
                        var error = dataPort.backupNow()
                        dataHint.text = error.isEmpty()
                                        ? "备份完成：" + dataPort.backupDir
                                        : "备份失败：" + error
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TextField {
                    id: importPathField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    placeholderText: "或粘贴文件完整路径后回车导入"
                    color: Theme.text
                    font.pixelSize: Theme.fontMicro
                    selectByMouse: true
                    background: Rectangle {
                        color: Theme.input
                        radius: Theme.radiusControl
                        border.width: 1
                        border.color: Theme.border
                    }
                    onAccepted: {
                        var path = text.trim()
                        if (path.length === 0) {
                            dataHint.text = "请输入文件路径"
                            return
                        }
                        var error = dataPort.importTasks(path, "auto")
                        dataHint.text = error.isEmpty() ? "导入完成：" + path : "导入失败：" + error
                    }
                }
            }

            Text {
                id: dataHint
                Layout.fillWidth: true
                text: "支持 JSON / Markdown / Todo.txt 三种格式；每天首次启动自动备份一次数据库。"
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        // ③ 项目（R18-01）：项目管理纯在视图侧 —— 新建 / 重命名 / 配色 / 删除项目。
        // 卡片本体不碰：删除只清归属（ProjectStore.removeProject 语义），不级联删卡。
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "项目"
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                }

                Text {
                    text: panel.unassignedTasks + " 张卡未归属"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                NeonField {
                    id: newProjectField
                    placeholderText: "新项目名，回车创建"
                    onAccepted: panel.newProject()
                }

                DataButton {
                    label: "添加"
                    Layout.preferredWidth: 56
                    onTriggered: panel.newProject()
                }
            }

            Repeater {
                model: {
                    const rev = projectStore.revision
                    return projectStore.projects()
                }

                Rectangle {
                    id: projectRow
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    implicitHeight: projectRowColumn.implicitHeight + 16
                    radius: Theme.radiusControl
                    color: Theme.card
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: projectRowColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                width: 10
                                height: 10
                                radius: 5
                                color: projectRow.modelData.color.length > 0
                                       ? projectRow.modelData.color : Theme.muted
                                opacity: projectRow.modelData.color.length > 0 ? 1 : 0.4
                            }

                            NeonField {
                                id: projectNameField
                                Layout.fillWidth: true
                                text: projectRow.modelData.name
                                onAccepted: {
                                    const name = projectNameField.text.trim()
                                    if (name.length > 0 && name !== projectRow.modelData.name)
                                        projectStore.renameProject(projectRow.modelData.id, name)
                                    else
                                        projectNameField.text = projectRow.modelData.name
                                }
                            }

                            Text {
                                text: projectRow.modelData.taskCount + " 张卡"
                                color: Theme.muted
                                font.pixelSize: Theme.fontMicro
                            }

                            Text {
                                text: "删除"
                                color: Theme.magenta
                                font.pixelSize: Theme.fontMicro

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: projectStore.removeProject(projectRow.modelData.id)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: "颜色"
                                color: Theme.muted
                                font.pixelSize: Theme.fontMicro
                            }

                            Repeater {
                                model: ["", "#a78bfa", "#ff5ec4", "#59d6ff", "#3ddc97", "#ffb454"]
                                delegate: Rectangle {
                                    required property var modelData
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: modelData.length > 0 ? modelData : "transparent"
                                    border.width: 1
                                    border.color: projectRow.modelData.color === modelData
                                           ? Theme.text
                                           : (modelData.length > 0 ? "transparent" : Theme.border)

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: projectStore.setProjectColor(projectRow.modelData.id, modelData)
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        // 仓库绑定（R18-02）：绑定后该项目的卡写回 <仓库>/progress.json，
                        // 面板每 15 秒轮询该文件；解绑后卡保留、家文件回落到全局快照
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: "仓库"
                                color: Theme.muted
                                font.pixelSize: Theme.fontMicro
                            }

                            Text {
                                Layout.fillWidth: true
                                text: projectRow.modelData.repoPath.length > 0
                                       ? projectRow.modelData.repoPath : "未绑定（卡写回全局快照）"
                                color: Theme.muted
                                font.pixelSize: Theme.fontMicro
                                elide: Text.ElideMiddle
                            }

                            Text {
                                text: projectRow.modelData.repoPath.length > 0 ? "解绑" : "绑定…"
                                color: projectRow.modelData.repoPath.length > 0 ? Theme.magenta : Theme.cyan
                                font.pixelSize: Theme.fontMicro

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (projectRow.modelData.repoPath.length > 0)
                                            projectStore.setRepoPath(projectRow.modelData.id, "")
                                        else
                                            panel.bindProjectRepo(projectRow.modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                id: projectSettingsHint
                Layout.fillWidth: true
                text: "项目只是卡片的分类层：重命名 / 删除不动卡片本体，删除只清归属（卡变未归属）。卡片归属在卡片详情抽屉里设置。"
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }
    }

    // ================= 分页 3：外观与动效 =================
    ColumnLayout {
        width: panel.bodyWidth
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
        visible: panel.page === "appearance"
        spacing: Theme.spacingMd

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "主题"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "主题预设"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: panel.currentThemeName
                    color: Theme.cyan
                    font.pixelSize: Theme.fontMicro
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "画质"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: panel.currentQualityName
                    color: Theme.cyan
                    font.pixelSize: Theme.fontMicro
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "减弱动效"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: Motion.reduced ? "已开启" : "未开启"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: "完整的外观面板在标题栏的主题按钮里，那里的主题是色卡网格。"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
    }

    // ================= 分页 4：任务执行 =================
    // 任务执行（R6-03）：总开关默认关；profile 存 meta 表，命令行只拼 argv
    ColumnLayout {
        width: panel.bodyWidth
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
        visible: panel.page === "exec"
        spacing: 3

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "任务执行"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            Item { Layout.fillWidth: true }

            Switch {
                checked: agentProfile.executorEnabled
                onToggled: agentProfile.setExecutorEnabled(checked)
            }
        }

        Text {
            Layout.fillWidth: true
            text: agentProfile.executorEnabled
                  ? "总开关已开：卡片执行前仍会弹出确认框，cwd 首次执行需勾选「信任本目录」"
                  : "总开关默认关：卡片上的「执行」只提示原因，不会启动任何进程"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8

            Text {
                text: "默认 agent"
                color: Theme.text
                font.pixelSize: Theme.fontCaption
            }

            NeonComboBox {
                id: defaultAgentCombo
                Layout.fillWidth: true
                model: agentProfile.profiles
                labelRole: "displayName"
                emptyText: "（未指定）"
                currentIndex: {
                    const list = agentProfile.profiles
                    for (let i = 0; i < list.length; ++i) {
                        if (list[i].id === agentProfile.defaultAgent)
                            return i
                    }
                    return -1
                }
                onActivated: (index) => agentProfile.setDefaultAgent(model[index].id)
            }
        }

        Repeater {
            model: agentProfile.profiles

            Rectangle {
                id: profileRow
                required property var modelData

                Layout.fillWidth: true
                Layout.topMargin: 2
                implicitHeight: profileRowColumn.implicitHeight + 16
                radius: Theme.radiusControl
                color: Theme.card
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: profileRowColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: profileRow.modelData.displayName
                            color: Theme.text
                            font.pixelSize: Theme.fontCaption
                        }

                        // R9-04：exe 在本机解析不到时显式标注。
                        // 不标的话列表里这一行与可用的长得一样，派活时才失败，
                        // 而失败会被执行器记成「进程启动失败」，看不出根因是 CLI 没装。
                        Rectangle {
                            visible: profileRow.modelData.available === false
                            width: unavailableLabel.implicitWidth + 10
                            height: 15
                            radius: Theme.radiusControl
                            color: Theme.alpha(Theme.warning, 0.14)
                            border.width: 1
                            border.color: Theme.warning

                            Text {
                                id: unavailableLabel
                                anchors.centerIn: parent
                                text: "不可用"
                                color: Theme.warning
                                font.pixelSize: Theme.fontMicro
                            }
                        }

                        Text {
                            text: profileRow.modelData.id
                            color: Theme.muted
                            font.family: "Consolas"
                            font.pixelSize: Theme.fontMicro
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "编辑"
                            color: Theme.cyan
                            font.pixelSize: Theme.fontMicro

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.editProfile(profileRow.modelData)
                            }
                        }

                        Text {
                            text: "删除"
                            color: Theme.magenta
                            font.pixelSize: Theme.fontMicro

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (agentProfile.removeProfile(profileRow.modelData.id))
                                        execSettingsHint.text = "已删除 " + profileRow.modelData.id
                                    else
                                        execSettingsHint.text = "删除失败：" + profileRow.modelData.id
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: profileRow.modelData.exe + " "
                              + (profileRow.modelData.args || []).join(" ")
                              + (profileRow.modelData.promptMode === "stdin"
                                 ? " · prompt 走 stdin" : "")
                              + (profileRow.modelData.available === false
                                 ? " · 已声明但不可用（exe 找不到）" : "")
                        color: profileRow.modelData.available === false
                               ? Theme.warning : Theme.muted
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideRight
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8

            DataButton {
                label: "新增 profile"
                Layout.fillWidth: true
                onTriggered: panel.editProfile(null)
            }

            DataButton {
                label: "恢复内置"
                accentColor: Theme.violet
                Layout.fillWidth: true
                onTriggered: {
                    agentProfile.resetProfiles()
                    execSettingsHint.text = "已恢复内置三条 profile"
                }
            }
        }

        Text {
            id: execSettingsHint
            Layout.fillWidth: true
            text: "profile 存 meta 表；命令行只拼 argv，不经过 shell。codex 未实测，不内置。"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
    }

    // ================= 分页 5：插件 =================
    ColumnLayout {
        width: panel.bodyWidth
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
        visible: panel.page === "plugin"
        spacing: Theme.spacingMd

        // ① 已装插件
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "已装插件"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            Text {
                Layout.fillWidth: true
                text: pluginLoader.plugins.length + " 个已发现"
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: pluginLoader.plugins.length === 0
                text: "当前没有已装插件"
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: pluginLoader.plugins

                Rectangle {
                    id: pluginRow
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    implicitHeight: pluginRowColumn.implicitHeight + 12
                    radius: Theme.radiusControl
                    color: Theme.card
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: pluginRowColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 6
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            Text {
                                text: pluginRow.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fontCaption
                            }

                            Text {
                                text: pluginRow.modelData.id
                                color: Theme.muted
                                font.pixelSize: Theme.fontMicro
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: pluginRow.modelData.status === "ok" ? "已装" : "异常"
                                color: pluginRow.modelData.status === "ok" ? Theme.success : Theme.danger
                                font.pixelSize: Theme.fontMicro
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: pluginRow.modelData.status !== "ok"
                            text: pluginRow.modelData.error
                            color: Theme.danger
                            font.pixelSize: Theme.fontMicro
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        // ② 插件来源与管理：目录 / 市场源 / 重新扫描都在这一页
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "插件来源与管理"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "插件目录"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }
            }

            Text {
                Layout.fillWidth: true
                text: pluginLoader.pluginDir
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSm
                text: "插件市场源"
                color: Theme.text
                font.pixelSize: Theme.fontCaption
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: pluginLibrary.marketSources.length + " 个源 · "
                          + (pluginLibrary.marketSource || "未配置")
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                    elide: Text.ElideRight
                }

                Text {
                    text: "管理源…"
                    color: marketManageMouse.containsMouse ? Theme.cyan : Theme.text
                    font.pixelSize: Theme.fontCaption
                    MouseArea {
                        id: marketManageMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: panel.openLibraryRequested()
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: "支持 owner/repo 简写（自动尝试 Gitee → GitHub）与完整 market.json 地址，多源并列生效"
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    height: 30
                    radius: Theme.radiusControl
                    color: rescanArea.pressed ? Theme.elevated : Theme.card
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "重新扫描插件"
                        color: Theme.cyan
                        font.pixelSize: Theme.fontCaption
                    }

                    MouseArea {
                        id: rescanArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            pluginRuntime.reload()
                            rescanHint.text = "已重新扫描，后台插件已重载"
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 30
                    radius: Theme.radiusControl
                    color: libraryArea.pressed ? Theme.elevated : Theme.card
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "打开插件库"
                        color: Theme.cyan
                        font.pixelSize: Theme.fontCaption
                    }

                    MouseArea {
                        id: libraryArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.openLibraryRequested()
                    }
                }
            }

            Text {
                id: rescanHint
                Layout.fillWidth: true
                text: "把插件目录放进上面的路径后点「重新扫描插件」"
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        // ③ 插件设置页（manifest 里声明 settings 的插件）
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "插件设置"
                color: Theme.text
                font.pixelSize: Theme.fontCaption
            }

            Text {
                Layout.fillWidth: true
                visible: pluginRuntime.settingsPages.length === 0
                text: "当前没有插件提供设置页（manifest 里声明 settings 即可出现在这里）"
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: pluginRuntime.settingsPages

                Rectangle {
                    id: settingsPageRow
                    required property var modelData

                    Layout.fillWidth: true
                    implicitHeight: settingsPageColumn.implicitHeight + 20
                    radius: Theme.radiusControl
                    color: Theme.card
                    border.width: 1
                    border.color: settingsPageRow.expanded ? Theme.borderStrong : Theme.border

                    property bool expanded: false

                    ColumnLayout {
                        id: settingsPageColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 10
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                Layout.fillWidth: true
                                text: settingsPageRow.modelData.pluginName
                                color: Theme.text
                                font.pixelSize: Theme.fontCaption
                            }

                            Text {
                                text: settingsPageRow.modelData.sourceLabel
                                color: Theme.muted
                                font.pixelSize: Theme.fontMicro
                            }

                            Text {
                                text: settingsPageRow.expanded ? "收起" : "展开"
                                color: Theme.cyan
                                font.pixelSize: Theme.fontMicro

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: settingsPageRow.expanded = !settingsPageRow.expanded
                                }
                            }
                        }

                        Loader {
                            id: settingsPageLoader
                            Layout.fillWidth: true
                            Layout.preferredHeight: item ? item.implicitHeight : 0
                            active: settingsPageRow.expanded
                            source: settingsPageRow.modelData.url

                            onLoaded: {
                                if (item && item.host !== undefined)
                                    item.host = pluginRuntime.hostFor(settingsPageRow.modelData.pluginId)
                            }
                        }
                    }
                }
            }
        }
    }

    // ================= 分页 6：关于 =================
    ColumnLayout {
        Layout.fillWidth: true
        visible: panel.page === "about"
        spacing: Theme.spacingMd

        // ① 版本与快捷键
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "关于"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "NeonBoard 0.1.0 · Qt 6.8 · SQLite"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Ctrl+Z 撤销上一步"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }
            }
        }

        // ② 软件更新
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "软件更新"
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: updateManager.currentVersion
                    color: Theme.muted
                    font.family: "Cascadia Code"
                    font.pixelSize: Theme.fontMicro
                }
            }

            Text {
                Layout.fillWidth: true
                text: (updateManager.installKind === "installed"
                       ? "安装版 · 下载后静默升级"
                       : "便携版 · 退出后自动替换文件")
                      + (updateManager.lastCheckAt > 0
                         ? " · 上次检查 " + Qt.formatDateTime(new Date(updateManager.lastCheckAt * 1000), "MM-dd hh:mm")
                         : " · 尚未检查")
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: updateManager.statusText
                color: updateManager.state === "error" ? Theme.danger
                     : updateManager.updateAvailable ? Theme.cyan
                     : Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            // 安装回执版本与自身编译版本不符 = 上次静默升级被占用文件打断（R5-09 留下的物证）。
            // 这条异常过去只写进日志文件，用户看不到也不知道该做什么；显性化在更新区，
            // 并让整条可点 —— 就地重查更新是最直接的补救动作。
            Rectangle {
                Layout.fillWidth: true
                visible: updateManager.lastInstallIncomplete
                implicitHeight: incompleteHint.implicitHeight + 12
                radius: Theme.radiusControl
                color: Theme.warningSoft
                border.width: 1
                border.color: Theme.warning

                Text {
                    id: incompleteHint
                    anchors.fill: parent
                    anchors.margins: 6
                    text: "上次更新未完成：安装时被占用的文件没能替换，当前可能是新旧混装。点这里重新检查更新。"
                    color: Theme.warning
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    enabled: updateManager.state !== "checking"
                             && updateManager.state !== "downloading"
                             && updateManager.state !== "installing"
                    onClicked: updateManager.checkForUpdates(false)
                }
            }

            // 有更新时展示版本与更新日志；日志可能很长，截断 6 行不撑爆面板
            Text {
                Layout.fillWidth: true
                visible: updateManager.updateAvailable
                text: "新版本 " + updateManager.latestVersion
                      + (updateManager.publishedAt.length > 0 ? "（" + updateManager.publishedAt + "）" : "")
                      + (updateManager.releaseSize > 0
                         ? " · " + (updateManager.releaseSize / 1048576).toFixed(1) + " MB" : "")
                      + (updateManager.releaseNotes.length > 0 ? "\n" + updateManager.releaseNotes : "")
                color: Theme.text
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                maximumLineCount: 6
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8

                DataButton {
                    label: updateManager.state === "checking" ? "检查中…" : "检查更新"
                    Layout.fillWidth: true
                    enabled: updateManager.state !== "checking"
                             && updateManager.state !== "downloading"
                             && updateManager.state !== "installing"
                    opacity: enabled ? 1 : 0.45
                    onTriggered: updateManager.checkForUpdates(false)
                }

                DataButton {
                    label: updateManager.state === "downloading" ? ("下载 " + updateManager.progress + "%")
                         : updateManager.state === "verifying" ? "校验中…"
                         : updateManager.state === "installing" ? "安装中…"
                         : "立即更新"
                    accentColor: Theme.cyan
                    Layout.fillWidth: true
                    enabled: updateManager.state === "available"
                    opacity: enabled ? 1 : 0.45
                    onTriggered: updateManager.downloadAndInstall()
                }

                DataButton {
                    label: "下载页"
                    accentColor: Theme.violet
                    Layout.preferredWidth: 68
                    onTriggered: updateManager.openDownloadPage()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "自动检查"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    visible: updateManager.updateAvailable
                    text: "跳过此版本"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: updateManager.skipVersion(updateManager.latestVersion)
                    }
                }

                Switch {
                    checked: updateManager.autoCheck
                    onToggled: updateManager.setAutoCheck(checked)
                }
            }

            Text {
                Layout.fillWidth: true
                text: "更新包下载后先校验 SHA-256 再安装；安装版走静默升级，便携版由自更新脚本在程序退出后替换文件并重新拉起。"
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }
    }

    // profile 编辑框（新增 / 修改）；放根层，避免被滚动区裁剪。
    // R11-10：根节点变成 DialogShell（Popup）后，parent 不能再指向 Popup 本身
    //（Popup 不是 QQuickItem，赋过去会报类型错），改指壳的 contentItem ——
    // 位置与原先一致（居中于设置面板），层级也在壳的内容之上。
    Dialog {
        id: profileEditor
        parent: panel.contentItem
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 18
        width: Math.min(480, panel.width - 60)
        closePolicy: Popup.CloseOnEscape
        property string editingId: ""
        property var baseEnv: ({})
        property string errorText: ""

        background: Rectangle {
            color: Theme.panelSolid
            radius: Theme.radiusPanel
            border.width: 1
            border.color: Theme.alpha(Theme.cyan, 0.35)
        }

        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: profileEditor.editingId.length > 0 ? "编辑 profile" : "新增 profile"
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.bold: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 6

                Text {
                    text: "id"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonField {
                    id: profileIdField
                    enabled: profileEditor.editingId.length === 0
                    placeholderText: "唯一 id（字母/数字/._-）；同名会覆盖"
                }

                Text {
                    text: "显示名"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonField {
                    id: profileNameField
                    placeholderText: "设置面板里显示的名字"
                }

                Text {
                    text: "可执行文件"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonField {
                    id: profileExeField
                    placeholderText: "opencode / claude / 绝对路径"
                }

                Text {
                    text: "参数"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonField {
                    id: profileArgsField
                    placeholderText: "按空格拆分，如 run --format json"
                }

                Text {
                    text: "只读参数"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonField {
                    id: profileReadonlyField
                    placeholderText: "只读模式追加的参数，可空"
                }

                Text {
                    text: "prompt"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonComboBox {
                    id: profilePromptCombo
                    model: panel.profilePromptModes
                }

                Text {
                    text: "输出格式"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonComboBox {
                    id: profileOutputCombo
                    model: panel.profileOutputModes
                }

                Text {
                    text: "默认超时"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                RowLayout {
                    spacing: 6

                    NeonField {
                        id: profileTimeoutField
                        Layout.preferredWidth: 70
                        Layout.fillWidth: false
                        horizontalAlignment: TextInput.AlignHCenter
                        validator: IntValidator { bottom: 1; top: 480 }
                    }

                    Text {
                        text: "分钟"
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                    }

                    Item { Layout.fillWidth: true }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: profileEditor.errorText.length > 0
                text: profileEditor.errorText
                color: Theme.danger
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8

                DataButton {
                    label: "删除"
                    accentColor: Theme.magenta
                    visible: profileEditor.editingId.length > 0
                    Layout.preferredWidth: 72
                    onTriggered: {
                        if (agentProfile.removeProfile(profileEditor.editingId)) {
                            execSettingsHint.text = "已删除 " + profileEditor.editingId
                            profileEditor.close()
                        } else {
                            profileEditor.errorText = "删除失败：" + profileEditor.editingId
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                DataButton {
                    label: "取消"
                    Layout.preferredWidth: 72
                    onTriggered: profileEditor.close()
                }

                DataButton {
                    label: "保存"
                    Layout.preferredWidth: 72
                    onTriggered: {
                        const id = profileIdField.text.trim()
                        if (id.length === 0) {
                            profileEditor.errorText = "id 不能为空"
                            return
                        }
                        if (profileExeField.text.trim().length === 0) {
                            profileEditor.errorText = "可执行文件不能为空"
                            return
                        }
                        const timeout = parseInt(profileTimeoutField.text, 10)
                        if (isNaN(timeout) || timeout < 1 || timeout > 480) {
                            profileEditor.errorText = "超时必须是 1–480 的整数"
                            return
                        }
                        const ok = agentProfile.saveProfile({
                            id: id,
                            displayName: profileNameField.text.trim(),
                            exe: profileExeField.text.trim(),
                            args: panel.splitArgs(profileArgsField.text),
                            promptMode: panel.profilePromptModes[profilePromptCombo.currentIndex],
                            readonlyArgs: panel.splitArgs(profileReadonlyField.text),
                            output: panel.profileOutputModes[profileOutputCombo.currentIndex],
                            env: profileEditor.baseEnv,
                            defaultTimeoutMin: timeout
                        })
                        if (!ok) {
                            profileEditor.errorText = "保存失败：请检查 id 与参数格式"
                            return
                        }
                        execSettingsHint.text = "已保存 " + id
                        profileEditor.close()
                    }
                }
            }
        }
    }

    // 目录选择器（R18-02）：QML 原生 FolderDialog，避免引入 QtWidgets 依赖
    FolderDialog {
        id: repoFolderDialog
        title: "选择要绑定的项目仓库目录"
        onAccepted: panel.onRepoPicked(selectedFolder)
    }

    // 仓库绑定对话框（R18-02 / R18-03）：确认路径 + 无台账仓库的脚手架开关；
    // 放根层，避免被滚动区裁剪（parent 说明同 profileEditor）
    Dialog {
        id: repoBindDialog
        parent: panel.contentItem
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 18
        width: Math.min(480, panel.width - 60)
        closePolicy: Popup.CloseOnEscape
        property string projectId: ""
        property string repoPath: ""
        property bool scaffoldNeeded: false
        property string errorText: ""

        onOpened: scaffoldCheck.checked = true

        background: Rectangle {
            color: Theme.panelSolid
            radius: Theme.radiusPanel
            border.width: 1
            border.color: Theme.alpha(Theme.cyan, 0.35)
        }

        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: "绑定仓库"
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                text: repoBindDialog.repoPath
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            CheckBox {
                id: scaffoldCheck
                Layout.fillWidth: true
                visible: repoBindDialog.scaffoldNeeded
                text: "该仓库还没有台账文件，生成脚手架（progress.json + docs/board-ledger.json，既有文件不覆盖）"
                checked: true
                font.pixelSize: Theme.fontCaption

                // CheckBox 在 Qt 6.8 没有 color 属性：写 `color:` 会让整份面板加载失败
                // （R18-02 引入时留下的硬错）。复选框的青色走自定义 indicator，与 Main.qml 同款。
                indicator: Rectangle {
                    implicitWidth: 16
                    implicitHeight: 16
                    radius: 4
                    color: scaffoldCheck.checked
                           ? Theme.alpha(Theme.cyan, 0.32)
                           : Theme.input
                    border.width: 1
                    border.color: scaffoldCheck.hovered ? Theme.cyan : Theme.border

                    Text {
                        anchors.centerIn: parent
                        visible: scaffoldCheck.checked
                        text: "✓"
                        color: Theme.text
                        font.pixelSize: Theme.fontMicro
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: repoBindDialog.errorText.length > 0
                text: repoBindDialog.errorText
                color: Theme.danger
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8

                Item { Layout.fillWidth: true }

                DataButton {
                    label: "取消"
                    Layout.preferredWidth: 72
                    onTriggered: repoBindDialog.close()
                }

                DataButton {
                    label: "绑定"
                    Layout.preferredWidth: 72
                    onTriggered: {
                        const path = repoBindDialog.repoPath
                        if (repoBindDialog.scaffoldNeeded && scaffoldCheck.checked) {
                            const scaffoldError = projectStore.scaffoldRepo(path)
                            if (scaffoldError.length > 0) {
                                repoBindDialog.errorText = scaffoldError
                                return
                            }
                        }
                        if (projectStore.setRepoPath(repoBindDialog.projectId, path)) {
                            projectSettingsHint.text = "已绑定仓库：" + path
                            repoBindDialog.close()
                        } else {
                            repoBindDialog.errorText = "绑定失败：路径未通过校验"
                        }
                    }
                }
            }
        }
    }
}
