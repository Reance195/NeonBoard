import QtQuick
import QtQuick.Controls

// 多板切换器（R21-1）：标题栏「板名 ▾」按钮 + 弹出面板。
//
// 板清单 / 当前板 / CRUD 全部走 boardColumns 门面（board 表是唯一事实源，
// 本组件不缓存任何板状态）；切板 = 改 boardColumns.boardId，列集合与统计由
// BoardColumns::reload 统一刷新。删板 fail-closed（默认板与板内有卡的板拒删），
// 失败原因读 boardColumns.lastError 显示在面板底部。

TopToolButton {
    id: root

    readonly property string currentBoardId: boardColumns.boardId
    property string currentBoardName: {
        const list = boardColumns.boards
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === currentBoardId)
                return list[i].name
        }
        return qsTr("看板")
    }

    text: currentBoardName
    description: qsTr("切换看板")
    onClicked: {
        confirmDeleteId = ""
        editingId = ""
        boardMenu.open()
    }

    // 两步删除确认：第一次点变成「确认删」，再点才真删 —— 不引入新弹窗壳
    property string confirmDeleteId: ""
    // 行内改名态：正在改名的板 id（空 = 无）
    property string editingId: ""

    Popup {
        id: boardMenu
        y: root.height + 6
        width: 260
        padding: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.input
            border.color: Theme.borderStrong
            radius: Theme.radiusControl
        }

        contentItem: Column {
            spacing: 4

            Repeater {
                model: boardColumns.boards

                delegate: Rectangle {
                    required property var modelData
                    readonly property bool isCurrent: modelData.id === root.currentBoardId
                    readonly property bool isEditing: root.editingId === modelData.id

                    width: boardMenu.availableWidth
                    height: 30
                    radius: Theme.radiusControl
                    color: isCurrent ? Theme.accentSoft
                                     : (rowMouse.containsMouse ? Theme.alpha(Theme.accent, 0.12)
                                                               : "transparent")

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 6
                        spacing: 6

                        Text {
                            width: parent.width - renameHint.width - deleteHint.width - parent.spacing * 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            color: isCurrent ? Theme.accent : Theme.text
                            font.pixelSize: Theme.fontCaption
                            font.bold: isCurrent
                            elide: Text.ElideRight
                        }

                        Text {
                            id: renameHint
                            anchors.verticalCenter: parent.verticalCenter
                            text: "✎"
                            color: Theme.muted
                            font.pixelSize: 11
                            opacity: renameMouse.containsMouse ? 1 : 0.45
                        }

                        Text {
                            id: deleteHint
                            anchors.verticalCenter: parent.verticalCenter
                            visible: modelData.id !== "board-default"
                            text: root.confirmDeleteId === modelData.id ? qsTr("确认删？") : "✕"
                            color: root.confirmDeleteId === modelData.id ? Theme.magenta : Theme.muted
                            font.pixelSize: 11
                            opacity: deleteMouse.containsMouse || root.confirmDeleteId === modelData.id ? 1 : 0.45
                        }
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (isEditing)
                                return
                            boardColumns.boardId = modelData.id
                            boardMenu.close()
                        }
                    }

                    MouseArea {
                        id: renameMouse
                        width: 24
                        height: parent.height
                        anchors.right: parent.right
                        anchors.rightMargin: deleteHint.visible ? deleteHint.width + 10 : 6
                        anchors.verticalCenter: parent.verticalCenter
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.confirmDeleteId = ""
                            root.editingId = modelData.id
                        }
                    }

                    MouseArea {
                        id: deleteMouse
                        width: deleteHint.width + 12
                        height: parent.height
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        visible: deleteHint.visible
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.confirmDeleteId === modelData.id) {
                                if (boardColumns.deleteBoard(modelData.id))
                                    root.confirmDeleteId = ""
                                else
                                    root.confirmDeleteId = ""
                            } else {
                                root.confirmDeleteId = modelData.id
                            }
                        }
                    }

                    // 行内改名：占位 Text 换成输入框
                    TextField {
                        anchors.fill: parent
                        anchors.margins: 4
                        visible: isEditing
                        text: modelData.name
                        font.pixelSize: Theme.fontCaption
                        selectByMouse: true
                        onAccepted: {
                            if (boardColumns.renameBoard(modelData.id, text))
                                root.editingId = ""
                        }
                        Component.onCompleted: if (isEditing) forceActiveFocus()
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }

            Row {
                width: boardMenu.availableWidth
                spacing: 6

                NeonField {
                    id: newBoardField
                    width: parent.width - createButton.width - parent.spacing
                    placeholderText: qsTr("新建看板…")
                    onAccepted: createButton.create()
                }

                TopToolButton {
                    id: createButton
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("新建")
                    onClicked: create()

                    function create() {
                        if (boardColumns.createBoard(newBoardField.text).length > 0)
                            newBoardField.text = ""
                    }
                }
            }

            Text {
                width: parent.width
                visible: boardColumns.lastError !== ""
                text: boardColumns.lastError
                color: Theme.magenta
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.WrapAnywhere
            }
        }
    }
}
