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

    implicitHeight: 40
    radius: Theme.radiusPanel
    color: Theme.panel
    border.width: 1
    border.color: Theme.hairline

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
            Layout.preferredWidth: 50
            Layout.preferredHeight: 26
            radius: Theme.radiusControl
            color: bar.activeStage === "" ? Theme.accentSoft : Theme.input
            border.width: 1
            border.color: bar.activeStage === "" ? Theme.accent : Theme.border

            Text {
                anchors.centerIn: parent
                text: "全部"
                color: bar.activeStage === "" ? Theme.accent : Theme.muted
                font.pixelSize: 11
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.stageSelected("")
            }
        }

        // R11-03：阶段 chip 改横向滚动。
        // 原先整排 chip 直接塞进 RowLayout，窗口一窄就被压扁变形（chip 挤成一条线，
        // 计数文字被挤掉）。现在放进 Flickable：宽度不够就滚，不再变形。
        Item {
            id: chipsViewport
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            clip: true

            Flickable {
                id: chipsFlick
                anchors.fill: parent
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                contentWidth: chipsRow.implicitWidth
                contentHeight: chipsRow.implicitHeight

                Row {
                    id: chipsRow
                    y: 1
                    spacing: 6

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

                            width: 78
                            height: 28
                            radius: Theme.radiusControl
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
                                anchors.margins: 5
                                spacing: 2

                                RowLayout {
                                    spacing: 3
                                    width: parent.width

                                    Text {
                                        text: chip.modelData.label
                                        color: chip.finished || chip.active ? chip.accent : Theme.text
                                        font.pixelSize: 11
                                        font.bold: true
                                        elide: Text.ElideRight
                                        Layout.maximumWidth: 40
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
                                        Layout.maximumWidth: 34
                                    }
                                }

                                // 阶段完成度细条
                                Rectangle {
                                    width: parent.width
                                    height: 2
                                    radius: 1
                                    color: Theme.border

                                    Rectangle {
                                        width: chip.modelData.total > 0
                                               ? parent.width * Math.min(1, chip.modelData.done / chip.modelData.total)
                                               : 0
                                        height: parent.height
                                        radius: 1
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
                }
            }

            // 右侧渐隐：提示「还有内容可以横向滚动」
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 20
                visible: chipsFlick.contentWidth > chipsFlick.width
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop {
                        position: 0.0
                        color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0)
                    }
                    GradientStop {
                        position: 1.0
                        color: Theme.panel
                    }
                }
            }
        }

        // R11-03：同步状态图标化 —— 原先只是一行 9 px 小字，失败时只靠颜色变化区分，
        // 读不出来「到底哪里出了问题」。现在失败带 ⚠ 图标 + 悬浮说明。
        MouseArea {
            id: syncMouse
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: syncRow.implicitWidth
            Layout.preferredHeight: 20
            hoverEnabled: true

            RowLayout {
                id: syncRow
                anchors.fill: parent
                spacing: 4

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: bar.syncOk ? "●" : "⚠"
                    color: bar.syncOk ? Theme.success : Theme.warning
                    font.pixelSize: 9
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: bar.syncText
                    color: bar.syncOk ? Theme.muted : Theme.warning
                    font.pixelSize: 9
                    elide: Text.ElideRight
                    Layout.maximumWidth: 160
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: 4
                width: tipText.implicitWidth + 16
                height: 32
                visible: syncMouse.containsMouse
                radius: 6
                color: Theme.panelSolid
                border.width: 1
                border.color: bar.syncOk ? Theme.border : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.45)
                z: 10

                Text {
                    id: tipText
                    anchors.centerIn: parent
                    width: parent.width - 12
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.text
                    font.pixelSize: 9
                    text: bar.syncOk
                          ? "路线图快照已与仓库同步"
                          : "路线图快照同步失败：看板状态可能与仓库不一致，稍后会自动重试"
                }
            }
        }
    }
}
