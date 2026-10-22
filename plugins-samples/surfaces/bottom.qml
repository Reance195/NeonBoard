import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 40
    radius: 10
    color: "#0A1524"
    border.width: 1
    border.color: Qt.rgba(0.75, 0.35, 0.95, 0.28)

    property var host

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 8

        Text {
            text: "◆"
            color: "#C084FC"
            font.pixelSize: 11
        }

        Text {
            Layout.fillWidth: true
            text: "底部面板槽位：插件可以在这里放状态条、播放控制或快捷入口。"
            color: "#8FA8BF"
            font.pixelSize: 10
            elide: Text.ElideRight
        }

        Text {
            text: "SURFACES"
            color: "#C084FC"
            font.pixelSize: 9
            font.letterSpacing: 1
        }
    }
}
