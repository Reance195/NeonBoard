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

### 3.1 启动性能：先纠正认知，再谈优化（R0 已实测）

> **状态：已完成诊断，结论已修正。** 原判断「冷启动 9.7 s 是设计缺陷」**不成立**——它是**首次运行 / 冷页缓存**的一次性成本。

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

剩余可做（优先级从高到低）：

| 子项 | 做法 | 预期 |
| --- | --- | --- |
| QML 树瘦身 | 面板类（设置 / 插件 / 编辑弹窗）改 `Loader` 按需加载；组件已拆出，可再下沉 | 200–400 ms |
| 数据库 | `Database.cpp` 在 UI 线程同步建表/迁移，改为独立线程 + 延后首帧 | 数十 ms |
| 插件扫描 | `PluginLoader` 扫描目录在主线程，改为后台扫描，首帧后再注入侧栏 | 数十 ms |
| 字体 | `Cascadia Code` 缺失时的回退字体加载可能阻塞首帧 | 数十 ms |
| Shader 预热 | `.qsb` 首次链接可在 Splash 阶段预热（当前冷态 BOOT shown 比热态多 1–2 s） | 可能秒级 |

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
| 拖起 | 卡片放大 1.04、阴影加深、倾斜、光轨拖尾 | 120 ms | ⬜ |
| 拖入目标列 | 目标列边框呼吸 + 插入位置虚线 | 持续 | ⬜ |
| 落位 | 涟漪扩散 + 卡片弹性落位 | 220 ms | ⬜ |
| 勾选完成 | 光扫过卡片 → 粒子迸发 → 吸入“已办结” | 320 ms | ⬜ |
| 删除 | RGB 分离故障 → 溶解（撤销反向播放） | 260 ms | ⬜ |
| 撤销 | 卡片从右侧滑回 + 高亮扫描 | 240 ms | ⬜ |
| 新增任务 | 从输入框方向飞入 + 描边点亮 | 240 ms | ⬜ |
| 逾期 | 红色呼吸描边（周期 2 s） | 循环 | ⬜ |
| 统计变化 | 数字滚动 + 芯片闪光 | 200 ms | ⬜ |

### 3.4 主题引擎 v1 —— **已完成**

**实际实现**：

- `qml/theme/Theme.qml`（QML Singleton）：颜色令牌（bg / text / muted / cyan / magenta / violet / success / warning / danger + 派生的 border / panel / card / input / accentSoft / elevated / ghost 等），每个令牌带 `Behavior on color`，切换时**全应用渐变**。
- `qml/theme/Motion.qml`（QML Singleton）：动效时长与缓动令牌 + 减弱动效开关。
- **6 套预设**（实际名称）：霓虹青（默认）/ 赛博紫 / 落日橙 / 深海蓝 / 森野绿 / 极夜灰。每套含 bg / text / muted / 三主色 / 成功 / 警告 / 危险色。
- **背景联动**：`shaders/neon.frag` 新增 `uBase / uPrimary / uSecondary / uAccent / uQuality` uniform，霓虹背景随主题一起渐变（这是「炫酷」的关键一环）。
- **入口**：标题栏右侧三色圆点按钮 → 外观面板（预设列表 + 画质三档 + 减弱动效），`MultiEffect` 真投影 + 逐行错峰渐入。
- **持久化**：C++ 侧 `src/UiSettings.{h,cpp}`（QSettings，`appearance/` 段）。**注意**：QML 侧 `import QtCore` 的 `Settings` 在本机不可用（`QtCore` 是磁盘动态插件且无 CMake 目标），故走 C++ 上下文属性 `uiSettings`。
- **颜色令牌化**：`Main.qml` / `TaskCard` / `WinButton` 中所有硬编码 rgba 与字面色值已全部替换为 `Theme.*` 令牌，主题切换时全应用一致生效。

**剩余**：

- 强调色可单独覆盖（设置页取色器），标题栏、芯片、插件面板、Shader 辉光联动。
- 主题切换 <100 ms（当前 260 ms 渐变，可再压）。
- 预留 `theme.json` 供 P1 主题包与插件贡献。

## 4. P0.5 日常底座（功能）

| 方向 | 竞品依据 | 落地要点 | 成本 |
| --- | --- | --- | --- |
| 提醒 + 逾期 | Tasks / Vikunja / Task Coach | `remind_at` + `notified_at`（防重复），`QSystemTrayIcon::showMessage` + 窗口边缘辉光；启动时补发漏提醒 | M |
| 重复任务 | Tasks / Taskwarrior / Task Coach | **存 RFC 5545 RRULE 字符串**（不要自造格式），便于日后导出 ICS / 对接 CalDAV | M |
| 子任务 / 清单 | Kanri / Vikunja / Wekan | 优先 `parent_id` 自关联（复用 Task 结构），卡片显示霓虹进度环 | M |
| 导入导出 + 自动备份 | Kanri / Vikunja | JSON 带 `schemaVersion`；支持导入 Todo.txt / Markdown；迁移与每日首次启动前自动备份 `board.db` | S |
| 快速录入解析 | Super Productivity / Todoist 式 | 解析「明天 15:00 #工作 !2」：时间、日期、标签、优先级；中文相对日期（今天/明天/周五/下周三） | S–M |
| 命令面板 | Super Productivity / Kanri | `Ctrl+K`：新建、跳转任务、切主题、切视图、打开插件、切换专注 | S–M |
| 列内精确插位 | 现状“落列尾”（已知限制） | 拖拽计算目标索引，`moveTask` 支持 `newIndex`（接口已存在） | S |
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
   - 热重载：改完 `main.qml` 即生效（`QQmlComponent` 卸载重建即可，无需重启进程）。
   - `settings.qml` + 设置页声明。
   - 命令注册 / 快捷键注册 / 任务事件（created / updated / moved / completed）。
   - 多槽位（侧栏 + 底部面板 + 卡片字段）。
   - 保持 `apiVersion 1` 向后兼容；插件仍为“受信插件”，但补**权限授权弹窗**与来源提示（无沙箱时更要做显式授权）。
2. **官方插件示例**（用插件承载工作台能力，避免核心膨胀）：番茄钟、日历视图、快速笔记、GitHub Issue 导入。
3. **动态托盘 + 窗口辉光**：托盘图标在有到期任务时脉冲；全局热键唤出窗口用缩放淡入；窗口边缘辉光提醒。
4. **启动序列**：Splash 从静态品牌图升级为终端式 boot 动画（**必须与主界面预热并行，不得延长总启动时间**）。
5. **可选音效**：hover 轻响、完成清脆音、删除故障音；设置可关（无障碍与静音场景）。

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
- 插件市场（在插件平台 2.0 与权限提示落地前，先不做，避免变成不安全的脚本分发渠道）。

## 9. 里程碑

| 版本 | 主题 | 内容 | 出口标准 | 进度 |
| --- | --- | --- | --- | --- |
| **R0** | 炫酷地基 | 启动性能剖析与优化、动效基线、组件拆分、Theme 单例、主题引擎 v1 | 热启动 <1.5 s；主题可热切；hover/进场/面板动效上线 | 主题引擎 + 组件拆分 + 画质分档 **已完成**；启动优化 **已诊断、待瘦身**；落位/完成特效 **待补** |
| **R1** | 日常底座 | 提醒 + 逾期、重复任务（RRULE）、子任务、导入导出 + 备份、快速录入解析、命令面板、列内插位、通用撤销 | 一周日常使用零遗漏；数据可导出可还原 | 未开始 |
| **R2** | 记忆点 | Shader 场景化、专注模式、统计可视化、主题包、卡片详情、拖拽手感升级 | 截图能直接当壁纸；动效全链路顺滑 | 未开始 |
| **R3** | 生态 | 插件平台 2.0、番茄钟/日历/笔记官方插件、动态托盘与窗口辉光、启动序列、音效 | 插件改一行即生效；主程序零改动可扩展 | 未开始 |

## 10. 验收指标（“炫酷”可量化）

| 指标 | 目标 | 当前实测 |
| --- | --- | --- |
| 启动到首帧可交互 | 热启动 < 1.5 s；首次运行 < 3 s | 热启动 **1.23 – 1.39 s** ✅；首次运行 5 – 9 s ⬜（冷页缓存，R0 待优化） |
| 常态帧率 | 60 fps @1080p；动画期间不掉帧 | **60.0 fps** ✅（热启动） |
| 主题切换 | < 100 ms，无需重启 | 260 ms 渐变，**无需重启 ✅**、时长待压 ⬜ |
| 反馈延迟 | 任何点击到视觉反馈 < 100 ms；动效 ≤ 250 ms（完成特效 ≤ 320 ms） | hover/按下/面板 均 ≤ 260 ms ✅ |
| 破坏性操作 | 必有动效 + 5 s 内可撤销 | 撤销已实现（Ctrl+Z + Toast），**动效待补** ⬜ |
| 空闲资源 | 最小化后台 CPU < 1% | 未测 ⬜ |
| 内存 | 常驻 < 250 MB | 199 – 230 MB ✅ |
| 截图门槛 | 任意一屏截图可直接发布（无占位、无错位、无模糊） | 外观面板/赛博紫主题截图已验证 ✅ |

### 诊断命令速查

```powershell
# 启动性能打点（日志：%APPDATA%\com.lin\NeonBoard\neonboard.log）
$env:QT_LOGGING_RULES = "qt.qml.import.debug=true"   # 仅排查 QML 类型解析时启用
build\Release\NeonBoard.exe --smoke                  # 旁路单实例的冒烟模式
build\Release\NeonBoard.exe --benchload controls     # 分档加载耗时
$env:NEONBOARD_NO_BG = "1"; build\Release\NeonBoard.exe   # 关闭动态背景做 A/B
```
