import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 项目全景侧边浮层（看板侧栏）。
//
// 设计原则：面板不存任何「手写事实」，只做聚合 + 对账——
//   里程碑 ← docs/iteration-roadmap.md §9（表里有几行就有几行）× 台账 × 结卡证据
//   链路   ← docs/project-map.json 的声明 + 每个节点 evidence 是否真实存在（缺一个就断链）
//   骨架   ← 真实目录扫描（文件数 / 行数 / 最近提交）
//   体检   ← 上述几层互相矛盾的地方（宣称完成但没结卡、台账有卡但路线图没写…）
Rectangle {
    id: root
    color: "transparent"

    signal requestClose()

    property string view: "milestones"      // milestones | chain | skeleton | drift | detail
    property string detailFrom: "milestones"
    property var detail: ({})

    // 把 C++ 侧的 revision 拉进来当依赖，refresh() 后所有列表与状态色都会重算
    property int rev: projectMap.revision

    readonly property var msList: { var dep = rev; return dep >= -1 ? projectMap.milestones() : [] }
    readonly property var chainList: { var dep = rev; return dep >= -1 ? projectMap.chain() : [] }
    readonly property var skelList: { var dep = rev; return dep >= -1 ? projectMap.skeleton() : [] }
    readonly property var driftList: { var dep = rev; return dep >= -1 ? projectMap.drift() : [] }
    readonly property var detailCards: (detail && detail.cards) ? detail.cards : []

    function cardStatus(code) {
        var dep = rev
        return dep >= -1 ? (projectMap.card(code).status || "todo") : "todo"
    }
    function cardStale(code) {
        var dep = rev
        return dep >= -1 ? (projectMap.card(code).staleBaseline === true) : false
    }
    function openDetail(from, d) {
        root.detailFrom = from
        root.detail = d
        root.view = "detail"
    }
    function claimLabel(c) {
        if (c === "done") return "宣称完成"
        if (c === "partial") return "部分完成"
        if (c === "planning") return "规划中"
        return "未宣称"
    }
    function claimColor(c) {
        if (c === "done") return Theme.success
        if (c === "partial") return Theme.warning
        return Theme.muted
    }
    function levelColor(l) {
        if (l === "error") return Theme.danger
        if (l === "warn") return Theme.warning
        return Theme.muted
    }

    Component.onCompleted: projectMap.refresh()

    // 背景遮罩：点外侧关闭
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.32
        MouseArea { anchors.fill: parent; onClicked: root.requestClose() }
    }

    // 右侧浮层
    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: Math.min(560, Math.max(400, parent.width * 0.46))
        // 用不透明底：面板叠在看板上，半透明会让卡片文字透上来
        color: Theme.panelSolid
        border.width: 1
        border.color: Theme.border

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ===== 头部 =====
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                Layout.leftMargin: 14
                Layout.rightMargin: 10
                spacing: 8

                Text { text: "项目全景"; color: Theme.text; font.pixelSize: 15; font.bold: true }
                Text {
                    text: projectMap.busy ? "派生中…" : "跟随仓库实时派生"
                    color: projectMap.busy ? Theme.cyan : Theme.muted
                    font.pixelSize: 10
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "↻"
                    color: refreshArea.containsMouse ? Theme.text : Theme.cyan
                    font.pixelSize: 14
                    MouseArea {
                        id: refreshArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: projectMap.refresh()
                    }
                }
                Text {
                    text: "✕"
                    color: closeArea.containsMouse ? Theme.cyan : Theme.text
                    font.pixelSize: 13
                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.requestClose()
                    }
                }
            }

            // 仓库上下文：分支 · HEAD
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.bottomMargin: 6
                text: projectMap.sourcePath
                      ? (projectMap.branch + " · " + projectMap.head)
                      : (projectMap.busy ? "正在定位仓库…" : "未找到仓库（docs/board-ledger.json）")
                color: Theme.muted
                font.pixelSize: 10
                font.family: "Consolas, monospace"
                elide: Text.ElideRight
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

            // ===== 分段 =====
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                Layout.topMargin: 4
                Layout.bottomMargin: 4
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                spacing: 6

                Repeater {
                    model: [
                        { key: "milestones", label: "里程碑", n: root.msList.length, alarm: 0 },
                        { key: "chain", label: "链路", n: root.chainList.length, alarm: projectMap.errorCount },
                        { key: "skeleton", label: "骨架", n: root.skelList.length, alarm: 0 },
                        { key: "drift", label: "体检", n: projectMap.driftCount, alarm: projectMap.errorCount }
                    ]
                    Rectangle {
                        Layout.preferredWidth: 78
                        Layout.preferredHeight: 28
                        radius: 7
                        color: root.view === modelData.key ? Theme.cyan : "transparent"
                        border.width: root.view === modelData.key ? 0 : 1
                        border.color: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: modelData.label + (modelData.alarm > 0
                                                     ? (" !" + modelData.alarm)
                                                     : (modelData.n > 0 ? (" " + modelData.n) : ""))
                            color: root.view === modelData.key ? "#04121f" : Theme.text
                            font.pixelSize: 11
                            font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { root.view = modelData.key; root.detail = ({}) }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: projectMap.refreshedAt && !projectMap.busy ? projectMap.refreshedAt : ""
                    color: Theme.muted
                    font.pixelSize: 9
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

            // ===== 内容 =====
            ScrollView {
                id: scroller
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: scroller.availableWidth
                    spacing: 0

                    Item { Layout.fillWidth: true; Layout.preferredHeight: 10 }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        spacing: 8

                        Text {
                            visible: projectMap.busy
                            Layout.fillWidth: true
                            text: "正在派生：读路线图 §9 → 扫 git 结卡标记 → 核对链路证据 → 统计目录"
                            color: Theme.cyan
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }

                        // ---------- 里程碑 ----------
                        ColumnLayout {
                            visible: root.view === "milestones"
                            Layout.fillWidth: true
                            spacing: 6

                            Repeater {
                                model: root.msList
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    radius: 9
                                    color: msArea.containsMouse ? Theme.elevated : Theme.card
                                    border.width: 1
                                    border.color: (modelData.driftLevel === "warn"
                                                   || modelData.driftLevel === "error")
                                                  ? Theme.warning : Theme.border

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 10

                                        Rectangle {
                                            Layout.preferredWidth: 34
                                            Layout.preferredHeight: 34
                                            radius: 8
                                            color: modelData.claim === "planning" ? Theme.border : Theme.cyan
                                            Text {
                                                anchors.centerIn: parent
                                                text: modelData.stage
                                                color: modelData.claim === "planning" ? Theme.text : "#04121f"
                                                font.pixelSize: 11
                                                font.bold: true
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1
                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData.topic
                                                color: Theme.text
                                                font.pixelSize: 12
                                                font.bold: true
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: root.claimLabel(modelData.claim)
                                                      + " · 共 " + modelData.total + " 卡"
                                                      + (modelData.doing > 0 ? (" · 进行 " + modelData.doing) : "")
                                                      + (modelData.driftText ? (" · ⚠ " + modelData.driftText) : "")
                                                color: modelData.driftText ? Theme.warning : Theme.muted
                                                font.pixelSize: 9
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: 64
                                            Layout.preferredHeight: 6
                                            radius: 3
                                            color: Theme.border
                                            Rectangle {
                                                anchors.left: parent.left
                                                anchors.top: parent.top
                                                anchors.bottom: parent.bottom
                                                width: parent.width
                                                       * (modelData.total > 0
                                                          ? modelData.done / modelData.total : 0)
                                                radius: 3
                                                color: (modelData.total > 0 && modelData.done === modelData.total)
                                                       ? Theme.success : Theme.cyan
                                            }
                                        }

                                        Text {
                                            text: modelData.done + "/" + modelData.total
                                            color: Theme.text
                                            font.pixelSize: 10
                                            font.bold: true
                                        }
                                        Text { text: "▸"; color: Theme.muted; font.pixelSize: 11 }
                                    }

                                    MouseArea {
                                        id: msArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            var d = { kind: "milestone" }
                                            for (var k in modelData)
                                                d[k] = modelData[k]
                                            root.openDetail("milestones", d)
                                        }
                                    }
                                }
                            }
                        }

                        // ---------- 链路 ----------
                        ColumnLayout {
                            visible: root.view === "chain"
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                Layout.fillWidth: true
                                Layout.bottomMargin: 4
                                text: "链路 = 声明（docs/project-map.json）+ 实证（证据文件真的在）"
                                color: Theme.muted
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }

                            Repeater {
                                model: root.chainList
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Layout.preferredHeight: 62

                                    Rectangle {
                                        Layout.preferredWidth: 28
                                        Layout.fillHeight: true
                                        color: "transparent"
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: 2
                                            color: Theme.border
                                            visible: index !== root.chainList.length - 1
                                        }
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 22
                                            height: 22
                                            radius: 11
                                            color: modelData.ok ? Theme.cyan : Theme.danger
                                            Text {
                                                anchors.centerIn: parent
                                                text: modelData.ok ? (index + 1) : "!"
                                                color: "#04121f"
                                                font.pixelSize: 11
                                                font.bold: true
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 54
                                        radius: 9
                                        color: nodeArea.containsMouse ? Theme.elevated : Theme.card
                                        border.width: 1
                                        border.color: modelData.ok ? Theme.border : Theme.danger

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 10
                                            anchors.topMargin: 6
                                            spacing: 2
                                            Text {
                                                text: modelData.title
                                                color: Theme.text
                                                font.pixelSize: 12
                                                font.bold: true
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData.ok
                                                      ? (modelData.tag + " · 证据齐备（"
                                                         + modelData.evidence.length + "）")
                                                      : (modelData.tag + " · 缺 " + modelData.missing + " 个文件")
                                                color: modelData.ok ? Theme.muted : Theme.danger
                                                font.pixelSize: 9
                                                font.family: "Consolas, monospace"
                                                elide: Text.ElideMiddle
                                            }
                                        }

                                        MouseArea {
                                            id: nodeArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.openDetail("chain", {
                                                kind: "chain",
                                                title: modelData.title,
                                                tag: modelData.tag,
                                                desc: modelData.desc,
                                                command: modelData.command,
                                                anchor: modelData.anchor,
                                                evidence: modelData.evidence,
                                                ok: modelData.ok
                                            })
                                        }
                                    }
                                }
                            }
                        }

                        // ---------- 骨架 ----------
                        ColumnLayout {
                            visible: root.view === "skeleton"
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                Layout.fillWidth: true
                                text: "真实目录扫描：文件数 / 行数 / 最近提交。改仓库结构，这里跟着变。"
                                color: Theme.muted
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }

                            Repeater {
                                model: root.skelList
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    radius: 9
                                    color: skArea.containsMouse ? Theme.elevated : Theme.card
                                    border.width: 1
                                    border.color: Theme.border

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        anchors.topMargin: 6
                                        spacing: 2
                                        RowLayout {
                                            spacing: 8
                                            Text {
                                                text: modelData.group
                                                color: Theme.text
                                                font.pixelSize: 12
                                                font.bold: true
                                            }
                                            Text {
                                                text: modelData.files + " 文件 · " + modelData.loc + " 行"
                                                color: Theme.cyan
                                                font.pixelSize: 9
                                                font.family: "Consolas, monospace"
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.lastCommit
                                                  ? ("最近：" + modelData.lastCommit
                                                     + "  (" + modelData.lastWhen + ")")
                                                  : "无提交记录"
                                            color: Theme.muted
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                        }
                                    }

                                    MouseArea {
                                        id: skArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.openDetail("skeleton", {
                                            kind: "group",
                                            title: modelData.group,
                                            desc: modelData.desc,
                                            paths: modelData.paths,
                                            files: modelData.files,
                                            loc: modelData.loc,
                                            lastCommit: modelData.lastCommit,
                                            lastWhen: modelData.lastWhen,
                                            top: modelData.top
                                        })
                                    }
                                }
                            }
                        }

                        // ---------- 体检 ----------
                        ColumnLayout {
                            visible: root.view === "drift"
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                Layout.fillWidth: true
                                text: projectMap.driftCount === 0
                                      ? "四层事实源一致：路线图宣称、台账、git 结卡标记、真实文件全部对得上。"
                                      : "发现 " + projectMap.driftCount
                                        + " 处不一致。error = 结构断了，warn = 宣称与事实不符。"
                                color: projectMap.driftCount === 0 ? Theme.success : Theme.muted
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }

                            Repeater {
                                model: root.driftList
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: driftContent.implicitHeight + 14
                                    radius: 8
                                    color: Theme.card
                                    border.width: 1
                                    border.color: modelData.level === "error"
                                                  ? Theme.danger
                                                  : (modelData.level === "warn" ? Theme.warning : Theme.border)

                                    ColumnLayout {
                                        id: driftContent
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.title
                                            color: Theme.text
                                            font.pixelSize: 11
                                            font.bold: true
                                            wrapMode: Text.Wrap
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.detail
                                            color: root.levelColor(modelData.level)
                                            font.pixelSize: 9
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                }
                            }
                        }

                        // ---------- 细节 ----------
                        ColumnLayout {
                            visible: root.view === "detail"
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 28
                                radius: 7
                                color: "transparent"
                                border.width: 1
                                border.color: Theme.border
                                Text {
                                    anchors.centerIn: parent
                                    text: "‹ 返回"
                                    color: Theme.text
                                    font.pixelSize: 11
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { root.view = root.detailFrom; root.detail = ({}) }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.detail.title || root.detail.code || ""
                                color: Theme.text
                                font.pixelSize: 14
                                font.bold: true
                                wrapMode: Text.Wrap
                            }

                            // 里程碑徽章
                            RowLayout {
                                visible: root.detail.kind === "milestone"
                                spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: 42
                                    Layout.preferredHeight: 20
                                    radius: 6
                                    color: Theme.cyan
                                    Text {
                                        anchors.centerIn: parent
                                        text: root.detail.stage || ""
                                        color: "#04121f"
                                        font.pixelSize: 10
                                        font.bold: true
                                    }
                                }
                                Text {
                                    text: root.claimLabel(root.detail.claim)
                                    color: root.claimColor(root.detail.claim)
                                    font.pixelSize: 10
                                    font.bold: true
                                }
                                Text {
                                    text: (root.detail.done || 0) + "/" + (root.detail.total || 0)
                                    color: Theme.text
                                    font.pixelSize: 10
                                    font.family: "Consolas, monospace"
                                }
                                Text {
                                    text: root.detail.gitDone !== undefined ? ("git 结卡 " + root.detail.gitDone) : ""
                                    color: Theme.muted
                                    font.pixelSize: 9
                                }
                            }

                            // 卡片徽章
                            RowLayout {
                                visible: root.detail.kind === "card"
                                spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: 52
                                    Layout.preferredHeight: 20
                                    radius: 6
                                    color: root.detail.status === "done"
                                           ? Theme.success
                                           : (root.detail.status === "doing" ? Theme.warning : Theme.border)
                                    Text {
                                        anchors.centerIn: parent
                                        text: root.detail.status === "done"
                                              ? "已完成"
                                              : (root.detail.status === "doing" ? "进行中" : "待办")
                                        color: root.detail.status === "todo" ? Theme.text : "#04121f"
                                        font.pixelSize: 10
                                        font.bold: true
                                    }
                                }
                                Text {
                                    text: root.detail.statusSource === "git"
                                          ? "依据 git #=done"
                                          : (root.detail.statusSource === "ledger" ? "依据台账" : "")
                                    color: Theme.muted
                                    font.pixelSize: 9
                                }
                                Text {
                                    text: root.detail.code || ""
                                    color: Theme.muted
                                    font.pixelSize: 10
                                    font.family: "Consolas, monospace"
                                }
                                Text {
                                    visible: root.detail.staleBaseline === true
                                    text: "baseline 陈旧"
                                    color: Theme.warning
                                    font.pixelSize: 9
                                }
                            }

                            // ===== 里程碑细节 =====
                            Text {
                                visible: root.detail.kind === "milestone"
                                Layout.fillWidth: true
                                text: root.detail.claimText || ""
                                color: Theme.muted
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }
                            SectionLabel { visible: root.detail.kind === "milestone"; text: "目标" }
                            Text {
                                visible: root.detail.kind === "milestone"
                                Layout.fillWidth: true
                                text: root.detail.goal || ""
                                color: Theme.text
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                            SectionLabel { visible: root.detail.kind === "milestone"; text: "出口标准" }
                            Text {
                                visible: root.detail.kind === "milestone"
                                Layout.fillWidth: true
                                text: root.detail.exit || ""
                                color: Theme.text
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                            SectionLabel { visible: root.detail.kind === "milestone" && root.detailCards.length > 0; text: "卡片" }
                            Repeater {
                                model: root.detail.kind === "milestone" ? root.detailCards : []
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 26
                                    radius: 6
                                    color: cardArea.containsMouse ? Theme.elevated : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        spacing: 8
                                        Text {
                                            text: modelData
                                            color: Theme.text
                                            font.pixelSize: 10
                                            font.family: "Consolas, monospace"
                                        }
                                        Text {
                                            visible: root.cardStale(modelData)
                                            text: "baseline 陈旧"
                                            color: Theme.warning
                                            font.pixelSize: 9
                                        }
                                        Item { Layout.fillWidth: true }
                                        Rectangle {
                                            Layout.preferredWidth: 8
                                            Layout.preferredHeight: 8
                                            radius: 4
                                            color: {
                                                var s = root.cardStatus(modelData)
                                                if (s === "done")
                                                    return Theme.success
                                                if (s === "doing")
                                                    return Theme.warning
                                                return Theme.border
                                            }
                                        }
                                    }
                                    MouseArea {
                                        id: cardArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            var c = projectMap.card(modelData)
                                            c.kind = "card"
                                            root.openDetail("milestones", c)
                                        }
                                    }
                                }
                            }

                            // ===== 卡片细节 =====
                            Text {
                                visible: root.detail.kind === "card"
                                Layout.fillWidth: true
                                text: root.detail.note || ""
                                color: Theme.muted
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }
                            SectionLabel { visible: root.detail.kind === "card"; text: "验收命令" }
                            Repeater {
                                visible: root.detail.kind === "card"
                                model: root.detail.kind === "card" ? (root.detail.acceptance || []) : []
                                Text {
                                    Layout.fillWidth: true
                                    text: "• " + modelData
                                    color: Theme.text
                                    font.pixelSize: 10
                                    font.family: "Consolas, monospace"
                                    wrapMode: Text.Wrap
                                }
                            }
                            SectionLabel { visible: root.detail.kind === "card"; text: "改动范围" }
                            Repeater {
                                visible: root.detail.kind === "card"
                                model: root.detail.kind === "card" ? (root.detail.scope || []) : []
                                Text {
                                    Layout.fillWidth: true
                                    text: "• " + modelData
                                    color: Theme.muted
                                    font.pixelSize: 10
                                    font.family: "Consolas, monospace"
                                    wrapMode: Text.Wrap
                                }
                            }
                            SectionLabel { visible: root.detail.kind === "card"; text: "依赖" }
                            Text {
                                visible: root.detail.kind === "card"
                                Layout.fillWidth: true
                                text: Array.isArray(root.detail.depends)
                                      ? root.detail.depends.join("  ·  ") : ""
                                color: Theme.text
                                font.pixelSize: 10
                                font.family: "Consolas, monospace"
                                wrapMode: Text.Wrap
                            }

                            // ===== 链路细节 =====
                            Text {
                                visible: root.detail.kind === "chain"
                                Layout.fillWidth: true
                                text: root.detail.desc || ""
                                color: Theme.text
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                            SectionLabel { visible: root.detail.kind === "chain"; text: "入口命令" }
                            Text {
                                visible: root.detail.kind === "chain"
                                Layout.fillWidth: true
                                text: root.detail.command || "—"
                                color: Theme.cyan
                                font.pixelSize: 10
                                font.family: "Consolas, monospace"
                                wrapMode: Text.Wrap
                            }
                            SectionLabel { visible: root.detail.kind === "chain"; text: "证据文件" }
                            Repeater {
                                visible: root.detail.kind === "chain"
                                model: root.detail.kind === "chain" ? (root.detail.evidence || []) : []
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text {
                                        text: modelData.exists ? "✓" : "✕"
                                        color: modelData.exists ? Theme.success : Theme.danger
                                        font.pixelSize: 10
                                        font.bold: true
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.path
                                        color: modelData.exists ? Theme.muted : Theme.danger
                                        font.pixelSize: 10
                                        font.family: "Consolas, monospace"
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }

                            // ===== 骨架细节 =====
                            Text {
                                visible: root.detail.kind === "group"
                                Layout.fillWidth: true
                                text: (root.detail.desc || "") + "  ·  " + (root.detail.files || 0)
                                      + " 文件 / " + (root.detail.loc || 0) + " 行"
                                color: Theme.text
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                            Text {
                                visible: root.detail.kind === "group"
                                Layout.fillWidth: true
                                text: Array.isArray(root.detail.paths) ? root.detail.paths.join("  ·  ") : ""
                                color: Theme.muted
                                font.pixelSize: 10
                                font.family: "Consolas, monospace"
                                wrapMode: Text.Wrap
                            }
                            SectionLabel { visible: root.detail.kind === "group"; text: "最大的 5 个文件" }
                            Repeater {
                                visible: root.detail.kind === "group"
                                model: root.detail.kind === "group" ? (root.detail.top || []) : []
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        color: Theme.text
                                        font.pixelSize: 10
                                        font.family: "Consolas, monospace"
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        text: modelData.loc + " 行"
                                        color: Theme.muted
                                        font.pixelSize: 9
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillWidth: true; Layout.preferredHeight: 12 }
                }
            }
        }
    }

    // 小节标签
    component SectionLabel: Text {
        color: Theme.cyan
        font.pixelSize: 10
        font.bold: true
        Layout.topMargin: 6
    }
}
