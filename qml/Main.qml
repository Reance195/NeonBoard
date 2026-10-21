import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 600
    visible: false
    title: "NeonBoard"
    color: "#05060A"
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

    function focusNewTask() {
        show()
        raise()
        requestActivate()
        todoColumn.focusQuickAdd()
    }

    function openSettings() {
        show()
        raise()
        requestActivate()
        settingsPanel.visible = true
    }

    function openPluginAt(index) {
        var plugin = pluginLoader.plugins[index]
        if (!plugin || plugin.status !== "ok")
            return
        if (!pluginHost.activate(plugin.id)) {
            pluginPanelTitle.text = "插件激活失败"
            pluginLoaderItem.source = ""
            return
        }
        pluginPanel.visible = true
        pluginPanelTitle.text = plugin.name
        pluginLoaderItem.source = ""
        pluginLoaderItem.source = plugin.mainUrl
    }

    function openPlugins() {
        show()
        raise()
        requestActivate()
        pluginPanel.visible = true
        openPluginAt(0)
    }

    property bool dragActive: false
    property string dragTaskId: ""
    property string dragTaskTitle: ""
    property string dragTaskStatus: ""
    property real dragX: 0
    property real dragY: 0

    function beginTaskDrag(id, title, status, x, y) {
        dragTaskId = id
        dragTaskTitle = title
        dragTaskStatus = status
        dragX = x
        dragY = y
        dragActive = true
    }

    function updateTaskDrag(x, y) {
        dragX = x
        dragY = y
    }

    function endTaskDrag(x, y) {
        if (!dragActive)
            return
        dragX = x
        dragY = y
        var columns = [todoColumn, doingColumn, doneColumn]
        for (var i = 0; i < columns.length; ++i) {
            var column = columns[i]
            var origin = column.mapToItem(root, 0, 0)
            if (x >= origin.x && x <= origin.x + column.width
                    && y >= origin.y && y <= origin.y + column.height) {
                if (column.status !== dragTaskStatus)
                    taskStore.moveTask(dragTaskId, column.status, -1)
                break
            }
        }
        dragActive = false
        dragTaskId = ""
        dragTaskTitle = ""
        dragTaskStatus = ""
    }

    property var lastDeletedTask: null

    function deleteTaskWithUndo(id) {
        lastDeletedTask = taskStore.removeTask(id)
        if (lastDeletedTask && lastDeletedTask.id) {
            undoToast.visible = true
            undoToastTimer.restart()
        }
    }

    function restoreDeleted() {
        if (!lastDeletedTask)
            return
        taskStore.restoreTask(lastDeletedTask)
        lastDeletedTask = null
        undoToast.visible = false
        undoToastTimer.stop()
    }

    Shortcut {
        sequence: "Ctrl+Z"
        onActivated: window.restoreDeleted()
    }

    Shortcut {
        sequence: "Ctrl+N"
        onActivated: window.focusNewTask()
    }

    Shortcut {
        sequence: "Ctrl+,"
        onActivated: window.openSettings()
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
            }

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0.015, 0.030, 0.060, 0.28)
            }
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
            onPressed: {
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

            Row {
                id: windowButtons
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                WinButton {
                    glyph: "—"
                    onClicked: windowController.minimize()
                }

                WinButton {
                    glyph: windowController.maximized ? "❐" : "□"
                    onClicked: windowController.toggleMaximize()
                }

                WinButton {
                    glyph: "✕"
                    danger: true
                    onClicked: window.hide()
                }
            }

            ThemePicker {
                id: themePicker
                anchors.right: windowButtons.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        RowLayout {
            anchors.top: titleBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                color: Qt.rgba(0.020, 0.040, 0.080, 0.62)
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
                        model: ["看板", "插件", "设置"]

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: 34
                            radius: 8
                            color: index === 0
                                   ? Qt.rgba(0.10, 0.60, 0.70, 0.16)
                                   : (navMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.04)
                                                             : "transparent")
                            border.width: index === 0 ? 1 : 0
                            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.22)

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData
                                color: index === 0 ? Theme.cyan : Theme.muted
                                font.pixelSize: 13
                            }

                            MouseArea {
                                id: navMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (index === 1) {
                                        window.openPlugins()
                                    } else if (index === 2) {
                                        window.openSettings()
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 158
                        radius: 12
                        color: Qt.rgba(0.030, 0.060, 0.110, 0.62)
                        border.width: 1
                        border.color: Qt.rgba(0.45, 0.80, 1.00, 0.12)

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: "插件"
                                    color: Theme.text
                                    font.pixelSize: 11
                                    font.bold: true
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: pluginLoader.plugins.length
                                    color: Theme.muted
                                    font.pixelSize: 10
                                }
                            }

                            Repeater {
                                model: pluginLoader.plugins

                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    height: 30
                                    radius: 6
                                    color: pluginMouse.containsMouse
                                           ? Qt.rgba(1, 1, 1, 0.05)
                                           : "transparent"

                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 4
                                        anchors.right: parent.right
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.name
                                        color: modelData.status === "ok" ? Theme.text : Theme.magenta
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }

                                    MouseArea {
                                        id: pluginMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            if (modelData.status === "ok")
                                                window.openPluginAt(index)
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: pluginLoader.plugins.length === 0
                                text: "未发现插件"
                                color: Theme.muted
                                font.pixelSize: 10
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
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 18
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20

                    ColumnLayout {
                        spacing: 2

                        Text {
                            text: "工作台"
                            color: Theme.text
                            font.pixelSize: 24
                            font.bold: true
                        }

                        Text {
                            text: "专注当下，逐项推进"
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StatChip {
                        label: "待办"
                        value: todoModel.count.toString()
                        accent: Theme.cyan
                    }

                    StatChip {
                        label: "进行中"
                        value: doingModel.count.toString()
                        accent: Theme.violet
                    }

                    StatChip {
                        label: "今日完成"
                        value: taskStore.doneTodayCount.toString()
                        accent: Theme.magenta
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 340
                        height: 34
                        radius: 8
                        color: Qt.rgba(0.020, 0.040, 0.080, 0.70)
                        border.width: 1
                        border.color: Qt.rgba(0.45, 0.80, 1.00, 0.18)

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
                            onTextChanged: {
                                todoModel.search = text
                                doingModel.search = text
                                doneModel.search = text
                            }
                            Keys.onEscapePressed: text = ""
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    id: boardRow
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.bottomMargin: 20
                    spacing: 12

                    BoardColumn {
                        id: todoColumn
                        title: "待办"
                        accent: Theme.cyan
                        status: "todo"
                        taskModel: todoModel
                        searchQuery: searchField.text
                        dragSpace: root
                        onEditRequested: function(taskId) { window.openTaskEditor(taskId) }
                        onStartRequested: function(taskId) { taskStore.moveTask(taskId, "doing", -1) }
                        onCompleteRequested: function(taskId) { taskStore.moveTask(taskId, "done", -1) }
                        onRestoreRequested: function(taskId) { taskStore.moveTask(taskId, "todo", -1) }
                        onDeleteRequested: function(taskId) { window.deleteTaskWithUndo(taskId) }
                    }

                    BoardColumn {
                        title: "进行中"
                        id: doingColumn
                        accent: Theme.violet
                        status: "doing"
                        taskModel: doingModel
                        searchQuery: searchField.text
                        dragSpace: root
                        onEditRequested: function(taskId) { window.openTaskEditor(taskId) }
                        onStartRequested: function(taskId) { taskStore.moveTask(taskId, "doing", -1) }
                        onCompleteRequested: function(taskId) { taskStore.moveTask(taskId, "done", -1) }
                        onRestoreRequested: function(taskId) { taskStore.moveTask(taskId, "todo", -1) }
                        onDeleteRequested: function(taskId) { window.deleteTaskWithUndo(taskId) }
                    }

                    BoardColumn {
                        title: "已办结"
                        id: doneColumn
                        accent: Theme.magenta
                        status: "done"
                        taskModel: doneModel
                        searchQuery: searchField.text
                        dragSpace: root
                        onEditRequested: function(taskId) { window.openTaskEditor(taskId) }
                        onStartRequested: function(taskId) { taskStore.moveTask(taskId, "doing", -1) }
                        onCompleteRequested: function(taskId) { taskStore.moveTask(taskId, "done", -1) }
                        onRestoreRequested: function(taskId) { taskStore.moveTask(taskId, "todo", -1) }
                        onDeleteRequested: function(taskId) { window.deleteTaskWithUndo(taskId) }
                    }
                }
            }
        }

        Rectangle {
            id: pluginPanel
            visible: false
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 24
            anchors.bottomMargin: 24
            width: Math.min(380, parent.width - 48)
            height: Math.min(420, parent.height - 140)
            radius: 14
            color: Qt.rgba(0.025, 0.050, 0.090, 0.94)
            border.width: 1
            border.color: Theme.border
            z: 100

            onVisibleChanged: {
                if (!visible) {
                    pluginLoaderItem.source = ""
                    pluginHost.deactivate()
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true

                    Rectangle {
                        width: 6
                        height: 6
                        radius: 3
                        color: Theme.cyan
                    }

                    Text {
                        id: pluginPanelTitle
                        text: pluginLoader.plugins.length > 0 ? pluginLoader.plugins[0].name : "插件"
                        color: Theme.text
                        font.pixelSize: 12
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "PLUGIN"
                        color: Theme.muted
                        font.pixelSize: 9
                        font.letterSpacing: 1.5
                    }

                    Rectangle {
                        width: 40
                        height: 20
                        radius: 6
                        color: openDirMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "目录"
                            color: Theme.muted
                            font.pixelSize: 10
                        }

                        MouseArea {
                            id: openDirMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pluginLoader.openPluginDir()
                        }
                    }

                    Rectangle {
                        width: 20
                        height: 20
                        radius: 6
                        color: closeMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            color: Theme.muted
                            font.pixelSize: 11
                        }

                        MouseArea {
                            id: closeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: pluginPanel.visible = false
                        }
                    }
                }

                Loader {
                    id: pluginLoaderItem
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                Text {
                    visible: pluginLoader.plugins.length > 0
                             && pluginLoader.plugins[0].status !== "ok"
                    text: pluginLoader.plugins.length > 0 ? pluginLoader.plugins[0].error : ""
                    color: Theme.magenta
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle {
            id: settingsPanel
            anchors.fill: parent
            visible: false
            color: Qt.rgba(0.0, 0.0, 0.0, 0.45)
            z: 150

            MouseArea {
                anchors.fill: parent
                onClicked: settingsPanel.visible = false
            }

            Rectangle {
                width: Math.min(520, parent.width - 48)
                height: 400
                anchors.centerIn: parent
                radius: 16
                color: Qt.rgba(0.030, 0.060, 0.110, 0.96)
                border.width: 1
                border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.35)

                MouseArea { anchors.fill: parent }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "设置"
                            color: Theme.text
                            font.pixelSize: 18
                            font.bold: true
                        }

                        Text {
                            text: "SETTINGS"
                            color: Theme.muted
                            font.pixelSize: 9
                            font.letterSpacing: 1.5
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            width: 26
                            height: 26
                            radius: 8
                            color: closeSettingsMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: "✕"
                                color: Theme.muted
                                font.pixelSize: 13
                            }

                            MouseArea {
                                id: closeSettingsMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: settingsPanel.visible = false
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.08)
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            spacing: 3

                            Text {
                                text: "开机自启"
                                color: Theme.text
                                font.pixelSize: 13
                            }

                            Text {
                                text: "登录 Windows 后静默启动到托盘"
                                color: Theme.muted
                                font.pixelSize: 10
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
                                font.pixelSize: 13
                            }

                            Text {
                                text: hotkey.registered ? "Ctrl + Shift + Space" : "注册失败（可能被占用）"
                                color: hotkey.registered ? Theme.muted : Theme.magenta
                                font.pixelSize: 10
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: hotkey.registered ? "ACTIVE" : "OFF"
                            color: hotkey.registered ? Theme.cyan : Theme.magenta
                            font.family: "Cascadia Code"
                            font.pixelSize: 10
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        Text {
                            text: "数据文件"
                            color: Theme.text
                            font.pixelSize: 13
                        }

                        Text {
                            Layout.fillWidth: true
                            text: database.path
                            color: Theme.muted
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        Text {
                            text: "插件"
                            color: Theme.text
                            font.pixelSize: 13
                        }

                        Text {
                            Layout.fillWidth: true
                            text: pluginLoader.plugins.length + " 个已发现 · 目录 %APPDATA%\\com.lin\\NeonBoard\\plugins"
                            color: Theme.muted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "NeonBoard 0.1.0 · Qt 6.8 · SQLite"
                            color: Theme.muted
                            font.pixelSize: 9
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "Ctrl+Z 撤销删除"
                            color: Theme.muted
                            font.pixelSize: 9
                        }
                    }
                }
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
            color: "#101B2E"
            border.width: 1
            border.color: Qt.rgba(0.40, 0.90, 1.00, 0.30)
            z: 300

            Text {
                id: undoLabel
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: "任务已删除"
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
                color: undoMouse.containsMouse ? Qt.rgba(0.13, 0.75, 0.85, 0.35)
                                               : Qt.rgba(0.10, 0.45, 0.55, 0.25)

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
                    onClicked: window.restoreDeleted()
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

        function openFor(id) {
            const task = taskStore.taskById(id)
            if (!task || !task.id)
                return
            taskId = id
            titleField.text = task.title
            noteField.text = task.note
            priorityValue = Math.max(1, Math.min(3, Number(task.priority) || 1))
            dueField.text = task.due || ""
            open()
            titleField.forceActiveFocus()
        }

        background: Rectangle {
            color: "#0A1220"
            radius: 16
            border.width: 1
            border.color: Qt.rgba(0.40, 0.85, 1.00, 0.32)
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                text: "编辑任务"
                color: Theme.text
                font.pixelSize: 18
                font.bold: true
            }

            TextField {
                id: titleField
                Layout.fillWidth: true
                placeholderText: "任务标题"
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: 14
                background: Rectangle {
                    color: "#111B2E"
                    radius: 8
                    border.width: 1
                    border.color: Qt.rgba(0.45, 0.80, 1.00, 0.20)
                }
            }

            TextArea {
                id: noteField
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                placeholderText: "备注"
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: 12
                wrapMode: TextArea.Wrap
                background: Rectangle {
                    color: "#111B2E"
                    radius: 8
                    border.width: 1
                    border.color: Qt.rgba(0.45, 0.80, 1.00, 0.20)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Row {
                    spacing: 6
                    Repeater {
                        model: ["低", "中", "高"]
                        delegate: Rectangle {
                            width: 58
                            height: 32
                            radius: 8
                            color: editor.priorityValue === index + 1
                                   ? Qt.rgba(0.13, 0.55, 0.65, 0.48)
                                   : Qt.rgba(0.03, 0.08, 0.14, 0.80)
                            border.width: 1
                            border.color: editor.priorityValue === index + 1
                                          ? Theme.cyan
                                          : Qt.rgba(0.45, 0.80, 1.00, 0.16)

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
                        color: "#111B2E"
                        radius: 8
                        border.width: 1
                        border.color: Qt.rgba(0.45, 0.80, 1.00, 0.20)
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
                        taskStore.updateTask(editor.taskId, {
                            title: title,
                            note: noteField.text,
                            priority: editor.priorityValue,
                            dueAt: dueAt
                        })
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

    Rectangle {
        id: dragGhost
        visible: window.dragActive
        width: Math.min(320, dragGhostText.implicitWidth + 40)
        height: 54
        radius: 12
        color: Qt.rgba(0.060, 0.120, 0.220, 0.96)
        border.width: 1
        border.color: Qt.rgba(0.34, 0.90, 1.00, 0.75)
        z: 300
        x: window.dragX - width / 2
        y: window.dragY - height / 2

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

    Component.onCompleted: {
        pluginLoader.scan()
    }
}
