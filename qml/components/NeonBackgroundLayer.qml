import QtQuick
import QtQml

// 霓虹背景包裹层（MOD-16 自 Main.qml 原样迁入）：
// hostWin = 宿主窗口（原 window）；background 经 alias 暴露
// （Main 级 burst() 调用）；noBackground / taskStore 为 context property。
Item {
    property var hostWin
    property alias background: background

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
