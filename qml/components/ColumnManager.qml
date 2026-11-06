import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 列管理抽屉（R11-11）：增列 / 改名 / 调序 / 删列。
//
// 形态改造：原来是「460 px 居中浮层」，现在统一为「主区右侧 320 px 抽屉」——
// 头固定 / 体滚动 / 底固定、右上角统一关闭键，全部由 DrawerShell 提供，
// 本组件只负责体里的内容。
//
// 列条目统一为「拖把手 + 名称 + 计数 + 操作」（规格 §3.4）：
// 拖把手竖向拖动改顺序，‹ › 按钮保留为等价的键盘/点击路径。
//
// 列结构的唯一事实源是库里的 board_column，所有写操作都走 boardColumns 的
// Q_INVOKABLE（内部写库后 reload），面板只负责展示与输入校验 ——
// 面板自己再存一份列数组，就会出现「面板里删了、看板上还在」的两套状态。
DrawerShell {
    id: manager

    // 主机用它开合面板（Main.qml：columnManager.open = true）
    property bool open: false

    // 待确认删除的列 id（空串 = 没有待确认的删除）
    property string pendingId: ""

    // 新列落到哪个状态
    property string key: "todo"

    signal closed()

    // 行高与行距：拖把手的落点换算成行号要用这两个数
    readonly property int rowHeight: 40
    readonly property int rowSpacing: 6

    titleText: "列管理"
    subtitleText: "增删 · 改名 · 调序"

    // 开合由 open 驱动（DrawerShell 是 Item，visible 会连带隐藏遮罩与面板）
    visible: open
    opacity: open ? 1.0 : 0.0
    Behavior on opacity {
        NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
    }

    // Esc 关闭：DrawerShell 不是 Popup，Esc 不会自动生效，得自己接
    focus: visible
    Keys.onEscapePressed: manager.close()
    onVisibleChanged: if (visible) manager.forceActiveFocus()
    Component.onCompleted: if (visible) manager.forceActiveFocus()

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

    // 主操作：新增列（名字取输入框，状态取 key）
    function activate() {
        var name = newField.text.trim()
        if (name.length === 0)
            return
        if (boardColumns.addColumn(name, manager.key).length > 0)
            newField.text = ""
    }

    // 拖把手落点：把列表内坐标换算成行号，再按 ±1 逐步挪。
    // 列顺序的唯一写入口仍是 boardColumns.moveColumn（内部写库后 reload）。
    function reorderTo(columnId, yInList) {
        var current = boardColumns.indexOfColumn(columnId)
        if (current < 0)
            return
        var step = manager.rowHeight + manager.rowSpacing
        var target = Math.floor((yInList + manager.rowHeight / 2) / step)
        target = Math.max(0, Math.min(boardColumns.count - 1, target))
        var delta = target - current
        if (delta === 0)
            return
        var moveStep = delta > 0 ? 1 : -1
        var times = Math.abs(delta)
        var id = columnId
        // moveColumn 会 reload 模型（委托重建），所以把写库挪到下一轮事件循环：
        // 在委托自己的回调里把自己销毁掉会出事
        Qt.callLater(function() {
            for (var i = 0; i < times; ++i)
                boardColumns.moveColumn(id, moveStep)
        })
    }

    // ---------- 分组标题 ----------
    Text {
        Layout.fillWidth: true
        text: "看板列"
        color: Theme.muted
        font.pixelSize: Theme.fontMicro
        font.letterSpacing: 1.4
    }

    // ---------- 现有列：改名 / 调序 / 删除 ----------
    ColumnLayout {
        id: columnList
        Layout.fillWidth: true
        spacing: manager.rowSpacing

        Repeater {
            model: boardColumns

            delegate: Rectangle {
                id: colRow
                required property string boardColumnId
                required property string boardColumnTitle
                required property string boardColumnStatus
                required property var boardColumnModel
                required property int boardColumnIndex

                Layout.fillWidth: true
                height: manager.rowHeight
                radius: Theme.radiusCard
                color: Theme.card
                border.width: 1
                border.color: Theme.alpha(manager.statusColor(boardColumnStatus), 0.22)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 6

                    // 拖把手
                    Rectangle {
                        Layout.preferredWidth: 14
                        Layout.fillHeight: true
                        color: "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "⠿"
                            color: gripMouse.containsMouse ? Theme.text : Theme.muted
                            font.pixelSize: Theme.fontBody
                        }

                        MouseArea {
                            id: gripMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.SizeVerCursor
                            onReleased: {
                                var p = gripMouse.mapToItem(columnList, gripMouse.mouseX, gripMouse.mouseY)
                                manager.reorderTo(colRow.boardColumnId, p.y)
                            }
                        }

                        Accessible.name: "拖动排序"
                    }

                    // 状态点（颜色即状态，文字给读屏）
                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: Theme.radiusControl
                        color: manager.statusColor(boardColumnStatus)

                        Accessible.name: manager.statusLabel(boardColumnStatus)
                    }

                    // 名称：就地改名
                    TextField {
                        id: nameField
                        Layout.fillWidth: true
                        text: boardColumnTitle
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        selectByMouse: true
                        background: null
                        placeholderText: "列名"
                        placeholderTextColor: Theme.muted
                        verticalAlignment: TextInput.AlignVCenter
                        // 失焦或回车时提交：边打边写会让「改名」变成逐字符事务
                        onEditingFinished: {
                            if (text.trim().length > 0 && text.trim() !== boardColumnTitle)
                                boardColumns.renameColumn(boardColumnId, text)
                        }
                    }

                    // 计数
                    Text {
                        text: boardColumnModel ? boardColumnModel.count : 0
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                    }

                    // ← →
                    Repeater {
                        model: [
                            { glyph: "‹", delta: -1 },
                            { glyph: "›", delta: 1 }
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            width: 20
                            height: 20
                            radius: Theme.radiusControl
                            color: arrowMouse.containsMouse ? Theme.ghost : Theme.input

                            Text {
                                anchors.centerIn: parent
                                text: modelData.glyph
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                            }

                            MouseArea {
                                id: arrowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: boardColumns.moveColumn(boardColumnId,
                                                                       modelData.delta)
                            }

                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.delta < 0 ? "前移" : "后移"
                        }
                    }

                    // 删除：某状态只剩一列时禁用（否则该状态的卡无处可去）
                    Rectangle {
                        width: 20
                        height: 20
                        radius: Theme.radiusControl
                        readonly property bool allowed:
                            boardColumns.columnCountForStatus(boardColumnStatus) > 1
                        color: allowed
                               ? (deleteMouse.containsMouse
                                  ? Theme.alpha(Theme.danger, 0.20)
                                  : Theme.input)
                               : Theme.alpha(Theme.text, 0.03)
                        opacity: allowed ? 1 : 0.4

                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            color: parent.allowed ? Theme.danger : Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }

                        MouseArea {
                            id: deleteMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: parent.allowed
                            // 卡还在列里时先确认：删除会把它们挪到同状态的另一列
                            onClicked: {
                                if (boardColumnModel && boardColumnModel.count > 0)
                                    manager.pendingId = boardColumnId
                                else
                                    boardColumns.removeColumn(boardColumnId)
                            }
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: "删除列"
                    }
                }
            }
        }
    }

    // 删除确认条（列里有卡时才出现）
    Rectangle {
        id: confirmRow
        Layout.fillWidth: true
        visible: manager.pendingId.length > 0
        height: visible ? 38 : 0
        radius: Theme.radiusCard
        color: Theme.alpha(Theme.danger, 0.12)
        border.width: 1
        border.color: Theme.alpha(Theme.danger, 0.35)

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: "该列里的卡片会挪到同状态的其它列，确定删除？"
                color: Theme.text
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }

            Rectangle {
                width: 52
                height: 24
                radius: Theme.radiusControl
                color: Theme.alpha(Theme.danger, 0.22)

                Text {
                    anchors.centerIn: parent
                    text: "删除"
                    color: Theme.danger
                    font.pixelSize: Theme.fontCaption
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (manager.pendingId.length > 0)
                            boardColumns.removeColumn(manager.pendingId)
                        manager.pendingId = ""
                    }
                }

                Accessible.role: Accessible.Button
                Accessible.name: "确认删除"
            }

            Rectangle {
                width: 44
                height: 24
                radius: Theme.radiusControl
                color: Theme.input

                Text {
                    anchors.centerIn: parent
                    text: "取消"
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: manager.pendingId = ""
                }

                Accessible.role: Accessible.Button
                Accessible.name: "取消删除"
            }
        }
    }

    // ---------- 新增列的列名 ----------
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        Rectangle {
            Layout.fillWidth: true
            height: 30
            radius: Theme.radiusCard
            color: Theme.input
            border.width: 1
            border.color: newField.activeFocus ? Theme.borderStrong : Theme.border

            TextField {
                id: newField
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                placeholderText: "新列名，例如「本周重点」"
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: Theme.fontBody
                background: null
                verticalAlignment: TextInput.AlignVCenter
                onAccepted: manager.activate()
            }
        }
    }

    // 失败原因（列名空 / 最后一列不许删 / 写库失败）
    Text {
        Layout.fillWidth: true
        visible: boardColumns.lastError.length > 0
        text: boardColumns.lastError
        color: Theme.danger
        font.pixelSize: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    // ---------- 底（固定）：左侧选状态，右侧主操作「添加」 ----------
    footerExtra: Component {
        Row {
            spacing: Theme.spacingXs

            Repeater {
                model: [
                    { key: "todo", label: "待办" },
                    { key: "doing", label: "进行中" },
                    { key: "done", label: "已完成" }
                ]

                delegate: Rectangle {
                    required property var modelData
                    width: 46
                    height: 26
                    radius: Theme.radiusControl
                    color: manager.key === modelData.key
                           ? Theme.alpha(manager.statusColor(modelData.key), 0.20)
                           : Theme.input
                    border.width: 1
                    border.color: manager.key === modelData.key
                                  ? Theme.alpha(manager.statusColor(modelData.key), 0.45)
                                  : Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: manager.key === modelData.key
                               ? manager.statusColor(modelData.key)
                               : Theme.muted
                        font.pixelSize: Theme.fontMicro
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: manager.key = modelData.key
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.label
                }
            }
        }
    }

    // 主操作「添加列」：底栏右侧（与 DialogShell 底栏同一纪律）
    footerAction: Component {
        Rectangle {
            id: addButton
            width: 56
            height: 26
            radius: Theme.radiusControl
            color: addMouse.containsMouse ? Theme.accentSoft : Theme.input
            border.width: 1
            border.color: Theme.borderStrong

            Text {
                anchors.centerIn: parent
                text: "添加"
                color: Theme.text
                font.pixelSize: Theme.fontCaption
            }

            MouseArea {
                id: addMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: manager.activate()
            }

            Accessible.role: Accessible.Button
            Accessible.name: "添加列"
        }
    }
}
