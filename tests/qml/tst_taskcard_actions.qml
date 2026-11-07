import QtQuick
import QtTest

// TaskCard 交互回归夹具（DX-102）。
//
// 这里复刻的是 TaskCard.qml 的层级与事件路由结构，而不是整张卡片：
//   · 内容层 inner 声明在前，内部有按钮 MouseArea；
//   · 全表面 cardMouse 声明在后，负责整卡点击 / 双击 / 拖拽；
//   · sweepBand / sweepAnimation 两个 id 用于钉住「id 点号访问恒 undefined」。
// 结构一旦回归（内容层被覆盖 / 用点号访问 id），对应用例会直接变红。
// 真实卡片上的同形陷阱由 scripts/check_qml_pitfalls.py 静态扫描兜底。
Rectangle {
    id: root

    width: 320
    height: 200

    property int buttonClicks: 0
    property int cardClicks: 0
    property int doubleClicks: 0
    property int dragStarts: 0
    property bool raiseContent: true

    Item {
        id: sweepBand
        width: 10
        height: 10

        Item {
            id: sweepAnimation
            property bool running: false
        }
    }

    Item {
        id: inner
        width: parent.width
        height: 80
        // 与 TaskCard.qml 的 inner 同义：内容层必须高于覆盖层，否则按钮 press 被吞。
        z: root.raiseContent ? 1 : 0

        Rectangle {
            width: 60
            height: 30
            color: "steelblue"

            MouseArea {
                anchors.fill: parent
                onClicked: root.buttonClicks += 1
            }
        }
    }

    MouseArea {
        id: cardMouse
        anchors.fill: parent
        property point pressPoint
        property bool dragging: false
        property bool draggedSincePress: false

        onClicked: {
            if (!draggedSincePress)
                root.cardClicks += 1
        }
        onDoubleClicked: root.doubleClicks += 1
        onPressed: function(mouse) {
            pressPoint = Qt.point(mouse.x, mouse.y)
            dragging = false
            draggedSincePress = false
        }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            var delta = Math.abs(mouse.x - pressPoint.x) + Math.abs(mouse.y - pressPoint.y)
            if (!dragging && delta > 8) {
                dragging = true
                draggedSincePress = true
                root.dragStarts += 1
            }
        }
        onReleased: function(mouse) {
            dragging = false
        }
    }

    TestCase {
        name: "TaskCardActions"
        when: windowShown

        function test_buttonClick() {
            root.buttonClicks = 0
            root.cardClicks = 0
            mouseClick(inner.children[0], 10, 10)
            compare(root.buttonClicks, 1)
            compare(root.cardClicks, 0)
        }

        function test_cardClick() {
            root.cardClicks = 0
            mouseClick(root, 200, 150)
            compare(root.cardClicks, 1)
        }

        function test_doubleClick() {
            root.doubleClicks = 0
            mouseDoubleClickSequence(root, 200, 150)
            compare(root.doubleClicks, 1)
        }

        function test_drag() {
            root.dragStarts = 0
            root.cardClicks = 0
            mouseDrag(root, 200, 150, 30, 0)
            compare(root.dragStarts, 1)
            compare(root.cardClicks, 0)
        }

        function test_contentOverlayTrap() {
            root.raiseContent = false
            root.buttonClicks = 0
            root.cardClicks = 0
            mouseClick(inner.children[0], 10, 10)
            compare(root.buttonClicks, 0)
            compare(root.cardClicks, 1)
            root.raiseContent = true
        }

        function test_idDottedAccessIsUndefined() {
            var probe = sweepBand.sweepAnimation
            verify(probe === undefined)
        }
    }
}
