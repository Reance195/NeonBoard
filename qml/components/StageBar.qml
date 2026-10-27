import QtQuick
import QtQuick.Layouts

// 阶段进度条：按里程碑聚合看板完成度。
// 一眼看出「哪个阶段收口了、哪个阶段还欠着」，点击某个阶段即按阶段过滤看板。
// 阶段集合与顺序由 C++ 侧 TaskModel::stageList() 给出（取自路线图 §9 行序），
// 本文件不写死有哪些阶段 —— §9 加一行，这里自动多一个 chip（DX-04）。
Rectangle {
    id: bar

    // [{ key, label, total, done, doing, todo }]
    property var stages: []
    property string activeStage: ""
    // 路线图同步状态文案
    property string syncText: ""
    property bool syncOk: false

    signal stageSelected(string stage)

    implicitHeight: 46
    radius: 12
    color: Theme.panel
    border.width: 1
    border.color: Theme.border

    // 阶段色板：阶段集合现在取自路线图 §9（R0–R7 + DX，以后还会加行），
    // 再按 key 写死颜色的话，第 4 个之后会全撞成同一个色。改成按行序取色、循环复用。
    // 「其他」是兜底桶而非真实阶段，单独用 muted 与它们区分开。
    readonly property var palette: [
        Theme.cyan, Theme.violet, Theme.magenta, Theme.success, Theme.warning, Theme.danger
    ]

    function accentFor(key, index) {
        if (key === "OTHER")
            return Theme.muted
        const i = index < 0 ? 0 : index
        return palette[i % palette.length]
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 8

        Text {
            text: "里程碑"
            color: Theme.muted
            font.pixelSize: 10
            font.letterSpacing: 1.2
        }

        // 全部
        Rectangle {
            Layout.preferredWidth: 54
            Layout.preferredHeight: 30
            radius: 8
            color: bar.activeStage === "" ? Theme.accentSoft : Theme.input
            border.width: 1
            border.color: bar.activeStage === "" ? Theme.cyan : Theme.border

            Text {
                anchors.centerIn: parent
                text: "全部"
                color: bar.activeStage === "" ? Theme.cyan : Theme.muted
                font.pixelSize: 11
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.stageSelected("")
            }
        }

        Repeater {
            model: bar.stages

            delegate: Rectangle {
                id: chip
                required property var modelData
                // 一旦用了 required，未声明的上下文属性就不注入了 —— index 也得显式声明，
                // 否则取色那行会 ReferenceError（Qt 6 的 required-property 语义）。
                required property int index

                readonly property bool finished: modelData.total > 0 && modelData.done >= modelData.total
                readonly property color accent: bar.accentFor(modelData.key, index)
                readonly property bool active: bar.activeStage === modelData.key

                Layout.preferredWidth: 86
                Layout.preferredHeight: 34
                radius: 9
                color: chip.active ? Qt.rgba(chip.accent.r, chip.accent.g, chip.accent.b, 0.16) : Theme.input
                border.width: 1
                border.color: chip.active
                              ? chip.accent
                              : (chip.finished
                                 ? Qt.rgba(chip.accent.r, chip.accent.g, chip.accent.b, 0.45)
                                 : Theme.border)

                Behavior on color {
                    ColorAnimation { duration: Motion.fast }
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 3

                    RowLayout {
                        spacing: 4
                        width: parent.width

                        Text {
                            text: chip.modelData.label
                            color: chip.finished || chip.active ? chip.accent : Theme.text
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Text {
                            visible: chip.finished
                            text: "✓"
                            color: chip.accent
                            font.pixelSize: 10
                            font.bold: true
                        }

                        // 弹性占位：把计数推到芯片右缘，位数变化也不会顶出边框
                        Item {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                        }

                        Text {
                            text: chip.modelData.done + "/" + chip.modelData.total
                            color: chip.finished ? chip.accent : Theme.muted
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            Layout.minimumWidth: 0
                            Layout.maximumWidth: 44
                        }
                    }

                    // 阶段完成度细条
                    Rectangle {
                        width: parent.width
                        height: 3
                        radius: 1.5
                        color: Theme.border

                        Rectangle {
                            width: chip.modelData.total > 0
                                   ? parent.width * Math.min(1, chip.modelData.done / chip.modelData.total)
                                   : 0
                            height: parent.height
                            radius: 1.5
                            color: chip.accent

                            Behavior on width {
                                NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic }
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.stageSelected(chip.modelData.key)
                }
            }
        }

        Item { Layout.fillWidth: true }

        Text {
            text: bar.syncText
            color: bar.syncOk ? Theme.muted : Theme.warning
            font.pixelSize: 9
            elide: Text.ElideRight
        }
    }
}
