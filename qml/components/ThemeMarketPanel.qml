import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 主题包市场（R8-02）：浏览 / 安装 / 卸载。
//
// 只做三件事：把目录列出来、装一个、卸一个。没有评分、没有截图、没有「推荐」——
// 一个主题包就是九个颜色，多出来的信息只会让人以为还有别的门道。
//
// R11-09：改用统一弹窗壳 DialogShell —— 头固定（标题 + 离线提示提为副标题 +
// 关闭键）、体滚动、底固定（打开主题目录 / 关闭），尺寸档 md 560。
DialogShell {
    id: root

    titleText: "主题市场"
    // 离线提示提到副标题位（规格 §3.4）：原来它在标题行里跟标题抢位置
    subtitleText: themeMarket.offline ? "离线 · 只显示已内置的目录" : ""
    sizeMode: "md"
    secondaryText: "关闭"

    onSecondaryTriggered: root.close()
    onCloseRequested: root.close()

    property var hostWindow: null
    property string notice: ""

    onOpened: notice = ""
    function open() {
        themeMarket.refresh()
        visible = true
    }

    // 目录变了（装 / 卸 / 刷新完）就重算一次；QML 这边只做展示，不自己猜状态
    readonly property var entries: {
        const rev = themeMarket.revision
        return themeMarket.catalog()
    }

    function entryCount() {
        return entries.length
    }

    // 底栏左侧的额外动作（打开主题目录）
    footerExtra: Component {
        Button {
            text: "打开主题目录"
            Layout.preferredHeight: 26
            Layout.preferredWidth: 96
            padding: 0
            onClicked: Qt.openUrlExternally("file:///" + themePacks.ensureDir())
            background: Rectangle {
                radius: Theme.radiusControl
                color: Theme.input
                border.width: 1
                border.color: Theme.border
            }
            contentItem: Text {
                text: "打开主题目录"
                color: Theme.muted
                font.pixelSize: Theme.fontMicro
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Text {
            text: "已安装 " + themeMarket.installedCount() + " 个"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
        }

        Item { Layout.fillWidth: true }
    }

    // 提示条：装成功 / 装失败都从这里出。失败一定要说清楚原因 ——
    // 「装了没反应」是这类功能最让人挠头的地方。
    Text {
        Layout.fillWidth: true
        visible: root.notice.length > 0
        text: root.notice
        color: root.notice.indexOf("失败") >= 0 || root.notice.indexOf("不是") >= 0
               ? Theme.warning : Theme.success
        font.pixelSize: Theme.fontMicro
        wrapMode: Text.WordWrap
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.border
    }

    ColumnLayout {
        id: listBody
        Layout.fillWidth: true
        spacing: 6

        Repeater {
            model: root.entries

            delegate: Rectangle {
                id: row
                required property int index
                required property var modelData

                Layout.fillWidth: true
                Layout.preferredHeight: 52
                radius: Theme.radiusCard
                color: rowMouse.containsMouse ? Theme.cardHover : Theme.card
                border.width: 1
                border.color: modelData.installed ? Theme.borderStrong : Theme.border

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3

                    Repeater {
                        model: [row.modelData.bg, row.modelData.cyan,
                                row.modelData.magenta, row.modelData.violet]
                        delegate: Rectangle {
                            width: 9
                            height: 9
                            radius: Theme.radiusControl
                            color: modelData
                            border.width: 1
                            border.color: Theme.alpha(Theme.text, 0.15)
                        }
                    }
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 58
                    anchors.right: actionButton.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Row {
                        spacing: 6

                        Text {
                            text: row.modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            font.bold: true
                        }

                        Text {
                            visible: row.modelData.installed
                            text: "已安装"
                            color: Theme.cyan
                            font.pixelSize: Theme.fontMicro
                        }
                    }

                    Text {
                        width: parent.width
                        text: row.modelData.desc.length > 0 ? row.modelData.desc
                                                            : row.modelData.key
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    id: actionButton
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 56
                    height: 24
                    radius: Theme.radiusControl
                    color: row.modelData.installed
                           ? Theme.alpha(Theme.danger, 0.14)
                           : Theme.alpha(Theme.cyan, 0.20)
                    border.width: 1
                    border.color: row.modelData.installed
                                  ? Theme.alpha(Theme.danger, 0.40)
                                  : Theme.alpha(Theme.cyan, 0.45)

                    Text {
                        anchors.centerIn: parent
                        text: row.modelData.installed ? "卸载" : "安装"
                        color: row.modelData.installed ? Theme.danger : Theme.cyan
                        font.pixelSize: Theme.fontMicro
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (row.modelData.installed) {
                                if (themeMarket.uninstall(row.modelData.key))
                                    root.notice = "已卸载「" + row.modelData.name + "」"
                                else
                                    root.notice = "卸载失败：找不到这个包的文件"
                                return
                            }
                            // 市场目录里的条目本身就是一份完整的主题包，
                            // 直接把它交给安装通道（与「从文件安装」同一条路径）
                            const result = themeMarket.installJson(
                                                         JSON.stringify(row.modelData),
                                                         row.modelData.key)
                            root.notice = result.ok
                                    ? ("已安装「" + row.modelData.name + "」，可在外观面板里选用")
                                    : ("安装失败：" + (result.reason || "未知原因"))
                        }
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    anchors.rightMargin: 66
                    hoverEnabled: true
                    // 行本身不响应点击：装 / 卸只由右边那颗按钮触发，
                    // 免得「点一下想看看详情」就把主题装上了
                    acceptedButtons: Qt.NoButton
                }
            }
        }
    }
}
