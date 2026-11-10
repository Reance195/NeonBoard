import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 面板内复用的下拉框（MOD-18 自 SettingsPanel.qml 内联组件原样迁出）：
// labelRole 是自定义字段名：ComboBox.textRole 是 FINAL，不能覆盖。
ComboBox {
    id: combo
        property string labelRole: ""
        property string emptyText: ""
        Layout.preferredHeight: 30
        font.pixelSize: Theme.fontMicro

        // displayText 对 QVariantList<map> 会退化成空串，必须按 labelRole 自己取
        readonly property string currentLabel: {
            if (combo.currentIndex < 0 || combo.currentIndex >= combo.count)
                return ""
            const item = combo.model[combo.currentIndex]
            if (combo.labelRole.length > 0 && item)
                return item[combo.labelRole] !== undefined ? String(item[combo.labelRole]) : ""
            return combo.displayText
        }

        contentItem: Text {
            leftPadding: 8
            rightPadding: 20
            text: combo.currentIndex >= 0 ? combo.currentLabel : combo.emptyText
            color: combo.currentIndex >= 0 ? Theme.text : Theme.muted
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.input
            border.width: 1
            border.color: Theme.border
        }

        delegate: ItemDelegate {
            width: combo.width
            height: 28
            contentItem: Text {
                leftPadding: 8
                text: combo.labelRole.length > 0 ? modelData[combo.labelRole] : modelData
                color: Theme.text
                font.pixelSize: Theme.fontMicro
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: parent.highlighted ? Theme.accentSoft : "transparent"
            }
        }

        popup: Popup {
            y: combo.height + 2
            width: combo.width
            implicitHeight: contentItem.implicitHeight
            padding: 1
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            background: Rectangle {
                radius: Theme.radiusControl
                color: Theme.panelSolid
                border.width: 1
                border.color: Theme.border
            }
        }
    }
