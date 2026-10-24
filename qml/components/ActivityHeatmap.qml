import QtQuick
import QtQuick.Layouts

// 完成度可视化：13 周热力图 + 周吞吐迷你曲线 + 连续天数。
// 数据由 C++ taskStats 提供，组件本身不查库。
Item {
    id: heatmapRoot

    property var cells: []
    property var weekly: []
    property int streak: 0
    property int totalDone: 0
    property int bestDay: 0

    implicitHeight: 66

    RowLayout {
        anchors.fill: parent
        spacing: 14

        ColumnLayout {
            spacing: 1

            Text {
                text: "活跃热力"
                color: Theme.text
                font.pixelSize: 11
                font.bold: true
            }

            Text {
                text: "近 13 周 · 共 " + heatmapRoot.totalDone + " 次完成"
                color: Theme.muted
                font.pixelSize: 9
            }

            Text {
                text: heatmapRoot.bestDay > 0 ? "单日峰值 " + heatmapRoot.bestDay : "暂无记录"
                color: Theme.muted
                font.pixelSize: 9
            }
        }

        Item {
            id: grid
            Layout.preferredWidth: 13 * 8 + 12 * 2
            Layout.preferredHeight: 7 * 8 + 6 * 2
            Row {
                anchors.centerIn: parent
                spacing: 2

                Repeater {
                    model: 13

                    delegate: Column {
                        id: weekCol
                        required property int index
                        spacing: 2

                        Repeater {
                            model: 7

                            delegate: Rectangle {
                                required property int index
                                width: 8
                                height: 8
                                radius: 2

                                property var cell: heatmapRoot.cells[weekCol.index * 7 + index]

                                color: {
                                    if (!cell || cell.level < 0)
                                        return "transparent"
                                    switch (cell.level) {
                                    case 0: return Qt.rgba(1, 1, 1, 0.05)
                                    case 1: return Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.28)
                                    case 2: return Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.48)
                                    case 3: return Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.72)
                                    default: return Theme.cyan
                                    }
                                }

                                border.width: cell && cell.level === 4 ? 1 : 0
                                border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.35)
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 42
            color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.12)
        }

        ColumnLayout {
            spacing: 1

            RowLayout {
                spacing: 5

                Text {
                    text: "连续"
                    color: Theme.muted
                    font.pixelSize: 9
                }

                Text {
                    text: heatmapRoot.streak
                    color: heatmapRoot.streak > 0 ? Theme.magenta : Theme.muted
                    font.pixelSize: 18
                    font.bold: true
                }

                Text {
                    text: "天"
                    color: Theme.muted
                    font.pixelSize: 9
                    Layout.alignment: Qt.AlignBottom
                    Layout.bottomMargin: 4
                }
            }

            Text {
                text: "周吞吐"
                color: Theme.muted
                font.pixelSize: 9
            }
        }

        Item {
            Layout.preferredWidth: 150
            Layout.preferredHeight: 46

            Canvas {
                id: throughput
                anchors.fill: parent
                renderStrategy: Canvas.Cooperative

                property var points: heatmapRoot.weekly

                onPointsChanged: requestPaint()

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    const data = points || []
                    if (data.length < 2)
                        return

                    let maxV = 1
                    for (let i = 0; i < data.length; ++i)
                        maxV = Math.max(maxV, data[i].count)

                    const padX = 4
                    const padY = 6
                    const usableW = width - padX * 2
                    const usableH = height - padY * 2
                    const stepX = usableW / (data.length - 1)

                    ctx.beginPath()
                    for (let i = 0; i < data.length; ++i) {
                        const x = padX + stepX * i
                        const y = padY + usableH * (1 - data[i].count / maxV)
                        if (i === 0)
                            ctx.moveTo(x, y)
                        else
                            ctx.lineTo(x, y)
                    }
                    ctx.lineWidth = 1.6
                    ctx.strokeStyle = Theme.cyan
                    ctx.stroke()

                    ctx.lineTo(padX + usableW, height - padY)
                    ctx.lineTo(padX, height - padY)
                    ctx.closePath()
                    const grad = ctx.createLinearGradient(0, 0, 0, height)
                    grad.addColorStop(0, Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.28))
                    grad.addColorStop(1, Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.0))
                    ctx.fillStyle = grad
                    ctx.fill()

                    ctx.fillStyle = Theme.magenta
                    for (let i = 0; i < data.length; ++i) {
                        const x = padX + stepX * i
                        const y = padY + usableH * (1 - data[i].count / maxV)
                        ctx.beginPath()
                        ctx.arc(x, y, 1.8, 0, Math.PI * 2)
                        ctx.fill()
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true

                property int activeIndex: -1

                onPositionChanged: function(mouse) {
                    const data = heatmapRoot.weekly || []
                    if (data.length < 2) {
                        activeIndex = -1
                        return
                    }
                    const padX = 4
                    const stepX = (width - padX * 2) / (data.length - 1)
                    let idx = Math.round((mouse.x - padX) / stepX)
                    idx = Math.max(0, Math.min(data.length - 1, idx))
                    activeIndex = idx
                    tip.text = data[idx].label + " 起那周 · " + data[idx].count + " 次"
                    tip.visible = true
                }

                onExited: {
                    activeIndex = -1
                    tip.visible = false
                }
            }

            Rectangle {
                id: tip
                visible: false
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.top
                anchors.bottomMargin: 2
                width: tipText.implicitWidth + 10
                height: 18
                radius: 4
                color: Theme.panel
                border.width: 1
                border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.25)
                z: 10

                Text {
                    id: tipText
                    anchors.centerIn: parent
                    color: Theme.text
                    font.pixelSize: 9
                }
            }
        }
    }
}
