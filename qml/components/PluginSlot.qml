import QtQuick
import QtQuick.Layouts

// 插件槽位：把插件声明的界面（contributes.slots）装进宿主预留的位置。
// 侧栏 / 底部面板 / 卡片字段共用这一个组件，用 slotName 区分。
// 插件页根节点声明 `property var host` 即可拿到绑定到该插件的 PluginHost 门面；
// 卡片槽位会额外注入 `taskId`（宿主当前卡片的任务 id）。
ColumnLayout {
    id: slot

    property string slotName: "sidebar"
    property string taskId: ""

    readonly property var slotModel: {
        if (slotName === "bottom")
            return pluginRuntime.bottomSlotItems
        if (slotName === "card")
            return pluginRuntime.cardSlotItems
        return pluginRuntime.sidebarSlotItems
    }

    spacing: 6
    visible: repeater.count > 0

    Repeater {
        id: repeater
        model: slot.slotModel

        Loader {
            required property var modelData

            Layout.fillWidth: true
            Layout.preferredHeight: item ? item.implicitHeight : 0
            source: modelData.url

            onLoaded: {
                if (!item)
                    return
                if (item.host !== undefined)
                    item.host = pluginRuntime.hostFor(modelData.pluginId)
                if (item.taskId !== undefined && slot.taskId.length > 0)
                    item.taskId = slot.taskId
            }
        }
    }
}
