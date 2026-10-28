import QtQuick

// 标题栏工具按钮的单一实现（R11-02）。
//
// 原先标题栏里「全景」「＋ 新建」是同一套样式各抄一遍：圆角、边框 alpha、悬浮底色
// 全部硬编码 rgba(Theme.cyan, …)。后果有两处：改主题时这两处不会跟着走，
// 改一处样式要记得改另一处。现统一收敛到本组件，标题栏只声明「文字 + 强调色 + 语义」。
Rectangle {
    id: btn

    // 按钮文案（不含图标；需要图标时把符号写进 text，字体自带符号字形）
    property string text: ""
    // 强调色（默认跟随主题主色）
    property color accent: Theme.accent
    // 主按钮：常亮底 + 更强描边（当前只有「＋ 新建」用）
    property bool emphasis: false
    // 无障碍名（读屏朗读用）
    property string description: text

    signal clicked()

    implicitWidth: label.implicitWidth + 22
    implicitHeight: 26
    // Rectangle 的 width 默认是 0，不显式给宽度按钮就是隐形的
    width: implicitWidth
    height: 26
    radius: Theme.radiusControl

    color: mouse.containsMouse
           ? Qt.rgba(accent.r, accent.g, accent.b, emphasis ? 0.32 : 0.22)
           : Qt.rgba(accent.r, accent.g, accent.b, emphasis ? 0.20 : 0.10)

    border.width: 1
    border.color: (mouse.containsMouse || emphasis)
                  ? Theme.borderStrong
                  : Qt.rgba(accent.r, accent.g, accent.b, 0.32)

    Behavior on color {
        ColorAnimation { duration: Motion.fast }
    }
    Behavior on border.color {
        ColorAnimation { duration: Motion.fast }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: btn.text
        color: (mouse.containsMouse || btn.emphasis) ? btn.accent : Theme.text
        font.pixelSize: 11
        font.bold: true
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: btn.clicked()
    }

    Accessible.role: Accessible.Button
    Accessible.name: btn.description
}
