import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 组件行为：Bound —— 委托（delegate）与 footerExtra 里的 Component 才能正确拿到
// 根对象（palette）的实例属性。根对象是 DialogShell 的实例时，不加这句，
// 委托里的 palette 会解析到「类型」而不是实例，currentIndex / results 全是 undefined。
pragma ComponentBehavior: Bound

// 命令面板（R11-10）：迁到统一弹窗壳 DialogShell —— lg 720 档。
//
// 规格 §3.4 的落位要求「输入框固定不随列表滚动」。壳的体区是整体滚的，
// 所以正文里放一个定高容器：输入框贴在容器顶部，列表在容器内部自己滚 ——
// 滚的是列表而不是壳的体区，输入框因此固定不动。
//
// 键盘行为不变：↑↓ 移动、Enter 执行、Esc 关闭。
DialogShell {
    id: palette

    titleText: "命令面板"
    subtitleText: "搜索命令、任务与主题"
    sizeMode: "lg"

    property var commandProvider: null
    property string query: ""
    property int currentIndex: 0
    property var results: []
    property string placeholder: "输入命令或任务标题…"

    // 右上角关闭键：与遮罩 / Esc 一样只关闭，不表态
    onCloseRequested: palette.close()

    // 打开时复位（原先在自绘的 open() 里做，现在挂在壳的 opened 上）
    onOpened: {
        query = ""
        input.text = ""
        refresh()
        currentIndex = 0
        input.forceActiveFocus()
    }

    onClosed: currentIndex = 0

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

    // ---------- 正文：输入框（固定）+ 结果列表（自滚）----------
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 340
        color: "transparent"

        Text {
            id: glyph
            anchors.left: parent.left
            anchors.verticalCenter: input.verticalCenter
            text: "⌕"
            color: Theme.cyan
            font.pixelSize: Theme.fontTitle
            font.bold: true
        }

        TextField {
            id: input
            anchors.top: parent.top
            anchors.left: glyph.right
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingSm
            height: 40
            placeholderText: palette.placeholder
            color: Theme.text
            placeholderTextColor: Theme.muted
            font.pixelSize: Theme.fontTitle
            selectByMouse: true
            background: Rectangle {
                color: "transparent"
            }
            onTextChanged: {
                palette.query = text
                palette.currentIndex = 0
                palette.refresh()
            }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape) {
                    palette.close()
                    event.accepted = true
                } else if (event.key === Qt.Key_Down) {
                    palette.move(1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Up) {
                    palette.move(-1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    palette.runCurrent()
                    event.accepted = true
                }
            }
            onAccepted: palette.runCurrent()
        }

        Rectangle {
            id: inputLine
            anchors.top: input.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.hairline
        }

        ListView {
            id: cmdList
            anchors.top: inputLine.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            clip: true
            spacing: Theme.spacingXs
            model: palette.results
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: cmdList.contentHeight > cmdList.height
                        ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            delegate: Rectangle {
                id: cmdRow
                width: cmdList.width
                height: 46
                radius: Theme.radiusCard
                color: index === palette.currentIndex ? Theme.accentSoft : "transparent"
                border.width: index === palette.currentIndex ? 1 : 0
                border.color: Theme.border

                Behavior on color {
                    ColorAnimation { duration: Motion.instant; easing.type: Easing.OutCubic }
                }

                required property int index
                required property var modelData

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMd
                    anchors.rightMargin: Theme.spacingMd
                    spacing: Theme.spacingSm

                    Rectangle {
                        Layout.preferredWidth: 4
                        Layout.preferredHeight: 20
                        radius: Theme.radiusControl
                        color: cmdRow.index === palette.currentIndex ? Theme.cyan : "transparent"
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        Text {
                            Layout.fillWidth: true
                            text: cmdRow.modelData.title
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: cmdRow.modelData.subtitle ? cmdRow.modelData.subtitle : ""
                            visible: text.length > 0
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        text: cmdRow.modelData.category ? cmdRow.modelData.category : ""
                        color: Theme.muted
                        font.pixelSize: Theme.fontCaption
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        palette.currentIndex = cmdRow.index
                        palette.runCurrent()
                    }
                }
            }
        }
    }

    // ---------- 底栏左侧：快捷键提示 ----------
    footerNote: "↑ ↓ 选择 · Enter 执行 · Esc 关闭 · " + results.length + " 项"
}
