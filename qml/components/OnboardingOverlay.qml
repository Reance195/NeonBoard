import QtQuick
import QtQuick.Layouts

// 首启动引导：三步说明（录任务 → 拖列 → 装插件）。
// 只在看板为空且未完成过引导时出现；跳过也算完成，避免反复打扰。
Rectangle {
    id: root

    property int step: 0
    property bool canSeed: true

    signal finished()
    signal sampleRequested()

    readonly property var steps: [
        {
            "title": "先记一条任务",
            "body": "按 Ctrl+N 聚焦顶部输入框，直接回车即建卡。\n支持自然语言：「明天 10:00 写周报 #工作 !3」。",
            "keys": "Ctrl+N  ·  Enter"
        },
        {
            "title": "把任务拖动起来",
            "body": "在卡片上按住拖动，从「待办」拖到「进行中」，\n完成后拖到「已办结」——完成时间会自动进入统计热力图。",
            "keys": "拖拽  ·  ←→ 换列"
        },
        {
            "title": "按需装个插件",
            "body": "侧栏「插件」里有插件库；计划钩子能把 Git 提交里的\n#T12=done 自动流转到看板。不需要就跳过。",
            "keys": "Ctrl+K  ·  命令面板"
        }
    ]

    color: Qt.rgba(0, 0, 0, 0.72)
    visible: false

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(480, root.width - 64)
        height: content.implicitHeight + 48
        radius: 16
        color: Theme.panelSolid
        border.width: 1
        border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.42)

        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "NEONBOARD"
                    color: Theme.cyan
                    font.pixelSize: 10
                    font.letterSpacing: 3
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Repeater {
                    model: root.steps.length

                    delegate: Rectangle {
                        width: 22
                        height: 4
                        radius: 2
                        color: index <= root.step ? Theme.cyan : Qt.rgba(1, 1, 1, 0.14)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.steps[root.step].title
                color: Theme.text
                font.pixelSize: 20
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                text: root.steps[root.step].body
                color: Theme.muted
                font.pixelSize: 12
                lineHeight: 1.45
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                radius: 8
                color: Theme.input
                border.width: 1
                border.color: Theme.border

                Text {
                    anchors.centerIn: parent
                    text: root.steps[root.step].keys
                    color: Theme.accent2
                    font.pixelSize: 11
                    font.family: "Consolas"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                radius: 8
                visible: root.step === 0 && root.canSeed
                color: seedMouse.containsMouse ? Theme.cardHover : Theme.card
                border.width: 1
                border.color: Theme.border

                Text {
                    anchors.centerIn: parent
                    text: "载入三张示例卡（可随时删除）"
                    color: Theme.cyan
                    font.pixelSize: 11
                }

                MouseArea {
                    id: seedMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.sampleRequested()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    width: 72
                    height: 30
                    radius: 8
                    color: skipMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "跳过"
                        color: Theme.muted
                        font.pixelSize: 11
                    }

                    MouseArea {
                        id: skipMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.finished()
                    }
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 72
                    height: 30
                    radius: 8
                    visible: root.step > 0
                    color: prevMouse.containsMouse ? Theme.cardHover : Theme.card
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "上一步"
                        color: Theme.muted
                        font.pixelSize: 11
                    }

                    MouseArea {
                        id: prevMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.step -= 1
                    }
                }

                Rectangle {
                    width: 92
                    height: 30
                    radius: 8
                    color: nextMouse.containsMouse ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.26)
                                                   : Theme.accentSoft
                    border.width: 1
                    border.color: Theme.borderStrong

                    Text {
                        anchors.centerIn: parent
                        text: root.step === root.steps.length - 1 ? "开始使用" : "下一步"
                        color: Theme.cyan
                        font.pixelSize: 11
                        font.bold: true
                    }

                    MouseArea {
                        id: nextMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (root.step === root.steps.length - 1)
                                root.finished()
                            else
                                root.step += 1
                        }
                    }
                }
            }
        }
    }

    Keys.onEscapePressed: root.finished()
    focus: visible
}
