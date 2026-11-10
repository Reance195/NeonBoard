import QtQuick
import QtQml

// 霓虹背景包裹层（MOD-16 自 Main.qml 原样迁入）：
// hostWin = 宿主窗口（原 window）；burst() = 语义接口（原 background alias，
// MOD-25 收敛：Main 调 burst() 而不再摸内部元素）；noBackground / taskStore 为 context property。
Item {
    property var hostWin

    // 落位涟漪 / 勾选光扫的触发入口（原 Main 直接 background.burst()）
    function burst() {
        background.burst()
    }

        anchors.fill: parent

        NeonBackground {
            id: background
            anchors.fill: parent
            active: hostWin.active
            visible: !noBackground
            iState: Math.min(1, taskStore.doingCount / 6)
            iFocus: hostWin.focusMode ? 1 : 0
        }

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(Theme.bg.r, Theme.bg.g, Theme.bg.b, 0.28)
        }
}
