import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 统一弹窗壳（R11-09）。
//
// 三条硬规则（docs/design/ui-relayout-spec.md §3.1）：
//   1. 头固定、体滚动、底固定 —— 原先多个弹窗的头/底会随内容滚走；
//   2. 底部按钮右对齐，主操作用 accent 色；
//   3. 关闭键统一右上角（26 × 26，hover 变危险色），Esc 只关闭不表态。
//
// 尺寸档走 Theme 的三个宽度令牌（440 / 560 / 720），一律按
// Math.min(档位, 窗口宽 - 80) 收敛，窄屏自动退化。
//
// 用法：
//   DialogShell {
//       id: shell
//       titleText: "新建任务"
//       sizeMode: "sm"
//       primaryText: "创建"
//       secondaryText: "取消"
//       onPrimaryTriggered: submit()
//       onSecondaryTriggered: close()
//       // 默认属性落在体滚动区
//       ColumnLayout { ... }
//   }
Dialog {
    id: shell

    // ---- 壳的对外接口 ----
    // 尺寸档：sm（单字段表单）/ md（内容型）/ lg（工作型）
    property string sizeMode: "md"
    property string titleText: ""
    property string subtitleText: ""
    // 底部按钮：主操作 / 次操作（空串 = 不显示）
    property string primaryText: ""
    property string secondaryText: ""
    property bool primaryEnabled: true
    property bool secondaryEnabled: true
    // 底部左侧的额外按钮区（危险操作等），由调用方给一个 Component
    property Component footerExtra: null
    // 左侧固定分页导航（设置面板用）：给了就占体区左侧一列
    property Component navRail: null
    // 体滚动区的最大高度（超出则体滚动，头与底仍固定）
    property int maxBodyHeight: 460

    // 宿主窗口：从 Loader 里实例化的跨文件组件拿不到 Main.qml 的 window id，
    // 由宿主显式注入（item.hostWindow = window）。给了就挂在它的 contentItem 上，
    // 否则挂在本窗口的 Overlay 上。
    property var hostWindow: null

    signal primaryTriggered()
    signal secondaryTriggered()
    signal closeRequested()

    // 内容落在体滚动区
    default property alias bodyData: bodyColumn.data

    readonly property int baseWidth: sizeMode === "sm" ? Theme.dialogWidthSm
                                         : (sizeMode === "lg" ? Theme.dialogWidthLg : Theme.dialogWidthMd)
    readonly property int hostWidth: hostWindow ? hostWindow.width
                                        : (Overlay.overlay ? Overlay.overlay.width : 0)

    parent: hostWindow ? hostWindow.contentItem : Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 0
    width: Math.max(240, Math.min(baseWidth, (hostWidth > 0 ? hostWidth : 900) - 80))
    height: header.height + Math.min(bodyFlick.contentHeight, maxBodyHeight) + footer.height
    // 关闭方式统一：Esc / 点遮罩 / 右上角关闭键（规格 §3.3）
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.panelSolid
        radius: Theme.radiusPanel
        border.width: 1
        border.color: Theme.alpha(Theme.cyan, 0.32)
    }

    contentItem: Item {
        // ---------- 头（固定）----------
        Item {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: headerRow.implicitHeight + Theme.dialogPadding

            RowLayout {
                id: headerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.dialogPadding
                anchors.rightMargin: Theme.dialogPadding
                spacing: Theme.spacingMd

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Text {
                        text: shell.titleText
                        color: Theme.text
                        font.pixelSize: Theme.fontTitle
                        font.bold: true
                    }

                    Text {
                        visible: shell.subtitleText.length > 0
                        text: shell.subtitleText
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                    }
                }

                // 关闭键：统一右上角，hover 变危险色
                Rectangle {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    radius: Theme.radiusControl
                    color: closeMouse.containsMouse ? Theme.alpha(Theme.danger, 0.18) : "transparent"
                    border.width: 1
                    border.color: closeMouse.containsMouse ? Theme.alpha(Theme.danger, 0.45) : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Motion.fast }
                    }
                    Behavior on border.color {
                        ColorAnimation { duration: Motion.fast }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: closeMouse.containsMouse ? Theme.danger : Theme.muted
                        font.pixelSize: 12
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: shell.closeRequested()
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: "关闭"
                }
            }
        }

        Rectangle {
            id: headerLine
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.hairline
        }

        // ---------- 体（滚动）----------
        Item {
            anchors.top: headerLine.bottom
            anchors.bottom: footer.top
            anchors.left: parent.left
            anchors.right: parent.right

            RowLayout {
                anchors.fill: parent
                spacing: 0

                // 左侧固定分页导航（设置面板）：不参与滚动
                Loader {
                    id: navLoader
                    Layout.preferredWidth: 160
                    Layout.fillHeight: true
                    visible: shell.navRail !== null
                    sourceComponent: shell.navRail
                }

                Flickable {
                    id: bodyFlick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: bodyColumn.implicitHeight + Theme.dialogPadding * 2
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar {
                        policy: bodyFlick.contentHeight > bodyFlick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                    }

                    ColumnLayout {
                        id: bodyColumn
                        x: 0
                        y: Theme.dialogPadding
                        width: bodyFlick.availableWidth
                        spacing: Theme.spacingMd
                    }
                }
            }
        }

        // ---------- 底（固定）----------
        Item {
            id: footer
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 48

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.dialogPadding
                anchors.rightMargin: Theme.dialogPadding
                spacing: Theme.spacingSm

                // 危险操作等额外按钮靠左
                Loader {
                    Layout.alignment: Qt.AlignVCenter
                    visible: shell.footerExtra !== null
                    sourceComponent: shell.footerExtra
                }

                Item { Layout.fillWidth: true }

                TopToolButton {
                    visible: shell.secondaryText.length > 0
                    enabled: shell.secondaryEnabled
                    opacity: enabled ? 1.0 : 0.45
                    text: shell.secondaryText
                    description: shell.secondaryText
                    onClicked: shell.secondaryTriggered()
                }

                TopToolButton {
                    visible: shell.primaryText.length > 0
                    enabled: shell.primaryEnabled
                    opacity: enabled ? 1.0 : 0.45
                    text: shell.primaryText
                    emphasis: true
                    description: shell.primaryText
                    onClicked: shell.primaryTriggered()
                }
            }
        }
    }
}
