import QtQuick
import QtQml
import QtQuick.Controls
import QtQuick.Layouts

// 执行前确认弹窗（R6-03，MOD-12 自 Main.qml 原样迁入）：
// hostWin = 宿主窗口（原 window），editorLoader = 任务编辑器 Loader（原 taskEditorLoader）。
// " + (块体含原两行说明注释，随迁。)
DialogShell {
    id: execConfirmDialog

    property var hostWin
    property var editorLoader
        hostWindow: hostWin
        titleText: "执行前确认"
        sizeMode: "md"

        secondaryText: "取消"
        primaryText: "确认执行"
        primaryEnabled: execConfirmDialog.cwdTrusted || execConfirmDialog.trustChecked
        footerNote: "确认后在本机启动 agent；执行完最多留下本地改动，不自动推送。"

        onSecondaryTriggered: execConfirmDialog.close()
        onPrimaryTriggered: execConfirmDialog.confirmExec()
        onCloseRequested: execConfirmDialog.close()

        property string taskId: ""
        property string agentId: ""
        property string agentName: ""
        property string cwd: ""
        property string command: ""
        property var acceptance: []
        property int timeoutMin: 30
        property int maxAttempts: 1
        property bool cwdTrusted: false
        property bool trustChecked: false
        property string errorText: ""

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10
                rowSpacing: 6

                Text {
                    text: "agent"
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Text {
                    Layout.fillWidth: true
                    text: execConfirmDialog.agentName + "（" + execConfirmDialog.agentId + "）"
                    color: Theme.text
                    font.pixelSize: 11
                }

                Text {
                    text: "工作目录"
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Text {
                    Layout.fillWidth: true
                    text: execConfirmDialog.cwd
                    color: execConfirmDialog.cwdTrusted ? Theme.text : Theme.warning
                    font.family: "Consolas"
                    font.pixelSize: 10
                    wrapMode: Text.WrapAnywhere
                }

                Text {
                    text: "预计耗时"
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Text {
                    Layout.fillWidth: true
                    text: "最多 " + execConfirmDialog.timeoutMin + " 分钟 · "
                          + execConfirmDialog.maxAttempts + " 轮"
                    color: Theme.text
                    font.pixelSize: 11
                }
            }

            Text {
                Layout.fillWidth: true
                text: "命令行（只读；执行时按 argv 传参，不经过 shell）"
                color: Theme.muted
                font.pixelSize: 10
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                radius: 8
                color: Theme.input
                border.width: 1
                border.color: Theme.border

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AsNeeded

                    TextArea {
                        id: execCommandText
                        readOnly: true
                        selectByMouse: true
                        text: execConfirmDialog.command
                        color: Theme.cyan
                        font.family: "Consolas"
                        font.pixelSize: 10
                        wrapMode: TextArea.WrapAnywhere
                        background: null
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: "验收项（执行后逐条判定）"
                color: Theme.muted
                font.pixelSize: 10
            }

            Column {
                Layout.fillWidth: true
                spacing: 2

                Repeater {
                    model: execConfirmDialog.acceptance

                    Text {
                        required property var modelData
                        width: parent.width
                        text: "· " + modelData
                        color: Theme.text
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: trustRow.implicitHeight + 16
                radius: 8
                color: Theme.card
                border.width: 1
                border.color: execConfirmDialog.cwdTrusted ? Theme.border : Theme.borderStrong

                RowLayout {
                    id: trustRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 8
                    spacing: 8

                    CheckBox {
                        id: trustCwdCheck
                        text: execConfirmDialog.cwdTrusted
                              ? "本目录已在信任白名单"
                              : "信任本目录（写入 cwd 白名单）"
                        checked: execConfirmDialog.cwdTrusted || execConfirmDialog.trustChecked
                        enabled: !execConfirmDialog.cwdTrusted
                        onToggled: execConfirmDialog.trustChecked = checked

                        indicator: Rectangle {
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 4
                            color: trustCwdCheck.checked
                                   ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.32)
                                   : Theme.input
                            border.width: 1
                            border.color: trustCwdCheck.hovered ? Theme.cyan : Theme.border

                            Text {
                                anchors.centerIn: parent
                                visible: trustCwdCheck.checked
                                text: "✓"
                                color: Theme.text
                                font.pixelSize: 10
                            }
                        }

                        contentItem: Text {
                            leftPadding: 6
                            text: trustCwdCheck.text
                            color: trustCwdCheck.enabled ? Theme.text : Theme.muted
                            font.pixelSize: 10
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "信任后该目录不再重复确认"
                        color: Theme.muted
                        font.pixelSize: 9
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: execConfirmDialog.errorText.length > 0
                text: execConfirmDialog.errorText
                color: Theme.danger
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }

        // 主操作「确认执行」：壳底栏按钮走这里
        function confirmExec() {
            if (!execConfirmDialog.cwdTrusted
                    && !agentProfile.trustCwd(execConfirmDialog.cwd)) {
                execConfirmDialog.errorText =
                    "无法信任该目录：请检查目录是否存在"
                return
            }
            // R6-06：真正拉起执行器。start 按执行定义取 agent/cwd/prompt，
            // 同卡已有活跃 run 会被 busy 拒绝（M1 不排队）。
            const r = taskRunner.start(execConfirmDialog.taskId)
            if (r && r.ok) {
                if (editorLoader.item)
                    editorLoader.item.execNotice =
                        "已启动执行（run " + String(r.runId).substring(0, 8) + "…），结果见下方执行态"
                execConfirmDialog.close()
                hostWin.openExecPanel(execConfirmDialog.taskId)
            } else {
                execConfirmDialog.errorText =
                    (r && r.message) ? r.message : "执行启动失败"
            }
        }
    }

