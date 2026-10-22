import QtQuick

Rectangle {
    id: root
    implicitHeight: 16
    color: "transparent"

    property var host
    property string taskId: ""

    Text {
        anchors.fill: parent
        verticalAlignment: Text.AlignVCenter
        text: "◆ 多表面示例 · 卡片槽位"
              + (root.taskId.length > 0 ? " · " + root.taskId.substring(0, 8) : "")
        color: Qt.rgba(0.75, 0.52, 0.99, 0.55)
        font.pixelSize: 8
        elide: Text.ElideRight
    }
}
