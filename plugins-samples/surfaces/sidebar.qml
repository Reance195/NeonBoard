import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: content.implicitHeight + 16
    radius: 10
    color: "#0A1524"
    border.width: 1
    border.color: Qt.rgba(0.13, 0.83, 0.93, 0.28)

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
            color: "#22D3EE"
            font.pixelSize: 10
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            text: "侧栏槽位 · " + (root.host && root.host.pluginId ? root.host.pluginId : "示例插件")
            color: "#8FA8BF"
            font.pixelSize: 9
            elide: Text.ElideRight
        }
    }
}
