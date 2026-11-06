import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 组件行为：Bound —— 委托里的 preview 才能拿到根对象实例的属性。
// 根对象是 DialogShell 的实例，不加这句的话委托里拿到的是「类型」，属性全是 undefined。
pragma ComponentBehavior: Bound

// 预览中心（R11-10）：迁到统一弹窗壳 DialogShell —— lg 720 档。
//
// 规格 §3.4 的落位：工作区列表行加「未推送提交」提示位（§7.2.1 的回收判据之一）——
// 以前这个信息只在日志里，看列表判断不了工作区能不能回收。
DialogShell {
    id: preview

    titleText: "预览中心"
    subtitleText: "各 agent 工作区 · 构建 · 启动 · 出包"
    sizeMode: "lg"

    // 最近操作的 agent，构建日志显示它的
    property string logAgentId: ""

    // 右上角关闭键：只关闭，不表态
    onCloseRequested: preview.close()

    // 打开时刷新工作区列表（原先在自绘的 open() 里做）
    onOpened: previewHub.refresh()

    // 「未推送提交」判据：条目里带 unpushed 才算数，模型没给就 -1（不显示徽章）
    function unpushedCount(agent) {
        if (!agent)
            return -1
        const n = agent.unpushed
        if (n === undefined || n === null)
            return -1
        const v = Number(n)
        return isNaN(v) ? -1 : v
    }

    // ---------- 正文 ----------
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingMd

        Text {
            Layout.fillWidth: true
            text: previewHub.available
                  ? "各 agent 工作区的构建与预览实例。预览实例走独立沙箱，不影响本机真实数据。"
                  : "未定位到仓库根目录：请设置环境变量 NEONBOARD_REPO_ROOT 指向仓库根目录后重启。"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: "回收判据：未推送提交 · 未提交改动 · 游离 HEAD —— 三项全 0 才可回收工作区。"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        // ---------- agent 列表（自己滚，日志始终留在视野里）----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 280
            radius: Theme.radiusCard
            color: Theme.panel
            border.width: 1
            border.color: Theme.border

            ListView {
                id: agentList
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: Theme.spacingSm
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
                    radius: Theme.radiusControl
                    color: Theme.card
                    border.width: 1
                    border.color: Theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSm
                        spacing: Theme.spacingSm

                        // 名称 / 分支 / 最近提交
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                spacing: Theme.spacingSm

                                Text {
                                    text: modelData.id
                                    color: Theme.text
                                    font.pixelSize: Theme.fontBody
                                    font.bold: true
                                }

                                Rectangle {
                                    visible: modelData.current
                                    radius: Theme.radiusControl
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
                                        font.pixelSize: Theme.fontMicro
                                    }
                                }

                                Rectangle {
                                    visible: modelData.dirty
                                    radius: Theme.radiusControl
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
                                        font.pixelSize: Theme.fontMicro
                                    }
                                }

                                // 未推送提交（规格 §3.4）：回收判据之一，模型给了数才显示
                                Rectangle {
                                    visible: preview.unpushedCount(modelData) > 0
                                    radius: Theme.radiusControl
                                    color: Theme.alpha(Theme.warning, 0.14)
                                    border.width: 1
                                    border.color: Theme.alpha(Theme.warning, 0.40)
                                    height: 16
                                    width: unpushedTag.implicitWidth + 10

                                    Text {
                                        id: unpushedTag
                                        anchors.centerIn: parent
                                        text: "未推送 " + preview.unpushedCount(modelData)
                                        color: Theme.warning
                                        font.pixelSize: Theme.fontMicro
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: (modelData.detached ? "(游离 HEAD) " : "") + modelData.branch
                                      + (modelData.head ? "  ·  " + modelData.head : "")
                                color: Theme.muted
                                font.pixelSize: Theme.fontMicro
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.subject
                                color: Theme.muted
                                font.pixelSize: Theme.fontMicro
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
                                font.pixelSize: Theme.fontMicro
                                wrapMode: Text.NoWrap
                            }

                            Text {
                                id: runText
                                text: modelData.running ? "运行中  pid " + modelData.pid : "未运行"
                                color: modelData.running ? Theme.cyan : Theme.muted
                                font.pixelSize: Theme.fontMicro
                                wrapMode: Text.NoWrap
                            }
                        }

                        // 操作
                        RowLayout {
                            Layout.alignment: Qt.AlignVCenter
                            spacing: Theme.spacingMd

                            Text {
                                text: "构建"
                                color: buildMouse.containsMouse ? Theme.cyan : Theme.text
                                font.pixelSize: Theme.fontCaption
                                MouseArea {
                                    id: buildMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        preview.logAgentId = modelData.id
                                        previewHub.build(modelData.id)
                                    }
                                }
                            }

                            Text {
                                text: "启动"
                                color: startMouse.containsMouse ? Theme.cyan
                                       : (modelData.built ? Theme.text : Theme.muted)
                                font.pixelSize: Theme.fontCaption
                                MouseArea {
                                    id: startMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        preview.logAgentId = modelData.id
                                        previewHub.start(modelData.id)
                                    }
                                }
                            }

                            Text {
                                text: "停止"
                                color: stopMouse.containsMouse ? Theme.danger
                                       : (modelData.running ? Theme.text : Theme.muted)
                                font.pixelSize: Theme.fontCaption
                                MouseArea {
                                    id: stopMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        preview.logAgentId = modelData.id
                                        previewHub.stop(modelData.id)
                                    }
                                }
                            }

                            Text {
                                text: "出包"
                                color: packageMouse.containsMouse ? Theme.cyan : Theme.text
                                font.pixelSize: Theme.fontCaption
                                MouseArea {
                                    id: packageMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        preview.logAgentId = modelData.id
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
            radius: Theme.radiusCard
            color: Theme.panel
            border.width: 1
            border.color: Theme.border

            Flickable {
                id: logFlick
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
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
                        return preview.logAgentId
                            ? previewHub.buildLog(preview.logAgentId) : ""
                    }
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                    font.family: "Consolas"
                }
            }
        }

        // ---------- 打包队列 ----------
        RowLayout {
            Layout.fillWidth: true
            visible: previewHub.packageQueue.length > 0
            spacing: Theme.spacingSm

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
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideRight
            }

            Text {
                text: "清除已完成"
                color: clearPackageMouse.containsMouse ? Theme.cyan : Theme.muted
                font.pixelSize: Theme.fontMicro
                MouseArea {
                    id: clearPackageMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: previewHub.clearFinishedPackages()
                }
            }
        }

        // ---------- 状态行 ----------
        Text {
            Layout.fillWidth: true
            text: previewHub.statusText
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            elide: Text.ElideRight
        }
    }

    // ---------- 底栏左侧：刷新 ----------
    footerExtra: Component {
        TopToolButton {
            text: "刷新"
            description: "刷新工作区列表"
            onClicked: previewHub.refresh()
        }
    }
}
