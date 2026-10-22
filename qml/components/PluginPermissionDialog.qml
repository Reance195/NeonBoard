import QtQuick
import QtQuick.Layouts

// 插件权限授权弹窗：安装后或首次调用高危能力时显式询问。
// 展示插件名、来源与权限逐条说明；「允许」把权限写入 meta.plugins.grants，拒绝不落任何数据。
// 用法：Main.qml 通过 Loader 按需加载，调用 present(pluginId)。
Rectangle {
    id: dialog

    visible: false
    color: "transparent"
    focus: visible
    Keys.onEscapePressed: dialog.close()

    property string pluginId: ""
    property var prompt: ({})

    function present(id) {
        pluginId = id
        prompt = pluginRuntime.permissionPrompt(id)
        visible = true
        forceActiveFocus()
    }

    function close() {
        visible = false
    }

    function pendingNames() {
        var names = []
        var list = prompt.permissions ? prompt.permissions : []
        for (var i = 0; i < list.length; ++i)
            names.push(list[i].name)
        return names
    }

    function allowAll() {
        var names = pendingNames()
        if (names.length > 0)
            pluginRuntime.grantPermissions(pluginId, names)
        close()
    }

    // 遮罩：点击空白处等同拒绝
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)

        MouseArea {
            anchors.fill: parent
            onClicked: dialog.close()
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(460, dialog.width - 80)
        height: content.implicitHeight + 36
        radius: 14
        color: Theme.panelSolid
        border.width: 1
        border.color: Theme.borderStrong

        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 18
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: Theme.warning
                }

                Text {
                    text: "插件权限请求"
                    color: Theme.text
                    font.pixelSize: 14
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: dialog.prompt.sourceLabel || ""
                    color: Theme.muted
                    font.pixelSize: 10
                }
            }

            Text {
                Layout.fillWidth: true
                text: "「" + (dialog.prompt.pluginName || dialog.pluginId) + "」请求以下权限："
                color: Theme.text
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    model: dialog.prompt.permissions || []

                    Rectangle {
                        required property var modelData

                        Layout.fillWidth: true
                        implicitHeight: permRow.implicitHeight + 20
                        radius: 8
                        color: Theme.card
                        border.width: 1
                        border.color: Theme.border

                        RowLayout {
                            id: permRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 10
                            spacing: 10

                            Rectangle {
                                width: 34
                                height: 18
                                radius: 5
                                color: modelData.risk === "高" ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.18)
                                     : modelData.risk === "中" ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.18)
                                     : Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.16)

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.risk || "低"
                                    color: modelData.risk === "高" ? Theme.danger
                                         : modelData.risk === "中" ? Theme.warning
                                         : Theme.cyan
                                    font.pixelSize: 9
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.title || modelData.name
                                    color: Theme.text
                                    font.pixelSize: 11
                                    font.bold: true
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: (modelData.detail || "") + "（" + modelData.name + "）"
                                    color: Theme.muted
                                    font.pixelSize: 9
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: dialog.pendingNames().length === 0
                text: "该插件没有新的权限请求。"
                color: Theme.muted
                font.pixelSize: 10
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 88
                    height: 32
                    radius: 9
                    color: denyMouse.pressed ? Theme.elevated : Theme.card
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "拒绝"
                        color: Theme.text
                        font.pixelSize: 12
                    }

                    MouseArea {
                        id: denyMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dialog.close()
                    }
                }

                Rectangle {
                    visible: dialog.pendingNames().length > 0
                    width: 88
                    height: 32
                    radius: 9
                    color: allowMouse.pressed ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.35)
                                              : Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.22)
                    border.width: 1
                    border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.5)

                    Text {
                        anchors.centerIn: parent
                        text: "允许"
                        color: Theme.cyan
                        font.pixelSize: 12
                        font.bold: true
                    }

                    MouseArea {
                        id: allowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dialog.allowAll()
                    }
                }
            }
        }
    }
}
