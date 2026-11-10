import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 外观与动效分页（MOD-24 自 SettingsPanel.qml 原样迁入）：
// 主题 / 画质只读展示（currentThemeName / currentQualityName 随迁）。
// visible 由实例化处按页绑定。
ColumnLayout {
    // 主题预设当前名（只读展示；换主题在标题栏的主题按钮里）
    readonly property string currentThemeName: {
        const list = Theme.presets
        const preset = list[Theme.presetIndex]
        return preset && preset.name ? preset.name : "（未知主题）"
    }

    // 画质当前档（只读展示）
    readonly property string currentQualityName: Theme.qualityNames[Theme.quality] || ""


        Layout.fillWidth: true
        Layout.leftMargin: Theme.spacingMd
        Layout.rightMargin: Theme.spacingMd
        spacing: Theme.spacingMd

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "主题"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "主题预设"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: currentThemeName
                    color: Theme.cyan
                    font.pixelSize: Theme.fontMicro
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "画质"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: currentQualityName
                    color: Theme.cyan
                    font.pixelSize: Theme.fontMicro
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "减弱动效"
                    color: Theme.text
                    font.pixelSize: Theme.fontCaption
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: Motion.reduced ? "已开启" : "未开启"
                    color: Theme.muted
                    font.pixelSize: Theme.fontMicro
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: "完整的外观面板在标题栏的主题按钮里，那里的主题是色卡网格。"
            color: Theme.muted
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
}
