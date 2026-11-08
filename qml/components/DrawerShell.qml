import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 右侧抽屉壳（R11-09 / R11-10）：浏览型信息的统一容器（外观 / 插件面板 / 项目全景 / 列管理）。
//
// 与 DialogShell 同结构（头固定 / 体滚动 / 底固定、关闭键右上角），两点不同：
//   1. 贴右边缘铺开，不是居中弹窗；
//   2. 根是 Item 而不是 Popup —— 尺寸与位置由调用方（Main.qml 的 Loader）决定，
//      于是它只盖主区，不会压住标题栏右端的窗口按钮。
//
// 遮罩自己画：Popup 的模态暗化由 Controls 样式决定，抽屉不是 Popup，
// 这一层由壳自己负责，两种形态的遮罩浓度才一致（Theme.scrimOpacity）。
//
// 开合由调用方驱动（组件自己的 visible / open 都行）：Item.visible 会连带
// 隐藏遮罩与面板，隐藏时遮罩不吃点击，底下的看板照常可点。
//
// 宽度档见 Theme.drawerWidth（320），窄屏按可用宽度退化。
//
// 用法：
//   Loader {
//       anchors.fill: parent
//       source: "PluginPanel.qml"
//   }
Item {
    id: drawer

    // ---- 壳的对外接口 ----
    property string titleText: ""
    property string subtitleText: ""
    // 底部左侧的额外按钮区（危险操作等），由调用方给一个 Component
    property Component footerExtra: null
    // 底部右侧的主操作区（右对齐，与 DialogShell 的底栏纪律一致）
    property Component footerAction: null
    // 左侧固定分页导航（给了就占体区左侧一列）
    property Component navRail: null

    signal closeRequested()

    // 内容落在体滚动区
    default property alias bodyData: bodyColumn.data

    // ---------- 遮罩（点它 = 关闭）----------
    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Theme.alpha(Theme.shadowDialog, Theme.scrimOpacity)

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: drawer.closeRequested()
        }
    }

    // ---------- 抽屉面板 ----------
    Rectangle {
        id: panel
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: Math.min(Theme.drawerWidth, Math.max(200, drawer.width - 40))
        color: Theme.panelSolid
        border.width: 1
        border.color: Theme.border

        // 挡点击穿透：遮罩铺满整个根，头部 / 底部的空白处若不挡，
        // 点下去会落到遮罩上误触发 closeRequested（点标题文字就关抽屉）。
        // 这里吃掉左键点击但不做事；声明在最前 = z 序最底，
        // 头 / 体 / 底里的控件仍照常收事件。
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.ArrowCursor
            onClicked: {}
        }

        // 头（固定）
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

                // 嵌套布局只有内部子项声明 fillWidth，外层 fillWidth 才会吃掉
                // 剩余空间 —— 否则标题列收缩到 implicit 宽，右侧关闭键停在标题旁
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Text {
                        Layout.fillWidth: true
                        text: drawer.titleText
                        color: Theme.text
                        font.pixelSize: Theme.fontTitle
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: drawer.subtitleText.length > 0
                        text: drawer.subtitleText
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
                        onClicked: drawer.closeRequested()
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

        // 体（滚动）
        Item {
            anchors.top: headerLine.bottom
            anchors.bottom: footer.visible ? footer.top : parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Loader {
                    id: navLoader
                    Layout.preferredWidth: 160
                    Layout.fillHeight: true
                    // 导航列同样离面板边 dialogPadding
                    Layout.leftMargin: Theme.dialogPadding
                    visible: drawer.navRail !== null
                    sourceComponent: drawer.navRail
                }

                Flickable {
                    id: bodyFlick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    // 用 Flickable 自身的 width，不用布局的 availableWidth ——
                    // 后者只有当本项确实在布局里才存在，首帧求值时会抛
                    // ReferenceError（availableWidth is not defined）。
                    contentWidth: width
                    contentHeight: bodyColumn.implicitHeight + Theme.dialogPadding * 2
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar {
                        policy: bodyFlick.contentHeight > bodyFlick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                    }

                    ColumnLayout {
                        id: bodyColumn
                        // 四边留 dialogPadding：与 DialogShell 同一口径，
                        // 否则抽屉正文会贴着面板右边框。
                        x: Theme.dialogPadding
                        y: Theme.dialogPadding
                        width: Math.max(0, bodyFlick.width - Theme.dialogPadding * 2)
                        spacing: Theme.spacingMd
                    }
                }
            }
        }

        // 底（固定，没有额外按钮时不占位）
        Item {
            id: footer
            visible: drawer.footerExtra !== null || drawer.footerAction !== null
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: visible ? 48 : 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.dialogPadding
                anchors.rightMargin: Theme.dialogPadding
                spacing: Theme.spacingSm

                Loader {
                    Layout.alignment: Qt.AlignVCenter
                    visible: drawer.footerExtra !== null
                    sourceComponent: drawer.footerExtra
                }

                Item { Layout.fillWidth: true }

                // 右侧主操作：与 DialogShell 底栏同一纪律（右对齐）
                Loader {
                    Layout.alignment: Qt.AlignVCenter
                    visible: drawer.footerAction !== null
                    sourceComponent: drawer.footerAction
                }
            }
        }
    }
}
