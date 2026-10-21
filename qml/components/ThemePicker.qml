import QtQuick
import QtQuick.Layouts
import QtQuick.Effects

// 外观面板：一键切换主题预设 / 画质档 / 减弱动效。
// 切换主题时全应用（含霓虹背景）颜色渐变，切换过程本身就是一段动效。
Item {
    id: picker

    property bool open: false

    implicitWidth: 34
    implicitHeight: 30

    function toggle() {
        open = !open
    }

    function close() {
        open = false
    }

    // ---------- 触发按钮 ----------
    Rectangle {
        id: trigger
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 34
        height: 30
        radius: 8
        color: picker.open
               ? Theme.accentSoft
               : (triggerMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
        border.width: 1
        border.color: picker.open ? Theme.borderStrong : "transparent"

        Behavior on color {
            ColorAnimation { duration: Motion.fast }
        }
        Behavior on border.color {
            ColorAnimation { duration: Motion.fast }
        }

        // 当前主题三色预览
        Row {
            anchors.centerIn: parent
            spacing: 3

            Repeater {
                model: [Theme.cyan, Theme.magenta, Theme.violet]
                delegate: Rectangle {
                    width: 5
                    height: 5
                    radius: 2.5
                    color: modelData
                }
            }
        }

        MouseArea {
            id: triggerMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: picker.toggle()
        }
    }

    // ---------- 弹出面板 ----------
    Item {
        id: panel
        visible: picker.open
        anchors.top: trigger.bottom
        anchors.topMargin: 8
        anchors.right: parent.right
        width: 300
        height: content.implicitHeight + 24
        opacity: picker.open ? 1.0 : 0.0
        scale: picker.open ? 1.0 : 0.96
        transformOrigin: Item.TopRight
        z: 50

        Behavior on opacity {
            NumberAnimation { duration: Motion.panel; easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            NumberAnimation { duration: Motion.panel; easing.type: Easing.OutCubic }
        }

        // 真·投影，让面板从霓虹背景里浮出来
        MultiEffect {
            anchors.fill: panel
            source: panelContent
            shadowEnabled: true
            shadowBlur: 1.0
            shadowOpacity: 0.55
            shadowScale: 1.02
        }

        Rectangle {
            id: panelContent
            anchors.fill: parent
            radius: 14
            color: Theme.panelSolid
            border.width: 1
            border.color: Theme.border
            clip: true

            Column {
                id: content
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Text {
                    text: "外观  APPEARANCE"
                    color: Theme.muted
                    font.pixelSize: 9
                    font.letterSpacing: 1.4
                }

                // ---------- 主题预设 ----------
                Column {
                    width: parent.width
                    spacing: 4

                    Repeater {
                        id: presetRepeater
                        model: Theme.presets

                        delegate: Rectangle {
                            id: presetRow
                            required property int index
                            required property var modelData

                            width: parent.width
                            height: 40
                            radius: 9
                            color: rowMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                            border.width: 1
                            border.color: Theme.presetIndex === index ? Theme.borderStrong : "transparent"

                            Behavior on color {
                                ColorAnimation { duration: Motion.fast }
                            }
                            Behavior on border.color {
                                ColorAnimation { duration: Motion.fast }
                            }

                            // 逐行渐入
                            NumberAnimation {
                                target: presetRow
                                property: "opacity"
                                running: picker.open
                                from: 0
                                to: 1
                                duration: Motion.base
                                delay: index * Motion.stagger
                                easing.type: Easing.OutCubic
                            }

                            // 三色预览
                            Row {
                                id: preview
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Repeater {
                                    model: [presetRow.modelData.cyan,
                                            presetRow.modelData.magenta,
                                            presetRow.modelData.violet]
                                    delegate: Rectangle {
                                        width: 9
                                        height: 9
                                        radius: 4.5
                                        color: modelData
                                        border.width: 1
                                        border.color: Qt.rgba(1, 1, 1, 0.15)
                                    }
                                }
                            }

                            Column {
                                anchors.left: preview.right
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 1

                                Text {
                                    text: presetRow.modelData.name
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.bold: true
                                }

                                Text {
                                    text: presetRow.modelData.desc
                                    color: Theme.muted
                                    font.pixelSize: 9
                                }
                            }

                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "✓"
                                visible: Theme.presetIndex === index
                                color: Theme.cyan
                                font.pixelSize: 13
                                font.bold: true
                            }

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Theme.applyPreset(presetRow.index)
                                    picker.close()
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.border
                }

                // ---------- 画质档位 ----------
                Row {
                    width: parent.width
                    spacing: 8

                    Text {
                        text: "画质"
                        color: Theme.muted
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Item { Layout.fillWidth: true }

                    Row {
                        spacing: 4

                        Repeater {
                            model: Theme.qualityNames

                            delegate: Rectangle {
                                required property int index
                                required property var modelData

                                width: 34
                                height: 22
                                radius: 6
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
                                    font.pixelSize: 10
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Theme.setQuality(index)
                                }
                            }
                        }
                    }
                }

                // ---------- 减弱动效 ----------
                Row {
                    width: parent.width
                    spacing: 8

                    Text {
                        text: "减弱动效"
                        color: Theme.muted
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        id: motionToggle
                        width: 38
                        height: 20
                        radius: 10
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
                            radius: 7
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
                    }
                }
            }
        }
    }
}
