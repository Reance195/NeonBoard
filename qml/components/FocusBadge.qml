import QtQuick

// 专注模式提示徽标（自 Main.qml 拆出，R4-08）：背景压暗成追光时显示，告知退出方式。
// 自包含：仅依赖 Theme 单例与传入的 focusMode，不引用 Main.qml 根作用域的其它 id。
Rectangle {
    id: badge

    property bool focusMode: false

    anchors.top: parent.top
    anchors.topMargin: 48
    anchors.horizontalCenter: parent.horizontalCenter
    z: 30
    visible: focusMode
    width: badgeText.implicitWidth + 30
    height: 28
    radius: 14
    color: Qt.rgba(Theme.bg.r, Theme.bg.g, Theme.bg.b, 0.72)
    border.width: 1
    border.color: Theme.violet

    Text {
        id: badgeText
        anchors.centerIn: parent
        text: "专注模式 · Ctrl+Shift+F 退出"
        color: Theme.violet
        font.pixelSize: 11
    }
}
