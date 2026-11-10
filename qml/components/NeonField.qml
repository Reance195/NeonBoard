import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 面板内复用的单行输入框（MOD-18 自 SettingsPanel.qml 内联组件原样迁出）。
TextField {
        Layout.fillWidth: true
        Layout.preferredHeight: 28
        color: Theme.text
        font.pixelSize: Theme.fontMicro
        selectByMouse: true
        background: Rectangle {
            color: Theme.input
            radius: Theme.radiusControl
            border.width: 1
            border.color: Theme.border
        }
    }
