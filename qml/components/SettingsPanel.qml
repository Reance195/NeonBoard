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
        radius: 8
        color: btnMouse.pressed ? Theme.elevated : Theme.card
        border.width: 1
        border.color: btnMouse.containsMouse
                         ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.55)
                         : Theme.border

        Text {
            anchors.centerIn: parent
            text: btn.label
            color: btn.accentColor
            font.pixelSize: 11
        }

        MouseArea {
            id: btnMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.triggered()
        }
    }

    // 导入来源：数据目录下已导出的文件 + 手动输入路径
    function refreshExports() {
        exportCombo.model = dataPort.listExports()
        if (exportCombo.model.length > 0 && exportCombo.currentIndex < 0)
            exportCombo.currentIndex = 0
    }
    visible: false
    color: Qt.rgba(0.0, 0.0, 0.0, 0.45)

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
        radius: 16
        color: Theme.panelSolid
        border.width: 1
        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.35)

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
                        onClicked: panel.close()
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
                    text: "导入导出与备份"
                    color: Theme.text
                    font.pixelSize: 13
                }

                Text {
                    Layout.fillWidth: true
                    text: "备份目录 " + dataPort.backupDir
                    color: Theme.muted
                    font.pixelSize: 10
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
                        font.pixelSize: 10
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
                            radius: 8
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
                                font.pixelSize: 10
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
                                radius: 8
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
                        font.pixelSize: 10
                        selectByMouse: true
                        background: Rectangle {
                            color: Theme.input
                            radius: 6
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
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
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
                    text: pluginLoader.plugins.length + " 个已发现 · 目录 " + pluginLoader.pluginDir
                    color: Theme.muted
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4

                    Rectangle {
                        Layout.fillWidth: true
                        height: 30
                        radius: 8
                        color: rescanArea.pressed ? Theme.elevated : Theme.card
                        border.width: 1
                        border.color: Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: "重新扫描插件"
                            color: Theme.cyan
                            font.pixelSize: 11
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
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8

                    Rectangle {
                        Layout.fillWidth: true
                        height: 30
                        radius: 8
                        color: libraryArea.pressed ? Theme.elevated : Theme.card
                        border.width: 1
                        border.color: Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: "打开插件库"
                            color: Theme.cyan
                            font.pixelSize: 11
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
                    font.pixelSize: 11
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: pluginLibrary.marketSources.length + " 个源 · "
                              + (pluginLibrary.marketSource || "未配置")
                        color: Theme.muted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }

                    Text {
                        text: "管理源…"
                        color: marketManageMouse.containsMouse ? Theme.cyan : Theme.text
                        font.pixelSize: 11
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
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    text: "插件设置"
                    color: Theme.text
                    font.pixelSize: 11
                }

                Text {
                    Layout.fillWidth: true
                    visible: pluginRuntime.settingsPages.length === 0
                    text: "当前没有插件提供设置页（manifest 里声明 settings 即可出现在这里）"
                    color: Theme.muted
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: pluginRuntime.settingsPages

                    Rectangle {
                        id: settingsPageRow
                        required property var modelData

                        Layout.fillWidth: true
                        implicitHeight: settingsPageColumn.implicitHeight + 20
                        radius: 8
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
                                    font.pixelSize: 11
                                }

                                Text {
                                    text: settingsPageRow.modelData.sourceLabel
                                    color: Theme.muted
                                    font.pixelSize: 9
                                }

                                Text {
                                    text: settingsPageRow.expanded ? "收起" : "展开"
                                    color: Theme.cyan
                                    font.pixelSize: 10

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
                    font.pixelSize: 9
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Ctrl+Z 撤销上一步"
                    color: Theme.muted
                    font.pixelSize: 9
                }
            }
            }
        }
    }
}
