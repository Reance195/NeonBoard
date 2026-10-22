# NeonBoard 迭代路线图（炫酷优先）

> 数据基准：GitHub star 快照 2026-10-21；竞品功能与本仓库代码现状均已核对。
> 定位：**单文件、离线、开机即用的赛博霓虹个人工作台**。

## 0. 三条不可妥协的原则

1. **第一眼惊艳** —— 启动画面、主界面、托盘、弹窗，任何一屏截图都必须能直接当壁纸发出去。
2. **每一帧顺滑** —— 炫酷的反面是卡顿。当前启动 9696 ms / 56 fps 是硬伤，掉帧的霓虹不如普通界面。
3. **每次操作有反馈** —— 拖动、勾选、删除、撤销、提醒，都要有动效 + 声音/光效，且动效不阻塞操作（≤ 250 ms）。

在此之上，功能是底座：**不炫的功能也要有，否则用两天就卸载；不酷的功能再好也留不住人。**
本路线图把“炫酷”拆成三层推进：

```text
P0 炫酷地基（不修，炫不起来）
   ├─ 启动性能 9.7s → <1.5s
   ├─ 帧率与画质分级（60fps 保底）
   ├─ 动效基线 + 组件拆分
   └─ 主题引擎 v1（多套配色 + 强调色）

P0.5 日常底座（不补，做不了主力工具）
   ├─ 提醒 / 逾期 / 重复任务
   ├─ 子任务与卡片详情
   ├─ 导入导出 + 自动备份
   ├─ 快速录入解析 + 命令面板
   └─ 列内精确插位 + 通用撤销

P1 炫酷记忆点（差异化，别人抄不走）
   ├─ Shader 场景化（列状态 / 专注模式 / 脉冲）
   ├─ 统计可视化（热力图、连续天数、吞吐曲线）
   ├─ 专注模式 + 番茄钟（官方插件）
   ├─ 主题包市场
   └─ 拖拽手感（抬升、磁吸、落点涟漪、光轨）

P2 炫酷生态
   ├─ 插件平台 2.0（热重载 / 设置页 / 命令 / 事件）
   ├─ 动态托盘 + 窗口边缘辉光提醒
   └─ 启动序列动画 + 可选音效
```

## 1. 竞品地图

| 分层 | 项目 | Stars | 定位 | 借鉴 / 不借鉴 |
| --- | --- | --- | --- | --- |
| 直接同类 | [Kanri](https://github.com/kanriapp/kanri) | 2.1k | Tauri 离线看板，理念“只做好一件事，永不做云同步” | 借鉴：自定义列、子任务、单文件存储、Trello 导入、键盘流。**不借鉴**：不追多端同步 |
| 直接同类 | Task Coach | 42 | 20 年老牌桌面任务管理 | 借鉴：子任务、时间追踪、重复、提醒、日历/时间线视图 |
| 直接同类 | yamlist | 99 | Windows 玻璃拟态任务板 + 日历 + 笔记 | 借鉴：本地优先 + 桌面原生；**它靠 macOS 风玻璃，与我们赛博路线正面冲突，是我们要超越的对象** |
| 任务增强 | [Super Productivity](https://github.com/super-productivity/super-productivity) | 23k | 时间盒 + 时间追踪 + Jira/GitHub 集成 | 借鉴：番茄钟、专注、每日计划、键盘优先、issue 导入 |
| 任务增强 | [Vikunja](https://github.com/go-vikunja/vikunja) | 5.8k | 自托管任务管理 | 借鉴：多视图（看板/列表/日历/甘特）、提醒、保存过滤器、CalDAV、导入 |
| 任务增强 | [Tasks.org](https://github.com/tasks/tasks) / [Taskwarrior](https://github.com/GothenburgBitFactory/taskwarrior) | 5.7k / 6.1k | 重复规则、提醒、上下文、紧急度 | 借鉴：**RRULE 标准重复规则**、上下文/标签、紧迫度排序 |
| 团队看板（只借交互） | Focalboard / Planka / Wekan / Kanboard | 26.5k / 12.6k / 21k / 9.9k | 团队协作看板 | 借鉴：自定义字段、泳道、WIP 限制、自动化规则、规则引擎。**不借鉴**：成员/评论/OIDC/权限矩阵 |
| 知识工作台（只借理念） | AppFlowy / AFFiNE / SiYuan / Trilium | 77k / 73k / 47k / 38k | 本地优先知识库 + AI | 借鉴：本地优先、插件/主题生态、AI 走插件而非核心 |

**信号解读（很重要）**：
- 5.2k star 的 Taskcafe 2023 年停更、1.1k 的 PomodoroLogger 也停更 → **重功能、团队化路线维护成本极高**。
- 2.1k 的 Kanri 十年如一日地活着，定位是“离线、只做一件事” → **小而聚焦 + 审美独特 = 活得久**。
- 结论：不和它们比功能数量，比**质感、速度、氛围**。

## 2. 能力差距矩阵

| 能力 | NeonBoard 现状 | 竞品普遍性 | 判定 |
| --- | --- | --- | --- |
| 视觉风格 | 霓虹 + 玻璃 + Shader，全 GitHub 独一份 | 几乎无同类 | **我们的护城河，要持续加码** |
| 启动速度 | 9696 ms | Kanri/Tauri 秒开 | **致命短板，优先修** |
| 帧率 | 56 fps 采样 | 应 60 fps 保底 | 需画质分级 |
| 动效 | 仅 2 处 Behavior（hover 缩放 1.012、颜色 160 ms） | 主流产品动效丰富 | 差距巨大，投入产出比最高 |
| 提醒 / 逾期 | 只有 `due_at` 展示，无通知 | 几乎全部具备 | 必补 |
| 重复任务 | 无 | Tasks / Vikunja / Task Coach | 必补（用 RRULE） |
| 子任务 / 清单 | 无 | Kanri / Vikunja / Wekan / Kanboard | 必补 |
| 多板 / 自定义列 | 固定三列，DB 里 `CHECK(status IN ('todo','doing','done'))` | Kanri / Focalboard / Vikunja | 架构级解锁 |
| 导入导出 / 备份 | 无（README 已列为限制） | Kanri / Vikunja | 必补（信任感） |
| 命令面板 / 键盘流 | 仅全局热键 + 少量快捷键 | Super Productivity / Kanri | 必补 |
| 日历 / 时间线 | 无 | Vikunja / Focalboard / yamlist | 可选 |
| 时间追踪 / 番茄 | 无 | Super Productivity / Task Coach | 差异化机会（做插件） |
| 插件 | 有 v1（QML 面板 + 权限 + KV + 任务 API） | 同类里几乎没人做 | **第二护城河，值得做深** |
| 团队协作 | 无 | Focalboard / Planka / Wekan | **明确不做** |
| 云同步 | 无 | Vikunja / Super Productivity | 不自建，只做文件级 |
| i18n | 中文单语 | Kanri / Focalboard / Wekan | P3 |

## 3. P0 炫酷地基

### 3.1 启动性能：先纠正认知，再谈优化（R0 已实测 + 已瘦身）

> **状态：诊断完成，QML 树瘦身已完成。** 原判断「冷启动 9.7 s 是设计缺陷」**不成立**——它是**首次运行 / 冷页缓存**的一次性成本。

R0 诊断手段（已落地的工具，保留复用）：

| 手段 | 位置 | 作用 |
| --- | --- | --- |
| 分段打点 | `src/main.cpp` 的 `BOOT db / engine / qml / loaded / attached / shown` + 新增 `BOOT firstframe` | 定位每一段耗时 |
| `--smoke` 旁路单实例 | `src/main.cpp` | 诊断标志在应用运行时也可用（原先被 `QLocalServer` 锁挡掉） |
| `--benchload [minimal\|controls\|button]` | `src/main.cpp` | 分档加载探针 QML，量化各部分开销 |
| `NEONBOARD_NO_BG=1` | `src/main.cpp` → `Main.qml` | A/B 关掉动态背景，区分「背景 shader」与「其余」 |
| `UiSettings` | `src/UiSettings.{h,cpp}` | 外观/画质持久化（QML 侧 `QtCore` 的 `Settings` 在本机不可用） |

实测结论（2026-10-21，Release 构建）：

| 场景 | 结果 |
| --- | --- |
| 首次运行（全新用户，磁盘/页缓存全冷） | `BOOT loaded` 4.8 s，`BOOT shown` 5.9 s，指标 6841 ms |
| 热启动（连续 5 次） | **1.23 – 1.39 s 稳定**，fps 60.0，内存 199–204 MB |
| 瓶颈段 | `engine → loaded`（QML 树构建）**0.80 – 0.94 s**，是最大头 |
| `--benchload` | 最小 QML 仅 112–166 ms（含 Qt Quick 初始化 + 图形设备创建）；`import QtQuick.Controls` 仅多 ~50 ms |
| 背景 shader | **不是**启动瓶颈（关掉后启动耗时无明显变化），但它是 fps 指标的来源 |

#### 已完成：重型面板改为 Loader 按需加载（R0 收官项）

首帧根本用不到的界面，之前却在启动时全部构造。已按需化：

| 面板 | 之前 | 现在 | 位置 |
| --- | --- | --- | --- |
| 设置面板 | `Main.qml` 内联 Rectangle（遮罩 + 居中卡片） | `Loader` 懒加载 `components/SettingsPanel.qml` | 首次「设置」/ `Ctrl+,` 才构造 |
| 插件面板 | `Main.qml` 内联 Rectangle | `Loader` 懒加载 `components/PluginPanel.qml` | 首次点插件才构造，插件 QML 再晚一层 |
| 外观面板 | `ThemePicker.qml` 内联（含 `MultiEffect` 投影 + 6 行预设） | `Loader` 懒加载 `components/AppearancePanel.qml` | 首次点外观按钮才构造 |

要点与坑：

- **Loader 会把自身尺寸强加给被加载项**（`widthValid/heightValid`）→ 定位与尺寸一律由 `Loader` 负责，被加载组件内部不再写 `width/height/anchors`。
  外观面板最初「按钮高亮但面板不显示」，根因就是 `Loader` 只锚定了 `top/right` 而高度为 0 → 面板被压成 0 高；改为 `height: item ? item.implicitHeight : 0` 后正常。
- 外观面板里的 `Layout.fillWidth` 用在普通 `Row` 里会报「不存在的附加对象」→ 整棵 `body` 改为 `ColumnLayout` 体系。
- 新增 `--appearance` 启动参数，用于无鼠标环境下直接打开外观面板做验证。

A/B 实测（`NEONBOARD_NO_BG=1` 关背景，用**进程 CPU 时间**与 `engine→loaded` 树构建耗时衡量，墙钟噪声过大不可用）：

| 版本 | 树构建 `engine→loaded`（avg / min） | 启动总 CPU（avg / min） |
| --- | --- | --- |
| 基线（面板常驻构造） | 1694–2024 ms / 1270 ms | 2380–2552 ms / 1875 ms |
| 按需加载后 | **1448–1851 ms / 781 ms** | **2260–2583 ms / 1344 ms** |

三轮交错 A/B（6 / 12 / 10 轮，其中一轮刻意反序以排除顺序偏差）结论一致：**树构建耗时下降约 250–490 ms（-15% ~ -27%），启动总 CPU 下降约 70–300 ms**，达成「QML 树瘦身 200–400 ms」目标。

剩余可做（优先级从高到低）：

| 子项 | 做法 | 预期 | 状态 |
| --- | --- | --- | --- |
| QML 树瘦身 | 面板类（设置 / 插件 / 外观）改 `Loader` 按需加载 | 200–400 ms | **已完成（实测 -250 ~ -490 ms）** |
| 数据库 | `Database.cpp` 在 UI 线程同步建表/迁移，改为独立线程 + 延后首帧 | 数十 ms | 待做 |
| 插件扫描 | `PluginLoader` 扫描目录在主线程，改为后台扫描，首帧后再注入侧栏 | 数十 ms | 待做（实测当前仅 2–8 ms，优先级下调） |
| 字体 | `Cascadia Code` 缺失时的回退字体加载可能阻塞首帧 | 数十 ms | 待做 |
| Shader 预热 | `.qsb` 首次链接可在 Splash 阶段预热（当前冷态 BOOT shown 比热态多 1–2 s） | 可能秒级 | 待做 |

**验收**：热启动 < 1.5 s（**已达成：1.23–1.39 s**）；首次运行 < 3 s（待优化）；指标面板保留可见（这本身也是炫酷元素）。

### 3.2 帧率与画质分级（已落地三档）

**已实现**：

- `quality` 三档（低 / 中 / 高），由外观面板直接切换，持久化在 `UiSettings`。
- Shader 分支：`shaders/neon.frag` 新增 `uQuality`，低档跳过星星与扫描线，高档额外叠加颗粒。
- 低档时 `NeonBackground` 的推进间隔从 16 ms 放宽到 33 ms（≈30 fps），明显省电。
- 窗口失焦 / 最小化时暂停背景动画（`active: window.active`）。

**剩余**：

- 背景改用 `FrameAnimation`（Qt 6.4+）或 `QQuickWindow::beforeRendering` 推进 `iTime`，替掉 `NeonBackground.qml` 里的 16 ms `Timer`（定时器抖动 + 常驻 CPU）。
- Shader 降采样：低档用 0.5 倍渲染纹理再放大（`ShaderEffect.sourceSize`）。
- 辉光用 `MultiEffect` 按需启用：仅 hover/激活卡片开 `layer.enabled`，避免每张卡都做全屏模糊。
- 主题与画质解耦：主题包（`theme.json`）可自带推荐画质档。

**验收**：1920×1080 常态 60 fps（**已达成：热启动 fps 60.0**）；动画期间无掉帧；低档在集显上 ≥50 fps；纯 CPU 占用 <3%。

### 3.3 动效基线与组件拆分（结构先于堆料）—— **已完成**

原状：`qml/Main.qml` 单文件 1604 行 / 53 KB，TaskCard 是内联 `component`，颜色硬编码在一处。

**实际落地结构**（`Main.qml` 1605 → 约 1180 行）：

```text
qml/
  Main.qml                 窗口骨架、布局、路由、拖拽协调（约 1180 行）
  NeonBackground.qml       霓虹 ShaderEffect 背景（主题色驱动）
  theme/
    Theme.qml              单例：颜色令牌 + 6 套预设 + 换色过渡 + 画质档
    Motion.qml             单例：动效时长/缓动令牌 + 减弱动效开关
  components/
    TaskCard.qml           卡片（进场淡入 / hover 浮起 / 按下微沉 / 动作渐显）
    BoardColumn.qml        列容器 + 快速添加 + 空态
    StatChip.qml           顶部统计芯片
    WinButton.qml          窗口按钮
    ThemePicker.qml        外观面板（预设 / 画质 / 减弱动效，MultiEffect 投影）
```

**约定（继续沿用）**：

- 组件只从 `Theme.qml` 取色，禁止再写死颜色常量；已对 `Main.qml` / `TaskCard` / `WinButton` 做全量令牌化。
- 组件只引用 `Motion.qml` 的时长，禁止魔法数字。
- 组件对外只发信号（`editRequested` / `dragStarted` …），不直接调 `window.*`；坐标换算用注入的 `dragSpace` 属性。

**动效基线（已实现的先落地，其余按序补）**：

| 场景 | 表现 | 时长 | 状态 |
| --- | --- | --- | --- |
| 卡片进场 | 淡入 | 260 ms | ✅ |
| 卡片 hover | 浮起 1.012 + 描边辉光 | 150 ms | ✅ |
| 卡片按下 | 缩放 0.985 | 90 ms | ✅ |
| 动作按钮 | 悬停渐显 | 150 ms | ✅ |
| 主题切换 | 全应用（含背景 shader）颜色渐变 | 260 ms | ✅ |
| 外观面板 | 缩放淡入 + 逐行错峰渐入 | 240 ms | ✅ |
| 拖起 | 卡片放大 1.04 + 拖拽幽灵外扩描边 | 150 ms | ✅ |
| 拖入目标列 | 目标列边框呼吸（accent 0.45→0.80 循环） | 900 ms | ✅ |
| 落位 | 目标列底部涟漪扩散 | 360 ms | ✅ |
| 勾选完成 | 光带横扫卡片（旋转 18° 渐变）后移入已办结 | 260 ms | ✅ |
| 删除 | 故障抖动（横向错位 + 描边转 danger）后移除 | 220 ms | ✅ |
| 撤销 | 卡片从右侧滑回（26 px → 0 + 淡入） | 260 ms | ✅ |
| 新增任务 | 卡片滑入淡入 | 260 ms | ✅ |
| 逾期 | 红色呼吸描边（1.4 s 周期） | 循环 | ✅ |
| 统计变化 | 数字滚动 + 芯片跳动闪光 | 220 ms | ✅ |
| 插入落点 | 霓虹横线指示线实时标出落点（R1-1） | 150 ms | ✅ |

> R0 先上「无粒子、无残影」的轻量版。光轨拖尾、粒子迸发、RGB 三色逐道残影、插入位置虚线、卡片弹性落位归入 P1「拖拽手感升级」。

### 3.4 主题引擎 v1 —— **已完成**

**实际实现**：

- `qml/theme/Theme.qml`（QML Singleton）：颜色令牌（bg / text / muted / cyan / magenta / violet / success / warning / danger + 派生的 border / panel / card / input / accentSoft / elevated / ghost 等），每个令牌带 `Behavior on color`，切换时**全应用渐变**。
- `qml/theme/Motion.qml`（QML Singleton）：动效时长与缓动令牌 + 减弱动效开关。
- **6 套预设**（实际名称）：霓虹青（默认）/ 赛博紫 / 落日橙 / 深海蓝 / 森野绿 / 极夜灰。每套含 bg / text / muted / 三主色 / 成功 / 警告 / 危险色。
- **背景联动**：`shaders/neon.frag` 新增 `uBase / uPrimary / uSecondary / uAccent / uQuality` uniform，霓虹背景随主题一起渐变（这是「炫酷」的关键一环）。
- **入口**：标题栏右侧三色圆点按钮 → 外观面板（预设列表 + 画质三档 + 减弱动效），`MultiEffect` 真投影 + 逐行错峰渐入。
- **持久化**：C++ 侧 `src/UiSettings.{h,cpp}`（QSettings，`appearance/` 段）。**注意**：QML 侧 `import QtCore` 的 `Settings` 在本机不可用（`QtCore` 是磁盘动态插件且无 CMake 目标），故走 C++ 上下文属性 `uiSettings`。
- **颜色令牌化**：`Main.qml` / `TaskCard` / `WinButton` 中所有硬编码 rgba 与字面色值已全部替换为 `Theme.*` 令牌，主题切换时全应用一致生效。
- **6 套预设已逐套实机出图验收**：截图存于 `docs/themes/theme-{neon,violet,sunset,abyss,forest,mono}.png`；并对三列圆点与统计芯片做了**像素采样比对**，确认强调色严格取自对应预设（如 neon 三列 = `#22D3EE / #A78BFA / #F472B6`，abyss = `#38BDF8 / #6366F1 / #2DD4BF`），无残留硬编码。

**剩余**：

- 强调色可单独覆盖（设置页取色器），标题栏、芯片、插件面板、Shader 辉光联动。
- 主题切换 <100 ms（当前 260 ms 渐变，可再压）。
- 预留 `theme.json` 供 P1 主题包与插件贡献。

### 3.5 看板阶段视图 + 路线图双向绑定 —— **已完成**

**问题诊断（来自实际使用反馈：「为啥面板上还没结束」）**

看板本身有两层缺陷，都不是「忘了勾」，而是设计缺口：

1. **同步是单向人工镜像**：`progress.json → board.db` 靠人跑脚本，git 提交不触发；应用里改卡也不回写；两侧不一致时系统没有自我纠正机制。
2. **看板表达能力不足**：卡片平铺，没有「阶段」维度。R0 的 10 张卡散在「已办结」列，R1/R2/R3 平铺在「待办」列，18 张 `#Mxx` 与 9 张粗卡混在一起、标签同为 R3 —— 数据里其实有阶段信息（标签首位），但界面没有聚合视图，「R0 是否收口」根本看不出来。

**A —— 阶段维度（表达力）**

- `TaskModel::stageStats()`：按里程碑聚合 `{total, done, doing, todo}`；阶段由**首个标签**判定（`^R\d+$` → R0..R3，否则 OTHER）。
- `TaskFilterModel` 新增 `stage` 属性，看板三列按阶段过滤。
- `qml/components/StageBar.qml`：看板顶部的里程碑进度条 ——「全部」+ 各阶段 chip（阶段名 / ✓ 完成标记 / `done/total` / 细进度条），点击即按阶段过滤；不同阶段不同强调色（R0 青 / R1 紫 / R2 品红 / 其他绿）。
- `--stage R0` 可在启动时直接进入某个阶段视图（便于脚本化验收）。

**B —— 自动绑定（事实源）**

- **快照格式 v2**：仓库 `progress.json` 改为 `{version, updated, items[]}`，每条含看板 UUID 作 id、完整字段与 epoch 时间戳（`createdAt` / `updatedAt` / `completedAt` / `orderKey` / `stage` / `deleted`）。
- **`src/RoadmapSync.{h,cpp}`**：应用侧读 / 写快照。路径解析优先级：环境变量 `NEONBOARD_ROADMAP` > ini `[roadmap]/path` > 从 exe 目录向上探测含 `.git` 与 `progress.json` 的仓库根。
- **三个触发点**：
  - 启动即导入（文件 → 看板）；
  - 每 15 秒轮询文件 mtime，变了就导入；
  - 看板侧改动 1.5 秒防抖后自动写回文件（看板 → 文件），内容无实质变化时**不写盘**，避免污染 git diff。
- **合并规则**（避免两侧互相覆盖）：
  - 运行中的进程持有基线，能识别「文件被外部改过」→ 这类条目一律以文件为准（不丢人工修改）；
  - 没有基线的冷启动 / CLI 则退化为「`updatedAt` 大者胜」，旧文件不会覆盖看板里的新改动；
  - 文件里有、看板里没有的条目写 `deleted: true`，导入时删除对应卡片。
- **git 钩子**：新增 `.githooks/post-commit`，提交若改到 `progress.json` 且应用未在运行，就用 CLI 把快照灌进看板；应用在运行则由它自己轮询接管。
- **可见性**：进度条右侧常驻同步状态（已连接 / 已对齐 / N 条外部改动），日志同步打印 `Roadmap: synced ...`。

> 手工改 `progress.json` 时请同步抬高该条目的 `updatedAt`，否则会以看板版本为准。

## 4. P0.5 日常底座（功能）

| 方向 | 竞品依据 | 落地要点 | 成本 |
| --- | --- | --- | --- |
| 提醒 + 逾期 | Tasks / Vikunja / Task Coach | `remind_at` + `notified_at`（防重复），`QSystemTrayIcon::showMessage` + 窗口边缘辉光；启动时补发漏提醒 | M |
| 重复任务 | Tasks / Taskwarrior / Task Coach | **存 RFC 5545 RRULE 字符串**（不要自造格式），便于日后导出 ICS / 对接 CalDAV | M |
| 子任务 / 清单 | Kanri / Vikunja / Wekan | **关联部分已完成**：父子关联存独立边表 `task_link`（不动 `task` 表结构、不升 `user_version`，老版本程序照常开库）；子卡缩进 + `↳ 父卡` 标注，父卡显示子任务完成度霓虹进度环；列内按父卡分组排序。`parent_id` 列方案与「清单」留待后续 | M |
| 导入导出 + 自动备份 | Kanri / Vikunja | **已完成（R1-5）**：`src/DataPort` 三格式导出（JSON 带 `schemaVersion` / Markdown / Todo.txt），导入按标题匹配已有任务并 `upsertTask` 落库（不触发快速录入解析）；设置面板「导入导出与备份」区一键导出、选文件导入、立即备份；每天首次启动自动备份 `board.db` 到 `backups/` | S |
| 快速录入解析 | Super Productivity / Todoist 式 | **已完成（R1-2）**：`src/TaskInputParser` 解析「明天 15:00 #工作 !2」→ 到期时间 / 标签 / 优先级；中文相对日期（今天 / 明天 / 后天 / 下周三）与时段（下午3点 / 3点半）；输入时实时预览解析结果 | S–M |
| 命令面板 | Super Productivity / Kanri | **已完成（R1-3）**：`Ctrl+K` 呼出；一条输入框聚合新建任务、按标题跳转到卡片（定位并高亮闪烁）、切换 6 套主题、按阶段过滤、打开插件库 / 设置 / 外观面板；`↑↓` 选择、`Enter` 执行、`Esc` 关闭 | S–M |
| 列内精确插位 | ~~现状“落列尾”（已知限制）~~ | **已完成（R1-1）**：`moveTaskBefore(id, status, beforeId)` 按目标卡片 id 精确插入；`taskIdsInStatus()` 提供与界面一致的 order_key 排序；拖拽时用霓虹横线实时标出落点 | S |
| 通用撤销 | 现状只撤销删除 | `QUndoStack` 化：删除 / 移动 / 完成 / 编辑全部可撤销 | M |

## 5. P1 炫酷记忆点

1. **Shader 场景化**：`shaders/neon.frag` 现为静态星云 + 透视网格 + 扫描线 + 鼠标辉光。扩展 uniform：`iState`（列/全局状态）、`iFocus`（专注强度）、`iQuality`。表现：进行中列越满，网格流速越快；完成任务时背景爆发一次能量脉冲；专注模式下背景压暗、只留一束追光。
2. **专注模式**：一列全屏 + 背景压暗 + 倒计时环 + 托盘剩余时间；对应 Super Productivity 的 timeboxing。
3. **统计可视化**：GitHub 式完成热力图（霓虹格子）、连续天数火焰计数、周吞吐曲线；替换现有静态统计芯片为动画数字。
4. **主题包市场**：主题 JSON + 预览图；P2 开放插件贡献主题。
5. **卡片详情层**：Markdown 笔记、附件、子任务进度环、审计时间线；同样用霓虹玻璃风格。
6. **拖拽手感升级**：落点涟漪、光轨、磁吸、列切换高亮；键盘也能移动（`Ctrl+←/→`）。

## 6. P2 炫酷生态

1. **插件平台 2.0**（`docs/plugin-api.md` 已列明缺口）：
   - ✅ `apiVersion 2` 双兼容（v1 插件零改动继续可用）
   - ✅ 后台常驻插件（manifest 的 `background` 入口，独立 QML 上下文，权限天然隔离）
   - ✅ 任务事件（created / updated / moved / completed / removed）+ 事件总线
   - ✅ `meta` 共享配置（跨插件读写同一份键值）、插件启停、重新扫描（热重载）
   - ⬜ `settings.qml` + 设置页声明
   - ⬜ 命令注册 / 快捷键注册
   - ⬜ 多槽位（侧栏 + 底部面板 + 卡片字段）
   - ⬜ 权限授权弹窗与来源提示（无沙箱时必须补显式授权）
2. **插件库与在线市场**（已落地）：
   - ✅ 侧栏插件库（已装 / 可装汇总，打开 / 启用 / 禁用 / 安装 / 卸载 / 更新）
   - ✅ `.neon` 单文件包格式（manifest + files 映射，零第三方依赖、跨平台）
   - ✅ 四道校验：包格式合法 → `manifest.id` 一致 → SHA-256 一致 → 路径不越界
   - ✅ 临时目录 + 原子替换安装（失败回滚，不留半成品）
   - ✅ 市场索引可配置（内置占位 GitHub 地址）、索引缓存到本地、断网自动降级
   - ⬜ 市场签名与发布者信誉
3. **官方插件示例**（用插件承载工作台能力，避免核心膨胀）：**计划钩子已落地**（`plugins/plan-hook`，见 [docs/plan-hook.md](plan-hook.md)）；待做：番茄钟、日历视图、快速笔记、GitHub Issue 导入。
4. **动态托盘 + 窗口辉光**：托盘图标在有到期任务时脉冲；全局热键唤出窗口用缩放淡入；窗口边缘辉光提醒。
5. **启动序列**：Splash 从静态品牌图升级为终端式 boot 动画（**必须与主界面预热并行，不得延长总启动时间**）。
6. **可选音效**：hover 轻响、完成清脆音、删除故障音；设置可关（无障碍与静音场景）。

## 7. P3 战略（有余力再做）

- **同步**：不做 CRDT。做“单板一文件 + OneDrive / Syncthing / WebDAV 备份”，对齐 Kanri 哲学。
- **系统集成**：ICS 日历订阅（Outlook/Google）、GitHub/Jira Issue 只读导入、Windows 通知/跳转列表。
- **i18n**：中英双语起步，低成本扩受众。
- **AI 走插件**：任务拆解、周报生成、自动打标签（AppFlowy / SiYuan 已在做，我们用插件试水，不进核心）。

## 8. 明确不做

- 团队协作、成员、评论、OIDC、权限矩阵（Focalboard / Planka / Wekan 赛道；Taskcafe 已证明高 star 也会因维护成本停更）。
- 自建云同步 / CRDT。
- Notion 式文档知识库（AppFlowy / AFFiNE / SiYuan 已占据，且与“炫酷工作台”定位冲突）。
- 完整甘特 / 资源管理（企业向，个人场景用不上）。
- 插件市场的**签名与发布者信誉**（包分发链路已落地：`.neon` 单文件包 + 四道校验 + 原子安装；见「6. P2 炫酷生态」第 2 条）。

## 9. 里程碑

| 版本 | 主题 | 内容 | 出口标准 | 进度 |
| --- | --- | --- | --- | --- |
| **R0** | 炫酷地基 | 启动性能剖析与优化、动效基线、组件拆分、Theme 单例、主题引擎 v1 | 热启动 <1.5 s；主题可热切；hover/进场/面板动效上线 | **全部完成**：主题引擎 + 组件拆分 + 画质分档 + 动效链 + 启动瘦身（重型面板按需加载，树构建 -250 ~ -490 ms）✅ |
| **R1** | 日常底座 | 提醒 + 逾期、重复任务（RRULE）、子任务、导入导出 + 备份、快速录入解析、命令面板、列内插位、通用撤销 | 一周日常使用零遗漏；数据可导出可还原 | **进行中**：列内精确插位（R1-1）✅ —— `moveTaskBefore` + 霓虹插入指示线；快速录入解析（R1-2）✅ —— 中文日期/时段/标签/优先级解析 + 输入实时预览；命令面板（R1-3）✅ —— `Ctrl+K` 聚合命令与任务跳转；子任务关联（R1-4）✅ —— 边表 `task_link` + 子卡缩进/归属标注 + 父卡进度环；导入导出 + 自动备份（R1-5）✅ —— 三格式导出 / 按标题匹配导入 / 每日自动备份；其余 3 项（提醒 / RRULE / 通用撤销）待做 ⬜ |
| **R2** | 记忆点 | Shader 场景化、专注模式、统计可视化、主题包、卡片详情、拖拽手感升级 | 截图能直接当壁纸；动效全链路顺滑 | 未开始 |
| **R3** | 生态 | 插件平台 2.0、番茄钟/日历/笔记官方插件、动态托盘与窗口辉光、启动序列、音效 | 插件改一行即生效；主程序零改动可扩展 | **进行中**：插件平台 2.0 底座（apiVersion 2 / 后台常驻 / 事件订阅 / meta / 启停 / 热重载）+ 首个官方插件「计划钩子」已落地 ✅；插件库与在线市场已落地 ✅（`.neon` 单文件包 / SHA-256 校验 / 原子安装 / 离线缓存）；待做：市场签名与更多官方插件 ⬜ |

## 10. 验收指标（“炫酷”可量化）

| 指标 | 目标 | 当前实测 |
| --- | --- | --- |
| 启动到首帧可交互 | 热启动 < 1.5 s；首次运行 < 3 s | 热启动 **1.23 – 1.39 s** ✅；首次运行 5 – 9 s ⬜（冷页缓存，R1 待优化） |
| QML 树构建耗时 | < 800 ms（`engine→loaded`） | 按需加载后 **781 – 1851 ms**（min 781 / avg 1448–1851）⬜ 仍是大头 |
| 常态帧率 | 60 fps @1080p；动画期间不掉帧 | **60.0 fps** ✅（热启动） |
| 主题切换 | < 100 ms，无需重启 | 260 ms 渐变，**无需重启 ✅**、时长待压 ⬜ |
| 反馈延迟 | 任何点击到视觉反馈 < 100 ms；动效 ≤ 250 ms（完成特效 ≤ 320 ms） | hover/按下/面板 均 ≤ 260 ms ✅ |
| 破坏性操作 | 必有动效 + 5 s 内可撤销 | 撤销已实现（Ctrl+Z + Toast）+ 删除故障动效 + 撤销滑回 ✅ |
| 空闲资源 | 最小化后台 CPU < 1% | 未测 ⬜ |
| 内存 | 常驻 < 250 MB | 199 – 230 MB ✅ |
| 截图门槛 | 任意一屏截图可直接发布（无占位、无错位、无模糊） | 外观面板 + **6 套预设逐套出图**（`docs/themes/`）已验证 ✅ |
| 阶段可见性 | 打开看板即知当前里程碑完成度，不需查文档 | 顶部阶段进度条（R0 ✓ 10/10、R1 0/6、R2 0/2、R3 0/19）✅ |
| 看板 ↔ 仓库一致 | 看板改动与仓库快照零差异，无需人工搬运 | 双向同步已打通（启动拉取 / 轮询 / 防抖写回 / 提交钩子），实测 `diffs = 0` ✅ |
| 拖拽落点可见 | 拖到任意两卡之间都能看清会落在哪 | 霓虹插入指示线实时标出落点（R1-1）✅ |
| 浮层唯一 | 同一时刻最多一个面板 | 外观 / 插件 / 设置面板互斥 + 点击外观面板外关闭 ✅ |
| 录入零摩擦 | 敲一行文字即可得到带时间/标签/优先级的任务 | 快速录入解析（R1-2）：`明天 15:00 #工作 !3` → 到期 10-23 15:00 + 标签「工作」+ 高优先，输入时实时预览 ✅ |
| 一键直达 | 一个入口完成绝大多数日常操作 | 命令面板（R1-3）：`Ctrl+K` 呼出，16 项固定命令 + 按标题跳转任务，实机出图验证 ✅ |
| 数据可导出可还原 | 任意时刻能把全部任务导出成通用格式，并能原样导回 | 三格式导出 + 按标题匹配导入（R1-5）：JSON / Markdown / Todo.txt 往返实测 41/41 条一致，导入幂等不重复 ✅；每日自动备份 board.db ✅ |

### 诊断命令速查

```powershell
# 启动性能打点（日志：%APPDATA%\com.lin\NeonBoard\neonboard.log）
$env:QT_LOGGING_RULES = "qt.qml.import.debug=true"   # 仅排查 QML 类型解析时启用
build\Release\NeonBoard.exe --smoke                  # 旁路单实例的冒烟模式
build\Release\NeonBoard.exe --benchload controls     # 分档加载耗时
$env:NEONBOARD_NO_BG = "1"; build\Release\NeonBoard.exe   # 关闭动态背景做 A/B
build\Release\NeonBoard.exe --appearance              # 直接打开外观面板（无鼠标时验证用）
build\Release\NeonBoard.exe --export-tasks out.json json   # 导出全部任务
build\Release\NeonBoard.exe --import-tasks out.json         # 导入（按标题匹配，幂等）
build\Release\NeonBoard.exe --backup                        # 立即备份 board.db
```
