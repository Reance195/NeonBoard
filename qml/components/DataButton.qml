import QtQuick

// 面板内复用的小按钮（MOD-18 自 SettingsPanel.qml 内联组件原样迁出）。
Rectangle {
    id: btn
        property string label: ""
        property color accentColor: Theme.cyan
        signal triggered()

        height: 30
        radius: Theme.radiusControl
        color: btnMouse.pressed ? Theme.elevated : Theme.card
        border.width: 1
        border.color: btnMouse.containsMouse
                         ? Theme.alpha(accentColor, 0.55)
                         : Theme.border

        Text {
            anchors.centerIn: parent
            text: btn.label
            color: btn.accentColor
            font.pixelSize: Theme.fontCaption
        }

        MouseArea {
            id: btnMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.triggered()
        }
    }
