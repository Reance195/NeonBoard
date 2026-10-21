import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 插件面板：按需加载。
// 插件 QML 只在用户点开某个插件时才实例化，首帧不再为它付出构造代价。
// 注意：本根节点不设 width/height/anchors —— 尺寸与位置由 Main.qml 里的 Loader 决定。
Rectangle {
    id: panel

    visible: false
    radius: 14
    color: Theme.panelSolid
    border.width: 1
    border.color: Theme.border

    function open() {
        visible = true
    }

    function close() {
        visible = false
        pluginContent.source = ""
        pluginHost.deactivate()
    }

    function openPlugin(index) {
        visible = true
        var plugin = pluginLoader.plugins[index]
        if (!plugin || plugin.status !== "ok")
            return
        if (!pluginHost.activate(plugin.id)) {
            title.text = "插件激活失败"
            pluginContent.source = ""
            return
        }
        title.text = plugin.name
        pluginContent.source = ""
        pluginContent.source = plugin.mainUrl
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
                id: title
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
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.close()
                }
            }
        }

        Loader {
            id: pluginContent
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
