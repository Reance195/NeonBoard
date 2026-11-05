# R11 批次：界面精致化立卡清单（看板主体 / 主题氛围 / 面板弹窗 / 空态细节）

授权来源：维护者 2026-11-05 本会话问卷明确授权代登记，并授权一次立一批子卡。
维护者当场选定：范围 = **全部**（看板主体 + 霓虹氛围 + 面板弹窗 + 空态细节，分批交付）；
方向 = **精致化**（保留赛博霓虹风格，不换皮，但降噪、提对比、加层级）。

起因（现状核对，2026-11-05）：
1. 看板主体：卡片底色 `Theme.card` alpha 仅 0.72，正文与背景对比偏弱；卡片辅助信息
   字号 10 px 与标题 13 px 层级太近；列内间距是写死的数字，与全局节奏不一致。
2. 霓虹氛围：辉光与描边强度偏高（`Theme.borderStrong` 0.40 / `glow` 0.35），多张卡片
   同时发亮时信息被光吃掉 —— 要的是「霓虹但不吵」。
3. 面板弹窗：设置 / 插件 / 命令面板 / 卡片详情等二级界面各自写死颜色与圆角，
   与看板主体的令牌体系脱节。
4. 空态细节：空列只有一行灰字，没有引导；搜索无结果、概览无数据都没有对应空态。

目标：不动数据结构、不换主题风格、不改交互语义，只把「看起来」这一层做扎实 ——
统一令牌、拉开层级、收敛噪点、补齐空态。
边界：不碰 `src/**`（无 C++ 改动）、不改数据库与任务模型、不引入新依赖；
「精致化」不等于堆特效，辉光只许降不许加。

```json
{
  "R11-05": {
    "stage": "R11",
    "title": "主题令牌与霓虹氛围：统一 spacing/typography 令牌，辉光降噪",
    "note": "起因（2026-11-05 维护者授权批次）：看板主体与二级界面各写各的间距与字号，辉光与描边强度偏高，多卡同屏时信息被光吃掉。目标：(1) 在 qml/theme/Theme.qml 补齐 spacing / typography 两组令牌，并提升卡片底色不透明度（card / cardHover）让卡片从背景里立起来；(2) 收敛辉光与描边强度（borderStrong / glow / accentSoft），保留赛博霓虹但降噪；(3) 在 qml/theme/Motion.qml 补 glow 强度档与 reduced 降级档；(4) qml/NeonBackground.qml 与 shaders/neon.frag 只做保守参数调整，不改场景结构。边界：不动 src/**、不动数据库、不加依赖；辉光只降不升。",
    "acceptance": [
      "qml/theme/Theme.qml 新增 spacing 与 typography 两组令牌：rg 'property int spacing|property int font' qml/theme/Theme.qml 有匹配",
      "卡片底色不透明度提升：qml/theme/Theme.qml 中 Theme.card 的 alpha >= 0.85、Theme.cardHover 的 alpha >= 0.9（按 Qt.rgba 第 4 个参数断言）",
      "辉光降噪：qml/theme/Theme.qml 中 Theme.glow 的 alpha <= 0.28 且 Theme.borderStrong 的 alpha <= 0.32（改动前分别为 0.35 / 0.40）",
      "qml/theme/Motion.qml 新增 glow 强度与 reduced 降级档：rg 'property.*glow|reduced' qml/theme/Motion.qml 有匹配",
      "6 套内置预设全部走同一套令牌，rg 'builtinPresets' qml/theme/Theme.qml 命中且预设键名仍为 6 套（neon/violet/sunset/abyss/forest/mono）",
      "pwsh -File scripts/verify.ps1 全过（build + ctest + selftest + smoke），NeonBoard.exe --smoke 日志无 QML 错误"
    ],
    "scope": [
      "qml/theme/Theme.qml",
      "qml/theme/Motion.qml",
      "qml/NeonBackground.qml",
      "shaders/neon.frag"
    ],
    "depends": []
  },
  "R11-04": {
    "stage": "R11",
    "title": "看板主体精致化：卡片层级、字号阶梯、列内密度",
    "note": "起因（2026-11-05 维护者授权批次）：看板是每天要看十几小时的一屏，当前卡片与背景对比偏弱、正文 10 px 与标题 13 px 层级太近、列内间距是散落的写死数字。目标：卡片标题/辅助信息字号阶梯拉开（正文最小 11 px）、圆角与内边距改走 Theme 令牌、列内间距与卡片间距统一取自 spacing 档位、顶栏统计带与阶段条的分隔线只取令牌不再写 rgba 字面量。边界：只动 qml/components 四个文件，不改数据结构与交互语义，不删任何现有徽章。依赖 R11-05 的令牌先行。",
    "acceptance": [
      "qml/components/TaskCard.qml 正文最小字号提升到 11：rg 'font.pixelSize: 10' qml/components/TaskCard.qml 无匹配",
      "qml/components/TaskCard.qml 的圆角与内边距取自 Theme 令牌：rg 'Theme.radiusCard' qml/components/TaskCard.qml 有匹配",
      "qml/components/BoardColumn.qml 列内间距取自 spacing 令牌：rg 'Theme.spacing' qml/components/BoardColumn.qml 有匹配，且不再出现写死的 spacing 数字",
      "qml/components/TopStatBar.qml 与 qml/components/StageBar.qml 的颜色只取 Theme 令牌：rg '#[0-9A-Fa-f]{6}|Qt.rgba' qml/components/TopStatBar.qml qml/components/StageBar.qml 无匹配",
      "现有徽章一个不少：项目角标 / 父任务 / 领用 / 标签 / 重复 / 到期 / 成本 / 执行态 8 类在 qml/components/TaskCard.qml 里仍有对应节点（rg 计数 >= 8）",
      "pwsh -File scripts/verify.ps1 全过（build + ctest + selftest + smoke），NeonBoard.exe --smoke 日志无 QML 错误"
    ],
    "scope": [
      "qml/components/TaskCard.qml",
      "qml/components/BoardColumn.qml",
      "qml/components/TopStatBar.qml",
      "qml/components/StageBar.qml"
    ],
    "depends": ["R11-05"]
  },
  "R11-06": {
    "stage": "R11",
    "title": "面板与弹窗统一：二级界面全部走同一套令牌",
    "note": "起因（2026-11-05 维护者授权批次）：设置 / 插件 / 命令面板 / 卡片详情等二级界面各自写死颜色与圆角，和看板主体的令牌体系脱节 —— 换主题时它们不跟着变。目标：这些面板的底色、描边、圆角、字号统一取自 Theme 令牌；面板标题层级一致；清掉硬编码颜色字面量。边界：不动交互与数据流，不改插件与市场逻辑。依赖 R11-05 的令牌先行。",
    "acceptance": [
      "面板硬编码颜色清零：rg '#[0-9A-Fa-f]{6}' qml/components/SettingsPanel.qml qml/components/PluginPanel.qml qml/components/PluginLibraryPanel.qml qml/components/CommandPalette.qml qml/components/CardDetailPanel.qml qml/components/AppearancePanel.qml qml/components/ThemePicker.qml qml/components/ThemeMarketPanel.qml qml/components/PreviewPanel.qml qml/components/ProjectMapPanel.qml qml/components/OnboardingOverlay.qml qml/components/ActivityHeatmap.qml qml/components/ColumnManager.qml 无匹配",
      "面板圆角统一取 Theme.radiusPanel：rg 'radius: [0-9]' qml/components/SettingsPanel.qml qml/components/CommandPalette.qml qml/components/CardDetailPanel.qml 无匹配",
      "面板底色统一取 Theme.panel / Theme.panelSolid：rg 'color: Qt.rgba' qml/components/CommandPalette.qml qml/components/CardDetailPanel.qml 无匹配",
      "换主题后二级界面跟着变色：qml/components/SettingsPanel.qml 等面板不再订阅固定颜色，改由 Theme 令牌驱动（rg 'Theme\\.' 命中数 >= 5）",
      "pwsh -File scripts/verify.ps1 全过（build + ctest + selftest + smoke），NeonBoard.exe --smoke 日志无 QML 错误"
    ],
    "scope": [
      "qml/components/SettingsPanel.qml",
      "qml/components/PluginPanel.qml",
      "qml/components/PluginLibraryPanel.qml",
      "qml/components/CommandPalette.qml",
      "qml/components/CardDetailPanel.qml",
      "qml/components/AppearancePanel.qml",
      "qml/components/ThemePicker.qml",
      "qml/components/ThemeMarketPanel.qml",
      "qml/components/PreviewPanel.qml",
      "qml/components/ProjectMapPanel.qml",
      "qml/components/OnboardingOverlay.qml",
      "qml/components/ActivityHeatmap.qml",
      "qml/components/ColumnManager.qml"
    ],
    "depends": ["R11-05"]
  },
  "R11-07": {
    "stage": "R11",
    "title": "空态与细节反馈：空列表有引导，筛选有回声",
    "note": "起因（2026-11-05 维护者授权批次）：空列只有一行灰字，搜索无结果 / 概览无数据也没有对应反馈，用户不知道是该继续输入还是该换条件。目标：新增 qml/components/EmptyState.qml 统一空态组件（图标 + 主文案 + 副文案 + 可选行动按钮），在主界面搜索无结果、录入框空态、统计芯片与工具按钮细节处复用；把「已完成 X / 待做 Y」写回需求卡 note，并同步 README 与路线图的界面描述。边界：不改数据流与筛选逻辑，只改「没有内容时给人看什么」。",
    "acceptance": [
      "新增 qml/components/EmptyState.qml 统一空态组件，含图标 / 主文案 / 副文案 / 可选行动按钮四段结构",
      "主界面搜索无结果走空态组件：rg 'EmptyState' qml/Main.qml 有匹配",
      "qml/Main.qml 的颜色字面量清零：rg '#[0-9A-Fa-f]{6}' qml/Main.qml 无匹配（除注释与 URL 之外）",
      "录入框与统计芯片的细节反馈统一取 Theme 令牌：rg 'Theme\\.' qml/components/TaskComposer.qml qml/components/StatChip.qml qml/components/TopToolButton.qml 命中数各 >= 3",
      "文档同步：README.md 与 docs/iteration-roadmap.md 写回本批「已完成 X / 待做 Y」与新的界面描述",
      "pwsh -File scripts/verify.ps1 全过（build + ctest + selftest + smoke），NeonBoard.exe --smoke 日志无 QML 错误"
    ],
    "scope": [
      "qml/components/EmptyState.qml",
      "qml/components/TaskComposer.qml",
      "qml/components/StatChip.qml",
      "qml/components/TopToolButton.qml",
      "qml/Main.qml",
      "README.md",
      "docs/iteration-roadmap.md"
    ],
    "depends": ["R11-05"]
  }
}
```
