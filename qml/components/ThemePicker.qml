import QtQuick

// 外观面板：一键切换主题预设 / 画质档 / 减弱动效。
// 切换主题时全应用（含霓虹背景）颜色渐变，切换过程本身就是一段动效。
// 面板内容按需加载：未点开时不会构造 6 套预设行与投影层。
Item {
    id: picker

    property bool open: false
    // 面板内容是否已加载（首次打开后保持加载，再次打开零延迟）
    property bool panelReady: false

    implicitWidth: 34
    implicitHeight: 30

    function toggle() {
        open = !open
    }

    function close() {
        open = false
    }

    // ---------- 触发按钮 ----------
    Rectangle {
        id: trigger
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 34
        height: 30
        radius: 8
        color: picker.open
               ? Theme.accentSoft
               : (triggerMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
        border.width: 1
        border.color: picker.open ? Theme.borderStrong : "transparent"

        Behavior on color {
            ColorAnimation { duration: Motion.fast }
        }
        Behavior on border.color {
            ColorAnimation { duration: Motion.fast }
        }

        // 当前主题三色预览
        Row {
            anchors.centerIn: parent
            spacing: 3

            Repeater {
                model: [Theme.cyan, Theme.magenta, Theme.violet]
                delegate: Rectangle {
                    width: 5
                    height: 5
                    radius: 2.5
                    color: modelData
                }
            }
        }

        MouseArea {
            id: triggerMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: picker.toggle()
        }
    }

    // ---------- 弹出面板（按需加载） ----------
    Loader {
        id: panelLoader
        anchors.top: trigger.bottom
        anchors.topMargin: 8
        anchors.right: parent.right
        // 显式给宽高：QQuickLoader 会把 Loader 的尺寸强加给被加载项，
        // 高度取面板自身的隐式高度，避免出现「高度为 0 看不见面板」。
        width: 300
        height: item ? item.implicitHeight : 0
        active: picker.panelReady || picker.open
        source: "AppearancePanel.qml"
        z: 50

        opacity: picker.open ? 1.0 : 0.0
        scale: picker.open ? 1.0 : 0.96
        transformOrigin: Item.TopRight
        visible: opacity > 0.01

        Behavior on opacity {
            NumberAnimation { duration: Motion.panel; easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            NumberAnimation { duration: Motion.panel; easing.type: Easing.OutCubic }
        }

        onLoaded: {
            item.pickerRef = picker
            item.closeRequested.connect(picker.close)
            picker.panelReady = true
        }
    }
}
