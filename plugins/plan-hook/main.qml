import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NeonBoard
import "rules.js" as Rules

// 计划钩子面板：规则编辑器 + 手动执行 + 执行记录。
// 规则存在 meta 表的 plan.rules 键里（与后台入口 background.qml 共用同一份），
// 因此这里的改动会立刻影响提交触发的自动流转行为。
Rectangle {
    id: root

    color: "transparent"

    property var rules: Rules.defaultRules()
    property var records: []

    function readRules() {
        rules = Rules.parseRules(pluginHost.metaValue("plan.rules", ""))
        var ds = rules.defaultStatus
        defaultStatusBox.currentIndex = ds === "done" ? 1 : (ds === "todo" ? 2 : 0)
        var mf = (rules.match && rules.match.field) ? rules.match.field : "title"
        matchFieldBox.currentIndex = mf === "note" ? 1 : (mf === "tags" ? 2 : 0)
        alreadyBox.currentIndex = rules.onAlreadyInTarget === "move" ? 1 : 0
    }

    function saveRules() {
        if (pluginHost.setMetaValue("plan.rules", JSON.stringify(rules)))
            statusText.text = "规则已保存"
        else
            statusText.text = "保存失败：缺少 meta 权限"
    }

    function runManual() {
        var text = inputField.text ? inputField.text : ""
        if (text.trim().length === 0) {
            statusText.text = "请输入提交信息"
            return
        }
        var actions = Rules.planActions(text, pluginHost.tasks(), rules)
        for (var i = 0; i < actions.length; i++)
            pluginHost.moveTaskToEnd(actions[i].id, actions[i].status)

        records.unshift({
            "text": text,
            "count": actions.length,
            "time": new Date().toLocaleTimeString()
        })
        while (records.length > 20)
            records.pop()

        statusText.text = actions.length > 0 ? (actions.length + " 个任务已流转") : "未匹配到任务"
    }

    Component.onCompleted: readRules()

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 10

                // ---------- 规则区 ----------
                Text {
                    text: "规则"
                    color: Theme.text
                    font.pixelSize: 12
                    font.bold: true
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "启用"
                        color: Theme.text
                        font.pixelSize: 11
                    }

                    Item { Layout.fillWidth: true }

                    Switch {
                        checked: root.rules.enabled !== false
                        onToggled: {
                            root.rules.enabled = checked
                            root.saveRules()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "默认目标"
                        color: Theme.muted
                        font.pixelSize: 10
                        Layout.preferredWidth: 60
                    }

                    ComboBox {
                        id: defaultStatusBox
                        Layout.fillWidth: true
                        model: ["doing", "done", "todo"]
                        font.pixelSize: 10
                        onActivated: {
                            root.rules.defaultStatus = currentText
                            root.saveRules()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "匹配字段"
                        color: Theme.muted
                        font.pixelSize: 10
                        Layout.preferredWidth: 60
                    }

                    ComboBox {
                        id: matchFieldBox
                        Layout.fillWidth: true
                        model: ["title", "note", "tags"]
                        font.pixelSize: 10
                        onActivated: {
                            root.rules.match.field = currentText
                            root.saveRules()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "已在目标列"
                        color: Theme.muted
                        font.pixelSize: 10
                        Layout.preferredWidth: 60
                    }

                    ComboBox {
                        id: alreadyBox
                        Layout.fillWidth: true
                        model: ["skip", "move"]
                        font.pixelSize: 10
                        onActivated: {
                            root.rules.onAlreadyInTarget = currentText
                            root.saveRules()
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "提交信息里写 #T12 → 标题含 #T12 的任务转为进行中；#T12=done / #T12=todo 指定目标状态。"
                    color: Theme.muted
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.border
                }

                // ---------- 手动执行区 ----------
                Text {
                    text: "手动执行"
                    color: Theme.text
                    font.pixelSize: 12
                    font.bold: true
                }

                TextField {
                    id: inputField
                    Layout.fillWidth: true
                    placeholderText: "粘贴提交信息，例如 fix(model): 修复 #T12 统计刷新"
                    font.pixelSize: 10
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 30
                    radius: 8
                    color: runArea.pressed ? Theme.elevated : Theme.card
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "执行"
                        color: Theme.cyan
                        font.pixelSize: 11
                    }

                    MouseArea {
                        id: runArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.runManual()
                    }
                }

                Text {
                    id: statusText
                    Layout.fillWidth: true
                    text: ""
                    color: Theme.muted
                    font.pixelSize: 9
                    elide: Text.ElideRight
                }

                // ---------- 执行记录 ----------
                Text {
                    text: "执行记录"
                    color: Theme.text
                    font.pixelSize: 12
                    font.bold: true
                }

                Repeater {
                    model: root.records

                    Text {
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.time + " · " + modelData.count + " 个任务 · "
                              + modelData.text.split("\n")[0]
                        color: Theme.muted
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
