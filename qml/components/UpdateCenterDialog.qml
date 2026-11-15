import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 更新中心（DX-170）：标题栏「更新」按钮打开的统一界面。
//
// 原先设置面板「关于」页只讲最新版本，且日志截断 6 行；这里补上倒序的版本历史，
// 让用户能回看每次发版改了什么。状态与操作全部复用 UpdateManager 既有的
// Q_PROPERTY / Q_INVOKABLE，不另起一套更新逻辑；历史正文一律按纯文本渲染，
// 外链统一交给 updateManager.openReleaseUrl() 做 https 白名单校验。
DialogShell {
    id: root

    readonly property bool busy: updateManager.state === "checking"
                                 || updateManager.state === "downloading"
                                 || updateManager.state === "verifying"
                                 || updateManager.state === "installing"
    readonly property bool hasHistory: updateManager.releaseHistory.length > 0

    titleText: "软件更新"
    subtitleText: "当前版本 v" + updateManager.currentVersion + " · "
                  + (updateManager.installKind === "installed" ? "安装版 · 静默升级" : "便携版 · 退出后替换")
    sizeMode: "lg"
    maxBodyHeight: 460
    footerNote: updateManager.statusText.length > 0
                ? updateManager.statusText
                : "更新包下载后先校验 SHA-256 再安装"

    primaryText: updateManager.state === "available" ? "立即更新"
               : updateManager.state === "downloading" ? ("下载 " + updateManager.progress + "%")
               : updateManager.state === "verifying" ? "校验中…"
               : updateManager.state === "installing" ? "安装中…"
               : updateManager.state === "checking" ? "检查中…"
               : "检查更新"
    primaryEnabled: !root.busy
    secondaryText: "关闭"

    onPrimaryTriggered: {
        if (updateManager.state === "available")
            updateManager.downloadAndInstall()
        else
            updateManager.checkForUpdates(false)
    }
    onSecondaryTriggered: root.close()

    // 由 Main.qml 的 openUpdateCenter() 显式调一次，避免 Popup 重复 open 时
    // 既不刷历史、也不在首次打开时连发两个同样的请求。
    function refresh() { updateManager.refreshReleaseHistory() }

    // 单条历史版本卡片。entry 显式取名，避免内层 Repeater 的 modelData 遮蔽外层。
    Component {
        id: historyEntry

        Rectangle {
            id: entryCard
            property var entry: modelData
            property bool expanded: false
            readonly property bool isCurrent: entry.version === updateManager.currentVersion

            Layout.fillWidth: true
            radius: Theme.radiusControl
            implicitHeight: entryCol.implicitHeight + Theme.spacingMd
            color: entryCard.isCurrent ? Theme.accentSoft : Theme.input
            border.width: 1
            border.color: entryCard.isCurrent ? Theme.cyan : Theme.border

            ColumnLayout {
                id: entryCol
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "v" + entryCard.entry.version
                        color: entryCard.isCurrent ? Theme.cyan : Theme.text
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                        textFormat: Text.PlainText
                    }

                    Rectangle {
                        visible: entryCard.isCurrent
                        implicitWidth: currentBadge.implicitWidth + 12
                        implicitHeight: 18
                        radius: 9
                        color: Theme.alpha(Theme.cyan, 0.18)
                        border.width: 1
                        border.color: Theme.cyan

                        Text {
                            id: currentBadge
                            anchors.centerIn: parent
                            text: "当前使用"
                            color: Theme.cyan
                            font.pixelSize: Theme.fontMicro
                            textFormat: Text.PlainText
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        visible: entryCard.entry.date.length > 0
                        text: entryCard.entry.date
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                        textFormat: Text.PlainText
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: entryCard.entry.title.length > 0
                    text: entryCard.entry.title
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                }

                Repeater {
                    model: entryCard.entry.highlights || []
                    delegate: Text {
                        Layout.fillWidth: true
                        text: "· " + modelData
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: entryCard.expanded && entryCard.entry.notes.length > 0
                    text: entryCard.entry.notes
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMd

                    Text {
                        visible: entryCard.entry.notes.length > 0
                        text: entryCard.expanded ? "收起详情" : "展开详情"
                        color: Theme.accent
                        font.pixelSize: Theme.fontMicro
                        textFormat: Text.PlainText
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: entryCard.expanded = !entryCard.expanded
                        }
                    }

                    Text {
                        visible: entryCard.entry.url.length > 0
                        text: "打开 Release 页 ↗"
                        color: Theme.violet
                        font.pixelSize: Theme.fontMicro
                        textFormat: Text.PlainText
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: updateManager.openReleaseUrl(entryCard.entry.url)
                        }
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        // ① 当前状态
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusControl
            implicitHeight: statusCol.implicitHeight + Theme.spacingMd
            color: Theme.input
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                id: statusCol
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "v" + updateManager.currentVersion
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: updateManager.lastCheckAt > 0
                              ? "上次检查 " + Qt.formatDateTime(new Date(updateManager.lastCheckAt * 1000), "MM-dd hh:mm")
                              : "尚未检查更新"
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: updateManager.statusText
                    color: updateManager.state === "error" ? Theme.danger
                         : updateManager.updateAvailable ? Theme.cyan : Theme.muted
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                }

                Rectangle {
                    Layout.fillWidth: true
                    visible: updateManager.lastInstallIncomplete
                    implicitHeight: incompleteText.implicitHeight + 12
                    radius: Theme.radiusControl
                    color: Theme.warningSoft
                    border.width: 1
                    border.color: Theme.warning
                    Text {
                        id: incompleteText
                        anchors.fill: parent
                        anchors.margins: 6
                        text: "上次更新未完成：安装时被占用的文件没能替换，当前可能是新旧混装。建议重新检查更新。"
                        color: Theme.warning
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: updateManager.updateAvailable
                    text: "新版本 v" + updateManager.latestVersion
                          + (updateManager.publishedAt.length > 0 ? "（" + updateManager.publishedAt + "）" : "")
                          + (updateManager.releaseSize > 0 ? " · " + (updateManager.releaseSize / 1048576).toFixed(1) + " MB" : "")
                          + (updateManager.releaseNotes.length > 0 ? "\n" + updateManager.releaseNotes : "")
                    color: Theme.cyan
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
        }

        // ② 操作区：全部调用既有 Q_INVOKABLE，不复制一份更新逻辑
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            NeonButton {
                text: "刷新检查"
                enabled: !root.busy
                onClicked: updateManager.checkForUpdates(false)
            }
            NeonButton {
                text: "下载页"
                onClicked: updateManager.openDownloadPage()
            }
            NeonButton {
                visible: updateManager.updateAvailable
                text: "跳过此版本"
                onClicked: updateManager.skipVersion(updateManager.latestVersion)
            }
            Item { Layout.fillWidth: true }
            Text {
                text: updateManager.autoCheck ? "自动检查：开" : "自动检查：关"
                color: updateManager.autoCheck ? Theme.cyan : Theme.muted
                font.pixelSize: Theme.fontMicro
                textFormat: Text.PlainText
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: updateManager.setAutoCheck(!updateManager.autoCheck)
                }
            }
        }

        // ③ 版本历史标题与来源状态
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8

            Text {
                text: "版本历史"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }
            Text {
                Layout.fillWidth: true
                text: updateManager.historyStatus
                color: updateManager.historyStatus.indexOf("离线") === 0 ? Theme.warning : Theme.muted
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
            }
            NeonButton {
                text: updateManager.historyLoading ? "载入中…" : "刷新历史"
                enabled: !updateManager.historyLoading
                onClicked: root.refresh()
            }
        }

        // ④ 四态：加载中 / 空 / 离线提示 / 正常列表
        Text {
            Layout.fillWidth: true
            visible: updateManager.historyLoading && !root.hasHistory
            text: "正在载入版本历史…"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
        }

        Text {
            Layout.fillWidth: true
            visible: !updateManager.historyLoading && !root.hasHistory
            text: "暂无版本历史：联网后点「刷新历史」重试。"
            color: Theme.warning
            font.pixelSize: Theme.fontMicro
        }

        Rectangle {
            Layout.fillWidth: true
            visible: updateManager.historyStatus.indexOf("离线") === 0 && root.hasHistory
            implicitHeight: offlineText.implicitHeight + 10
            radius: Theme.radiusControl
            color: Theme.warningSoft
            border.width: 1
            border.color: Theme.warning
            Text {
                id: offlineText
                anchors.fill: parent
                anchors.margins: 5
                text: "当前离线，以下为本地缓存与内置摘要；联网后会自动补齐。"
                color: Theme.warning
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
            }
        }

        Repeater {
            model: updateManager.releaseHistory
            delegate: historyEntry
        }
    }
}
