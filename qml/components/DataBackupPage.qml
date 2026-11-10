import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 数据与备份分页（R18-01/02/03，MOD-19 自 SettingsPanel.qml 原样迁入）：
// 分页 ColumnLayout + 项目/数据函数段 + repoFolderDialog / repoBindDialog 两个专属弹层。
// hostShell = 宿主面板（原 panel，弹层 parent 挂其 contentItem）；visible 由实例化处按页绑定。
ColumnLayout {
    property var hostShell
        Layout.fillWidth: true
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
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
                        dataHint.text = error === "" ? "已导出：" + path : "导出失败：" + error
                    }
                }

                DataButton {
                    label: "导出 Markdown"
                    Layout.fillWidth: true
                    onTriggered: {
                        var path = dataPort.defaultPath("markdown")
                        var error = dataPort.exportTasks(path, "markdown")
                        dataHint.text = error === "" ? "已导出：" + path : "导出失败：" + error
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
                        dataHint.text = error === "" ? "导入完成：" + path : "导入失败：" + error
                    }
                }

                DataButton {
                    label: "立即备份"
                    accentColor: Theme.success
                    Layout.preferredWidth: 84
                    onTriggered: {
                        var error = dataPort.backupNow()
                        dataHint.text = error === ""
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
                        dataHint.text = error === "" ? "导入完成：" + path : "导入失败：" + error
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
                    text: unassignedTasks + " 张卡未归属"
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
                    onAccepted: newProject()
                }

                DataButton {
                    label: "添加"
                    Layout.preferredWidth: 56
                    onTriggered: newProject()
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
                                            bindProjectRepo(projectRow.modelData.id)
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
        bindProjectId = projectId
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
        repoBindDialog.projectId = bindProjectId
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


    // 目录选择器（R18-02）：QML 原生 FolderDialog，避免引入 QtWidgets 依赖
    FolderDialog {
        id: repoFolderDialog
        title: "选择要绑定的项目仓库目录"
        onAccepted: onRepoPicked(selectedFolder)
    }

    // 仓库绑定对话框（R18-02 / R18-03）：确认路径 + 无台账仓库的脚手架开关；
    // 放根层，避免被滚动区裁剪（parent 说明同 profileEditor）
    Dialog {
        id: repoBindDialog
        parent: hostShell.contentItem
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 18
        width: Math.min(480, hostShell.width - 60)
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
