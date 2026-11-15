import QtQuick
import QtQuick.Controls

// 全应用按钮的单一实现（DX-169）。
//
// 原生 Button 不覆盖 background / contentItem 时会落到 Qt 默认浅色样式（白底深字），
// 与霓虹深色面板割裂。此前 5 个文件各自手写一遍圆角 / 描边 / 字号，另 14 处完全裸用，
// 同一套皮肤散成 19 份。这里把皮肤收成一处：颜色只从 Theme 取，语义只由 tone / selected 表达。
//
// 仍基于 QtQuick.Controls.Button，保留原生 enabled / hovered / pressed / 键盘可达 /
// Accessible 语义，调用方只换类型，不改任何 onClicked 逻辑。
Button {
    id: root

    // neutral 次操作 / primary 主操作 / danger 破坏性操作
    property string tone: "neutral"
    // 页签类按钮的选中态（选中时用主色底 + 亮文字）
    property bool selected: false

    readonly property color toneColor: tone === "danger" ? Theme.danger : Theme.accent

    implicitWidth: Math.max(72, label.implicitWidth + Theme.spacingMd * 2)
    implicitHeight: 28
    padding: 0

    background: Rectangle {
        radius: Theme.radiusControl
        color: {
            if (!root.enabled)
                return Theme.input
            if (root.pressed)
                return Theme.elevated
            if (root.selected || root.tone === "primary")
                return Theme.alpha(root.toneColor, 0.32)
            if (root.hovered)
                return Theme.cardHover
            return Theme.input
        }

        border.width: 1
        border.color: {
            if (!root.enabled)
                return Theme.border
            if (root.visualFocus)
                return Theme.alpha(root.toneColor, 0.85)
            if (root.selected || root.tone === "primary" || root.hovered)
                return Theme.alpha(root.toneColor, 0.55)
            return Theme.border
        }

        Behavior on color {
            ColorAnimation { duration: Motion.fast }
        }
        Behavior on border.color {
            ColorAnimation { duration: Motion.fast }
        }
    }

    contentItem: Text {
        id: label
        text: root.text
        color: {
            if (!root.enabled)
                return Theme.muted
            if (root.selected || root.tone === "primary")
                return Theme.text
            if (root.tone === "danger")
                return Theme.danger
            return root.hovered ? root.toneColor : Theme.muted
        }
        font.pixelSize: Theme.fontCaption
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.text
}
