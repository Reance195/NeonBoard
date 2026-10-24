import QtQuick
import QtQuick.Window

ShaderEffect {
    id: root

    property bool active: true
    property real iTime: 0
    property vector2d iResolution: Qt.vector2d(Math.max(1, width), Math.max(1, height))
    property vector2d iMouse: Qt.vector2d(0.5, 0.5)
    property real iPulse: 0

    // 场景通道：负载（进行中越满，网格越快）/ 专注（压暗 + 追光）/ 完成脉冲
    property real iState: 0
    property real iFocus: 0
    property real iBurst: 0

    // 主题驱动的着色器参数：切换主题时背景随之渐变
    property color uBase: Theme.bg
    property color uPrimary: Theme.cyan
    property color uSecondary: Theme.magenta
    property color uAccent: Theme.violet
    property real uQuality: Theme.quality

    fragmentShader: "qrc:/shaders/neon.frag.qsb"

    Behavior on iState {
        NumberAnimation {
            duration: Motion.slow
            easing.type: Easing.OutCubic
        }
    }

    Behavior on iFocus {
        NumberAnimation {
            duration: Motion.slow
            easing.type: Easing.InOutSine
        }
    }

    // 完成任务时调用：能量环由中心扩散一次
    function burst() {
        if (Motion.reduced)
            return
        burstAnim.restart()
    }

    SequentialAnimation {
        id: burstAnim

        NumberAnimation {
            target: root
            property: "iBurst"
            to: 1.0
            duration: Motion.instant
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: root
            property: "iBurst"
            to: 0.0
            duration: Motion.slow
            easing.type: Easing.OutCubic
        }
    }

    Timer {
        // 低画质降到 30fps，高画质满帧
        interval: Theme.quality === 0 ? 33 : 16
        running: root.visible && root.active
        repeat: true
        onTriggered: root.iTime += root.interval / 1000.0
    }

    SequentialAnimation on iPulse {
        running: root.visible && root.active
        loops: Animation.Infinite
        NumberAnimation {
            to: 1.0
            duration: 500
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            to: 0.0
            duration: 1100
            easing.type: Easing.InOutSine
        }
        PauseAnimation {
            duration: 4200
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: function(mouse) {
            root.iMouse = Qt.vector2d(
                mouse.x / Math.max(1, root.width),
                mouse.y / Math.max(1, root.height))
        }
    }
}
