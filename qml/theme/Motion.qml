pragma Singleton

import QtQuick
import QtCore

// 动效基线：全应用的时长与缓动令牌。
// 组件只允许引用这里的常量，禁止再写魔法数字，保证「每一帧顺滑」有统一标尺。
QtObject {
    id: root

    // 减弱动效（无障碍 / 低性能设备）：开启后所有时长归零，只保留状态切换
    property bool reduced: false
    property bool persistedReduced: false

    Settings {
        id: settings
        category: "appearance"
        property bool reducedMotion: false
    }

    onPersistedReducedChanged: settings.reducedMotion = persistedReduced
    Component.onCompleted: {
        persistedReduced = settings.reducedMotion
        reduced = settings.reducedMotion
    }

    // ---- 基础时长（毫秒）----
    readonly property int instant: reduced ? 0 : 90     // 按下反馈
    readonly property int fast: reduced ? 0 : 150       // 悬停
    readonly property int base: reduced ? 0 : 220        // 常规转场
    readonly property int slow: reduced ? 0 : 360        // 大面积位移
    readonly property int enter: reduced ? 0 : 260       // 进场
    readonly property int exit: reduced ? 0 : 180        // 退场
    readonly property int panel: reduced ? 0 : 240        // 面板展开
    readonly property int theme: reduced ? 0 : 260        // 主题换色
    readonly property int toast: reduced ? 0 : 240        // 撤销提示
    readonly property int stagger: reduced ? 0 : 40       // 列表错峰

    // ---- 缓动 ----
    readonly property int easeOut: Easing.OutCubic
    readonly property int easeInOut: Easing.InOutSine
    readonly property int easeSpring: Easing.OutBack
    readonly property int easeLinear: Easing.Linear
}
