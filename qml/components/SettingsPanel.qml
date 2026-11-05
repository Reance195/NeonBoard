import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 设置面板：按需加载。
// 由 Main.qml 的 Loader 在用户第一次打开设置时才构造，避免首帧为「几乎不用」的面板付出代价。
// 注意：本根节点不设 anchors.fill —— 尺寸由 Loader 决定。
Rectangle {
    id: panel

    signal openLibraryRequested()

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

    // 导入来源：数据目录下已导出的文件 + 手动输入路径
    function refreshExports() {
        exportCombo.model = dataPort.listExports()
        if (exportCombo.model.length > 0 && exportCombo.currentIndex < 0)
            exportCombo.currentIndex = 0
    }
    visible: false
    color: Theme.alpha(Theme.bg, 0.45)

    function open() {
        visible = true
        refreshExports()
    }

    function close() {
        visible = false
    }

    MouseArea {
        anchors.fill: parent
        onClicked: panel.close()
    }

    Rectangle {
        width: Math.min(520, parent.width - 48)
        height: Math.min(560, parent.height - 48)
        anchors.centerIn: parent
        radius: Theme.radiusPanel
        color: Theme.panelSolid
        border.width: 1
        border.color: Theme.alpha(Theme.cyan, 0.35)

        MouseArea { anchors.fill: parent }

        ScrollView {
            id: scroller
            anchors.fill: parent
            anchors.margins: 20
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: scroller.availableWidth
                spacing: 14

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "设置"
                    color: Theme.text
                    font.pixelSize: Theme.fontDisplay
                    font.bold: true
                }

                Text {
                    text: "SETTINGS"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                    font.letterSpacing: 1.5
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 26
                    height: 26
                    radius: Theme.radiusControl
                    color: closeSettingsMouse.containsMouse ? Theme.alpha(Theme.text, 0.08) : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: Theme.muted
                        font.pixelSize: Theme.fontBody
                    }

                    MouseArea {
                        id: closeSettingsMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: panel.close()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.alpha(Theme.text, 0.08)
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

            // 任务执行（R6-03）：总开关默认关；profile 存 meta 表，命令行只拼 argv
            ColumnLayout {
                Layout.fillWidth: true
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

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    text: "插件"
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                }

                Text {
                    Layout.fillWidth: true
                    text: pluginLoader.plugins.length + " 个已发现 · 目录 " + pluginLoader.pluginDir
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4

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
                }

                Text {
                    id: rescanHint
                    Layout.fillWidth: true
                    text: "把插件目录放进上面的路径后点「重新扫描插件」"
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
                    Layout.fillWidth: true
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

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
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

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4

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
        }
    }

    // profile 编辑框（新增 / 修改）；放根层，避免被滚动区裁剪
    Dialog {
        id: profileEditor
        parent: panel
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
}
