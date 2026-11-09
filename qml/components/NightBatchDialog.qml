import QtQuick
import QtQml
import QtQuick.Controls
import QtQuick.Layouts

// 夜间批量执行弹窗（R7-03，MOD-14 自 Main.qml 原样迁入）：
// hostWin = 宿主窗口（原 window）。
    // 夜间批量（R7-05）：勾选多张待办卡一次性入队，给出「预计总时长 / 约几点完成」。
    //
    // 硬约束（与 R7-01 / R7-02 的分工一致）：
    //   - 入队只是把卡放进 R7-01 的 FIFO 队列（同卡已在队列中会被拒绝，不重复）；
    //   - 不改卡片所在列（沿用 ADR-0002）；单卡失败只把该 run 结算成 blocked，不阻断其余；
    //   - 只用 Theme.xxx 令牌，不写死颜色。
    // 注：卡面 Ctrl/Shift 直接多选需要改 BoardColumn/TaskCard（不在本卡 scope），
    // 这里用「勾选清单」实现「勾选多卡 → 排队」。
DialogShell {
    id: nightBatchDialog
    property var hostWin
        hostWindow: hostWin
        titleText: "夜间批量执行"
        subtitleText: "NIGHT BATCH · 已选 " + nightBatchDialog.selectedIds().length + " 张"
        sizeMode: "md"

        secondaryText: "关闭"
        primaryText: nightBatchDialog.selectedIds().length > 0
                      ? "加入夜间队列（" + nightBatchDialog.selectedIds().length + " 张）"
                      : "加入夜间队列"
        primaryEnabled: nightBatchDialog.selectedIds().length > 0

        onSecondaryTriggered: nightBatchDialog.close()
        onPrimaryTriggered: nightBatchDialog.enqueueSelected()
        onCloseRequested: nightBatchDialog.close()

        // 底栏左侧：清空选择
        footerExtra: Component {
            Button {
                text: "清空选择"
                enabled: nightBatchDialog.selectedIds().length > 0
                onClicked: {
                    nightBatchDialog.selected = ({})
                    nightBatchDialog.refreshEstimate()
                }
            }
        }

        // id -> true 的选中集合；estimate 是 scheduler.nightBatchEstimate 的结果
        property var selected: ({})
        property var estimate: ({})
        property string notice: ""

        function selectedIds() {
            var ids = []
            for (var k in selected)
                if (selected[k])
                    ids.push(k)
            return ids
        }

        function refreshEstimate() {
            estimate = scheduler.nightBatchEstimate(selectedIds(), 300)
        }

        function isSelected(id) {
            return selected[id] === true
        }

        function toggle(id) {
            var s = {}
            for (var k in selected)
                if (selected[k])
                    s[k] = true
            if (s[id])
                delete s[id]
            else
                s[id] = true
            selected = s
            refreshEstimate()
        }

        function humanDuration(sec) {
            if (sec <= 0)
                return "—"
            var m = Math.floor(sec / 60)
            if (m >= 60)
                return Math.floor(m / 60) + " 小时 " + (m % 60) + " 分"
            var s = Math.round(sec % 60)
            if (m <= 0)
                return s + " 秒"
            return s > 0 ? m + " 分 " + s + " 秒" : m + " 分"
        }

        function finishClock(sec) {
            if (sec <= 0)
                return ""
            var d = new Date(Date.now() + sec * 1000)
            return ("0" + d.getHours()).slice(-2) + ":" + ("0" + d.getMinutes()).slice(-2)
        }

        onOpened: {
            selected = ({})
            notice = ""
            refreshEstimate()
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: "勾选待办卡（每张卡会按它的 workflow 排队执行）；队列按 FIFO 顺序推进，"
                      + "并发受设置约束。单张失败只标 blocked，不影响其余。"
                color: Theme.muted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            // 待办卡清单：点整行切换选中
            ListView {
                id: batchList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(320, contentHeight)
                clip: true
                spacing: 2
                model: taskStore
                delegate: Rectangle {
                    width: batchList.width
                    height: (model.status === "todo") ? 32 : 0
                    visible: height > 0
                    color: rowMouse.containsMouse ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.08)
                                                  : "transparent"
                    radius: 6

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: nightBatchDialog.toggle(model.id)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 8

                        Rectangle {
                            width: 16
                            height: 16
                            radius: 4
                            color: nightBatchDialog.isSelected(model.id)
                                   ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.25) : "transparent"
                            border.width: 1
                            border.color: nightBatchDialog.isSelected(model.id) ? Theme.cyan : Theme.muted

                            Text {
                                anchors.centerIn: parent
                                visible: nightBatchDialog.isSelected(model.id)
                                text: "✓"
                                color: Theme.cyan
                                font.pixelSize: 10
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: model.title
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // 预计总时长：按各卡历史平均耗时（无历史则用全局均值 / 兜底）求和，按并发折算
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    text: "预计总时长：" + nightBatchDialog.humanDuration(
                              nightBatchDialog.estimate.totalSec || 0)
                    color: Theme.text
                    font.pixelSize: 11
                }

                Text {
                    visible: (nightBatchDialog.estimate.totalSec || 0) > 0
                    text: "约 " + nightBatchDialog.finishClock(nightBatchDialog.estimate.totalSec || 0) + " 完成"
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "并发 " + (nightBatchDialog.estimate.concurrency || scheduler.concurrency)
                    color: Theme.muted
                    font.pixelSize: 11
                }
            }

            Text {
                Layout.fillWidth: true
                visible: nightBatchDialog.notice.length > 0
                text: nightBatchDialog.notice
                color: Theme.warning
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }
        }

        // 壳底栏的主操作「加入夜间队列」：入队后清空勾选并刷新估算
        function enqueueSelected() {
            var res = scheduler.enqueueNightBatch(nightBatchDialog.selectedIds(), {})
            var queued = (res.queued || []).length
            var rejected = (res.rejected || []).length
            var msg = "已入队 " + queued + " 张"
            if (rejected > 0)
                msg += "，跳过 " + rejected + " 张（已在队列中或参数缺失）"
            nightBatchDialog.notice = msg + "。可在「执行队列」面板查看进度。"
            nightBatchDialog.selected = ({})
            nightBatchDialog.refreshEstimate()
        }
}
