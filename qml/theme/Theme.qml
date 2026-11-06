pragma Singleton

import QtQuick

// 主题引擎 v1：颜色令牌 + 预设切换 + 换色过渡动画。
// 全应用只从这里取色（Theme.xxx），组件不允许再写死颜色常量。
QtObject {
    id: root

    // ================= 主题预设 =================
    // 6 套内置预设，覆盖不同「气质」：冷色科技 / 暖色活力 / 中性克制
    readonly property var builtinPresets: [
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

    // 外部主题包（R8-01）：把 themes/ 目录里的 themepack.json 追加在内置预设之后。
    // 撞 key 的包直接丢掉 —— 内置那 6 套是「一定存在」的锚点，不许被同名包顶掉，
    // 否则用户装了个叫 neon 的包，重启后外观突然变了却找不到原因。
    readonly property var presets: {
        const out = builtinPresets.slice()
        const external = typeof themePacks !== "undefined" ? themePacks.packs() : []
        for (let i = 0; i < external.length; ++i) {
            const pack = external[i]
            let duplicate = false
            for (let j = 0; j < out.length; ++j) {
                if (out[j].key === pack.key) {
                    duplicate = true
                    break
                }
            }
            if (!duplicate)
                out.push(pack)
        }
        return out
    }

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
    //
    // R11-05「精致化」的取法是**降噪 + 提对比**，不是加特效：
    //   · 卡片底色从 0.72 提到 0.88 —— 卡片原先几乎溶进背景，正文读起来发虚；
    //   · 辉光与强描边从 0.35 / 0.40 降到 0.26 / 0.30 —— 多卡同屏时光会盖住字。
    readonly property color border: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.16)
    readonly property color borderStrong: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.30)
    readonly property color panel: Qt.rgba(bg.r + 0.012, bg.g + 0.026, bg.b + 0.050, 0.68)
    readonly property color panelSolid: Qt.rgba(bg.r + 0.018, bg.g + 0.038, bg.b + 0.070, 0.96)
    readonly property color card: Qt.rgba(bg.r + 0.020, bg.g + 0.048, bg.b + 0.092, 0.88)
    readonly property color cardHover: Qt.rgba(bg.r + 0.050, bg.g + 0.098, bg.b + 0.180, 0.96)
    readonly property color input: Qt.rgba(bg.r + 0.010, bg.g + 0.030, bg.b + 0.060, 0.78)
    readonly property color elevated: Qt.rgba(bg.r + 0.055, bg.g + 0.105, bg.b + 0.190, 0.96)
    readonly property color ghost: Qt.rgba(bg.r + 0.040, bg.g + 0.096, bg.b + 0.181, 0.96)
    readonly property color glow: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.26 * Motion.glowScale)
    readonly property color accentSoft: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.14)

    // R11-02：面板描边层级与分组分隔线。
    // 顶栏 / 统计横幅原先各自写死 rgba(Theme.cyan, 0.10~0.32)，改主题时不会跟着走；
    // 这里把「面板级细线 / 分组分隔线 / 各类柔和底色」收成令牌，组件只取名不取字面量。
    readonly property color hairline: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.10)
    readonly property color divider: Qt.rgba(text.r, text.g, text.b, 0.10)
    readonly property color glowSoft: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.18 * Motion.glowScale)
    readonly property color dangerSoft: Qt.rgba(danger.r, danger.g, danger.b, 0.16)
    readonly property color warningSoft: Qt.rgba(warning.r, warning.g, warning.b, 0.16)
    readonly property color successSoft: Qt.rgba(success.r, success.g, success.b, 0.16)

    // 圆角阶梯：面板 12 / 卡片 10 / 控件 8 —— 全应用只用这三档。
    readonly property int radiusPanel: 12
    readonly property int radiusCard: 10
    readonly property int radiusControl: 8

    // 间距阶梯（R11-05）：全应用只从这五档里取间距。
    // 以前每个组件各写各的数字（8 / 10 / 12 / 14 / 16 / 18…），同一屏里节奏对不上；
    // 现在统一成 4 / 8 / 12 / 16 / 24 五档，组件只取名不取字面量。
    readonly property int spacingXs: 4     // 徽章内边距、图标与文字之间
    readonly property int spacingSm: 8     // 卡片之间、控件内边距
    readonly property int spacingMd: 12    // 面板内边距、列内外边距
    readonly property int spacingLg: 16    // 分组之间
    readonly property int spacingXl: 24    // 大块留白

    // 字号阶梯（R11-05）：从「看不清」到「主标题」共五档。
    // 10 px 及以下在 100% 缩放下已经开始发虚（这是实测，不是审美偏好），
    // 所以正文最小 11 px，标题 15 px，页面级标题 18 px。
    readonly property int fontMicro: 10    // 极小装饰（计数角标、图标旁的说明）
    readonly property int fontCaption: 11  // 卡片辅助信息（标签 / 到期 / 成本）
    readonly property int fontBody: 13     // 正文、列表项
    readonly property int fontTitle: 15    // 卡片标题、面板标题
    readonly property int fontDisplay: 18  // 页面级标题

    // 弹窗壳令牌（R11-09）：统一弹窗与抽屉的尺寸档、遮罩与阴影。
    // 规格见 docs/design/ui-relayout-spec.md §4.2 —— 这 7 个是本次唯一动 Theme 的地方，
    // 其余重排全部靠布局重组完成，不另立第二份尺寸事实源。
    readonly property int dialogWidthSm: 440   // 单字段表单（新建任务、权限授权）
    readonly property int dialogWidthMd: 560   // 内容型（卡片详情、主题市场、执行相关）
    readonly property int dialogWidthLg: 720   // 工作型（设置、命令面板、插件库、预览中心）
    readonly property int drawerWidth: 320     // 右侧抽屉（外观、插件面板、项目全景、列管理）
    readonly property int dialogPadding: 20    // 弹窗内边距
    readonly property real scrimOpacity: 0.62  // 遮罩不透明度
    readonly property color shadowDialog: Qt.rgba(0, 0, 0, 0.55) // 弹窗阴影（与浮层阴影分离）

    // 半透明派生的统一入口：组件要「某色按 alpha 摊平」时走这里，
    // 而不是自己写 Qt.rgba(...) —— 于是「颜色只从 Theme 出」这条规矩
    // 在 grep 口径上也能验（rg 'Qt.rgba' qml/components 无匹配）。
    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

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
        // 主题包被删掉之后（或换了一台机器），存着的 key 找不到：
        // 退回第一套内置预设，别停在一个已经不存在的外观上。
        if (presets.length > 0 && key !== presets[0].key)
            applyPresetByKey(presets[0].key, animate)
    }

    function nextPreset() {
        applyPreset((presetIndex + 1) % presets.length)
    }

    function setQuality(level) {
        quality = Math.max(0, Math.min(2, level))
    }
}
