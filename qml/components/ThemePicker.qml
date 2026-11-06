import QtQuick

// 标题栏右侧的两个入口（R11-8 / R11-11）：
//   1. 主题三点 —— 打开「外观」抽屉（一键切换主题预设 / 画质档 / 减弱动效）。
//      切换主题时全应用（含霓虹背景）颜色渐变，切换过程本身就是一段动效。
//      外观面板已从「标题栏下方的浮动小面板」改成「主区右侧 320 px 抽屉」
//      （AppearancePanel 以 DrawerShell 为根），抽屉由 Main.qml 承载，
//      所以这里**不再内部加载 AppearancePanel**，只负责发开合意图。
//   2. 齿轮 —— 打开设置面板。原先设置入口在侧栏导航里（低频全局操作占一整行），
//      R11-8 把它收到标题栏右上角，与窗口按钮同区；Ctrl+, 快捷键不变。
//
// 组件名沿用 ThemePicker（改动最小，不牵动外部引用），但它现在是「外观 + 设置」
// 两个标题栏入口的宿主。
Item {
    id: picker

    // 外观抽屉的开合意图：true = 想开，false = 想关。
    // 抽屉本体在 Main.qml 侧（右侧抽屉），这里只同步意图。
    property bool open: false

    // 开合意图变化时发一次：由宿主把抽屉的实际状态对齐过来
    // （宿主收到后把抽屉 visible 设成 picker.open）。
    signal appearanceRequested()

    // 设置入口交给宿主接线（Main.qml → window.openSettings()）
    signal settingsRequested()

    implicitWidth: 64
    implicitHeight: 30

    // open 任何来源的变化（toggle / close / 宿主直接赋值）都发一次信号，
    // 免得「按钮高亮了但抽屉没开」——宿主只需要在一个地方对齐 visible。
    onOpenChanged: appearanceRequested()

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
}
