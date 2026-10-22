import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 400

    property var commandProvider: null
    property string query: ""
    property int currentIndex: 0
    property var results: []
    property string placeholder: "输入命令或任务标题…"

    signal closed()

    function open() {
        visible = true
        query = ""
        input.text = ""
        refresh()
        currentIndex = 0
        input.forceActiveFocus()
        openAnim.restart()
    }

    function close() {
        visible = false
        closed()
    }

    function setQuery(text) {
        query = text ? text : ""
        input.text = query
        refresh()
        currentIndex = 0
    }

    function refresh() {
        var list = commandProvider ? commandProvider(query) : null
        results = list ? list : []
        if (currentIndex >= results.length) currentIndex = Math.max(0, results.length - 1)
    }

    function move(delta) {
        if (results.length === 0) return
        currentIndex = (currentIndex + delta + results.length) % results.length
        cmdList.positionViewAtIndex(currentIndex, ListView.Contain)
    }

    function runCurrent() {
        if (results.length === 0) return
        var cmd = results[currentIndex]
        close()
        if (cmd && cmd.run) cmd.run()
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(Theme.bg.r, Theme.bg.g, Theme.bg.b, 0.66)
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    Rectangle {
        id: panel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 88
        width: Math.min(660, root.width - 64)
        height: Math.min(430, root.height - 140)
        radius: 18
        color: Theme.panelSolid
        border.width: 1
        border.color: Theme.border
        transformOrigin: Item.Top

        Behavior on opacity {
            NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            NumberAnimation { duration: Motion.base; easing.type: Easing.OutCubic }
        }

        SequentialAnimation {
            id: openAnim
            NumberAnimation { target: panel; property: "opacity"; from: 0; to: 1; duration: Motion.fast }
            NumberAnimation { target: panel; property: "scale"; from: 0.97; to: 1; duration: Motion.base; easing.type: Easing.OutCubic }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: "⌘"
                    color: Theme.cyan
                    font.pixelSize: 15
                    font.bold: true
                }

                TextField {
                    id: input
                    Layout.fillWidth: true
                    placeholderText: root.placeholder
                    color: Theme.text
                    placeholderTextColor: Theme.muted
                    font.pixelSize: 14
                    selectByMouse: true
                    background: Rectangle {
                        color: "transparent"
                    }
                    onTextChanged: {
                        root.query = text
                        root.currentIndex = 0
                        root.refresh()
                    }
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Escape) {
                            root.close()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Down) {
                            root.move(1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Up) {
                            root.move(-1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            root.runCurrent()
                            event.accepted = true
                        }
                    }
                    onAccepted: root.runCurrent()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            ListView {
                id: cmdList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: root.results
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: cmdRow
                    width: cmdList.width
                    height: 46
                    radius: 10
                    color: index === root.currentIndex ? Theme.accentSoft : "transparent"
                    border.width: index === root.currentIndex ? 1 : 0
                    border.color: Theme.border

                    Behavior on color {
                        ColorAnimation { duration: Motion.instant; easing.type: Easing.OutCubic }
                    }

                    required property int index
                    required property var modelData

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 4
                            Layout.preferredHeight: 20
                            radius: 2
                            color: cmdRow.index === root.currentIndex ? Theme.cyan : "transparent"
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                Layout.fillWidth: true
                                text: cmdRow.modelData.title
                                color: Theme.text
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: cmdRow.modelData.subtitle ? cmdRow.modelData.subtitle : ""
                                visible: text.length > 0
                                color: Theme.muted
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }

                        Text {
                            text: cmdRow.modelData.category ? cmdRow.modelData.category : ""
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            root.currentIndex = cmdRow.index
                            root.runCurrent()
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                Text {
                    text: "↑ ↓ 选择"
                    color: Theme.muted
                    font.pixelSize: 11
                }
                Text {
                    text: "Enter 执行"
                    color: Theme.muted
                    font.pixelSize: 11
                }
                Text {
                    text: "Esc 关闭"
                    color: Theme.muted
                    font.pixelSize: 11
                }
                Item {
                    Layout.fillWidth: true
                }
                Text {
                    text: root.results.length + " 项"
                    color: Theme.muted
                    font.pixelSize: 11
                }
            }
        }
    }
}
