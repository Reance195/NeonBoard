import QtQuick
import QtQuick.Layouts

// 项目范围切换器（R10-02）：全局 ⇄ 单项目。
//
// 与阶段条正交 —— 项目回答「这是谁的事」，里程碑回答「做到哪一步」，两者可叠加。
// 范围是**视图状态**：只改各列模型的过滤条件，不写库、不动 task.order_key，
// 退出重开回到全局。切换后列里剩哪些卡、每列多宽，全由数据决定。
Rectangle {
    id: switcher

    // 空串 = 全局
    property string activeProject: ""
    signal projectSelected(string projectId)

    // 依赖 revision：项目增删、改名、改色、归属变化都会让它重算，
    // 不需要界面另外监听一堆信号。
    readonly property var projectList: {
        var rev = projectStore.revision
        return projectStore.projects()
    }
    readonly property int unassignedCards: {
        var rev = projectStore.revision
        return projectStore.unassignedCount()
    }

    // 项目没设颜色时按行序取色（与阶段条同一套 token，视觉上是一家的）
    readonly property var palette: [
        Theme.violet, Theme.magenta, Theme.success, Theme.warning, Theme.cyan
    ]

    function colorFor(project, index) {
        const custom = project.color ? String(project.color) : ""
        return custom.length > 0 ? custom : palette[index % palette.length]
    }

    // 全局 chip 的计数：各项目之和 + 未归属。
    // 直接写「全部卡数」而不是项目数之和，否则未归属的卡在全局视图里可见、
    // 却不在任何计数里，对不上账。
    function totalCount() {
        let n = unassignedCards
        for (let i = 0; i < projectList.length; ++i)
            n += projectList[i].taskCount
        return n
    }

    implicitHeight: 40

    // 宽度跟着内容走（R10-05）。原先 Main.qml 把它写死 230，默认窗口下
    // 「全局 125 / NeonBoard 112」就占满了，第三个项目被裁在可视区外 ——
    // 界面看起来只有一个项目，两级看板的意义被削掉一半。
    // 上限 460：项目再多也不能把阶段条挤没，超出的部分走滚动 + 渐隐提示。
    readonly property real maxWidth: 460
    readonly property real stripWidth: labelText.implicitWidth + 8
                                        + projectRow.childrenRect.width + 18
    implicitWidth: Math.min(stripWidth, maxWidth)

    radius: Theme.radiusPanel
    color: Theme.panel
    border.width: 1
    border.color: Theme.hairline

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 8
        spacing: 8

        Text {
            id: labelText
            text: "项目"
            color: Theme.muted
            font.pixelSize: 10
            font.letterSpacing: 1.2
        }

        // chip 横向滚动：项目数没有上限（归类脚本随时可能多一类），
        // 固定宽度塞不下时会挤成一条线，与阶段条同样的处理（R11-03）。
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            clip: true

            Flickable {
                id: projectFlick
                anchors.fill: parent
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick

                Row {
                    id: projectRow
                    y: 1
                    spacing: 6

                    // 全局：所有项目的卡都显示，未归属的也在这里露面
                    Rectangle {
                        id: allChip
                        readonly property bool active: switcher.activeProject === ""
                        width: allInner.implicitWidth + 18
                        height: 28
                        radius: Theme.radiusControl
                        color: active ? Theme.accentSoft : Theme.input
                        border.width: 1
                        border.color: active ? Theme.accent : Theme.border

                        Behavior on color {
                            ColorAnimation { duration: Motion.fast }
                        }

                        RowLayout {
                            id: allInner
                            anchors.centerIn: parent
                            spacing: 5

                            Text {
                                text: "全局"
                                color: allChip.active ? Theme.accent : Theme.muted
                                font.pixelSize: 11
                                font.bold: true
                            }

                            Text {
                                text: switcher.totalCount()
                                color: allChip.active ? Theme.accent : Theme.muted
                                font.pixelSize: 10
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: switcher.projectSelected("")
                        }
                    }

                    Repeater {
                        model: switcher.projectList

                        delegate: Rectangle {
                            id: projectChip
                            required property var modelData
                            required property int index

                            readonly property bool active: switcher.activeProject === modelData.id
                            readonly property color accent: switcher.colorFor(modelData, index)

                            width: projectInner.implicitWidth + 18
                            height: 28
                            radius: Theme.radiusControl
                            color: active
                                   ? Qt.rgba(accent.r, accent.g, accent.b, 0.16)
                                   : Theme.input
                            border.width: 1
                            border.color: active ? accent : Theme.border

                            Behavior on color {
                                ColorAnimation { duration: Motion.fast }
                            }

                            RowLayout {
                                id: projectInner
                                anchors.centerIn: parent
                                spacing: 5

                                Rectangle {
                                    Layout.preferredWidth: 6
                                    Layout.preferredHeight: 6
                                    radius: 3
                                    color: projectChip.accent
                                }

                                Text {
                                    text: projectChip.modelData.name
                                    color: projectChip.active ? projectChip.accent : Theme.text
                                    font.pixelSize: 11
                                    font.bold: projectChip.active
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 88
                                }

                                Text {
                                    text: projectChip.modelData.taskCount
                                    color: projectChip.active ? projectChip.accent : Theme.muted
                                    font.pixelSize: 10
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: switcher.projectSelected(projectChip.modelData.id)
                            }
                        }
                    }
                }
            }
            // 溢出提示：右缘渐隐。项目多到放不下时，这一条是「还能往右看」的唯一信号 ——
            // 没有它，被裁掉的项目跟「不存在」在视觉上没有区别。
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 20
                visible: projectFlick.contentWidth > projectFlick.width + 1
                         && projectFlick.contentX < projectFlick.contentWidth - projectFlick.width - 1
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop {
                        position: 0.0
                        color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.0)
                    }
                    GradientStop { position: 1.0; color: Theme.panel }
                }
            }

            // 纵向滚轮映射成横向翻页：横向 Flickable 默认不吃滚轮，
            // 用户滚了半天没反应，只会得出「只有这些项目」的结论。
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse
                onWheel: function(event) {
                    const delta = event.angleDelta.y !== 0 ? event.angleDelta.y
                                                           : event.angleDelta.x
                    if (delta === 0)
                        return
                    const maxX = Math.max(0, projectFlick.contentWidth - projectFlick.width)
                    projectFlick.contentX = Math.max(0, Math.min(maxX, projectFlick.contentX - delta))
                }
            }

        }
    }
}
