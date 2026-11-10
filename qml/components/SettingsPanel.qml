import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 设置面板（R11-10）：迁到统一弹窗壳 DialogShell —— lg 720 档。
//
// 结构（规格 §3.4「设置 → lg 720，左侧 160 px 固定导航，底部按钮常驻」）：
//   头固定：标题「设置 / SETTINGS」+ 右上角统一关闭键
//   体：左侧 160 px 固定分页导航（不随正文滚动）+ 右侧滚动正文（6 个分页）
//   底固定：左侧说明 + 右侧「关闭」
//
// 默认落在「热键与自动化」，与原型 docs/design/dialogs.html 的 SETTINGS_PAGES 一致。
//
// 底栏没有「保存」：面板里每一项都是即时生效的（开关即改、导入导出当场执行），
// 放一个什么都不做的「保存」只会让人以为设置没存住。
//
// profile 编辑器与「绑定仓库」两个浮层留在文件末尾，不进分页、不改成壳：
// 它们是浮在设置面板之上的二级弹窗，拆进分页会把「编辑」变成「换页」。
DialogShell {
    id: panel

    // ---- 壳配置 ----
    titleText: "设置"
    subtitleText: "SETTINGS"
    sizeMode: "lg"
    maxBodyHeight: 520

    // 底栏常驻：只有「关闭」。没有「保存」——设置即时生效。
    secondaryText: "关闭"
    footerNote: "设置即时生效，改完即用，无需保存。"
    onSecondaryTriggered: panel.close()
    onCloseRequested: panel.close()

    // Main.qml 用它连到插件库面板
    signal openLibraryRequested()

    // 当前分页（键与左侧导航一一对应）
    property string page: "hotkey"

    // 左侧导航的 6 项（顺序即导航顺序）
    readonly property var pages: [
        { key: "hotkey", label: "热键与自动化" },
        { key: "data", label: "数据与备份" },
        { key: "appearance", label: "外观与动效" },
        { key: "exec", label: "任务执行" },
        { key: "plugin", label: "插件" },
        { key: "about", label: "关于" }
    ]

    // 每次打开回到第一页（与原型一致），同时刷新导入文件清单
    onOpened: {
        panel.page = "hotkey"
        dataBackupPage.refreshExports()
    }

    // ---- 左侧固定分页导航（壳给的 160 px 一列，不参与滚动）----
    navRail: Component {
        Item {
            id: rail

            implicitWidth: 160
            implicitHeight: 240

            // 与正文之间的分隔线
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.hairline
            }

            Column {
                id: railColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingSm
                spacing: Theme.spacingXs

                Repeater {
                    model: panel.pages

                    delegate: Rectangle {
                        id: navRow
                        required property var modelData

                        width: railColumn.width
                        height: 30
                        radius: Theme.radiusControl
                        color: panel.page === navRow.modelData.key
                               ? Theme.accentSoft
                               : (navMouse.containsMouse ? Theme.card : "transparent")
                        border.width: 1
                        border.color: panel.page === navRow.modelData.key
                                      ? Theme.alpha(Theme.cyan, 0.45)
                                      : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSm
                            anchors.rightMargin: Theme.spacingSm
                            spacing: Theme.spacingXs

                            Text {
                                Layout.fillWidth: true
                                text: navRow.modelData.label
                                color: panel.page === navRow.modelData.key ? Theme.cyan : Theme.muted
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                            }

                            // 插件页角标：已装插件数
                            Rectangle {
                                visible: navRow.modelData.key === "plugin"
                                width: pluginBadge.implicitWidth + 8
                                height: 15
                                radius: Theme.radiusControl
                                color: Theme.alpha(Theme.cyan, 0.16)
                                border.width: 1
                                border.color: Theme.alpha(Theme.cyan, 0.40)

                                Text {
                                    id: pluginBadge
                                    anchors.centerIn: parent
                                    text: pluginLoader.plugins.length + ""
                                    color: Theme.cyan
                                    font.pixelSize: Theme.fontMicro
                                }
                            }
                        }

                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.page = navRow.modelData.key
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: navRow.modelData.label
                        Accessible.description: "切换到「" + navRow.modelData.label + "」页"
                    }
                }
            }
        }
    }

    // ================= 分页 1：热键与自动化（MOD-20 整迁 components/HotkeyPage.qml）=================
    HotkeyPage {
        visible: panel.page === "hotkey"
    }
    // ================= 分页 2：数据与备份（MOD-19 整迁 components/DataBackupPage.qml）=================
    DataBackupPage {
        id: dataBackupPage
        visible: panel.page === "data"
        hostShell: panel
    }

    // ================= 分页 3：外观与动效（MOD-24 整迁 components/AppearancePage.qml）=================
    AppearancePage {
        visible: panel.page === "appearance"
    }

    // ================= 分页 4：任务执行（MOD-22 整迁 components/ExecPage.qml）=================
    ExecPage {
        id: execPage
        visible: panel.page === "exec"
        hostShell: panel
    }
    // ================= 分页 5：插件（MOD-21 整迁 components/PluginPage.qml）=================
    PluginPage {
        id: pluginPage
        visible: panel.page === "plugin"
        onOpenLibraryRequested: openLibraryRequested()
    }

    // ================= 分页 6：关于（MOD-23 整迁 components/AboutPage.qml）=================
    AboutPage {
        visible: panel.page === "about"
    }

}
