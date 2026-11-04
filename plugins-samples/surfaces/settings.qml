import QtQuick
import QtQuick.Layouts
import NeonBoard

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
            color: Theme.text
            font.pixelSize: 11
        }

        Rectangle {
            width: 72
            height: 26
            radius: 13
            color: root.highlight ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.22) : Theme.input
            border.width: 1
            border.color: root.highlight ? Theme.cyan : Theme.border

            Text {
                anchors.centerIn: parent
                text: root.highlight ? "已开启" : "已关闭"
                color: root.highlight ? Theme.cyan : Theme.muted
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
        color: Theme.muted
        font.pixelSize: 10
    }

    Text {
        Layout.fillWidth: true
        text: "设置页由宿主设置面板聚合展示，根节点声明 host 属性即可拿到本插件的门面。"
        color: Theme.muted
        font.pixelSize: 10
        wrapMode: Text.WordWrap
    }
}
