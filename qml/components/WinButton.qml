import QtQuick

// 无边框窗口的标题栏按钮：悬停浮起、按下微沉。
Rectangle {
    id: winButton

    property string glyph: ""
    property bool danger: false

    signal clicked()

    width: 42
    height: 40
    color: winMouse.containsMouse
           ? (winButton.danger ? Qt.rgba(0.95, 0.30, 0.40, 0.22)
                               : Qt.rgba(1, 1, 1, 0.06))
           : "transparent"
    scale: winMouse.pressed ? 0.90 : 1.0

    Behavior on color {
        ColorAnimation { duration: Motion.fast }
    }
    Behavior on scale {
        NumberAnimation { duration: Motion.instant; easing.type: Easing.OutCubic }
    }

    Text {
        anchors.centerIn: parent
        text: winButton.glyph
        color: winButton.danger && winMouse.containsMouse ? Theme.magenta : Theme.muted
        font.pixelSize: 14
    }

    MouseArea {
        id: winMouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: winButton.clicked()
    }
}
