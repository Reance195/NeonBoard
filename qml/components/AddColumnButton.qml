import QtQuick

// 看板右侧的「＋ 列」入口（R11-07 从 Main.qml 拆出）。
//
// 拆出来的理由不只是整洁：Main.qml 触到模块守门的行数上限（4260 行），
// 而空态这类新功能要往主界面加行 —— 把这块独立出来，主界面净减行，
// 以后要改这个按钮也不必再翻四千多行的文件。
Rectangle {
    id: btn

    signal clicked()

    implicitWidth: 46
    implicitHeight: 46
    radius: Theme.radiusPanel
    color: mouse.containsMouse ? Theme.cardHover : Theme.card
    border.width: 1
    border.color: mouse.containsMouse ? Theme.borderStrong : Theme.border

    Behavior on color {
        ColorAnimation { duration: Motion.fast }
    }
    Behavior on border.color {
        ColorAnimation { duration: Motion.fast }
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacingXs

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "＋"
            color: mouse.containsMouse ? Theme.cyan : Theme.muted
            font.pixelSize: Theme.fontDisplay
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "列"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: "添加看板列"
        Accessible.onPressAction: btn.clicked()
        onClicked: btn.clicked()
    }
}
