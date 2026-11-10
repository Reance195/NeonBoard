import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 插件分页（MOD-21 自 SettingsPanel.qml 原样迁入）：
// ①已装插件 / ②市场源管理 / ③设置页签注入（pluginLoader / pluginRuntime /
// pluginLibrary 均为上下文属性）。
// visible 由实例化处按页绑定；openLibraryRequested 由实例化处转发面板信号。
ColumnLayout {
    signal openLibraryRequested()
        Layout.fillWidth: true
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
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
                        onClicked: openLibraryRequested()
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
                        onClicked: openLibraryRequested()
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
