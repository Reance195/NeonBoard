import QtQuick
import QtQuick.Layouts
import NeonBoard

Rectangle {
    id: root
    implicitHeight: 40
    radius: 10
    color: Theme.card
    border.width: 1
    border.color: Qt.rgba(Theme.violet.r, Theme.violet.g, Theme.violet.b, 0.28)

    property var host

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 8

        Text {
            text: "◆"
            color: Theme.violet
            font.pixelSize: 11
        }

        Text {
            Layout.fillWidth: true
            text: "底部面板槽位：插件可以在这里放状态条、播放控制或快捷入口。"
            color: Theme.muted
            font.pixelSize: 10
            elide: Text.ElideRight
        }

        Text {
            text: "SURFACES"
            color: Theme.violet
            font.pixelSize: 9
            font.letterSpacing: 1
        }
    }
}
