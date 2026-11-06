import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 插件抽屉（R11-11）：展示当前插件的内容。
//
// 形态改造：原来是「右下角 380 × 420 的小浮层」，位置与其它面板都不一样；
// 现在统一为「主区右侧 320 px 抽屉」—— 头固定 / 体滚动 / 底固定、右上角统一
// 关闭键，全部由 DrawerShell 提供，本组件只负责体里的内容。
//
// 插件 QML 只在用户点开某个插件时才实例化，首帧不再为它付出构造代价。
// 注意：本根节点不设 width/height/anchors —— 尺寸与位置由 Main.qml 里的 Loader 决定。
DrawerShell {
    id: panel

    // 开合由 open() / close() 驱动（DrawerShell 是 Item，visible 会连带隐藏遮罩与面板）
    visible: false

    // 头部标题：默认取第一个插件名，openPlugin() 打开某个插件后换成它的名字
    property string pluginTitle: ""

    // 当前打开的插件 id：关闭时清空，命令补投递也靠它定位
    property string currentPluginId: ""

    titleText: pluginTitle.length > 0
               ? pluginTitle
               : (pluginLoader.plugins.length > 0 ? pluginLoader.plugins[0].name : "插件")
    subtitleText: "当前插件内容"

    // 底部固定：插件目录入口（原先塞在自绘头部里，现在归到底栏右侧）
    footerAction: Component {
        Rectangle {
            width: 58
            height: 24
            radius: Theme.radiusControl
            color: openDirMouse.containsMouse ? Theme.alpha(Theme.text, 0.08) : "transparent"

            Text {
                anchors.centerIn: parent
                text: "目录"
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
            }

            MouseArea {
                id: openDirMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: pluginLoader.openPluginDir()
            }

            Accessible.role: Accessible.Button
            Accessible.name: "打开插件目录"
        }
    }

    // Esc 关闭：DrawerShell 不是 Popup，Esc 不会自动生效，得自己接
    focus: visible
    Keys.onEscapePressed: panel.close()
    onVisibleChanged: if (visible) panel.forceActiveFocus()
    Component.onCompleted: if (visible) panel.forceActiveFocus()

    // R16-04：agent profile 名片及其零件。内联组件不能嵌套声明，全部提升到根级。
    // 声明「agentProfiles 但无面板页」的插件由 ProfileCard 接管，数据单源于 manifest ——
    // 四份 ~90% 同构、与 plugin.json 双源靠人肉对齐的 main.qml 名片已删；
    // 机台实测细节（stdin 接 NUL、.cmd 包装器等）在 docs/agent-profile.md。
    component Badge: Rectangle {
        property string label: ""
        property bool good: true

        width: badgeText.implicitWidth + 12
        height: 16
        radius: Theme.radiusControl
        color: good ? Theme.alpha(Theme.success, 0.14)
                    : Theme.alpha(Theme.warning, 0.14)
        border.width: 1
        border.color: good ? Theme.success : Theme.warning

        Text {
            id: badgeText
            anchors.centerIn: parent
            text: parent.label
            color: parent.good ? Theme.success : Theme.warning
            font.pixelSize: Theme.fontMicro
        }
    }

    component KeyValue: RowLayout {
        property string k: ""
        property string v: ""
        Layout.fillWidth: true
        spacing: 8

        Text {
            text: k
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            Layout.preferredWidth: 64
        }

        // 只读值用 TextEdit：R16-04 原本想用 selectByMouse 让路径 / 命令可复制，
        // 但 Text 没有这个属性，QML 报「Cannot assign to non-existent property」
        // 会让整个插件面板加载失败、抽屉打不开。TextEdit 才是能选中的那个控件。
        TextEdit {
            Layout.fillWidth: true
            text: v
            color: Theme.text
            font.pixelSize: Theme.fontMicro
            font.family: "Consolas"
            wrapMode: TextEdit.WrapAnywhere
            readOnly: true
            selectByMouse: true
            selectionColor: Theme.alpha(Theme.cyan, 0.35)
            selectedTextColor: Theme.text
        }
    }

    component ProfileCard: ColumnLayout {
        id: card

        property var plugin: null

        readonly property var profile: plugin && plugin.agentProfiles && plugin.agentProfiles.length > 0
                                       ? plugin.agentProfiles[0] : null
        readonly property bool granted: pluginHost.hasPermission("exec.agentProfiles")
        readonly property bool profileEnabled: !profile || profile.enabled !== false
        readonly property string exeLine: profile
            ? profile.exe + " " + (profile.args || []).join(" ") + " <prompt>" : ""
        readonly property string readonlyLine: profile
            ? profile.exe + " " + (profile.readonlyArgs || []).join(" ") + " <prompt>" : ""

        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: card.profile ? (card.profile.displayName || card.plugin.name) : ""
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.bold: true
            }

            Badge { label: card.profileEnabled ? "已启用" : "默认未启用"; good: card.profileEnabled }
            Badge { label: card.granted ? "已授权" : "待授权"; good: card.granted }
            Item { Layout.fillWidth: true }
        }

        Text {
            Layout.fillWidth: true
            text: card.plugin && card.plugin.description ? card.plugin.description : ""
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        KeyValue { k: "执行命令"; v: card.exeLine }
        KeyValue { k: "只读命令"; v: card.readonlyLine }
        KeyValue {
            k: "输出解析"
            v: card.profile && card.profile.output === "stream-json"
               ? "stream-json（逐事件流式写进运行日志）" : "json"
        }
        KeyValue {
            k: "超时"
            v: card.profile ? (card.profile.defaultTimeoutMin + " 分钟（可在设置页逐条覆盖）") : ""
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        Text {
            Layout.fillWidth: true
            text: card.granted
                  ? "已获「声明 Agent」权限：声明的 profile 会出现在执行器与卡片派活的 agent 列表里，来源标为 plugin。"
                  : "尚未获得「声明 Agent」权限：声明被静默忽略（权限决定拉起哪个进程，不是提示项）。请到插件授权里开启后再试。"
            color: card.granted ? Theme.muted : Theme.warning
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: "本页由宿主按 plugin.json 渲染，参数以 manifest 为唯一事实源；机台装法与实测边界见 docs/agent-profile.md。"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }
    }

    function open() {
        visible = true
    }

    function close() {
        visible = false
        currentPluginId = ""
        pluginContent.source = ""
        profileCard.visible = false
        pluginHost.deactivate()
    }

    function openPlugin(index) {
        visible = true
        var plugin = pluginLoader.plugins[index]
        if (!plugin || plugin.status !== "ok")
            return
        if (!pluginHost.activate(plugin.id)) {
            pluginTitle = "插件激活失败"
            pluginContent.source = ""
            profileCard.visible = false
            return
        }
        currentPluginId = plugin.id
        pluginTitle = plugin.name
        pluginContent.source = ""
        profileCard.visible = false
        if (plugin.mainUrl && plugin.mainUrl.length > 0) {
            pluginContent.source = plugin.mainUrl
        } else if (plugin.agentProfiles && plugin.agentProfiles.length > 0) {
            // R16-04：无面板页且声明了 agentProfiles 的插件，由宿主按 manifest 渲染名片页
            profileCard.plugin = plugin
            profileCard.visible = true
            pluginRuntime.dispatchPending(panel.currentPluginId)
        }
    }

    // ---------- 已装插件概况 ----------
    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Rectangle {
            width: 6
            height: 6
            radius: Theme.radiusControl
            color: Theme.cyan
        }

        Text {
            text: "已装插件 " + pluginLoader.plugins.length + " 个"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
        }

        Item { Layout.fillWidth: true }

        Text {
            text: "PLUGIN"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            font.letterSpacing: 1.5
        }
    }

    // ---------- 插件内容区 ----------
    // 插件自己的 QML 用 anchors.fill 铺满，所以这里必须给一块确定高度的区域，
    // 不能让它塌成 0（壳的体是滚动列，没有「填满剩余高度」这一说）。
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(240, Math.min(520, panel.height - 200))

        Loader {
            id: pluginContent
            anchors.fill: parent

            // 插件界面就绪后补投递排队中的命令（快捷键/命令面板触发的）
            onLoaded: {
                if (panel.currentPluginId.length > 0)
                    pluginRuntime.dispatchPending(panel.currentPluginId)
            }
        }

        ProfileCard {
            id: profileCard
            visible: false
            anchors.fill: parent
        }
    }

    // 插件装不上时的原因
    Text {
        visible: pluginLoader.plugins.length > 0
                 && pluginLoader.plugins[0].status !== "ok"
        text: pluginLoader.plugins.length > 0 ? pluginLoader.plugins[0].error : ""
        color: Theme.magenta
        font.pixelSize: Theme.fontMicro
        wrapMode: Text.WordWrap
    }
}
