import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 热键与自动化分页（MOD-20 自 SettingsPanel.qml 原样迁入）：
// ①全局热键（hotkey 上下文属性）+ ②开机与提醒（tray 上下文属性）+ 只读快捷键清单。
// visible 由实例化处按页绑定。
ColumnLayout {
    // 只读快捷键清单：固定键位改不了，列出来省得用户到处找
    readonly property var shortcuts: [
        { label: "显示 / 隐藏窗口", keys: "Ctrl+Shift+Space" },
        { label: "呼出命令面板", keys: "Ctrl+K" },
        { label: "新建任务", keys: "Ctrl+N" },
        { label: "撤销上一步", keys: "Ctrl+Z" },
        { label: "专注一轮", keys: "Ctrl+Shift+F" },
        { label: "打开设置", keys: "Ctrl+," }
    ]


        Layout.fillWidth: true
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
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
                        // 组名已是「全局热键」，行内再叫一遍是重复；
                        // 这一行的主语其实是「显示 / 隐藏窗口」这个键（原型同名）。
                        text: "显示 / 隐藏窗口"
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
                model: shortcuts

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
