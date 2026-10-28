pragma Singleton

import QtQuick

// 主题引擎 v1：颜色令牌 + 预设切换 + 换色过渡动画。
// 全应用只从这里取色（Theme.xxx），组件不允许再写死颜色常量。
QtObject {
    id: root

    // ================= 主题预设 =================
    // 6 套预设，覆盖不同「气质」：冷色科技 / 暖色活力 / 中性克制
    readonly property var presets: [
        {
            key: "neon",
            name: "霓虹青",
            desc: "经典赛博青",
            bg: "#05060A",
            text: "#E6F1FF",
            muted: "#7C8BA1",
            cyan: "#22D3EE",
            magenta: "#F472B6",
            violet: "#A78BFA",
            success: "#5EEAD4",
            warning: "#FBBF24",
            danger: "#FB7185"
        },
        {
            key: "violet",
            name: "赛博紫",
            desc: "夜幕下的电紫",
            bg: "#0A0614",
            text: "#EFE9FF",
            muted: "#8B81A8",
            cyan: "#B794F6",
            magenta: "#FF6FB5",
            violet: "#7C5CFF",
            success: "#4ADE80",
            warning: "#FCD34D",
            danger: "#FB7185"
        },
        {
            key: "sunset",
            name: "落日橙",
            desc: "黄昏与余温",
            bg: "#120904",
            text: "#FFEFE2",
            muted: "#A08070",
            cyan: "#FFB454",
            magenta: "#FF6B6B",
            violet: "#FFD166",
            success: "#86EFAC",
            warning: "#FDBA74",
            danger: "#F87171"
        },
        {
            key: "abyss",
            name: "深海蓝",
            desc: "静谧的深水",
            bg: "#030A14",
            text: "#E0F2FE",
            muted: "#6B8399",
            cyan: "#38BDF8",
            magenta: "#2DD4BF",
            violet: "#6366F1",
            success: "#34D399",
            warning: "#FBBF24",
            danger: "#F87171"
        },
        {
            key: "forest",
            name: "森野绿",
            desc: "雨后的林间",
            bg: "#04100C",
            text: "#E6FFF4",
            muted: "#6E9384",
            cyan: "#34D399",
            magenta: "#FB7185",
            violet: "#A3E635",
            success: "#A3E635",
            warning: "#FCD34D",
            danger: "#F87171"
        },
        {
            key: "mono",
            name: "极夜灰",
            desc: "克制与专注",
            bg: "#0B0D10",
            text: "#E5E7EB",
            muted: "#8A9199",
            cyan: "#9CA3AF",
            magenta: "#D1D5DB",
            violet: "#6B7280",
            success: "#9CA3AF",
            warning: "#D1D5DB",
            danger: "#EF4444"
        }
    ]

    property int presetIndex: 0
    property string presetKey: "neon"
    // 启动阶段不做换色动画，避免首帧闪一下默认配色
    property bool initializing: true

    // ================= 画质档位 =================
    // 0 低（省电） / 1 中（默认） / 2 高（全特效）
    property int quality: 1
    readonly property var qualityNames: ["低", "中", "高"]

    // ================= 颜色令牌 =================
    property color bg: "#05060A"
    property color text: "#E6F1FF"
    property color muted: "#7C8BA1"
    property color cyan: "#22D3EE"
    property color magenta: "#F472B6"
    property color violet: "#A78BFA"
    property color success: "#5EEAD4"
    property color warning: "#FBBF24"
    property color danger: "#FB7185"

    // 换色动画：所有令牌同时渐变，主题切换 <100ms 内可感知但不刺眼
    Behavior on bg { ColorAnimation { duration: root.initializing ? 0 : Motion.theme; easing.type: Easing.InOutSine } }
    Behavior on text { ColorAnimation { duration: root.initializing ? 0 : Motion.theme; easing.type: Easing.InOutSine } }
    Behavior on muted { ColorAnimation { duration: root.initializing ? 0 : Motion.theme; easing.type: Easing.InOutSine } }
    Behavior on cyan { ColorAnimation { duration: root.initializing ? 0 : Motion.theme; easing.type: Easing.InOutSine } }
    Behavior on magenta { ColorAnimation { duration: root.initializing ? 0 : Motion.theme; easing.type: Easing.InOutSine } }
    Behavior on violet { ColorAnimation { duration: root.initializing ? 0 : Motion.theme; easing.type: Easing.InOutSine } }
    Behavior on success { ColorAnimation { duration: root.initializing ? 0 : Motion.theme; easing.type: Easing.InOutSine } }
    Behavior on warning { ColorAnimation { duration: root.initializing ? 0 : Motion.theme; easing.type: Easing.InOutSine } }
    Behavior on danger { ColorAnimation { duration: root.initializing ? 0 : Motion.theme; easing.type: Easing.InOutSine } }

    // 由主色派生的半透明层级（跟随 bg 与 cyan 自动变化）
    readonly property color border: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.16)
    readonly property color borderStrong: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.40)
    readonly property color panel: Qt.rgba(bg.r + 0.012, bg.g + 0.026, bg.b + 0.050, 0.62)
    readonly property color panelSolid: Qt.rgba(bg.r + 0.018, bg.g + 0.038, bg.b + 0.070, 0.96)
    readonly property color card: Qt.rgba(bg.r + 0.020, bg.g + 0.048, bg.b + 0.092, 0.72)
    readonly property color cardHover: Qt.rgba(bg.r + 0.050, bg.g + 0.098, bg.b + 0.180, 0.82)
    readonly property color input: Qt.rgba(bg.r + 0.010, bg.g + 0.030, bg.b + 0.060, 0.70)
    readonly property color elevated: Qt.rgba(bg.r + 0.055, bg.g + 0.105, bg.b + 0.190, 0.96)
    readonly property color ghost: Qt.rgba(bg.r + 0.040, bg.g + 0.096, bg.b + 0.181, 0.96)
    readonly property color glow: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.35)
    readonly property color accentSoft: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.16)

    // R11-02：面板描边层级与分组分隔线。
    // 顶栏 / 统计横幅原先各自写死 rgba(Theme.cyan, 0.10~0.32)，改主题时不会跟着走；
    // 这里把「面板级细线 / 分组分隔线 / 各类柔和底色」收成令牌，组件只取名不取字面量。
    readonly property color hairline: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.10)
    readonly property color divider: Qt.rgba(text.r, text.g, text.b, 0.10)
    readonly property color glowSoft: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.22)
    readonly property color dangerSoft: Qt.rgba(danger.r, danger.g, danger.b, 0.16)
    readonly property color warningSoft: Qt.rgba(warning.r, warning.g, warning.b, 0.16)
    readonly property color successSoft: Qt.rgba(success.r, success.g, success.b, 0.16)

    // 圆角阶梯：面板 12 / 卡片 10 / 控件 8 —— 全应用只用这三档。
    readonly property int radiusPanel: 12
    readonly property int radiusCard: 10
    readonly property int radiusControl: 8

    // 语义化别名：让组件读起来更自解释
    readonly property color accent: cyan
    readonly property color accent2: magenta
    readonly property color accent3: violet

    // ================= 持久化 =================
    onPresetKeyChanged: {
        if (uiSettings.themePreset !== presetKey)
            uiSettings.themePreset = presetKey
    }
    onQualityChanged: {
        if (uiSettings.quality !== quality)
            uiSettings.quality = quality
    }

    Component.onCompleted: {
        applyPresetByKey(uiSettings.themePreset, false)
        quality = uiSettings.quality
        initializing = false
    }

    // ================= 对外接口 =================
    function applyPreset(index, animate) {
        if (index < 0 || index >= presets.length)
            return
        presetIndex = index
        applyPresetByKey(presets[index].key, animate !== false)
    }

    function applyPresetByKey(key, animate) {
        for (var i = 0; i < presets.length; ++i) {
            var p = presets[i]
            if (p.key !== key)
                continue
            presetIndex = i
            presetKey = p.key
            bg = p.bg
            text = p.text
            muted = p.muted
            cyan = p.cyan
            magenta = p.magenta
            violet = p.violet
            success = p.success
            warning = p.warning
            danger = p.danger
            return
        }
    }

    function nextPreset() {
        applyPreset((presetIndex + 1) % presets.length)
    }

    function setQuality(level) {
        quality = Math.max(0, Math.min(2, level))
    }
}
