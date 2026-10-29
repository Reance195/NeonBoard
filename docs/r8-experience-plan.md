# R8 记忆点收尾：主题包市场 / 卡片详情层 / 拖拽手感

> 状态：**已完成（2026-10-29 结项，6/6）**。六张卡的落地提交与「与计划的偏差」
> 见 [`iteration-roadmap.md`](iteration-roadmap.md) §9.4；本文保留立项时的判断与拆分口径。
> 上游设计：[`iteration-roadmap.md`](iteration-roadmap.md) §5 P1 炫酷记忆点（第 4/5/6 条）
> 已完成的相关前置：[§9 里程碑表 R2 行](iteration-roadmap.md)（Shader 场景化 R2-02、专注模式与统计 R2-01）
> 卡片规范：[`task-card.md`](task-card.md)
> 授权来源：维护者 2026-10-29 在会话中明示「R8 主题走 B 本体记忆点，计划 + 台账登记一次到位」

## 0. 一页速览

R2 只做完了记忆点的一半：Shader 场景化、专注模式、统计可视化已落地，**主题包市场、卡片详情层、
拖拽手感升级**三项自 R2 挂到现在仍是空白。R8 把这三项收口。

```text
主题：内置 6 套预设 ──► 主题包格式 + 外部加载 ──► 主题包市场（安装 / 预览 / 卸载）
详情：双击卡 ──► 详情抽屉（Markdown 笔记 / 字段编辑）
              ──► 子任务进度环 + 审计时间线（新增 task_event 侧表）
拖拽：按住拖动 ──► 涟漪落点 + 光轨 + 列磁吸 + 键盘 Ctrl+←/→ 位移
```

**R8 出口标准**

1. 第三方（非内置）主题包能被安装、切换、卸载，卸载后回到内置预设且主题令牌无残留；
2. 主题包市场列表带预览图与来源标注，断网降级到「已安装」而不是空白；
3. 双击卡片进入详情抽屉：Markdown 笔记所见即所得、字段可就地编辑，浮层遵守「同时最多一个」；
4. 详情里能看到子任务进度环与按时间倒序的审计时间线（谁在什么时候改了什么）；
5. 拖拽全程有落点、有轨迹、有吸附反馈；键盘能完成同类位移且有无障碍播报；
6. `Motion.reduced` 开启时全部新动效退化为瞬时，`--smoke` 日志零 QML 告警。

## 1. 为什么现在做

| 事实 | 含义 |
| --- | --- |
| 路线图 §5 P1 六项记忆点，第 1–3 项已落地（R2-01 / R2-02），第 4–6 项仍标 ⬜ | R8 是把三年前就能背出来的清单清掉，不是新发明 |
| 全仓 `src/` `qml/` 内 grep `SoundEffect` / `switchBoard` / `bsdiff` / 卡片详情 / 主题包 均为零命中 | 三项确实零基础，不存在「做了一半」的隐性债务 |
| §10 验收指标里「截图门槛」已达成（6 套主题逐套出图），但**主题仍是编译期常量**：`qml/theme/Theme.qml` 的 `presets` 是硬编码数组 | 用户想换配色必须改代码重编译，「6 套」是天花板而不是起点 |
| 看板侧能力已经过剩（R6/R7 派发器、R5 自动更新、DX 治理），而每天被高频使用的仍是「卡片」本身 | 提升每日体感的边际收益，高于再加一层引擎能力 |
| R4-10 / DX 治理链已稳定（结卡自动化、代登记可审计） | 新卡片可以**出生即带** `acceptance` / `scope` / `depends`，不再给 DX-09 补账（#2）添新债 |

**一句话**：R6/R7 让 NeonBoard 变得与众不同，R8 让它每天用起来值得。

## 2. 现状与差距

| 区块 | 现状（2026-10-29 实测） | 差距 |
| --- | --- | --- |
| 主题 | `qml/theme/Theme.qml`：`presets` 数组写死 6 套（neon/violet/sunset/abyss/forest/mono）；`applyPresetByKey()` 逐个给 `bg/text/muted/cyan/magenta/violet/success/warning/danger` 赋值；持久化只有 `UiSettings::themePreset` 一个 key 字符串 | 无外部主题来源、无预览图管线、无安装/卸载语义；用户 key 与内置 key 无隔离，撞名即覆盖 |
| 卡片编辑 | `Main.qml` 内联表单（约 1807–1938 行，`noteField` 是纯文本，走 `TaskModel::note`） | 无独立详情层：Markdown 不渲染、无附件、无 history；编辑表单与卡片本体争夺同一块视觉空间 |
| 审计 | `Database` 只有 `task.created_at / updated_at / completed_at` 三个时间戳；侧表惯例（`task_extra` / `task_link` / `task_exec` / `task_run`）已成体系 | 「什么时候从待办移到进行中」这类历史**无处可查**，只能看最终值 |
| 拖拽 | `TaskCard.qml`：MouseArea 手算位移（>8px 判定 dragging），原卡降到 `opacity 0.35`，由 `BoardColumn` 画插入线（R1-1 `moveTaskBefore`）；拖拽期间原卡缩为半透明矩形 | 有插入线但**无拖拽物轨迹**（跟随的是缩小版原卡）、无落点涟漪、无磁吸、无列切换高亮；键盘无法完成同类位移 |
| 动效令牌 | `qml/theme/Motion.qml` 已有 `instant/fast/base/slow/enter/exit/panel/theme/toast/stagger` + `easeOut/easeInOut/easeSpring` + `reduced` 开关 | 令牌齐备，新动效必须挂上去并尊重 `reduced`，不得自带魔法数字 |

## 3. 已定架构决策（摘要）

### 3.1 主题包是数据，不是代码

沿用本仓既有的「侧表 / 外部文件不动主结构」惯例（对照 `task_extra` 不动 `task` 表）：
主题包是**一份 JSON + 一张预览图**，放在 `AppPaths::themeDir()` 下；`Theme.qml` 的 `presets`
拆成「内置常量」+「用户包（运行时追加）」两段，用户包的 key 统一前缀 `user:<id>`，
从根上避免与内置 key 撞名。

### 3.2 主题令牌集合冻结（v1）

包只允许覆盖既有 12 个令牌（bg / text / muted / cyan / magenta / violet / success / warning / danger
+ name / desc / key）。**不支持自定义字体、不支持自定义间距、不支持在包里塞脚本** ——
一旦开了脚本口子，主题包就从「配色」变成「代码」，随之而来的就是权限、签名与审查
（这正是 §6 P2「主题由插件贡献」留在后面的原因）。

### 3.3 详情层沿用浮层互斥纪律

§10 已立「浮层唯一：同一时刻最多一个面板」。详情抽屉不是新开一套机制，
而是**注册进既有互斥集合**（与外观 / 插件 / 设置面板同一入口管理），`Esc` 关闭、点击外部关闭。

### 3.4 时间线用侧表，不升 `user_version`

审计事件写入新侧表 `task_event(id, task_id, kind, payload, at)`，
沿用 `ensureColumn` / `CREATE TABLE IF NOT EXISTS` 的老路：**不动 `task` 表、不升 `user_version`**，
老版本程序打开同一个库照常工作。写入点在 C++ 侧（模型），QML 不直接写库。

### 3.5 动效一律走 Motion 令牌

新增动效的时长 /  easing 必须从 `Motion` 取；`Motion.reduced` 为 true 时全部退化为 0 ms。
这条是 §10「反馈延迟 ≤ 250 ms」能长期成立的前提，也让 §3.3 的三档画质真正可降级。

## 4. 卡片拆分总览

| 短号 | 标题 | 依赖 | 波次 | 口径 |
| --- | --- | --- | --- | --- |
| R8-01 | 主题包格式与外部加载 | — | 2 | `tst_themepack` 覆盖格式校验与降级 |
| R8-02 | 主题包市场（浏览 / 安装 / 卸载） | R8-01 | 4 | 复用 PluginMarket 的四道校验与原子替换 |
| R8-03 | 卡片详情抽屉（Markdown 笔记 + 就地编辑） | — | 1 | 最早可见、零依赖，先出收益 |
| R8-04 | 详情进阶：子任务进度环 + 审计时间线 | R8-03 | 3 | 新增 `task_event` 侧表 |
| R8-05 | 拖拽手感升级（涟漪 / 光轨 / 磁吸 / 列高亮） | — | 2 | 纯 QML，动效走 Motion 令牌 |
| R8-06 | 键盘位移与拖拽可达性 | R8-05 | 3 | `Ctrl+←/→` + 无障碍播报 |

## 5. 卡片定义

### R8-01 主题包格式与外部加载

- **起因**：主题是编译期常量，用户要改配色只能改代码；「6 套预设」是上限而不是起点。
- **目标**：定义 `themepack.json` 格式（12 令牌 + name/desc/key）与包目录；`Theme.qml` 启动时扫描
  `AppPaths::themeDir()`，把合法包追加进可选列表；缺字段 / 非法色值 / 重复 key 一律降级跳过并记日志，
  **一个坏包不能让应用起不来**。
- **验收**：
  - `ctest --test-dir build -C Release -R tst_themepack --output-on-failure`
  - 坏 JSON / 缺令牌 / 非法色值的三种包均被跳过，其余包正常加载，应用可启动
  - `build/Release/NeonBoard.exe --selftest` 退出码 0；`--smoke` 日志无 QML 告警
- **scope**：`qml/theme/Theme.qml`、`qml/components/AppearancePanel.qml`、`src/ThemePack.*`、
  `src/AppPaths.*`、`src/main.cpp`、`tests/tst_themepack.cpp`、`tests/CMakeLists.txt`
- **非目标**：不做市场、不做在线下载、不允许包携带脚本或自定义字体。

### R8-02 主题包市场（浏览 / 安装 / 卸载）

- **起因**：只有本地加载，用户拿不到别人的包；P2 原定「主题包市场 + 插件贡献主题」，
  R8 做前者（浏览安装），后者仍留 P2。
- **目标**：市场索引（可配置，内置占位地址）+ 本地缓存 + 断网降级；列表带预览图与来源标注
  （内置 / 用户安装）；安装走「临时目录 + 原子替换」，失败回滚不留半成品；卸载回落到内置预设且代币无残留。
- **验收**：
  - `ctest --test-dir build -C Release -R tst_thememarket --output-on-failure`（含包格式 / key 一致 / 路径不越界 / SHA-256 校验）
  - 断网时列表不空白：显示已安装包 + 明确的「离线」标注
  - 安装失败（写入中途杀进程）后重启，主题列表回到安装前状态
  - `--selftest` 退出码 0
- **scope**：`qml/components/ThemeMarketPanel.qml`、`qml/Main.qml`、`src/ThemeMarket.*`、
  `src/AppActions.*`、`tests/tst_thememarket.cpp`、`tests/CMakeLists.txt`
- **非目标**：不做签名与发布者信誉（§8 已列为明确不做）、不做付费、不做用户上传。

### R8-03 卡片详情抽屉（Markdown 笔记 + 就地编辑）

- **起因**：`note` 字段存在但只能在内联表单里当纯文本改，「详情层」在 P1 里挂了很久。
- **目标**：双击（或回车）卡片打开详情抽屉：标题 / 优先级 / 标签 / 到期 / 提醒 / 备注一次改完；
  备注用 `Text.MarkdownText` 渲染（Qt 自带，不引第三方），编辑时切回纯文本，保存即刻渲染；
  抽屉注册进既有浮层互斥集合，`Esc` / 点击外部关闭。
- **验收**：
  - `ctest --test-dir build -C Release -R tst_carddetail --output-on-failure`（保存 / 取消不改库 / 浮层互斥 / Markdown 开关）
  - 打开详情再按 Esc，数据库零变更；同时只能存在一个浮层
  - 备注写入后重开应用仍渲染正确
  - `--smoke` 日志无 QML 告警
- **scope**：`qml/components/CardDetailPanel.qml`、`qml/Main.qml`、`qml/components/TaskCard.qml`、
  `src/TaskModel.*`、`tests/tst_carddetail.cpp`、`tests/CMakeLists.txt`
- **非目标**：附件不在本卡（见第 10 节待拍板 A）；不改 `task` 表结构。

### R8-04 详情进阶：子任务进度环 + 审计时间线

- **起因**：子任务完成度已在卡面有进度环（R1-4 边表 `task_link`），但详情里看不出「谁的进度」；
  而「什么时候改的」压根没存 —— 表上只有 3 个时间戳。
- **目标**：新增侧表 `task_event`（不动 `task`、不升 `user_version`），在移动 / 完成 / 编辑等动作后
  落一条 `(task_id, kind, payload, at)`；详情抽屉底部按时间倒序渲染时间线；子任务区改成
  「进度环 + 逐条子卡」。
- **验收**：
  - `ctest --test-dir build -C Release -R tst_taskevent --output-on-failure`（写入 / 倒序查询 / 删除卡级联清理 / 老库自动建表）
  - 用不带 `task_event` 的老库文件启动，程序自动建表且历史任务不受影响
  - 详情抽屉时间线条数与 `sqlite` 里的事件数一致
  - `--selftest` 退出码 0
- **scope**：`src/TaskAudit.*`、`src/Database.cpp`、`src/TaskModel.*`、`src/TaskTree.*`、
  `qml/components/CardDetailPanel.qml`、`tests/tst_taskevent.cpp`、`tests/CMakeLists.txt`
- **非目标**：不做 undo/redo 回放（撤销仍走 `TaskUndo`）、不做跨设备事件同步。

### R8-05 拖拽手感升级（涟漪 / 光轨 / 磁吸 / 列高亮）

- **起因**：现在拖起来只是一张半透明缩略 followed by 一条线，「手感」三个字还没兑现。
- **目标**：拖拽物带光轨尾迹而不是缩小的半透明卡；落点处一圈霓虹涟漪（不替换既有插入线，
  而是叠在它上面）；拖到两卡之间磁吸到最近落点并有轻微回弹；跨列时目标列整体高亮；
  所有时长与 easing 取自 `Motion`，`reduced` 时全部瞬时。
- **验收**：
  - `ctest --test-dir build -C Release -R tst_dragfeel --output-on-failure`（磁吸附着点计算 / 跨列切换判定）
  - `Motion.reduced = true` 时拖拽仍可完成，且无 >100 ms 的等待动画
  - `--smoke` 日志无 QML 告警；拖拽 20 次无残留动画对象（靠日志无 `ReferenceError` 佐证）
- **scope**：`qml/components/TaskCard.qml`、`qml/components/BoardColumn.qml`、`qml/Main.qml`、
  `qml/theme/Motion.qml`、`tests/tst_dragfeel.cpp`、`tests/CMakeLists.txt`
- **非目标**：不改落点定位算法（`moveTaskBefore` 的语义与 R1-1 一致）、不做触屏手势。

### R8-06 键盘位移与拖拽可达性

- **起因**：拖拽是纯鼠标能力，`Ctrl+←/→` 在 P1 第 6 条里挂了但没做；无障碍要求「能做完同样的事」。
- **目标**：选中卡后 `Ctrl+←/→` 跨列移、`Ctrl+↑/↓` 列内调位；拖拽过程中Esc 取消回到原位；
  关键状态变化有 `Accessible` 播报（移到了哪一列、取消了拖拽）。
- **验收**：
  - `ctest --test-dir build -C Release -R tst_keymove --output-on-failure`（移动 / 越界不越 / 撤销可回退）
  - 纯键盘完成「选中 → 跨列 → 撤销」全链路，鼠标一次不用
  - 拖拽中按 Esc，卡回到原列原位（`TaskUndo` 之外不产生多余记录）
  - `--selftest` 退出码 0
- **scope**：`qml/components/BoardColumn.qml`、`qml/components/TaskCard.qml`、`qml/Main.qml`、
  `src/AppActions.*`、`tests/tst_keymove.cpp`、`tests/CMakeLists.txt`
- **非目标**：不做自定义快捷键绑定（那是插件能力，R3 已落地）。

## 6. 台账登记片段（维护者授权，已执行）

六张卡与对应 baseline 已按维护者授权追加进 `docs/board-ledger.json`，
提交类型为 `chore(board):`，body 带「授权：」「批次：」两行，不与代码改动混提交：

```json
"R8-01": {
  "acceptance": [ ... ],
  "depends": [],
  "id": "<uuid>",
  "note": "...",
  "scope": [ ... ],
  "stage": "R8",
  "title": "R8 主题包格式与外部加载"
}
```

`baseline` 同步补 `"R8-01" ~ "R8-06": "todo"`（6 条），
`python scripts/derive_board_status.py --baseline-check` 结果应为「漂移 0 项 / 缺项 0 项」。

## 7. 波次与工期

| 波次 | 卡片 | 出口 | 累计 |
| --- | --- | --- | --- |
| 1 | R8-03 | 详情抽屉可用，每天读写卡片的人先拿到收益 | 3 d |
| 2 | R8-01 + R8-05 | 外部主题可加载 + 拖拽有手感 | 8 d |
| 3 | R8-04 + R8-06 | 时间线可查 + 键盘可达 | 13 d |
| 4 | R8-02 | 主题可分享安装 | 16 d |

> 工期按熟悉代码库的人估算；**界面类卡片的真实成本在验证**——UI 改动在 `ctest` 里通常是看不见的
> （`pitfalls` 4.1 / 4.4 都栽在这），每张卡收尾必须补 `--smoke` + 查 `.sandbox/neonboard.log`，
> 必要时出图。R8-02 / R8-04 涉及安装与时间线的跨重启行为，建议把验收写细而不是砍验证省时间。

## 8. 非目标（R8 明确不做）

| 不做 | 归属 |
| --- | --- |
| 主题包携带脚本 / 自定义字体 / 自定义布局 | §3.2：开了就是「主题即代码」，退回安全议题 |
| 市场签名与发布者信誉 | 路线图 §8「明确不做」 |
| 插件贡献主题（插件 API 里写主题） | P2 生态，等 R8 的格式稳定后再谈 |
| 附件（见待拍板 A） | 未定，可能单开一张卡 |
| 撤销/重做回放成时间线 UI 的一部分 | `TaskUndo` 仍是唯一撤销通道 |
| 触屏 / 手写笔拖拽手势 | 桌面场景，无收益 |
| 把执行 / 派发相关内容搬进详情抽屉 | R6/R7 的执行面板已专门承载这块，不重复做第二套 |

## 9. 风险与回退

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| 坏主题包让应用起不来 | 用户被锁死在看板外，最严重的失败模式 | 逐个包容错：坏包跳过 + 记日志；启动时至少保证内置 6 套可用；`tst_themepack` 用三种坏包实测 |
| Markdown 渲染吞掉老笔记内容 | 数据看起来丢了一半 | 编辑后才渲染；渲染失败回落纯文本；`note` 字段原样保存不做「清洗」 |
| `task_event` 写爆磁盘 | 长期用下来库变大 | 事件只记 delta 不记全量快照；同一 `(task_id, kind)` 短间隔合并；删除卡级联清理 |
| 拖拽动效在低端机上拖慢帧率 | 手感反过来变成卡顿 | 全部动效绑 `Motion` + 三档画质；`reduced` 或低画质档自动降到瞬时 |
| 详情抽屉与既有浮层争抢 | 「同时多个面板」违背 §10 | 抽屉注册进同一互斥集合，`tst_carddetail` 断言互斥 |
| 新 UI 卡挤掉 `#2` 覆盖率努力（DX-09） | 旧债未清新债又来 | R8 六张卡**出生即带** `acceptance` / `scope` / `depends`，不进 DX-09 的补账批次 |

**回退路径**：三块新 UI 都是「新增文件 + `Main.qml` 新增区块 / 新增 Loader」；
主题包覆盖只作用于用户区，删目录即回到纯内置；详情抽屉不与内联表单二选一，关掉入口仍在；
拖拽动效全部受 `Motion` 控制，画质最低档等于关闭。任一卡出问题都可单点撤回，不需要回滚数据库。

## 10. 待拍板事项

| # | 问题 | 倾向 | 需要谁定 |
| --- | --- | --- | --- |
| A | **卡片详情要不要带附件**：带了要回答备份（每日自动备份 board.db）与导出（DataPort 三格式）要不要连带文件，§10「数据可导出可还原」这条指标可能会被打破 | 建议 R8 先不带附件，把它与「导出打包」合并成一张独立卡 | 维护者 |
| B | **主题包是否允许插件贡献**（P2 原文：P2 开放插件贡献主题） | 建议等格式稳定一轮再说，先不让插件写主题 | 维护者 |
| C | **审计时间线的保留窗口**：永久 / 保留 N 条 / 保留 N 天 | 建议先永久，真出体积问题再收紧（写入量很小） | 维护者 |
| D | **要不要把 §5 P1 剩余项打包成对外发布的卖点**（配合 #7 对外发布） | 建议 R8 完成后重出一组主题截图再发 | 维护者 |

## 11. 登记与领取

1. 六张卡已登记在 `docs/board-ledger.json`，路线图 §9 表新增 R8 行、§9.4 记本节摘要；
2. 实施按 `scripts/agent-init.ps1 -Card R8-0x` 开区，**一卡一分支**（一分支多卡合法但需逐短号持证，DX-03）；
3. 结项用提交标记 `#R8-0x=done`，`git-done.ps1` 会自动把 baseline 补成 done（DX-07）；
4. scope 越界由 `scripts/check_scope.py --card <短号>` 拦截；改动过程中发现要越界，
   **按 check_scope 的提示在本分支里把 scope 补齐**，而不是绕过去。
