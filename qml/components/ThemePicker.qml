import QtQuick

// 标题栏右侧的两个入口（R11-8）：
//   1. 主题三点 —— 打开外观面板（一键切换主题预设 / 画质档 / 减弱动效）。
//      切换主题时全应用（含霓虹背景）颜色渐变，切换过程本身就是一段动效。
//      面板内容按需加载：未点开时不会构造 6 套预设行与投影层。
//   2. 齿轮 —— 打开设置面板。原先设置入口在侧栏导航里（低频全局操作占一整行），
//      R11-8 把它收到标题栏右上角，与窗口按钮同区；Ctrl+, 快捷键不变。
//
// 组件名沿用 ThemePicker（改动最小，不牵动外部引用），但它现在是「外观 + 设置」
// 两个标题栏入口的宿主。
Item {
    id: picker

    property bool open: false
    // 面板内容是否已加载（首次打开后保持加载，再次打开零延迟）
    property bool panelReady: false

    // 设置入口交给宿主接线（Main.qml → window.openSettings()）
    signal settingsRequested()

    implicitWidth: 64
    implicitHeight: 30

    function toggle() {
        open = !open
    }

    function close() {
        open = false
    }

    // ---------- 齿轮按钮（设置） ----------
    Rectangle {
        id: gear
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 26
        height: 26
        radius: Theme.radiusControl
        color: gearMouse.containsMouse ? Theme.alpha(Theme.text, 0.06) : "transparent"
        border.width: 1
        border.color: gearMouse.containsMouse ? Theme.borderStrong : "transparent"

        Behavior on color {
            ColorAnimation { duration: Motion.fast }
        }
        Behavior on border.color {
            ColorAnimation { duration: Motion.fast }
        }

        Text {
            anchors.centerIn: parent
            text: "⚙"
            color: gearMouse.containsMouse ? Theme.cyan : Theme.muted
            font.pixelSize: 14
        }

        MouseArea {
            id: gearMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: picker.settingsRequested()
        }

        Accessible.role: Accessible.Button
        Accessible.name: "设置"
    }

    // ---------- 触发按钮（外观面板） ----------
    Rectangle {
        id: trigger
        anchors.right: gear.left
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 34
        height: 30
        radius: Theme.radiusControl
        color: picker.open
               ? Theme.accentSoft
               : (triggerMouse.containsMouse ? Theme.alpha(Theme.text, 0.06) : "transparent")
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
                    radius: Theme.radiusControl
                    color: modelData
                }
            }
        }

        MouseArea {
            id: triggerMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: picker.toggle()
        }

        Accessible.role: Accessible.Button
        Accessible.name: "外观面板"
    }

    // ---------- 弹出面板（按需加载） ----------
    Loader {
        id: panelLoader
        anchors.top: trigger.bottom
        anchors.topMargin: 8
        anchors.right: trigger.right
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
