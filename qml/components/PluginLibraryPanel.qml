import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 组件行为：Bound —— 委托里的 library 才能拿到根对象实例的属性。
// 根对象是 DialogShell 的实例，不加这句的话委托里拿到的是「类型」，属性全是 undefined。
pragma ComponentBehavior: Bound

// 插件库（R11-10）：迁到统一弹窗壳 DialogShell —— lg 720 档。
//
// 规格 §3.4 的落位：已装 / 可装左右两栏（原型 docs/design/dialogs.html 同款）。
// 正文区横向分两列：
//   左栏「已装」= 已安装插件清单；
//   右栏「可装」= 市场源管理 + 链接安装 + 可安装清单
//   （市场源归「可装」这一侧：插件库管「有哪些、能装什么」）。
DialogShell {
    id: library

    titleText: "插件库"
    subtitleText: "已装 " + installedCount + " · 可装 " + installableCount
    sizeMode: "lg"

    // 安装/更新完成后请求宿主弹出权限授权框
    signal permissionRequested(string pluginId)

    readonly property var installedEntries: entriesFor(true)
    readonly property var installableEntries: entriesFor(false)
    readonly property int installedCount: entriesFor(true).length
    readonly property int installableCount: entriesFor(false).length

    // 右上角关闭键：只关闭，不表态
    onCloseRequested: library.close()

    // 打开时刷新本地插件目录（原先在自绘的 open() 里做）
    onOpened: pluginLibrary.refresh()

    // 装完立刻检查新插件声明的权限，缺授权就交给宿主弹窗
    function checkPermissions(pluginId) {
        if (pluginRuntime.missingPermissions(pluginId).length > 0)
            permissionRequested(pluginId)
    }

    function sourceStatusText(spec) {
        var list = pluginMarket.sourceStatuses
        for (var i = 0; i < list.length; ++i) {
            if (list[i].spec === spec)
                return (list[i].ok ? (list[i].cached ? "缓存" : "已更新") : "失败") + " " + (list[i].message || "")
        }
        return ""
    }

    function addSource() {
        var spec = newSourceField.text.trim()
        if (spec.length === 0)
            return
        pluginLibrary.addMarketSource(spec, "")
        newSourceField.text = ""
        pluginMarket.refresh()
    }

    function installDirect() {
        var url = directUrlField.text.trim()
        if (url.length === 0)
            return
        pluginLibrary.installFromUrl(url)
        directUrlField.text = ""
    }

    // 按「已装 / 可装」拆分的插件清单
    function entriesFor(installed) {
        var out = []
        var list = pluginLibrary.entries
        for (var i = 0; i < list.length; ++i) {
            if (!!list[i].installed === installed)
                out.push(list[i])
        }
        return out
    }

    // 插件条目：两栏共用同一份委托（一份事实源，不抄两遍）
    property Component entryDelegate: Component {
        Rectangle {
            required property var modelData

            Layout.fillWidth: true
            // R15-02：行高随依赖说明行数伸缩（固定 58 会截断多行依赖）
            Layout.preferredHeight: entryBody.implicitHeight + 16
            radius: Theme.radiusControl
            color: Theme.card
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                id: entryBody
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: Theme.spacingXs

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        text: modelData.name
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                    }

                    Text {
                        text: modelData.version || "0.0.0"
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                    }

                    // 类别徽章（R15-02）：manifest / 市场索引的 category
                    Rectangle {
                        visible: (modelData.category || "").length > 0
                        width: categoryText.width + 10
                        height: 15
                        radius: Theme.radiusControl
                        color: Theme.alpha(Theme.cyan, 0.14)
                        border.width: 1
                        border.color: Theme.alpha(Theme.cyan, 0.4)

                        Text {
                            id: categoryText
                            anchors.centerIn: parent
                            text: modelData.category || ""
                            color: Theme.cyan
                            font.pixelSize: Theme.fontMicro
                        }
                    }

                    Rectangle {
                        id: statusBadge
                        width: badgeText.width + 10
                        height: 15
                        radius: Theme.radiusControl
                        color: Theme.alpha(badgeColor, 0.16)
                        border.width: 1
                        border.color: Theme.alpha(badgeColor, 0.4)

                        property color badgeColor: {
                            if (!modelData.installed)
                                return Theme.accent2
                            if (modelData.status === "disabled")
                                return Theme.muted
                            if (modelData.status !== "ok")
                                return Theme.danger
                            return Theme.success
                        }

                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: {
                                if (modelData.installed) {
                                    if (modelData.status === "disabled")
                                        return "已禁用"
                                    if (modelData.status !== "ok")
                                        return "异常"
                                    return "已安装"
                                }
                                if (modelData.origin === "market")
                                    return "市场"
                                if (modelData.source === "appdir")
                                    return "随包"
                                if (modelData.source === "official")
                                    return "官方"
                                if (modelData.source === "sample")
                                    return "示例"
                                return "可安装"
                            }
                            color: statusBadge.badgeColor
                            font.pixelSize: Theme.fontMicro
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        visible: modelData.origin === "market"
                        text: "市场"
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMd

                    Text {
                        visible: modelData.installed && modelData.status === "ok" && modelData.enabled
                        text: "打开"
                        color: openMouse.containsMouse ? Theme.cyan : Theme.text
                        font.pixelSize: Theme.fontCaption
                        MouseArea {
                            id: openMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: pluginLibrary.requestOpen(modelData.id)
                        }
                    }

                    Text {
                        visible: modelData.installed
                        text: modelData.enabled ? "禁用" : "启用"
                        color: toggleMouse.containsMouse ? Theme.cyan : Theme.text
                        font.pixelSize: Theme.fontCaption
                        MouseArea {
                            id: toggleMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: pluginLibrary.setEnabled(modelData.id, !modelData.enabled)
                        }
                    }

                    Text {
                        visible: modelData.installed
                        text: "卸载"
                        color: uninstallMouse.containsMouse ? Theme.danger : Theme.text
                        font.pixelSize: Theme.fontCaption
                        MouseArea {
                            id: uninstallMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: pluginLibrary.uninstall(modelData.id)
                        }
                    }

                    Text {
                        visible: !modelData.installed
                        text: "安装"
                        color: installMouse.containsMouse ? Theme.cyan : Theme.text
                        font.pixelSize: Theme.fontCaption
                        MouseArea {
                            id: installMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (pluginLibrary.install(modelData.id))
                                    library.checkPermissions(modelData.id)
                            }
                        }
                    }

                    Text {
                        visible: modelData.installed && modelData.hasMarket
                        text: "更新"
                        color: updateMouse.containsMouse ? Theme.cyan : Theme.text
                        font.pixelSize: Theme.fontCaption
                        MouseArea {
                            id: updateMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (pluginLibrary.update(modelData.id))
                                    library.checkPermissions(modelData.id)
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: modelData.error || ""
                        color: Theme.danger
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideRight
                        Layout.maximumWidth: 200
                    }
                }

                // 依赖说明（R15-02）：逐条列出运行前提，无内容时不占行
                Text {
                    visible: modelData.dependencies && modelData.dependencies.length > 0
                    text: "依赖：" + (modelData.dependencies || []).join("；")
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }

    // ---------- 正文：已装 / 可装左右两栏（规格 §3.4）----------
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            // ---- 左栏：已装 ----
            ColumnLayout {
                Layout.fillWidth: true
                // preferredWidth 在这里当权重用：布局按它的比例分配多余宽度（3 : 4 = 左窄右宽）
                Layout.preferredWidth: 3
                spacing: Theme.spacingSm

                Text {
                    Layout.fillWidth: true
                    text: "已装（" + library.installedCount + "）"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                    font.bold: true
                }

                Repeater {
                    model: library.installedEntries
                    delegate: library.entryDelegate
                }

                Text {
                    Layout.fillWidth: true
                    visible: library.installedEntries.length === 0
                    text: "还没有已安装的插件"
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                }
            }

            // 两栏之间的竖分隔线
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.hairline
            }

            // ---- 右栏：可装 ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 4
                spacing: Theme.spacingSm

                Text {
                    Layout.fillWidth: true
                    text: "可装（" + library.installableCount + "）"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                    font.bold: true
                }

                // ---- 市场源与安装入口（归「可装」这一栏：插件库管「有哪些、能装什么」）----
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    // 市场源
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            text: "市场源"
                            color: Theme.muted
                            font.pixelSize: Theme.fontMicro
                        }

                        Text {
                            text: pluginLibrary.marketSources.length + " 个"
                            color: Theme.muted
                            font.pixelSize: Theme.fontMicro
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: pluginLibrary.marketStatus
                            color: Theme.muted
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                            Layout.maximumWidth: 220
                        }
                    }

                    // 源列表
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs

                        Repeater {
                            model: pluginLibrary.marketSources

                            delegate: Rectangle {
                                required property int index
                                required property var modelData

                                Layout.fillWidth: true
                                Layout.preferredHeight: 26
                                radius: Theme.radiusControl
                                color: Theme.card
                                border.width: 1
                                border.color: Theme.border

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spacingSm
                                    anchors.rightMargin: Theme.spacingSm
                                    spacing: Theme.spacingSm

                                    Rectangle {
                                        Layout.preferredWidth: 6
                                        Layout.preferredHeight: 6
                                        radius: Theme.radiusControl
                                        color: modelData.enabled ? Theme.success : Theme.muted
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name + "  " + modelData.spec
                                        color: modelData.enabled ? Theme.text : Theme.muted
                                        font.pixelSize: Theme.fontMicro
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: library.sourceStatusText(modelData.spec)
                                        color: Theme.muted
                                        font.pixelSize: Theme.fontMicro
                                    }

                                    Text {
                                        text: "↑"
                                        color: srcUpMouse.containsMouse ? Theme.cyan : Theme.muted
                                        font.pixelSize: Theme.fontCaption
                                        MouseArea {
                                            id: srcUpMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: pluginLibrary.moveMarketSource(index, index - 1)
                                        }
                                    }

                                    Text {
                                        text: "↓"
                                        color: srcDownMouse.containsMouse ? Theme.cyan : Theme.muted
                                        font.pixelSize: Theme.fontCaption
                                        MouseArea {
                                            id: srcDownMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: pluginLibrary.moveMarketSource(index, index + 1)
                                        }
                                    }

                                    Text {
                                        text: modelData.enabled ? "停用" : "启用"
                                        color: srcToggleMouse.containsMouse ? Theme.cyan : Theme.muted
                                        font.pixelSize: Theme.fontMicro
                                        MouseArea {
                                            id: srcToggleMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: pluginLibrary.setMarketSourceEnabled(index, !modelData.enabled)
                                        }
                                    }

                                    Text {
                                        text: "✕"
                                        color: srcDelMouse.containsMouse ? Theme.danger : Theme.muted
                                        font.pixelSize: Theme.fontCaption
                                        MouseArea {
                                            id: srcDelMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: pluginLibrary.removeMarketSource(index)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 添加源
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        TextField {
                            id: newSourceField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 28
                            text: ""
                            placeholderText: "owner/repo 或 https://host/path/market.json"
                            color: Theme.text
                            font.pixelSize: Theme.fontMicro
                            selectByMouse: true
                            background: Rectangle {
                                color: Theme.input
                                radius: Theme.radiusControl
                                border.width: 1
                                border.color: Theme.border
                            }
                            onAccepted: library.addSource()
                        }

                        Text {
                            text: "添加源"
                            color: addSourceMouse.containsMouse ? Theme.cyan : Theme.text
                            font.pixelSize: Theme.fontCaption
                            MouseArea {
                                id: addSourceMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: library.addSource()
                            }
                        }
                    }

                    // 从链接安装
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            text: "链接安装"
                            color: Theme.muted
                            font.pixelSize: Theme.fontMicro
                        }

                        TextField {
                            id: directUrlField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 28
                            text: ""
                            placeholderText: "https://host/plugin.neon（可加 #sha256=...）"
                            color: Theme.text
                            font.pixelSize: Theme.fontMicro
                            selectByMouse: true
                            background: Rectangle {
                                color: Theme.input
                                radius: Theme.radiusControl
                                border.width: 1
                                border.color: Theme.border
                            }
                            onAccepted: library.installDirect()
                        }

                        Text {
                            text: "安装"
                            color: directInstallMouse.containsMouse ? Theme.cyan : Theme.text
                            font.pixelSize: Theme.fontCaption
                            MouseArea {
                                id: directInstallMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: library.installDirect()
                            }
                        }
                    }
                }

                // ---- 可安装清单 ----
                Repeater {
                    model: library.installableEntries
                    delegate: library.entryDelegate
                }

                Text {
                    Layout.fillWidth: true
                    visible: library.installableEntries.length === 0
                    text: "没有可安装的插件"
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                }
            }
        }

        // ---- 状态行 ----
        Text {
            Layout.fillWidth: true
            text: pluginLibrary.statusText
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            elide: Text.ElideRight
        }
    }

    // ---------- 底栏左侧：市场 / 目录管理动作 ----------
    footerExtra: Component {
        RowLayout {
            spacing: Theme.spacingSm

            TopToolButton {
                text: "刷新市场"
                description: "刷新插件市场索引"
                onClicked: pluginMarket.refresh()
            }

            TopToolButton {
                text: "重新扫描"
                description: "重新扫描本地插件目录"
                onClicked: pluginLibrary.refresh()
            }

            TopToolButton {
                text: "打开目录"
                description: "打开插件目录"
                onClicked: pluginLibrary.openPluginDir()
            }
        }
    }
}
