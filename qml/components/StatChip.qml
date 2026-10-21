import QtQuick

// 统计小挂件：侧栏/标题区的数字徽标，带发光描边。
Rectangle {
    id: chip

    property string label: ""
    property string value: "0"
    property color accent: Theme.cyan

    Layout.preferredWidth: 110
    height: 48
    radius: 10
    color: Theme.input
    border.width: 1
    border.color: Qt.rgba(accent.r, accent.g, accent.b, 0.28)

    Behavior on color {
        ColorAnimation { duration: Motion.fast }
    }

    Column {
        anchors.centerIn: parent
        spacing: 2

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: chip.value
            color: chip.accent
            font.pixelSize: 18
            font.bold: true
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: chip.label
            color: Theme.muted
            font.pixelSize: 9
        }
    }
}
