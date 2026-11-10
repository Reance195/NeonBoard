import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 任务执行分页（R6-01/03/07，MOD-22 自 SettingsPanel.qml 原样迁入）：
// 执行页 ColumnLayout + profile 系函数（editProfile / splitArgs / 两个模式表）
// + profileEditor 编辑弹窗。agentProfile / taskRuns / taskExec 等为上下文属性。
// hostShell = 宿主面板（原 panel，弹窗 parent/width 挂其 contentItem）；
// visible 由实例化处按页绑定。
ColumnLayout {
    property var hostShell
        Layout.fillWidth: true
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
        spacing: 3

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "任务执行"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            Item { Layout.fillWidth: true }

            Switch {
                checked: agentProfile.executorEnabled
                onToggled: agentProfile.setExecutorEnabled(checked)
            }
        }

        Text {
            Layout.fillWidth: true
            text: agentProfile.executorEnabled
                  ? "总开关已开：卡片执行前仍会弹出确认框，cwd 首次执行需勾选「信任本目录」"
                  : "总开关默认关：卡片上的「执行」只提示原因，不会启动任何进程"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8

            Text {
                text: "默认 agent"
                color: Theme.text
                font.pixelSize: Theme.fontCaption
            }

            NeonComboBox {
                id: defaultAgentCombo
                Layout.fillWidth: true
                model: agentProfile.profiles
                labelRole: "displayName"
                emptyText: "（未指定）"
                currentIndex: {
                    const list = agentProfile.profiles
                    for (let i = 0; i < list.length; ++i) {
                        if (list[i].id === agentProfile.defaultAgent)
                            return i
                    }
                    return -1
                }
                onActivated: (index) => agentProfile.setDefaultAgent(model[index].id)
            }
        }

        Repeater {
            model: agentProfile.profiles

            Rectangle {
                id: profileRow
                required property var modelData

                Layout.fillWidth: true
                Layout.topMargin: 2
                implicitHeight: profileRowColumn.implicitHeight + 16
                radius: Theme.radiusControl
                color: Theme.card
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: profileRowColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: profileRow.modelData.displayName
                            color: Theme.text
                            font.pixelSize: Theme.fontCaption
                        }

                        // R9-04：exe 在本机解析不到时显式标注。
                        // 不标的话列表里这一行与可用的长得一样，派活时才失败，
                        // 而失败会被执行器记成「进程启动失败」，看不出根因是 CLI 没装。
                        Rectangle {
                            visible: profileRow.modelData.available === false
                            width: unavailableLabel.implicitWidth + 10
                            height: 15
                            radius: Theme.radiusControl
                            color: Theme.alpha(Theme.warning, 0.14)
                            border.width: 1
                            border.color: Theme.warning

                            Text {
                                id: unavailableLabel
                                anchors.centerIn: parent
                                text: "不可用"
                                color: Theme.warning
                                font.pixelSize: Theme.fontMicro
                            }
                        }

                        Text {
                            text: profileRow.modelData.id
                            color: Theme.muted
                            font.family: "Consolas"
                            font.pixelSize: Theme.fontMicro
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "编辑"
                            color: Theme.cyan
                            font.pixelSize: Theme.fontMicro

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: editProfile(profileRow.modelData)
                            }
                        }

                        Text {
                            text: "删除"
                            color: Theme.magenta
                            font.pixelSize: Theme.fontMicro

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (agentProfile.removeProfile(profileRow.modelData.id))
                                        execSettingsHint.text = "已删除 " + profileRow.modelData.id
                                    else
                                        execSettingsHint.text = "删除失败：" + profileRow.modelData.id
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: profileRow.modelData.exe + " "
                              + (profileRow.modelData.args || []).join(" ")
                              + (profileRow.modelData.promptMode === "stdin"
                                 ? " · prompt 走 stdin" : "")
                              + (profileRow.modelData.available === false
                                 ? " · 已声明但不可用（exe 找不到）" : "")
                        color: profileRow.modelData.available === false
                               ? Theme.warning : Theme.muted
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideRight
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8

            DataButton {
                label: "新增 profile"
                Layout.fillWidth: true
                onTriggered: editProfile(null)
            }

            DataButton {
                label: "恢复内置"
                accentColor: Theme.violet
                Layout.fillWidth: true
                onTriggered: {
                    agentProfile.resetProfiles()
                    execSettingsHint.text = "已恢复内置三条 profile"
                }
            }
        }

        Text {
            id: execSettingsHint
            Layout.fillWidth: true
            text: "profile 存 meta 表；命令行只拼 argv，不经过 shell。codex 未实测，不内置。"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }


    // profile 编辑框：map 为空表示新建
    function editProfile(map) {
        profileEditor.editingId = map && map.id ? map.id : ""
        profileEditor.baseEnv = map && map.env ? map.env : ({ "NO_COLOR": "1" })
        profileIdField.text = map && map.id ? map.id : ""
        profileNameField.text = map && map.displayName ? map.displayName : ""
        profileExeField.text = map && map.exe ? map.exe : ""
        profileArgsField.text = map && map.args ? map.args.join(" ") : ""
        profileReadonlyField.text = map && map.readonlyArgs ? map.readonlyArgs.join(" ") : ""
        profilePromptCombo.currentIndex = Math.max(0, profilePromptModes.indexOf(
            map && map.promptMode ? map.promptMode : "arg"))
        profileOutputCombo.currentIndex = Math.max(0, profileOutputModes.indexOf(
            map && map.output ? map.output : "text"))
        profileTimeoutField.text = String(map && map.defaultTimeoutMin ? map.defaultTimeoutMin : 30)
        profileEditor.errorText = ""
        profileEditor.open()
    }

    property var profilePromptModes: ["arg", "stdin"]
    property var profileOutputModes: ["stream-json", "json", "text"]

    function splitArgs(text) {
        const trimmed = String(text || "").trim()
        return trimmed.length === 0 ? [] : trimmed.split(/\s+/)
    }


    // profile 编辑框（新增 / 修改）；放根层，避免被滚动区裁剪。
    // R11-10：根节点变成 DialogShell（Popup）后，parent 不能再指向 Popup 本身
    //（Popup 不是 QQuickItem，赋过去会报类型错），改指壳的 contentItem ——
    // 位置与原先一致（居中于设置面板），层级也在壳的内容之上。
    Dialog {
        id: profileEditor
        parent: hostShell.contentItem
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 18
        width: Math.min(480, hostShell.width - 60)
        closePolicy: Popup.CloseOnEscape
        property string editingId: ""
        property var baseEnv: ({})
        property string errorText: ""

        background: Rectangle {
            color: Theme.panelSolid
            radius: Theme.radiusPanel
            border.width: 1
            border.color: Theme.alpha(Theme.cyan, 0.35)
        }

        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: profileEditor.editingId.length > 0 ? "编辑 profile" : "新增 profile"
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.bold: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 6

                Text {
                    text: "id"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonField {
                    id: profileIdField
                    enabled: profileEditor.editingId.length === 0
                    placeholderText: "唯一 id（字母/数字/._-）；同名会覆盖"
                }

                Text {
                    text: "显示名"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonField {
                    id: profileNameField
                    placeholderText: "设置面板里显示的名字"
                }

                Text {
                    text: "可执行文件"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonField {
                    id: profileExeField
                    placeholderText: "opencode / claude / 绝对路径"
                }

                Text {
                    text: "参数"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonField {
                    id: profileArgsField
                    placeholderText: "按空格拆分，如 run --format json"
                }

                Text {
                    text: "只读参数"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonField {
                    id: profileReadonlyField
                    placeholderText: "只读模式追加的参数，可空"
                }

                Text {
                    text: "prompt"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonComboBox {
                    id: profilePromptCombo
                    model: profilePromptModes
                }

                Text {
                    text: "输出格式"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                NeonComboBox {
                    id: profileOutputCombo
                    model: profileOutputModes
                }

                Text {
                    text: "默认超时"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }

                RowLayout {
                    spacing: 6

                    NeonField {
                        id: profileTimeoutField
                        Layout.preferredWidth: 70
                        Layout.fillWidth: false
                        horizontalAlignment: TextInput.AlignHCenter
                        validator: IntValidator { bottom: 1; top: 480 }
                    }

                    Text {
                        text: "分钟"
                        color: Theme.muted
                        font.pixelSize: Theme.fontMicro
                    }

                    Item { Layout.fillWidth: true }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: profileEditor.errorText.length > 0
                text: profileEditor.errorText
                color: Theme.danger
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8

                DataButton {
                    label: "删除"
                    accentColor: Theme.magenta
                    visible: profileEditor.editingId.length > 0
                    Layout.preferredWidth: 72
                    onTriggered: {
                        if (agentProfile.removeProfile(profileEditor.editingId)) {
                            execSettingsHint.text = "已删除 " + profileEditor.editingId
                            profileEditor.close()
                        } else {
                            profileEditor.errorText = "删除失败：" + profileEditor.editingId
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                DataButton {
                    label: "取消"
                    Layout.preferredWidth: 72
                    onTriggered: profileEditor.close()
                }

                DataButton {
                    label: "保存"
                    Layout.preferredWidth: 72
                    onTriggered: {
                        const id = profileIdField.text.trim()
                        if (id.length === 0) {
                            profileEditor.errorText = "id 不能为空"
                            return
                        }
                        if (profileExeField.text.trim().length === 0) {
                            profileEditor.errorText = "可执行文件不能为空"
                            return
                        }
                        const timeout = parseInt(profileTimeoutField.text, 10)
                        if (isNaN(timeout) || timeout < 1 || timeout > 480) {
                            profileEditor.errorText = "超时必须是 1–480 的整数"
                            return
                        }
                        const ok = agentProfile.saveProfile({
                            id: id,
                            displayName: profileNameField.text.trim(),
                            exe: profileExeField.text.trim(),
                            args: splitArgs(profileArgsField.text),
                            promptMode: profilePromptModes[profilePromptCombo.currentIndex],
                            readonlyArgs: splitArgs(profileReadonlyField.text),
                            output: profileOutputModes[profileOutputCombo.currentIndex],
                            env: profileEditor.baseEnv,
                            defaultTimeoutMin: timeout
                        })
                        if (!ok) {
                            profileEditor.errorText = "保存失败：请检查 id 与参数格式"
                            return
                        }
                        execSettingsHint.text = "已保存 " + id
                        profileEditor.close()
                    }
                }
            }
        }
    }
}
