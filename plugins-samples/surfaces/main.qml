import QtQuick
import QtQuick.Layouts
import NeonBoard

Rectangle {
    id: root
    color: "transparent"

    property string lastCommand: ""

    Connections {
        target: pluginHost
        function onCommandInvoked(commandId) {
            root.lastCommand = commandId
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Text {
            text: "多表面示例"
            color: Theme.text
            font.pixelSize: 14
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            text: root.lastCommand.length > 0
                  ? ("已收到命令：" + root.lastCommand)
                  : "按 Ctrl+Shift+T 触发插件命令；插件未运行时命令会自动排队并打开本面板。"
            color: Theme.muted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: "本插件还贡献了设置页、侧栏、底部面板与任务卡槽位，可在设置面板与看板中查看。"
            color: Theme.muted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }
    }
}
