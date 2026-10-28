import QtQuick
import QtQuick.Layouts

// 完成度可视化：13 周热力图 + 周吞吐迷你曲线 + 连续天数。
// 数据由 C++ taskStats 提供，组件本身不查库。
//
// R11-03 的三处改动：
//   1. 热力网格由 91 个 Rectangle 改为 1 个 Canvas —— 首帧少构造 90 个 item；
//   2. 色阶跟随主题 accent（原先写死青色，换主题时热力图不变色）；
//   3. 无任何完成记录时给出空态文案（原先是一片 5% 白格，看不出「还没数据」），
//      格子可悬浮看当天完成数。
Item {
    id: heatmapRoot

    property var cells: []
    property var weekly: []
    property int streak: 0
    property int totalDone: 0
    property int bestDay: 0

    readonly property int weeks: 13
    readonly property int days: 7
    readonly property int cellSize: 7
    readonly property int cellGap: 2
    readonly property real gridWidth: weeks * cellSize + (weeks - 1) * cellGap
    readonly property real gridHeight: days * cellSize + (days - 1) * cellGap
    readonly property bool hasData: totalDone > 0

    // 当前悬浮的格子下标（-1 = 没悬停）
    property int activeIndex: -1

    implicitHeight: gridHeight

    RowLayout {
        anchors.fill: parent
        spacing: 12

        ColumnLayout {
            spacing: 1

            Text {
                text: "活跃热力"
                color: Theme.text
                font.pixelSize: 11
                font.bold: true
            }

            Text {
                text: "近 " + heatmapRoot.weeks + " 周 · 共 " + heatmapRoot.totalDone + " 次完成"
                color: Theme.muted
                font.pixelSize: 9
            }

            Text {
                visible: heatmapRoot.bestDay > 0
                text: "单日峰值 " + heatmapRoot.bestDay
                color: Theme.muted
                font.pixelSize: 9
            }
        }

        // 热力网格：一次 Canvas 画完 13×7 = 91 个格子
        Item {
            Layout.preferredWidth: heatmapRoot.gridWidth
            Layout.preferredHeight: heatmapRoot.gridHeight

            Canvas {
                id: gridCanvas
                width: heatmapRoot.gridWidth
                height: heatmapRoot.gridHeight
                renderStrategy: Canvas.Cooperative

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    const data = heatmapRoot.cells || []
                    const accent = Theme.accent
                    // 没有数据时整片压暗：格子还在，但一眼看得出「还没点亮」
                    const dim = heatmapRoot.hasData ? 1.0 : 0.30
                    const size = heatmapRoot.cellSize
                    const gap = heatmapRoot.cellGap

                    function roundRectPath(c, x, y, w, h, r) {
                        c.beginPath()
                        c.moveTo(x + r, y)
                        c.lineTo(x + w - r, y)
                        c.arcTo(x + w, y, x + w, y + r, r)
                        c.lineTo(x + w, y + h - r)
                        c.arcTo(x + w, y + h, x + w - r, y + h, r)
                        c.lineTo(x + r, y + h)
                        c.arcTo(x, y + h, x, y + h - r, r)
                        c.lineTo(x, y + r)
                        c.arcTo(x, y, x + r, y, r)
                        c.closePath()
                    }

                    for (let i = 0; i < data.length; ++i) {
                        const cell = data[i]
                        if (!cell || cell.level < 0)
                            continue // 未来日期：不画

                        const week = Math.floor(i / heatmapRoot.days)
                        const dow = i % heatmapRoot.days
                        const x = week * (size + gap)
                        const y = dow * (size + gap)

                        let fill
                        switch (cell.level) {
                        case 0:
                            fill = Qt.rgba(1, 1, 1, 0.05 * dim)
                            break
                        case 1:
                            fill = Qt.rgba(accent.r, accent.g, accent.b, 0.28 * dim)
                            break
                        case 2:
                            fill = Qt.rgba(accent.r, accent.g, accent.b, 0.48 * dim)
                            break
                        case 3:
                            fill = Qt.rgba(accent.r, accent.g, accent.b, 0.72 * dim)
                            break
                        default:
                            fill = Qt.rgba(accent.r, accent.g, accent.b, dim)
                            break
                        }

                        roundRectPath(ctx, x, y, size, size, 2)
                        ctx.fillStyle = fill
                        ctx.fill()

                        if (cell.level === 4) {
                            ctx.strokeStyle = Qt.rgba(accent.r, accent.g, accent.b, 0.35 * dim)
                            ctx.lineWidth = 1
                            ctx.stroke()
                        }
                    }
                }
            }

            // 数据换主题色时重绘一次（色阶跟主题走）
            Connections {
                target: Theme
                function onAccentChanged() {
                    gridCanvas.requestPaint()
                }
            }

            // 悬浮提示：当天日期 + 完成次数
            MouseArea {
                id: gridMouse
                anchors.fill: parent
                hoverEnabled: true

                onPositionChanged: function(mouse) {
                    const data = heatmapRoot.cells || []
                    const size = heatmapRoot.cellSize
                    const gap = heatmapRoot.cellGap
                    const week = Math.floor(mouse.x / (size + gap))
                    const dow = Math.floor(mouse.y / (size + gap))
                    if (week < 0 || week >= heatmapRoot.weeks || dow < 0 || dow >= heatmapRoot.days) {
                        heatmapRoot.activeIndex = -1
                        return
                    }
                    heatmapRoot.activeIndex = week * heatmapRoot.days + dow
                }

                onExited: {
                    heatmapRoot.activeIndex = -1
                }
            }

            // 空态：没有任何完成记录时给出可读的提示，而不是一片空格子
            Text {
                anchors.centerIn: parent
                visible: !heatmapRoot.hasData
                text: "还没有完成记录 · 完成第一张卡即可点亮"
                color: Theme.muted
                font.pixelSize: 9
            }

            Rectangle {
                id: gridTip
                visible: heatmapRoot.activeIndex >= 0
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.top
                anchors.bottomMargin: 2
                width: gridTipText.implicitWidth + 12
                height: 20
                radius: 5
                color: Theme.panelSolid
                border.width: 1
                border.color: Theme.border
                z: 10

                Text {
                    id: gridTipText
                    anchors.centerIn: parent
                    color: Theme.text
                    font.pixelSize: 9
                    text: {
                        const data = heatmapRoot.cells || []
                        const cell = data[heatmapRoot.activeIndex]
                        if (!cell)
                            return ""
                        return cell.date + " · 完成 " + cell.count + " 次"
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 42
            color: Theme.hairline
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
                    ctx.strokeStyle = Theme.accent
                    ctx.stroke()

                    ctx.lineTo(padX + usableW, height - padY)
                    ctx.lineTo(padX, height - padY)
                    ctx.closePath()
                    const grad = ctx.createLinearGradient(0, 0, 0, height)
                    grad.addColorStop(0, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.28))
                    grad.addColorStop(1, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.0))
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

                // 主题色变化时重绘（吞吐曲线跟随主题）
                Connections {
                    target: Theme
                    function onAccentChanged() {
                        throughput.requestPaint()
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
                    // 赋给 tipText 而不是 tip：tip 是 Rectangle，没有 text 属性，
                    // 赋过去会在日志里刷「Cannot assign to non-existent property "text"」
                    tipText.text = data[idx].label + " 起那周 · " + data[idx].count + " 次"
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
                border.color: Theme.border
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
