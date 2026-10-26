import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 列管理面板（R4-03）：增列 / 改名 / 调序 / 删列。
//
// 列结构的唯一事实源是库里的 board_column，所有写操作都走 boardColumns 的
// Q_INVOKABLE（内部写库后 reload），面板只负责展示与输入校验 ——
// 面板自己再存一份列数组，就会出现「面板里删了、看板上还在」的两套状态。
Item {
    id: manager

    property bool open: false

    signal closed()

    function close() {
        open = false
        manager.closed()
    }

    function statusLabel(status) {
        return status === "done" ? "已完成" : (status === "doing" ? "进行中" : "待办")
    }

    function statusColor(status) {
        return status === "done" ? Theme.magenta
             : (status === "doing" ? Theme.violet : Theme.cyan)
    }

    visible: opacity > 0.001
    opacity: open ? 1 : 0

    Behavior on opacity {
        NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
    }

    // 遮罩：点空白关闭
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.62)

        MouseArea {
            anchors.fill: parent
            onClicked: manager.close()
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: 460
        height: Math.min(parent.height - 96, content.implicitHeight + 40)
        radius: 16
        color: Theme.panelSolid
        border.width: 1
        border.color: Theme.borderStrong

        // 顶部霓虹条
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 1
            height: 2
            radius: 1
            color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.35)
        }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "列管理"
                    color: Theme.text
                    font.pixelSize: 16
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "列的先后就是看板上的左右顺序"
                    color: Theme.muted
                    font.pixelSize: 11
                }

                // 关闭
                Rectangle {
                    width: 28
                    height: 28
                    radius: 8
                    color: closeMouse.containsMouse ? Theme.ghost : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: Theme.muted
                        font.pixelSize: 13
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: manager.close()
                    }
                }
            }

            // 现有列：改名 / 左右调序 / 删除
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(columnList.implicitHeight, 300)
                contentHeight: columnList.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: columnList
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: boardColumns

                        delegate: Rectangle {
                            required property string boardColumnId
                            required property string boardColumnTitle
                            required property string boardColumnStatus
                            required property var boardColumnModel
                            required property int boardColumnIndex

                            Layout.fillWidth: true
                            height: 40
                            radius: 9
                            color: Theme.card
                            border.width: 1
                            border.color: Qt.rgba(manager.statusColor(boardColumnStatus).r,
                                                  manager.statusColor(boardColumnStatus).g,
                                                  manager.statusColor(boardColumnStatus).b, 0.22)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 8
                                spacing: 8

                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: manager.statusColor(boardColumnStatus)
                                }

                                TextField {
                                    id: nameField
                                    Layout.fillWidth: true
                                    text: boardColumnTitle
                                    color: Theme.text
                                    font.pixelSize: 12
                                    selectByMouse: true
                                    background: null
                                    placeholderText: "列名"
                                    placeholderTextColor: Theme.muted
                                    // 失焦或回车时提交：边打边写会让「改名」变成逐字符事务
                                    onEditingFinished: {
                                        if (text.trim().length > 0 && text.trim() !== boardColumnTitle)
                                            boardColumns.renameColumn(boardColumnId, text)
                                    }
                                }

                                Text {
                                    text: manager.statusLabel(boardColumnStatus)
                                    color: manager.statusColor(boardColumnStatus)
                                    font.pixelSize: 10
                                }

                                Text {
                                    text: boardColumnModel ? boardColumnModel.count : 0
                                    color: Theme.muted
                                    font.pixelSize: 10
                                }

                                // ← →
                                Repeater {
                                    model: [
                                        { glyph: "‹", delta: -1 },
                                        { glyph: "›", delta: 1 }
                                    ]

                                    delegate: Rectangle {
                                        required property var modelData
                                        width: 24
                                        height: 24
                                        radius: 7
                                        color: arrowMouse.containsMouse ? Theme.ghost : Theme.input

                                        Text {
                                            anchors.centerIn: parent
                                            text: modelData.glyph
                                            color: Theme.text
                                            font.pixelSize: 14
                                        }

                                        MouseArea {
                                            id: arrowMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: boardColumns.moveColumn(boardColumnId,
                                                                               modelData.delta)
                                        }
                                    }
                                }

                                // 删除：某状态只剩一列时禁用（否则该状态的卡无处可去）
                                Rectangle {
                                    width: 24
                                    height: 24
                                    radius: 7
                                    readonly property bool allowed:
                                        boardColumns.columnCountForStatus(boardColumnStatus) > 1
                                    color: allowed
                                           ? (deleteMouse.containsMouse
                                              ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.20)
                                              : Theme.input)
                                           : Qt.rgba(1, 1, 1, 0.03)
                                    opacity: allowed ? 1 : 0.4

                                    Text {
                                        anchors.centerIn: parent
                                        text: "✕"
                                        color: parent.allowed ? Theme.danger : Theme.muted
                                        font.pixelSize: 11
                                    }

                                    MouseArea {
                                        id: deleteMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: parent.allowed
                                        // 卡还在列里时先确认：删除会把它们挪到同状态的另一列
                                        onClicked: {
                                            if (boardColumnModel && boardColumnModel.count > 0)
                                                confirmRow.pendingId = boardColumnId
                                            else
                                                boardColumns.removeColumn(boardColumnId)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 删除确认条（列里有卡时才出现）
            Rectangle {
                id: confirmRow
                property string pendingId: ""
                Layout.fillWidth: true
                visible: pendingId.length > 0
                height: visible ? 38 : 0
                radius: 9
                color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
                border.width: 1
                border.color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.35)

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: "该列里的卡片会挪到同状态的其它列，确定删除？"
                        color: Theme.text
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        width: 52
                        height: 24
                        radius: 7
                        color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.22)

                        Text {
                            anchors.centerIn: parent
                            text: "删除"
                            color: Theme.danger
                            font.pixelSize: 11
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (confirmRow.pendingId.length > 0)
                                    boardColumns.removeColumn(confirmRow.pendingId)
                                confirmRow.pendingId = ""
                            }
                        }
                    }

                    Rectangle {
                        width: 44
                        height: 24
                        radius: 7
                        color: Theme.input

                        Text {
                            anchors.centerIn: parent
                            text: "取消"
                            color: Theme.muted
                            font.pixelSize: 11
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: confirmRow.pendingId = ""
                        }
                    }
                }
            }

            // 新增列
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    height: 34
                    radius: 9
                    color: Theme.input
                    border.width: 1
                    border.color: newField.activeFocus ? Theme.borderStrong : Theme.border

                    TextField {
                        id: newField
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        placeholderText: "新列名，例如「本周重点」"
                        placeholderTextColor: Theme.muted
                        color: Theme.text
                        font.pixelSize: 12
                        background: null
                        onAccepted: addButton.activate()
                    }
                }

                // 新列落到哪个状态
                Row {
                    spacing: 6

                    Repeater {
                        model: [
                            { key: "todo", label: "待办" },
                            { key: "doing", label: "进行中" },
                            { key: "done", label: "已完成" }
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            width: 52
                            height: 34
                            radius: 9
                            color: newStatus.key === modelData.key
                                   ? Qt.rgba(manager.statusColor(modelData.key).r,
                                             manager.statusColor(modelData.key).g,
                                             manager.statusColor(modelData.key).b, 0.20)
                                   : Theme.input
                            border.width: 1
                            border.color: newStatus.key === modelData.key
                                          ? Qt.rgba(manager.statusColor(modelData.key).r,
                                                    manager.statusColor(modelData.key).g,
                                                    manager.statusColor(modelData.key).b, 0.45)
                                          : Theme.border

                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                color: newStatus.key === modelData.key
                                       ? manager.statusColor(modelData.key)
                                       : Theme.muted
                                font.pixelSize: 10
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: newStatus.key = modelData.key
                            }
                        }
                    }
                }

                Rectangle {
                    id: addButton
                    width: 56
                    height: 34
                    radius: 9
                    color: addMouse.containsMouse ? Theme.accentSoft : Theme.input
                    border.width: 1
                    border.color: Theme.borderStrong

                    function activate() {
                        var name = newField.text.trim()
                        if (name.length === 0)
                            return
                        if (boardColumns.addColumn(name, newStatus.key).length > 0)
                            newField.text = ""
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "添加"
                        color: Theme.text
                        font.pixelSize: 11
                    }

                    MouseArea {
                        id: addMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: addButton.activate()
                    }
                }
            }

            QtObject {
                id: newStatus
                property string key: "todo"
            }

            // 失败原因（列名空 / 最后一列不许删 / 写库失败）
            Text {
                Layout.fillWidth: true
                visible: boardColumns.lastError.length > 0
                text: boardColumns.lastError
                color: Theme.danger
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }
}
