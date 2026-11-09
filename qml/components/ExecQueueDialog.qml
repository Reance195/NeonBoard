import QtQuick
import QtQml
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 执行队列弹窗（R7-02，MOD-11 自 Main.qml 原样迁入）：
// 壳与 content 整体迁移，id 保留原名，外部调用点零改动。
DialogShell {
    id: execQueueDialog
        hostWindow: window
        titleText: "执行队列"
        subtitleText: "EXEC QUEUE"
        sizeMode: "lg"

        secondaryText: "关闭"
        footerNote: "队列顺序只影响运行态，不改卡片所在列；跳过 / 失败都不会自动把卡标为已办结。"

        onSecondaryTriggered: execQueueDialog.close()
        onCloseRequested: execQueueDialog.close()

        // 头右侧：暂停 / 恢复（只停派发，不杀在跑的 run）
        headerExtra: Component {
            Rectangle {
                implicitWidth: 62
                implicitHeight: 22
                width: implicitWidth
                height: implicitHeight
                radius: 6
                color: execQueueDialog.queuePaused
                       ? Theme.alpha(Theme.warning, 0.18)
                       : Theme.accentSoft
                border.width: 1
                border.color: execQueueDialog.queuePaused ? Theme.warning : Theme.borderStrong

                Text {
                    anchors.centerIn: parent
                    text: execQueueDialog.queuePaused ? "▶ 恢复" : "⏸ 暂停"
                    color: execQueueDialog.queuePaused ? Theme.warning : Theme.cyan
                    font.pixelSize: 10
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (execQueueDialog.queuePaused)
                            scheduler.resumeQueue()
                        else
                            scheduler.pauseQueue()
                        execQueueDialog.refresh()
                    }
                }
            }
        }

        property var rows: []
        property int concurrencyCount: 1
        property bool queuePaused: false
        property int estimateSec: 0
        // 最近一次操作的提示（已跳过 / 已到队首等），避免静默失败
        property string notice: ""

        function refresh() {
            // DX-143：数组整体赋值会触发 ListView 全量 reset（delegate 每秒销毁重建、running 频闪、滚动回跳），仅内容真变化才赋值；标量直赋开销可忽略
            const nr = scheduler.queueView(100), nc = scheduler.concurrency, np = scheduler.paused, ne = scheduler.estimateRemainingSec(300)
            if (JSON.stringify(nr) !== JSON.stringify(rows)) rows = nr
            if (concurrencyCount !== nc) concurrencyCount = nc; if (queuePaused !== np) queuePaused = np; if (estimateSec !== ne) estimateSec = ne
        }

        // 秒 → 「12 分 30 秒」/「约 01:20 完成」
        function humanDuration(sec) {
            if (sec <= 0)
                return "—"
            if (sec < 60)
                return sec + " 秒"
            var m = Math.floor(sec / 60)
            var s = Math.round(sec % 60)
            if (m >= 60)
                return Math.floor(m / 60) + " 小时 " + (m % 60) + " 分"
            return s > 0 ? m + " 分 " + s + " 秒" : m + " 分"
        }

        function finishClock(sec) {
            if (sec <= 0)
                return ""
            var d = new Date(Date.now() + sec * 1000)
            return ("0" + d.getHours()).slice(-2) + ":" + ("0" + d.getMinutes()).slice(-2)
        }

        // 已运行时长（秒）：started_at 为空表示还没开始跑
        function runDuration(row) {
            if (!row || !row.startedAt)
                return 0
            return Math.max(0, Math.floor((Date.now() / 1000) - row.startedAt))
        }

        function stateLabel(row) {
            var q = row.queueStatus || ""
            if (q === "running")
                return "执行中"
            if (q === "dispatching")
                return "启动中"
            return "排队中"
        }

        function stateColor(row) {
            var q = row.queueStatus || ""
            if (q === "running")
                return Theme.cyan
            if (q === "dispatching")
                return Theme.warning
            return Theme.muted
        }

        function waitingCount() {
            var n = 0
            for (var i = 0; i < rows.length; ++i) {
                if ((rows[i].queueStatus || "") === "waiting")
                    ++n
            }
            return n
        }

        onOpened: refresh()

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            // 概览：并发 / 排队数 / 预计完成
            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                Text {
                    text: "并发 " + scheduler.running + "/" + execQueueDialog.concurrencyCount
                    color: Theme.text
                    font.pixelSize: 11
                }

                Text {
                    text: "排队 " + execQueueDialog.waitingCount()
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Text {
                    visible: execQueueDialog.queuePaused
                    text: "⏸ 队列已暂停，不会派发新的 run"
                    color: Theme.warning
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: execQueueDialog.estimateSec > 0
                          ? "预计 " + execQueueDialog.humanDuration(execQueueDialog.estimateSec)
                            + "，约 " + execQueueDialog.finishClock(execQueueDialog.estimateSec) + " 完成"
                          : ""
                    color: Theme.muted
                    font.pixelSize: 11
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }

            Text {
                Layout.fillWidth: true
                visible: execQueueDialog.rows.length === 0
                text: "队列为空。在卡片上点「执行」，排队中的 run 会出现在这里。"
                color: Theme.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            // 队列列表：waiting / dispatching / running
            //
            // 规格 §3.4 落位表 execQueueDialog 行：「表头固定；队列行高统一 40 px」。
            // 表头放在 ListView 之外 —— 列表滚动时表头不动；行高 40 px、间距 0，
            // 三列（任务 / 状态 / 操作）的列宽与表头逐列对齐，扫一眼就能对上。
            Rectangle {
                Layout.fillWidth: true
                // 表头块（6 + 18 + 1 + 4）+ 列表视口（最多 320）+ 底留白与边框
                Layout.preferredHeight: 43 + Math.min(320, execQueueDialog.rows.length * 40)
                visible: execQueueDialog.rows.length > 0
                radius: 10
                color: Theme.panel
                border.width: 1
                border.color: Theme.border

                // ---- 表头（固定，不随列表滚动）----
                RowLayout {
                    id: queueHeader
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 6
                    height: 18
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: "任务"
                        color: Theme.muted
                        font.pixelSize: 10
                        font.letterSpacing: 1.0
                    }

                    Text {
                        Layout.preferredWidth: 72
                        horizontalAlignment: Text.AlignRight
                        text: "状态"
                        color: Theme.muted
                        font.pixelSize: 10
                    }

                    Text {
                        Layout.preferredWidth: 64
                        horizontalAlignment: Text.AlignRight
                        text: "操作"
                        color: Theme.muted
                        font.pixelSize: 10
                    }
                }

                Rectangle {
                    id: queueHeaderLine
                    anchors.top: queueHeader.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    height: 1
                    color: Theme.hairline
                }

                ListView {
                    id: execQueueList
                    anchors.top: queueHeaderLine.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.bottomMargin: 8
                    anchors.topMargin: 4
                    spacing: 0
                    clip: true
                    model: execQueueDialog.rows

                    ScrollBar.vertical: ScrollBar {
                        width: 6
                        policy: execQueueList.contentHeight > execQueueList.height
                                ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    }

                    delegate: Rectangle {
                        required property var modelData

                        width: execQueueList.width
                        height: 40
                        color: (modelData.queueStatus || "") === "running"
                               ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.10)
                               : "transparent"

                        // 行分隔线（表格感：行贴着行，不再是圆角卡片）
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 1
                            color: Theme.hairline
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.topMargin: 3
                            anchors.bottomMargin: 3
                            spacing: 8

                            // 状态点：只有 running 才脉冲（脉冲语义 = 正在跑）
                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: execQueueDialog.stateColor(modelData)
                                opacity: (modelData.queueStatus || "") === "running"
                                         ? 0.4 + 0.6 * itemPulse.value : 1.0

                                SequentialAnimation {
                                    running: (modelData.queueStatus || "") === "running"
                                    loops: Animation.Infinite
                                    NumberAnimation {
                                        target: itemPulse; property: "value"
                                        from: 0; to: 1; duration: 700
                                        easing.type: Easing.InOutSine
                                    }
                                    NumberAnimation {
                                        target: itemPulse; property: "value"
                                        from: 1; to: 0; duration: 700
                                        easing.type: Easing.InOutSine
                                    }
                                }

                                QtObject {
                                    id: itemPulse
                                    property real value: 0
                                }
                            }

                            // 第一列：任务（标题 + 副信息），与表头「任务」列同宽
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.title || modelData.taskId
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        var parts = []
                                        if ((modelData.queueStatus || "") === "running")
                                            parts.push("已运行 " + execQueueDialog.humanDuration(
                                                           execQueueDialog.runDuration(modelData)))
                                        if ((modelData.role || "").length > 0)
                                            parts.push("角色 " + modelData.role)
                                        if (modelData.agent)
                                            parts.push(modelData.agent)
                                        if ((modelData.lastError || "").length > 0)
                                            parts.push(modelData.lastError)
                                        return parts.join(" · ")
                                    }
                                    color: Theme.muted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }

                            // 第二列：状态（从副信息里提出来，与表头「状态」列对齐）
                            Text {
                                Layout.preferredWidth: 72
                                horizontalAlignment: Text.AlignRight
                                text: execQueueDialog.stateLabel(modelData)
                                color: (modelData.queueStatus || "") === "running"
                                       ? Theme.cyan : Theme.muted
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }

                            // 第三列：操作（调序 / 跳过，只对 waiting 生效）
                            RowLayout {
                                Layout.preferredWidth: 64
                                Layout.alignment: Qt.AlignRight
                                spacing: 8

                                // 调序 / 跳过：只对 waiting 生效（running 的不许被打断）
                                Text {
                                    visible: (modelData.queueStatus || "") === "waiting"
                                    text: "↑"
                                    color: upMouse.containsMouse ? Theme.cyan : Theme.muted
                                    font.pixelSize: 13
                                    MouseArea {
                                        id: upMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (!scheduler.reorder(modelData.id, -1))
                                                execQueueDialog.notice = "已经是最前面一条"
                                            execQueueDialog.refresh()
                                        }
                                    }
                                }

                                Text {
                                    visible: (modelData.queueStatus || "") === "waiting"
                                    text: "↓"
                                    color: downMouse.containsMouse ? Theme.cyan : Theme.muted
                                    font.pixelSize: 13
                                    MouseArea {
                                        id: downMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (!scheduler.reorder(modelData.id, 1))
                                                execQueueDialog.notice = "已经是最后面一条"
                                            execQueueDialog.refresh()
                                        }
                                    }
                                }

                                Text {
                                    visible: (modelData.queueStatus || "") === "waiting"
                                    text: "跳过"
                                    color: skipMouse.containsMouse ? Theme.danger : Theme.muted
                                    font.pixelSize: 10
                                    MouseArea {
                                        id: skipMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (!scheduler.skip(modelData.id))
                                                execQueueDialog.notice = "该条已不在等待状态"
                                            else
                                                execQueueDialog.notice =
                                                    "已跳过：" + (modelData.title || "")
                                            execQueueDialog.refresh()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: execQueueDialog.notice.length > 0
                text: execQueueDialog.notice
                color: Theme.warning
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }
        }

        // 队列变化（入队 / 派发 / 结束 / 暂停）时刷新；面板开着才刷
        Connections {
            target: scheduler
            function onChanged() {
                if (execQueueDialog.opened)
                    execQueueDialog.refresh()
            }
        }

        // 每秒刷一次「已运行时长」：不依赖 revision，避免心跳把整个面板重建
        Timer {
            interval: 1000
            repeat: true
            running: execQueueDialog.opened
            onTriggered: {
                if (execQueueDialog.opened)
                    execQueueDialog.refresh()
            }
        }
}
