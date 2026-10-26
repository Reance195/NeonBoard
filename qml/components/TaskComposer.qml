import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 统一新建入口：标题（复用输入解析）+ 目标列 + 父卡。
// 列底快速添加保留，本弹窗用于「明确选列 / 挂父卡 / 先看解析结果」的场景。
Dialog {
    id: composer

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 20
    width: Math.min(460, Overlay.overlay.width - 80)
    closePolicy: Popup.CloseOnEscape

    property string status: "todo"

    readonly property var preview: taskStore.previewInput(titleField.text)
    readonly property var parentChoices: {
        const tasks = taskStore.allTasks()
        const out = [{ id: "", title: "（无父卡）" }]
        for (let i = 0; i < tasks.length; ++i) {
            if (tasks[i].status !== "done")
                out.push({ id: tasks[i].id, title: tasks[i].title })
        }
        return out
    }

    function openComposer(defaultStatus) {
        status = defaultStatus && defaultStatus.length > 0 ? defaultStatus : "todo"
        titleField.text = ""
        parentBox.currentIndex = 0
        open()
        titleField.forceActiveFocus()
    }

    function submit() {
        const text = titleField.text.trim()
        if (text.length === 0)
            return
        const id = taskStore.addTask(text, composer.status, 1, "", 0)
        if (id.length > 0 && parentBox.currentValue)
            taskTree.setParent(id, parentBox.currentValue)
        composer.close()
    }

    background: Rectangle {
        color: Theme.panelSolid
        radius: 16
        border.width: 1
        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            text: "新建任务"
            color: Theme.text
            font.pixelSize: 18
            font.bold: true
        }

        TextField {
            id: titleField
            Layout.fillWidth: true
            placeholderText: "任务标题，支持「明天」「!高」「#标签」"
            placeholderTextColor: Theme.muted
            color: Theme.text
            font.pixelSize: 14
            selectByMouse: true
            background: Rectangle {
                color: Theme.elevated
                radius: 8
                border.width: 1
                border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
            }
            onAccepted: composer.submit()
        }

        // 解析预览：与列底快速添加同一套 TaskInputParser，避免两处行为不一致
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: titleField.text.trim().length > 0

            Text {
                text: "解析"
                color: Theme.muted
                font.pixelSize: 11
            }

            Text {
                Layout.fillWidth: true
                text: composer.preview.title
                color: Theme.text
                font.pixelSize: 11
                elide: Text.ElideRight
            }

            Text {
                visible: composer.preview.hasDue
                text: "◷ " + composer.preview.dueText
                color: Theme.cyan
                font.pixelSize: 10
            }

            Text {
                visible: composer.preview.priority > 1
                text: composer.preview.priority === 3 ? "!! 高" : "! 中"
                color: Theme.accent3
                font.pixelSize: 10
            }

            Repeater {
                model: composer.preview.tags
                delegate: Text {
                    text: "#" + modelData
                    color: Theme.cyan
                    font.pixelSize: 10
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "列"
                color: Theme.muted
                font.pixelSize: 12
            }

            Repeater {
                model: [["待办", "todo"], ["进行中", "doing"], ["已办结", "done"]]
                delegate: Rectangle {
                    width: 68
                    height: 30
                    radius: 8
                    color: composer.status === modelData[1]
                           ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.48)
                           : Theme.input
                    border.width: 1
                    border.color: composer.status === modelData[1] ? Theme.cyan : Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: modelData[0]
                        color: composer.status === modelData[1] ? Theme.text : Theme.muted
                        font.pixelSize: 11
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: composer.status = modelData[1]
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "父卡"
                color: Theme.muted
                font.pixelSize: 12
            }

            ComboBox {
                id: parentBox
                Layout.fillWidth: true
                model: composer.parentChoices
                textRole: "title"
                valueRole: "id"
                font.pixelSize: 12
                contentItem: Text {
                    leftPadding: 10
                    text: parentBox.displayText
                    color: Theme.text
                    font: parentBox.font
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitHeight: 32
                    color: Theme.elevated
                    radius: 8
                    border.width: 1
                    border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.20)
                }
                delegate: ItemDelegate {
                    width: parentBox.width
                    highlighted: parentBox.highlightedIndex === index
                    contentItem: Text {
                        text: modelData.title
                        color: Theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: highlighted ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.16)
                                           : "transparent"
                    }
                }
                popup: Popup {
                    y: parentBox.height + 4
                    width: parentBox.width
                    implicitHeight: Math.min(contentItem.implicitHeight + 2, 280)
                    padding: 1
                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: parentBox.popup.visible ? parentBox.delegateModel : null
                        currentIndex: parentBox.highlightedIndex
                    }
                    background: Rectangle {
                        color: Theme.panelSolid
                        radius: 8
                        border.width: 1
                        border.color: Theme.border
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                text: "取消"
                onClicked: composer.close()
            }

            Button {
                text: "创建"
                enabled: titleField.text.trim().length > 0
                onClicked: composer.submit()
            }
        }
    }

    onClosed: titleField.text = ""
}
