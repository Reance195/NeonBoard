import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 关于分页（MOD-23 自 SettingsPanel.qml 原样迁入）：
// 版本信息 / 更新设置（updateManager 等为上下文属性）。
// visible 由实例化处按页绑定。
ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
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
