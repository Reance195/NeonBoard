import QtQuick
import QtQuick.Layouts

// 外观抽屉（R11-11）：主题预设 / 画质档 / 减弱动效。
//
// 形态改造：原来是「锚在标题栏下方的 300 px 浮动面板」（由 ThemePicker 内部的
// Loader 承载），现在统一为「主区右侧 320 px 抽屉」—— 头固定 / 体滚动 / 底固定、
// 右上角统一关闭键，全部由 DrawerShell 提供，本组件只负责体里的内容。
//
// 位置与开合由宿主（Main.qml）承载：宿主给一个覆盖主区的 Loader，并用根的
// visible 控制开合 —— 本组件用 visible 顺带触发入场动画。
DrawerShell {
    id: appearance

    titleText: "外观"
    subtitleText: "主题 · 画质 · 动效"

    // 入场：宿主切 visible 时淡入（DrawerShell 是 Item，不是 Popup，没有内置开合动画）
    opacity: visible ? 1.0 : 0.0
    Behavior on opacity {
        NumberAnimation { duration: Motion.panel; easing.type: Easing.OutCubic }
    }

    // Esc 关闭：DrawerShell 不是 Popup，Esc 不会自动生效，得自己接
    focus: visible
    Keys.onEscapePressed: appearance.closeRequested()
    onVisibleChanged: if (visible) appearance.forceActiveFocus()
    Component.onCompleted: if (visible) appearance.forceActiveFocus()

    // ---------- 分组标题 ----------
    Text {
        Layout.fillWidth: true
        text: "主题预设"
        color: Theme.muted
        font.pixelSize: Theme.fontMicro
        font.letterSpacing: 1.4
    }

    // ---------- 主题预设（3 列色卡网格）----------
    // 原先是「一套预设一行」的文字列表，6 套要滚半屏；改成网格后一眼可比。
    GridLayout {
        Layout.fillWidth: true
        columns: 3
        // GridLayout 没有 spacing，只有行距 / 列距两个
        rowSpacing: Theme.spacingSm
        columnSpacing: Theme.spacingSm

        Repeater {
            model: Theme.presets

            delegate: Rectangle {
                id: presetCell
                required property int index
                required property var modelData

                Layout.fillWidth: true
                Layout.preferredHeight: 62
                radius: Theme.radiusControl
                color: Theme.presetIndex === index ? Theme.accentSoft : Theme.input
                border.width: 1
                border.color: Theme.presetIndex === index ? Theme.cyan : Theme.border

                Behavior on color {
                    ColorAnimation { duration: Motion.fast }
                }
                Behavior on border.color {
                    ColorAnimation { duration: Motion.fast }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 3

                    // 三色预览
                    Row {
                        spacing: 3

                        Repeater {
                            model: [presetCell.modelData.cyan,
                                    presetCell.modelData.magenta,
                                    presetCell.modelData.violet]
                            delegate: Rectangle {
                                width: 12
                                height: 12
                                radius: Theme.radiusControl
                                color: modelData
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: presetCell.modelData.name
                        color: Theme.text
                        font.pixelSize: Theme.fontCaption
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: presetCell.modelData.desc
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideRight
                    }
                }

                // 当前生效的那套打勾
                Text {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 4
                    visible: Theme.presetIndex === index
                    text: "✓"
                    color: Theme.cyan
                    font.pixelSize: Theme.fontMicro
                    font.bold: true
                }

                MouseArea {
                    id: presetMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Theme.applyPreset(presetCell.index)
                        appearance.closeRequested()
                    }
                }

                Accessible.role: Accessible.Button
                Accessible.name: presetCell.modelData.name
                Accessible.description: presetCell.modelData.desc
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.border
    }

    // ---------- 外部主题包（R8-01）----------
    // 装进来的包已经混进 Theme.presets 一起列在上面了；这里只交代
    // 「主题包放在哪」和「哪个没装上」—— 后者不显示的话，用户只会看到
    // 自己丢进去的文件「没反应」。
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        Text {
            text: themePacks.count > 0
                  ? ("外部主题包 " + themePacks.count + " 个")
                  : "支持外部主题包"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            Layout.preferredWidth: 58
            Layout.preferredHeight: 18
            radius: Theme.radiusCard
            color: dirMouse.containsMouse
                   ? Theme.alpha(Theme.cyan, 0.20)
                   : Theme.alpha(Theme.cyan, 0.10)
            border.width: 1
            border.color: Theme.alpha(Theme.cyan, 0.30)

            Text {
                anchors.centerIn: parent
                text: "打开目录"
                color: Theme.cyan
                font.pixelSize: Theme.fontMicro
            }

            MouseArea {
                id: dirMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Qt.openUrlExternally("file:///" + themePacks.ensureDir())
            }

            Accessible.role: Accessible.Button
            Accessible.name: "打开主题包目录"
        }
    }

    Text {
        Layout.fillWidth: true
        visible: themePacks.skippedCount > 0
        text: {
            const list = themePacks.skipped()
            if (list.length === 0)
                return ""
            const first = list[0]
            return "有 " + list.length + " 个包没装上：" + first.file + "（" + first.reason + "）"
        }
        color: Theme.warning
        font.pixelSize: Theme.fontMicro
        wrapMode: Text.WordWrap
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.border
    }

    // ---------- 画质档位 ----------
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        Text {
            Layout.alignment: Qt.AlignVCenter
            text: "画质"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
        }

        Item { Layout.fillWidth: true }

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingXs

            Repeater {
                model: Theme.qualityNames

                delegate: Rectangle {
                    required property int index
                    required property var modelData

                    width: 34
                    height: 22
                    radius: Theme.radiusControl
                    color: Theme.quality === index ? Theme.accentSoft : "transparent"
                    border.width: 1
                    border.color: Theme.quality === index ? Theme.borderStrong : Theme.border

                    Behavior on color {
                        ColorAnimation { duration: Motion.fast }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: Theme.quality === index ? Theme.cyan : Theme.muted
                        font.pixelSize: Theme.fontMicro
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Theme.setQuality(index)
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: "画质 " + modelData
                }
            }
        }
    }

    // ---------- 减弱动效 ----------
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        Text {
            Layout.alignment: Qt.AlignVCenter
            text: "减弱动效"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            id: motionToggle
            Layout.alignment: Qt.AlignVCenter
            width: 38
            height: 20
            radius: Theme.radiusCard
            color: Motion.reduced ? Theme.accentSoft : Theme.input
            border.width: 1
            border.color: Motion.reduced ? Theme.borderStrong : Theme.border

            Behavior on color {
                ColorAnimation { duration: Motion.fast }
            }

            Rectangle {
                id: knob
                width: 14
                height: 14
                radius: Theme.radiusControl
                y: 3
                x: Motion.reduced ? parent.width - width - 3 : 3
                color: Motion.reduced ? Theme.cyan : Theme.muted

                Behavior on x {
                    NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    Motion.persistedReduced = !Motion.persistedReduced
                    Motion.reduced = Motion.persistedReduced
                }
            }

            Accessible.role: Accessible.CheckBox
            Accessible.name: "减弱动效"
            Accessible.checked: Motion.reduced
        }
    }
}
