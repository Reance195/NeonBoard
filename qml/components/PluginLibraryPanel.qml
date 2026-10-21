import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 插件库：已安装 + 可安装（本地只读源 / 在线市场）
// 每项支持 打开 / 启用 / 禁用 / 安装 / 卸载 / 更新
Rectangle {
    id: libraryPanel

    visible: false
    anchors.fill: parent
    z: 140
    color: Qt.rgba(0, 0, 0, 0.55)

    function open() {
        visible = true
        pluginLibrary.refresh()
    }

    function close() {
        visible = false
    }

    MouseArea {
        anchors.fill: parent
        onClicked: libraryPanel.close()
    }

    Rectangle {
        id: panelBody
        anchors.centerIn: parent
        width: Math.min(580, parent.width - 48)
        height: Math.min(660, parent.height - 48)
        radius: 14
        color: Theme.panelSolid
        border.width: 1
        border.color: Theme.border

        ColumnLayout {
            id: bodyLayout
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            // ---------- 标题行 ----------
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "插件库"
                    color: Theme.text
                    font.pixelSize: 15
                    font.bold: true
                }

                Text {
                    text: "PLUGIN LIBRARY"
                    color: Theme.muted
                    font.pixelSize: 10
                    font.letterSpacing: 1.2
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: pluginLibrary.marketStatus
                    color: Theme.muted
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    Layout.maximumWidth: 220
                }

                Text {
                    text: "✕"
                    color: libraryMouse.containsMouse ? Theme.text : Theme.muted
                    font.pixelSize: 14
                    MouseArea {
                        id: libraryMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: libraryPanel.close()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }

            // ---------- 市场源 + 操作按钮 ----------
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: "市场源"
                    color: Theme.muted
                    font.pixelSize: 10
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
                    text: "刷新市场"
                    color: marketMouse.containsMouse ? Theme.cyan : Theme.text
                    font.pixelSize: 11
                    MouseArea {
                        id: marketMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: pluginMarket.refresh()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                Text {
                    text: "重新扫描"
                    color: rescanMouse.containsMouse ? Theme.cyan : Theme.text
                    font.pixelSize: 11
                    MouseArea {
                        id: rescanMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            pluginRuntime.reload()
                            pluginLibrary.refresh()
                        }
                    }
                }

                Text {
                    text: "打开插件目录"
                    color: dirMouse.containsMouse ? Theme.cyan : Theme.text
                    font.pixelSize: 11
                    MouseArea {
                        id: dirMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: pluginLibrary.openPluginDir()
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: pluginLibrary.entries.length + " 项"
                    color: Theme.muted
                    font.pixelSize: 10
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }

            // ---------- 插件列表 ----------
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: bodyLayout.width
                    spacing: 8

                    Repeater {
                        model: pluginLibrary.entries

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 58
                            radius: 8
                            color: Theme.card
                            border.width: 1
                            border.color: Theme.border

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Text {
                                        text: modelData.name
                                        color: Theme.text
                                        font.pixelSize: 12
                                        font.bold: true
                                    }

                                    Text {
                                        text: modelData.version || "0.0.0"
                                        color: Theme.muted
                                        font.pixelSize: 10
                                    }

                                    Rectangle {
                                        width: badgeText.width + 10
                                        height: 15
                                        radius: 7
                                        color: Qt.rgba(badgeColor.r, badgeColor.g, badgeColor.b, 0.16)
                                        border.width: 1
                                        border.color: Qt.rgba(badgeColor.r, badgeColor.g, badgeColor.b, 0.4)

                                        property color badgeColor: {
                                            if (!modelData.installed)
                                                return Theme.accent2
                                            if (modelData.status === "disabled")
                                                return Theme.muted
                                            if (modelData.status !== "ok")
                                                return Theme.danger
                                            return Theme.success
                                        }

                                        Text {
                                            id: badgeText
                                            anchors.centerIn: parent
                                            text: {
                                                if (modelData.installed) {
                                                    if (modelData.status === "disabled")
                                                        return "已禁用"
                                                    if (modelData.status !== "ok")
                                                        return "异常"
                                                    return "已安装"
                                                }
                                                if (modelData.origin === "market")
                                                    return "市场"
                                                if (modelData.source === "appdir")
                                                    return "随包"
                                                if (modelData.source === "official")
                                                    return "官方"
                                                if (modelData.source === "sample")
                                                    return "示例"
                                                return "可安装"
                                            }
                                            color: badgeText.parent.badgeColor
                                            font.pixelSize: 9
                                        }
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        visible: modelData.origin === "market"
                                        text: "市场"
                                        color: Theme.muted
                                        font.pixelSize: 9
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    Text {
                                        visible: modelData.installed && modelData.status === "ok" && modelData.enabled
                                        text: "打开"
                                        color: openMouse.containsMouse ? Theme.cyan : Theme.text
                                        font.pixelSize: 11
                                        MouseArea {
                                            id: openMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: pluginLibrary.requestOpen(modelData.id)
                                        }
                                    }

                                    Text {
                                        visible: modelData.installed
                                        text: modelData.enabled ? "禁用" : "启用"
                                        color: toggleMouse.containsMouse ? Theme.cyan : Theme.text
                                        font.pixelSize: 11
                                        MouseArea {
                                            id: toggleMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: pluginLibrary.setEnabled(modelData.id, !modelData.enabled)
                                        }
                                    }

                                    Text {
                                        visible: modelData.installed
                                        text: "卸载"
                                        color: uninstallMouse.containsMouse ? Theme.danger : Theme.text
                                        font.pixelSize: 11
                                        MouseArea {
                                            id: uninstallMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: pluginLibrary.uninstall(modelData.id)
                                        }
                                    }

                                    Text {
                                        visible: !modelData.installed
                                        text: "安装"
                                        color: installMouse.containsMouse ? Theme.cyan : Theme.text
                                        font.pixelSize: 11
                                        MouseArea {
                                            id: installMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: pluginLibrary.install(modelData.id)
                                        }
                                    }

                                    Text {
                                        visible: modelData.installed && modelData.hasMarket
                                        text: "更新"
                                        color: updateMouse.containsMouse ? Theme.cyan : Theme.text
                                        font.pixelSize: 11
                                        MouseArea {
                                            id: updateMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: pluginLibrary.update(modelData.id)
                                        }
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: modelData.error || ""
                                        color: Theme.danger
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                        Layout.maximumWidth: 200
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: pluginLibrary.entries.length === 0
                        text: "插件库为空"
                        color: Theme.muted
                        font.pixelSize: 11
                    }
                }
            }

            // ---------- 状态行 ----------
            Text {
                Layout.fillWidth: true
                text: pluginLibrary.statusText
                color: Theme.muted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
    }
}
