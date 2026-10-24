import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 预览中心：列出各 agent 的工作区，一键构建并启动隔离的预览实例
// 只有主线实例展示本面板（agent 沙箱实例里不显示，避免混淆）
Rectangle {
    id: previewPanel

    visible: false
    anchors.fill: parent
    z: 140
    color: Qt.rgba(0, 0, 0, 0.55)

    // 最近操作的 agent，构建日志显示它的
    property string logAgentId: ""

    function open() {
        visible = true
        previewHub.refresh()
    }

    function close() {
        visible = false
    }

    MouseArea {
        anchors.fill: parent
        onClicked: previewPanel.close()
    }

    Rectangle {
        id: panelBody
        anchors.centerIn: parent
        width: Math.min(780, parent.width - 40)
        height: Math.min(640, parent.height - 32)
        radius: 14
        color: Theme.panelSolid
        border.width: 1
        border.color: Theme.border

        ColumnLayout {
            id: bodyLayout
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            // ---------- 标题行 ----------
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "预览中心"
                    color: Theme.text
                    font.pixelSize: 15
                    font.bold: true
                }

                Text {
                    text: "PREVIEW CENTER"
                    color: Theme.muted
                    font.pixelSize: 10
                    font.letterSpacing: 1.2
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "刷新"
                    color: refreshMouse.containsMouse ? Theme.cyan : Theme.text
                    font.pixelSize: 11
                    MouseArea {
                        id: refreshMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: previewHub.refresh()
                    }
                }

                Text {
                    text: "✕"
                    color: closeMouse.containsMouse ? Theme.text : Theme.muted
                    font.pixelSize: 14
                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: previewPanel.close()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }

            Text {
                Layout.fillWidth: true
                text: previewHub.available
                      ? "各 agent 工作区的构建与预览实例。预览实例走独立沙箱，不影响本机真实数据。"
                      : "未定位到仓库根目录：请设置环境变量 NEONBOARD_REPO_ROOT 指向仓库根目录后重启。"
                color: Theme.muted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            // ---------- agent 列表 ----------
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                // 内容不足时收缩到实际高度，避免大片空白
                Layout.maximumHeight: agentList.contentHeight + 16
                radius: 10
                color: Theme.panel
                border.width: 1
                border.color: Theme.border

                ListView {
                    id: agentList
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    clip: true
                    model: previewHub.agents

                    ScrollBar.vertical: ScrollBar {
                        width: 6
                        policy: agentList.contentHeight > agentList.height
                                ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    }

                    delegate: Rectangle {
                        required property var modelData
                        width: agentList.width
                        height: 64
                        radius: 8
                        color: Theme.card
                        border.width: 1
                        border.color: Theme.border

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 10

                            // 名称 / 分支 / 最近提交
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                RowLayout {
                                    spacing: 6

                                    Text {
                                        text: modelData.id
                                        color: Theme.text
                                        font.pixelSize: 13
                                        font.bold: true
                                    }

                                    Rectangle {
                                        visible: modelData.current
                                        radius: 4
                                        color: Theme.accentSoft
                                        border.width: 1
                                        border.color: Theme.border
                                        height: 16
                                        width: currentTag.implicitWidth + 10

                                        Text {
                                            id: currentTag
                                            anchors.centerIn: parent
                                            text: "当前实例"
                                            color: Theme.cyan
                                            font.pixelSize: 9
                                        }
                                    }

                                    Rectangle {
                                        visible: modelData.dirty
                                        radius: 4
                                        color: "transparent"
                                        border.width: 1
                                        border.color: Theme.warning
                                        height: 16
                                        width: dirtyTag.implicitWidth + 10

                                        Text {
                                            id: dirtyTag
                                            anchors.centerIn: parent
                                            text: "有未提交改动"
                                            color: Theme.warning
                                            font.pixelSize: 9
                                        }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: (modelData.detached ? "(游离 HEAD) " : "") + modelData.branch
                                          + (modelData.head ? "  ·  " + modelData.head : "")
                                    color: Theme.muted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.subject
                                    color: Theme.muted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }

                            // 状态
                            ColumnLayout {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: Math.max(builtText.implicitWidth,
                                                                runText.implicitWidth)
                                spacing: 2

                                Text {
                                    id: builtText
                                    text: modelData.built ? "已构建 " + modelData.builtAt : "未构建"
                                    color: modelData.built ? Theme.success : Theme.muted
                                    font.pixelSize: 10
                                    wrapMode: Text.NoWrap
                                }

                                Text {
                                    id: runText
                                    text: modelData.running ? "运行中  pid " + modelData.pid : "未运行"
                                    color: modelData.running ? Theme.cyan : Theme.muted
                                    font.pixelSize: 10
                                    wrapMode: Text.NoWrap
                                }
                            }

                            // 操作
                            RowLayout {
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 12

                                Text {
                                    text: "构建"
                                    color: buildMouse.containsMouse ? Theme.cyan : Theme.text
                                    font.pixelSize: 11
                                    MouseArea {
                                        id: buildMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            previewPanel.logAgentId = modelData.id
                                            previewHub.build(modelData.id)
                                        }
                                    }
                                }

                                Text {
                                    text: "启动"
                                    color: startMouse.containsMouse ? Theme.cyan
                                           : (modelData.built ? Theme.text : Theme.muted)
                                    font.pixelSize: 11
                                    MouseArea {
                                        id: startMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            previewPanel.logAgentId = modelData.id
                                            previewHub.start(modelData.id)
                                        }
                                    }
                                }

                                Text {
                                    text: "停止"
                                    color: stopMouse.containsMouse ? Theme.danger
                                           : (modelData.running ? Theme.text : Theme.muted)
                                    font.pixelSize: 11
                                    MouseArea {
                                        id: stopMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            previewPanel.logAgentId = modelData.id
                                            previewHub.stop(modelData.id)
                                        }
                                    }
                                }

                                Text {
                                    text: "出包"
                                    color: packageMouse.containsMouse ? Theme.cyan : Theme.text
                                    font.pixelSize: 11
                                    MouseArea {
                                        id: packageMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            previewPanel.logAgentId = modelData.id
                                            previewHub.enqueuePackage(modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---------- 构建日志 ----------
            // 没有日志时整块不占位，把高度让给列表
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                visible: logArea.text.length > 0
                radius: 10
                color: Theme.panel
                border.width: 1
                border.color: Theme.border

                Flickable {
                    id: logFlick
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    contentWidth: logFlick.width
                    contentHeight: logArea.height
                    boundsBehavior: Flickable.StopAtBounds

                    Text {
                        id: logArea
                        width: logFlick.width
                        wrapMode: Text.WordWrap
                        // 顺带读一次 agents：buildLog 不是可通知属性，
                        // 借 agents 的变更信号让日志随构建进度刷新
                        text: {
                            previewHub.agents
                            return previewPanel.logAgentId
                                ? previewHub.buildLog(previewPanel.logAgentId) : ""
                        }
                        color: Theme.muted
                        font.pixelSize: 10
                        font.family: "Consolas"
                    }
                }
            }

            // ---------- 打包队列 ----------
            RowLayout {
                Layout.fillWidth: true
                visible: previewHub.packageQueue.length > 0
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: {
                        var queue = previewHub.packageQueue
                        var parts = []
                        for (var i = 0; i < queue.length; ++i) {
                            var job = queue[i]
                            var mark = job.state === "done" ? "✓"
                                     : job.state === "failed" ? "✗"
                                     : job.state === "running" ? "▶" : "…"
                            parts.push(mark + " " + job.agentId)
                        }
                        return "打包队列：" + parts.join("   ")
                    }
                    color: Theme.muted
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }

                Text {
                    text: "清除已完成"
                    color: clearPackageMouse.containsMouse ? Theme.cyan : Theme.muted
                    font.pixelSize: 10
                    MouseArea {
                        id: clearPackageMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: previewHub.clearFinishedPackages()
                    }
                }
            }

            // ---------- 状态行 ----------
            Item { Layout.fillHeight: true; Layout.preferredHeight: 0 }

            Text {
                Layout.fillWidth: true
                text: previewHub.statusText
                color: Theme.muted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
    }
}
