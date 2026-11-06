import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 组件行为：Bound —— 委托里的 dialog 才能拿到根对象实例的属性。
// 根对象是 DialogShell 的实例，不加这句的话委托里拿到的是「类型」，属性全是 undefined。
pragma ComponentBehavior: Bound

// 插件权限授权弹窗（R11-10）：迁到统一弹窗壳 DialogShell —— sm 440 档。
//
// 规格 §3.5（本轮唯一的语义变更）：遮罩点击 / Esc / 右上角关闭键**只关闭不表态** ——
// 清掉「已提示」标记，插件下次调用该能力还能再弹一次；拒绝必须显式点「拒绝」。
// 旧实现里遮罩点击等同拒绝，而后端 m_promptedPermissions 只增不减，
// 于是误点窗口外的空白 = 永久拒绝且没有撤销入口。
//
// 三条路径（decided 区分「有没有表态过」）：
//   遮罩 / Esc / 关闭键 → releasePermissionPrompt（清「已提示」标记，不写任何授权数据）
//   拒绝              → setPermissionGranted(false) 逐条显式拒绝（不再重复骚扰）
//   允许              → grantPermissions
DialogShell {
    id: dialog

    titleText: "插件权限请求"
    subtitleText: dialog.prompt.sourceLabel ? "来源：" + dialog.prompt.sourceLabel : ""
    sizeMode: "sm"

    property string pluginId: ""
    property var prompt: ({})

    // 用户是否已经明确表态（点了「允许」/「拒绝」）
    property bool decided: false

    // 展示插件名、来源与权限逐条说明（含中文用途说明与风险级别）
    function present(id) {
        pluginId = id
        prompt = pluginRuntime.permissionPrompt(id)
        decided = false
        open()
    }

    function pendingNames() {
        var names = []
        var list = prompt.permissions ? prompt.permissions : []
        for (var i = 0; i < list.length; ++i)
            names.push(list[i].name)
        return names
    }

    // 「允许」：写入授权
    function allowAll() {
        decided = true
        var names = pendingNames()
        if (names.length > 0)
            pluginRuntime.grantPermissions(pluginId, names)
        close()
    }

    // 「拒绝」：显式拒绝，语义上「不再重复骚扰用户」——不调 releasePermissionPrompt
    function denyAll() {
        decided = true
        var list = prompt.permissions ? prompt.permissions : []
        for (var i = 0; i < list.length; ++i)
            pluginRuntime.setPermissionGranted(pluginId, list[i].name, false)
        close()
    }

    // 右上角关闭键：与遮罩 / Esc 同属「只关闭不表态」
    onCloseRequested: dialog.close()

    // 壳关闭后的收尾：没表态过 = 遮罩 / Esc / 关闭键 → 只清「已提示」标记
    onClosed: {
        if (!decided) {
            var names = pendingNames()
            if (names.length > 0)
                pluginRuntime.releasePermissionPrompt(pluginId, names)
        }
        decided = false
    }

    // 打开时把焦点交给壳（Esc 要能被壳收到）
    onOpened: forceActiveFocus()

    // 底部：主操作「允许」accent 强调，次操作「拒绝」
    primaryText: pendingNames().length > 0 ? "允许" : ""
    secondaryText: "拒绝"

    onPrimaryTriggered: dialog.allowAll()
    onSecondaryTriggered: dialog.denyAll()

    // ---------- 正文 ----------
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        Text {
            Layout.fillWidth: true
            text: "「" + (dialog.prompt.pluginName || dialog.pluginId) + "」请求以下权限："
            color: Theme.text
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: dialog.prompt.permissions || []

            delegate: Rectangle {
                required property var modelData

                Layout.fillWidth: true
                implicitHeight: permRow.implicitHeight + 20
                radius: Theme.radiusCard
                color: Theme.card
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    id: permRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 10
                    spacing: Theme.spacingSm

                    // 风险级别
                    Rectangle {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 18
                        radius: Theme.radiusControl
                        color: modelData.risk === "高" ? Theme.alpha(Theme.danger, 0.18)
                             : modelData.risk === "中" ? Theme.alpha(Theme.warning, 0.18)
                             : Theme.alpha(Theme.cyan, 0.16)

                        Text {
                            anchors.centerIn: parent
                            text: modelData.risk || "低"
                            color: modelData.risk === "高" ? Theme.danger
                                 : modelData.risk === "中" ? Theme.warning
                                 : Theme.cyan
                            font.pixelSize: Theme.fontMicro
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: modelData.title || modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.fontCaption
                            font.bold: true
                        }

                        // 用途说明：这条权限用来干什么（规格 §3.4）
                        Text {
                            Layout.fillWidth: true
                            text: (modelData.detail || "") + "（" + modelData.name + "）"
                            color: Theme.muted
                            font.pixelSize: Theme.fontMicro
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: dialog.pendingNames().length === 0
            text: "该插件没有新的权限请求。"
            color: Theme.muted
            font.pixelSize: Theme.fontCaption
        }
    }

    // ---------- 底栏左侧：说明「关闭不表态」的含义 ----------
    footerNote: "点空白 / Esc = 稍后再说"
}
