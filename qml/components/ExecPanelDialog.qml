import QtQuick
import QtQml
import QtQuick.Controls
import QtQuick.Layouts

// 执行结果面板弹窗（R6-06，MOD-13 自 Main.qml 原样迁入）：
// hostWin = 宿主窗口（原 window）。块体含原三行说明注释。
DialogShell {
    id: execPanelDialog

    property var hostWin
        hostWindow: hostWin
        titleText: "执行面板"
        sizeMode: "md"

        secondaryText: "重试"
        primaryText: "标为已办结"
        secondaryEnabled: execPanelDialog.summary.hasRun === true
                         && execPanelDialog.summary.status !== "running"
                         && execPanelDialog.summary.status !== "queued"
        primaryEnabled: execPanelDialog.summary.hasRun === true
                       && execPanelDialog.summary.status === "passed"

        onSecondaryTriggered: execPanelDialog.retryRun()
        onPrimaryTriggered: execPanelDialog.markDone()
        onCloseRequested: execPanelDialog.close()

        // 底栏左侧：看完整日志 / 跳到执行队列
        footerExtra: Component {
            RowLayout {
                spacing: Theme.spacingSm

                Button {
                    text: "查看完整日志"
                    enabled: (execPanelDialog.summary.logPath || "").length > 0
                    onClicked: Qt.openUrlExternally("file:///" + execPanelDialog.summary.logPath)
                }

                // R7-02：从执行面板直接跳到队列（排队 / 在跑的都在这看）
                Button {
                    text: "执行队列"
                    visible: execPanelDialog.summary.hasRun === true
                    onClicked: {
                        execPanelDialog.close()
                        hostWin.openExecQueue()
                    }
                }
            }
        }

        // 头右侧：结论徽章（通过 / 失败或受阻 / 取消 / 执行中）
        headerExtra: Component {
            Rectangle {
                visible: execPanelDialog.summary.hasRun === true
                implicitWidth: execBadgeText.implicitWidth + 16
                implicitHeight: 22
                width: implicitWidth
                height: implicitHeight
                radius: 6
                color: Theme.alpha(execBadgeText.color, 0.14)
                border.width: 1
                border.color: Theme.alpha(execBadgeText.color, 0.34)

                Text {
                    id: execBadgeText
                    anchors.centerIn: parent
                    text: {
                        var s = execPanelDialog.summary.status || ""
                        if (s === "passed") return "✔ 通过"
                        if (s === "blocked") return "✘ 失败/受阻"
                        if (s === "cancelled") return "○ 已取消"
                        return "◌ 执行中"
                    }
                    color: {
                        var s = execPanelDialog.summary.status || ""
                        if (s === "passed") return Theme.success
                        if (s === "blocked") return Theme.danger
                        if (s === "cancelled") return Theme.muted
                        return Theme.cyan
                    }
                    font.pixelSize: 11
                    font.bold: true
                }
            }
        }

        property string taskId: ""
        property var summary: ({})
        // R7-06 成本账：本卡累计运行耗时 + 执行次数
        property var costTotalMs: 0
        property int costRuns: 0

        function refresh() {
            summary = taskRuns.summaryOf(taskId, 30)
            var cost = taskRuns.costSummary(taskId)
            costTotalMs = cost ? (cost.totalCostMs || 0) : 0
            costRuns = cost ? (cost.runs || 0) : 0
        }

        // ms → 「3 分 20 秒」/「45 秒」/「1 小时 5 分」
        function humanCost(ms) {
            if (ms <= 0)
                return "—"
            var s = Math.round(ms / 1000)
            if (s < 60)
                return s + " 秒"
            var m = Math.floor(s / 60)
            if (m < 60)
                return s % 60 > 0 ? m + " 分 " + (s % 60) + " 秒" : m + " 分"
            return Math.floor(m / 60) + " 小时 " + (m % 60) + " 分"
        }

        onOpened: refresh()

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                Layout.fillWidth: true
                visible: execPanelDialog.summary.hasRun !== true
                text: "这张卡还没有执行记录。到 编辑 → 执行 页签发起一次执行。"
                color: Theme.muted
                font.pixelSize: 11
            }

            GridLayout {
                Layout.fillWidth: true
                visible: execPanelDialog.summary.hasRun === true
                columns: 2
                columnSpacing: 10
                rowSpacing: 4

                Text { text: "轮次"; color: Theme.muted; font.pixelSize: 11 }
                Text {
                    Layout.fillWidth: true
                    text: "attempt " + (execPanelDialog.summary.attempt || 1)
                    color: Theme.text
                    font.pixelSize: 11
                }

                Text { text: "耗时"; color: Theme.muted; font.pixelSize: 11 }
                Text {
                    Layout.fillWidth: true
                    text: (execPanelDialog.summary.durationSec || 0) + " 秒"
                          + (execPanelDialog.summary.exitCode !== undefined
                             && execPanelDialog.summary.exitCode !== null
                             ? " · 退出码 " + execPanelDialog.summary.exitCode : "")
                    color: Theme.text
                    font.pixelSize: 11
                }

                // R7-06 成本账：本卡累计耗时（所有已结算 run 之和）
                Text {
                    text: "本卡累计"
                    color: Theme.muted
                    font.pixelSize: 11
                    visible: execPanelDialog.costTotalMs > 0
                }
                Text {
                    Layout.fillWidth: true
                    visible: execPanelDialog.costTotalMs > 0
                    text: execPanelDialog.humanCost(execPanelDialog.costTotalMs)
                          + (execPanelDialog.costRuns > 0
                             ? "（" + execPanelDialog.costRuns + " 次执行）" : "")
                    color: Theme.text
                    font.pixelSize: 11
                }

                Text {
                    text: "失败原因"
                    color: Theme.muted
                    font.pixelSize: 11
                    visible: (execPanelDialog.summary.lastError || "").length > 0
                }
                Text {
                    Layout.fillWidth: true
                    visible: (execPanelDialog.summary.lastError || "").length > 0
                    text: execPanelDialog.summary.lastError || ""
                    color: Theme.danger
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }

                Text {
                    text: "越界改动"
                    color: Theme.muted
                    font.pixelSize: 11
                    visible: (execPanelDialog.summary.scopeViolation || "").length > 0
                }
                Text {
                    Layout.fillWidth: true
                    visible: (execPanelDialog.summary.scopeViolation || "").length > 0
                    text: execPanelDialog.summary.scopeViolation || ""
                    color: Theme.warning
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                }
            }

            Text {
                Layout.fillWidth: true
                visible: (execPanelDialog.summary.logTail || "").length > 0
                text: "日志末尾"
                color: Theme.muted
                font.pixelSize: 10
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                visible: (execPanelDialog.summary.logTail || "").length > 0
                radius: 8
                color: Theme.input
                border.width: 1
                border.color: Theme.border

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true

                    TextArea {
                        readOnly: true
                        selectByMouse: true
                        text: execPanelDialog.summary.logTail || ""
                        color: Theme.text
                        font.family: "Consolas"
                        font.pixelSize: 10
                        wrapMode: TextArea.WrapAnywhere
                        background: null
                    }
                }
            }

            Text {
                id: execNoticeText
                Layout.fillWidth: true
                visible: text.length > 0
                color: Theme.warning
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }
        }

        // 壳底栏的次操作「重试」
        function retryRun() {
            const r = taskRunner.start(execPanelDialog.taskId)
            if (r && r.ok) {
                execPanelDialog.refresh()
            } else {
                execNoticeText.text = (r && r.message) ? r.message : "重试启动失败"
            }
        }

        // 壳底栏的主操作「标为已办结」：人点了才走既有完成路径（taskUndo，可 Ctrl+Z 撤销）；
        // 程序绝不自动改列。
        function markDone() {
            taskUndo.moveTask(execPanelDialog.taskId, "done", -1)
            execPanelDialog.close()
        }

        // run 结束 / 状态变化时刷新面板（面板开着才刷）
        Connections {
            target: taskRunner
            function onRunFinished(runId, detail) {
                if (execPanelDialog.opened)
                    execPanelDialog.refresh()
            }
        }
    }

