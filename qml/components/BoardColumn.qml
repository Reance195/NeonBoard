import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 看板单列：标题 + 任务列表 + 快速添加。
// 只负责呈现与局部交互，任务变更通过信号抛给上层处理。
Rectangle {
    id: column

    property string title: ""
    property color accent: Theme.cyan
    property var taskModel
    property string status: "todo"
    // 搜索关键字（由上层注入，用于空态文案判断）
    property string searchQuery: ""
    // 拖拽坐标基准（与卡片保持同一坐标系）
    property Item dragSpace: null
    // 是否为当前拖拽悬停的列（边框呼吸）
    property bool dropTarget: false
    property real dropPulse: 0

    signal editRequested(string taskId)
    signal startRequested(string taskId)
    signal completeRequested(string taskId)
    signal restoreRequested(string taskId)
    signal deleteRequested(string taskId)
    signal dragStarted(string taskId, string title, string status, real x, real y)
    signal dragMoved(real x, real y)
    signal dragEnded(real x, real y)

    function focusQuickAdd() {
        quickAdd.forceActiveFocus()
    }

    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.minimumWidth: 150
    Layout.preferredWidth: 340
    radius: 16
    color: Theme.panel
    border.width: 1
    border.color: column.dropTarget
               ? Qt.rgba(accent.r, accent.g, accent.b, 0.45 + 0.35 * column.dropPulse)
               : Qt.rgba(accent.r, accent.g, accent.b, 0.20)

    Behavior on border.color {
        ColorAnimation { duration: Motion.base }
    }

    // 拖入呼吸：仅在被拖拽悬停时循环
    NumberAnimation on dropPulse {
        running: column.dropTarget
        loops: Animation.Infinite
        from: 0
        to: 1
        duration: 900
        easing.type: Easing.InOutSine
    }

    // 落位涟漪
    function ripple() {
        rippleAnimation.restart()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: column.accent
            }

            Text {
                text: column.title
                color: Theme.text
                font.pixelSize: 14
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 28
                height: 20
                radius: 10
                color: Qt.rgba(column.accent.r, column.accent.g, column.accent.b, 0.12)
                border.width: 1
                border.color: Qt.rgba(column.accent.r, column.accent.g, column.accent.b, 0.24)

                Text {
                    anchors.centerIn: parent
                    text: column.taskModel ? column.taskModel.count : 0
                    color: column.accent
                    font.pixelSize: 10
                    font.bold: true
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                spacing: 8
                boundsBehavior: Flickable.StopAtBounds
                model: column.taskModel

                delegate: TaskCard {
                    width: list.width
                    title: model.title
                    tag: model.tag
                    priority: model.priority
                    due: model.due
                    taskId: model.id
                    taskStatus: model.status
                    dragSpace: column.dragSpace
                    onEditRequested: column.editRequested(model.id)
                    onStartRequested: column.startRequested(model.id)
                    onCompleteRequested: column.completeRequested(model.id)
                    onRestoreRequested: column.restoreRequested(model.id)
                    onDeleteRequested: column.deleteRequested(model.id)
                    onDragStarted: function(taskId, title, status, x, y) {
                        column.dragStarted(taskId, title, status, x, y)
                    }
                    onDragMoved: function(x, y) { column.dragMoved(x, y) }
                    onDragEnded: function(x, y) { column.dragEnded(x, y) }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: column.taskModel && column.taskModel.count === 0
                text: column.searchQuery.length > 0
                      ? "没有匹配的任务"
                      : (column.status === "done"
                         ? "完成的任务会出现在这里"
                         : "暂无任务\n在下方输入，回车添加")
                color: Theme.muted
                opacity: 0.75
                font.pixelSize: 12
                lineHeight: 1.5
                horizontalAlignment: Text.AlignHCenter
            }

            // 落位涟漪
            Rectangle {
                id: rippleRing
                width: 24
                height: 24
                radius: 12
                x: (list.width - width) / 2
                y: list.height - height
                color: "transparent"
                border.width: 2
                border.color: column.accent
                opacity: 0
                visible: opacity > 0.01
                z: 5

                SequentialAnimation {
                    id: rippleAnimation
                    NumberAnimation { target: rippleRing; property: "width"; from: 24; to: 240; duration: Motion.slow; easing.type: Easing.OutCubic }
                    NumberAnimation { target: rippleRing; property: "height"; from: 24; to: 240; duration: Motion.slow; easing.type: Easing.OutCubic }
                    NumberAnimation { target: rippleRing; property: "radius"; from: 12; to: 120; duration: Motion.slow; easing.type: Easing.OutCubic }
                    NumberAnimation { target: rippleRing; property: "opacity"; from: 0.85; to: 0; duration: Motion.slow }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 34
            radius: 8
            color: Theme.input
            border.width: 1
            border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.14)

            Behavior on border.color {
                ColorAnimation { duration: Motion.fast }
            }

            TextField {
                id: quickAdd
                anchors.fill: parent
                leftPadding: 10
                rightPadding: 10
                placeholderText: "＋ 添加任务，回车"
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: 12
                background: null
                onAccepted: {
                    if (column.taskModel && text.trim().length > 0) {
                        column.taskModel.addTask(text.trim(), column.status, 1, "", 0)
                        text = ""
                    }
                }
            }
        }
    }
}
