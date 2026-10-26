import QtQuick

// 窗口控制按钮（最小化 / 最大化 / 关闭），自 Main.qml 拆出（R4-08）：标题栏右侧。
// windowController 为 C++ 注入的 context property，任意组件可直接使用；
// 「关闭」经 closeRequested 信号上交，避免拆出后引用 Main.qml 根 id（原直接调用 window.hide()）。
Row {
    id: windowButtons

    signal closeRequested()

    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
    spacing: 0

    WinButton {
        glyph: "—"
        onClicked: windowController.minimize()
    }

    WinButton {
        glyph: windowController.maximized ? "❐" : "□"
        onClicked: windowController.toggleMaximize()
    }

    WinButton {
        glyph: "✕"
        danger: true
        onClicked: windowButtons.closeRequested()
    }
}
