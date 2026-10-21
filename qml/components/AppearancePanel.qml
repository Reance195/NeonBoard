import QtQuick
import QtQuick.Effects
import QtQuick.Layouts

// 外观面板内容：按需加载。
// 由 ThemePicker 在用户第一次点开时构造，6 套预设 / 画质三档 / 减弱动效的重对象不再占用首帧。
Item {
    id: appearance

    // 由 ThemePicker 注入自身引用，用于跟随开合状态播放逐行渐入
    property var pickerRef: null
    readonly property bool panelOpen: pickerRef !== null && pickerRef.open === true
    signal closeRequested()

    implicitWidth: 300
    implicitHeight: body.implicitHeight + 24

    // 真·投影，让面板从霓虹背景里浮出来
    MultiEffect {
        anchors.fill: parent
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

        ColumnLayout {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: "外观  APPEARANCE"
                color: Theme.muted
                font.pixelSize: 9
                font.letterSpacing: 1.4
            }

            // ---------- 主题预设 ----------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Repeater {
                    model: Theme.presets

                    delegate: Rectangle {
                        id: presetRow
                        required property int index
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
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

                        // 逐行渐入（错峰 40ms）
                        SequentialAnimation {
                            running: appearance.panelOpen
                            PauseAnimation { duration: index * Motion.stagger }
                            NumberAnimation {
                                target: presetRow
                                property: "opacity"
                                from: 0
                                to: 1
                                duration: Motion.base
                                easing.type: Easing.OutCubic
                            }
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
                                appearance.closeRequested()
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            // ---------- 画质档位 ----------
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: "画质"
                    color: Theme.muted
                    font.pixelSize: 10
                }

                Item { Layout.fillWidth: true }

                Row {
                    Layout.alignment: Qt.AlignVCenter
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
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: "减弱动效"
                    color: Theme.muted
                    font.pixelSize: 10
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    id: motionToggle
                    Layout.alignment: Qt.AlignVCenter
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
