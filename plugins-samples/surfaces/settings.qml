import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    width: parent ? parent.width : implicitWidth
    spacing: 8

    property var host
    property bool highlight: true

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: "强调显示"
            color: "#E8F6FF"
            font.pixelSize: 11
        }

        Rectangle {
            width: 72
            height: 26
            radius: 13
            color: root.highlight ? Qt.rgba(0.13, 0.83, 0.93, 0.22) : "#12202F"
            border.width: 1
            border.color: root.highlight ? "#22D3EE" : "#1E3245"

            Text {
                anchors.centerIn: parent
                text: root.highlight ? "已开启" : "已关闭"
                color: root.highlight ? "#22D3EE" : "#8FA8BF"
                font.pixelSize: 10
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.highlight = !root.highlight
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: "插件标识：" + (root.host && root.host.pluginId ? root.host.pluginId : "(未注入)")
        color: "#8FA8BF"
        font.pixelSize: 10
    }

    Text {
        Layout.fillWidth: true
        text: "设置页由宿主设置面板聚合展示，根节点声明 host 属性即可拿到本插件的门面。"
        color: "#8FA8BF"
        font.pixelSize: 10
        wrapMode: Text.WordWrap
    }
}
