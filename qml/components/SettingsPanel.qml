import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 设置面板：按需加载。
// 由 Main.qml 的 Loader 在用户第一次打开设置时才构造，避免首帧为「几乎不用」的面板付出代价。
// 注意：本根节点不设 anchors.fill —— 尺寸由 Loader 决定。
Rectangle {
    id: panel

    signal openLibraryRequested()

    visible: false
    color: Qt.rgba(0.0, 0.0, 0.0, 0.45)

    function open() {
        visible = true
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
        height: 400
        anchors.centerIn: parent
        radius: 16
        color: Theme.panelSolid
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

                TextField {
                    id: marketSourceField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    text: pluginLibrary.marketSource
                    color: Theme.text
                    font.pixelSize: 10
                    selectByMouse: true
                    background: Rectangle {
                        color: Theme.input
                        radius: 6
                        border.width: 1
                        border.color: Theme.border
                    }
                    onEditingFinished: pluginLibrary.marketSource = text.trim()
                }

                Text {
                    Layout.fillWidth: true
                    text: "在线市场地址（留空则只使用本地插件源）"
                    color: Theme.muted
                    font.pixelSize: 9
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
