import QtQuick
import QtQuick.Layouts
import NeonBoard

Rectangle {
    id: root
    implicitHeight: content.implicitHeight + 16
    radius: 10
    color: Theme.card
    border.width: 1
    border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.28)

    property var host

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        spacing: 2

        Text {
            text: "多表面示例"
            color: Theme.cyan
            font.pixelSize: 10
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            text: "侧栏槽位 · " + (root.host && root.host.pluginId ? root.host.pluginId : "示例插件")
            color: Theme.muted
            font.pixelSize: 9
            elide: Text.ElideRight
        }
    }
}
