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


    // 主题预设当前名（只读展示；换主题在标题栏的主题按钮里）
    readonly property string currentThemeName: {
        const list = Theme.presets
        const preset = list[Theme.presetIndex]
        return preset && preset.name ? preset.name : "（未知主题）"
    }

    // 画质当前档（只读展示）
    readonly property string currentQualityName: Theme.qualityNames[Theme.quality] || ""


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

    // ================= 分页 3：外观与动效 =================
    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
        visible: panel.page === "appearance"
        spacing: Theme.spacingMd

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "主题"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "主题预设"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: panel.currentThemeName
                    color: Theme.cyan
                    font.pixelSize: Theme.fontMicro
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "画质"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: panel.currentQualityName
                    color: Theme.cyan
                    font.pixelSize: Theme.fontMicro
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "减弱动效"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: Motion.reduced ? "已开启" : "未开启"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: "完整的外观面板在标题栏的主题按钮里，那里的主题是色卡网格。"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
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

    // ================= 分页 6：关于 =================
    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
        visible: panel.page === "about"
        spacing: Theme.spacingMd

        // ① 版本与快捷键
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "关于"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "NeonBoard 0.1.0 · Qt 6.8 · SQLite"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Ctrl+Z 撤销上一步"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }
            }
        }

        // ② 软件更新
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "软件更新"
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: updateManager.currentVersion
                    color: Theme.muted
                    font.family: "Cascadia Code"
                    font.pixelSize: Theme.fontMicro
                }
            }

            Text {
                Layout.fillWidth: true
                text: (updateManager.installKind === "installed"
                       ? "安装版 · 下载后静默升级"
                       : "便携版 · 退出后自动替换文件")
                      + (updateManager.lastCheckAt > 0
                         ? " · 上次检查 " + Qt.formatDateTime(new Date(updateManager.lastCheckAt * 1000), "MM-dd hh:mm")
                         : " · 尚未检查")
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: updateManager.statusText
                color: updateManager.state === "error" ? Theme.danger
                     : updateManager.updateAvailable ? Theme.cyan
                     : Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            // 安装回执版本与自身编译版本不符 = 上次静默升级被占用文件打断（R5-09 留下的物证）。
            // 这条异常过去只写进日志文件，用户看不到也不知道该做什么；显性化在更新区，
            // 并让整条可点 —— 就地重查更新是最直接的补救动作。
            Rectangle {
                Layout.fillWidth: true
                visible: updateManager.lastInstallIncomplete
                implicitHeight: incompleteHint.implicitHeight + 12
                radius: Theme.radiusControl
                color: Theme.warningSoft
                border.width: 1
                border.color: Theme.warning

                Text {
                    id: incompleteHint
                    anchors.fill: parent
                    anchors.margins: 6
                    text: "上次更新未完成：安装时被占用的文件没能替换，当前可能是新旧混装。点这里重新检查更新。"
                    color: Theme.warning
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    enabled: updateManager.state !== "checking"
                             && updateManager.state !== "downloading"
                             && updateManager.state !== "installing"
                    onClicked: updateManager.checkForUpdates(false)
                }
            }

            // 有更新时展示版本与更新日志；日志可能很长，截断 6 行不撑爆面板
            Text {
                Layout.fillWidth: true
                visible: updateManager.updateAvailable
                text: "新版本 " + updateManager.latestVersion
                      + (updateManager.publishedAt.length > 0 ? "（" + updateManager.publishedAt + "）" : "")
                      + (updateManager.releaseSize > 0
                         ? " · " + (updateManager.releaseSize / 1048576).toFixed(1) + " MB" : "")
                      + (updateManager.releaseNotes.length > 0 ? "\n" + updateManager.releaseNotes : "")
                color: Theme.text
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                maximumLineCount: 6
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8

                DataButton {
                    label: updateManager.state === "checking" ? "检查中…" : "检查更新"
                    Layout.fillWidth: true
                    enabled: updateManager.state !== "checking"
                             && updateManager.state !== "downloading"
                             && updateManager.state !== "installing"
                    opacity: enabled ? 1 : 0.45
                    onTriggered: updateManager.checkForUpdates(false)
                }

                DataButton {
                    label: updateManager.state === "downloading" ? ("下载 " + updateManager.progress + "%")
                         : updateManager.state === "verifying" ? "校验中…"
                         : updateManager.state === "installing" ? "安装中…"
                         : "立即更新"
                    accentColor: Theme.cyan
                    Layout.fillWidth: true
                    enabled: updateManager.state === "available"
                    opacity: enabled ? 1 : 0.45
                    onTriggered: updateManager.downloadAndInstall()
                }

                DataButton {
                    label: "下载页"
                    accentColor: Theme.violet
                    Layout.preferredWidth: 68
                    onTriggered: updateManager.openDownloadPage()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "自动检查"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    visible: updateManager.updateAvailable
                    text: "跳过此版本"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: updateManager.skipVersion(updateManager.latestVersion)
                    }
                }

                Switch {
                    checked: updateManager.autoCheck
                    onToggled: updateManager.setAutoCheck(checked)
                }
            }

            Text {
                Layout.fillWidth: true
                text: "更新包下载后先校验 SHA-256 再安装；安装版走静默升级，便携版由自更新脚本在程序退出后替换文件并重新拉起。"
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }
    }


}
