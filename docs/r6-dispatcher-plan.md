# R6 任务派发器：拆分与验收

> 状态：**规划完成，卡片待登记**（2026-10-27）
> 上游设计：[`product-design.md`](product-design.md)（M1 单次执行）、[`agent-profile.md`](agent-profile.md)、[`agent-triage.md`](agent-triage.md)
> 决策记录：[`adr/0001-task-definition-source.md`](adr/0001-task-definition-source.md)、[`adr/0002-run-state-and-executor-safety.md`](adr/0002-run-state-and-executor-safety.md)
> 卡片规范：[`task-card.md`](task-card.md)

## 0. 一页速览

R6 只做**一件事**：让「看板上的卡」能一键派给本机 agent 执行，并给出可验证的结论。

```text
卡片定义（goal / acceptance / scope / cwd）
   ↓ 点「执行」（先确认）
拉起 agent 子进程（只读 triage 或可写执行）
   ↓ 流式日志
进程退出 → 跑 acceptance → 越界检查
   ↓
run 记录落库（passed / blocked）→ 面板可见 → 人决定重试或结卡
```

**出口标准（R6 整体验收）**

1. 一张填好 `goal + acceptance + scope + cwd` 的卡，点执行后跑完并给出通过/失败结论；
2. 同一张卡不可并发，全局同一时刻只跑一个 run；
3. 执行完**不自动 push、不自动改看板列**；
4. 失败有可读摘要、有完整日志、可重试；
5. 执行器总开关默认关闭，关闭时看板本体行为与今天完全一致。

## 1. 为什么现在做

| 事实 | 含义 |
| --- | --- |
| 看板本体 92/92 卡全部 done，阶段条已饱和 | 继续堆卡片字段/动画的边际收益趋近于零 |
| 治理层（领用闸门、任务闸门、快照派生）已成体系 | 别人抄界面容易，抄「agent 协作协议」难；这是真差异点 |
| `task_exec` 侧表与 `TaskExec` 门面已在（M0 收尾） | 执行定义的**存储**已就位，缺的是**使用它的引擎** |
| `agent-profile.md` / `agent-triage.md` 已有实测结论 | CLI 参数、只读模式、输出格式不必再试错 |

方向判断：从「看板」升级为「任务派发器」是产品跃迁；但**必须先把交付底座补齐**（v0.2 的
CI 硬门禁、卡片 acceptance/scope 覆盖、首启性能），否则派发器建立在一个「验收靠人肉」的地基上。
两条线并行推进：v0.2 补底座，R6 做引擎。

## 2. 现状与差距

| 已有 | 位置 | R6 要补的缺口 |
| --- | --- | --- |
| 执行定义侧表 `task_exec(task_id, goal, acceptance, scope, agent, cwd, workflow, timeout_min, max_attempts)` | `src/Database.cpp` / `src/TaskExec.*` | UI 只暴露 goal/acceptance/scope/agent/workflow；`cwd` / 超时 / 轮数不可编辑，`cwd` 无合法性校验 |
| 单测 `tst_taskexec` | `tests/tst_taskexec.cpp` | 只覆盖字段读写与校验，无运行态 |
| agent 调用参数与只读模式（opencode / claude / cursor-agent 已实测） | `docs/agent-profile.md` | 未代码化：没有 profile 存储、没有只读参数拼装 |
| triage 输出 schema 与 prompt 模板 | `docs/agent-triage.md` | 未实现：没有只读分析器、没有草案回填 |
| 规划钩子事件链（`agent.task`） | `plugins/agent-bridge` / `HookEventQueue` | 只服务卡片状态，没有 run 事件 |
| 运行记录 | —— | **完全缺失**：没有 run 表、没有锁、没有日志、没有心跳 |

## 3. 已定架构决策（摘要）

完整理由见 ADR，本节只列结论，执行时按此为准。

### 3.1 任务定义单源（ADR-0001）

- **开发卡**（路线图任务）以 `docs/board-ledger.json` 为唯一事实源：进 git、可评审、CI 校验；
- **个人卡**以 `board.db` 的 `task_exec` 为唯一事实源：随手改、不必进 git；
- 两者用卡片 `code` 关联，只允许**台账 → board.db 单向导入**（R6-09，幂等 upsert，冲突只报告不覆盖）；
- 反向（board.db → 台账）**永远由人确认后提交**，agent 不得自动写台账 `cards` / `baseline`。

### 3.2 运行态不进看板状态机（ADR-0002）

- 新增侧表 `task_run`，生命周期 `queued / running / passed / blocked / cancelled`；
- 看板列仍只有 `todo / doing / done`，**程序不自动改列**；验收通过后在执行面板给「标为已办结」一键动作，由人点；
- **不往 `task` 表加列、不升 `user_version`**，老版本程序照常开库；
- 同卡同一时刻最多一个活跃 run（部分唯一索引兜底），心跳 10 s，超过 90 s 无心跳判孤儿 → `blocked`。

### 3.3 M1 忙策略：拒绝，不排队

并发默认 1（4 核 8 线程，多开只会互相拖慢）。已有 run 在跑时，新执行请求**直接拒绝并提示**，
不引入队列。理由：排队会让「锁 / 用户预期 / 取消语义」同时变复杂，而这些属于 M1 之后的事。

### 3.4 安全默认

| 约束 | 默认值 |
| --- | --- |
| 执行器总开关 | **关**（`executor.enabled = false`） |
| cwd | 必须命中已登记的工作目录列表，首次登记需人确认 |
| 执行前确认 | 展示 agent、cwd、命令、acceptance 全文，人点确认才启动 |
| acceptance 命令 | 白名单脚本路径，**禁止管道 / 重定向 / 命令拼接** |
| 环境变量 | 白名单透传（`PATH` / `HOME` / agent 自己的 key），不整包继承 |
| 日志 | 按 run 落文件 + 脱敏（token / key / 长十六进制串） |
| 自动推送 | **禁止**；`deliverable` 默认 `commit`，push 由人做 |
| 超时 / 轮数 | `timeoutMin` 1–480（默认 30）、`maxAttempts` 1–5（默认 1） |

## 4. 卡片拆分总览

| 短号 | 标题 | 依赖 | 预估 | 波次 |
| --- | --- | --- | --- | --- |
| R6-01 | 执行定义收尾：cwd / 超时 / 轮数可编辑 + 执行页签 | — | 1.5 d | 1 |
| R6-02 | 运行记录侧表与任务锁 | — | 2 d | 1 |
| R6-03 | Agent Profile 与执行前确认 | R6-01 | 1.5 d | 2 |
| R6-04 | 单次执行器：QProcess + 流式日志 + 超时/终止 | R6-02, R6-03 | 3 d | 3 |
| R6-05 | 验收器与越界检查 | R6-04 | 2 d | 4 |
| R6-06 | 执行结果回写与执行面板 | R6-04, R6-05 | 2 d | 5 |
| R6-07 | Triage：只读分析生成执行草案 | R6-01, R6-03 | 2.5 d | 6 |
| R6-08 | Loop：失败摘要回灌与有限重试 | R6-04, R6-05 | 1.5 d | 6 |
| R6-09 | 台账卡单向导入（可选） | R6-01 | 1.5 d | 7（可不做） |

合计 17–19 人日（agent 实际执行会更快，按 1.5–2 倍折算为开发会话）。

**关键路径**：`R6-01 → R6-03 → R6-04 → R6-05 → R6-06`。R6-07 / R6-08 可与 R6-05 之后的收尾并行，
但它们都依赖 R6-04 的执行器骨架，不要提前开工。

**波次说明**

| 波次 | 卡片 | 可并行 | 意义 |
| --- | --- | --- | --- |
| 1 | R6-01 + R6-02 | 是（不同文件） | 定义可编辑 + 运行态有地方存 |
| 2 | R6-03 | — | agent 可被正确拉起，人能确认再执行 |
| 3 | R6-04 | — | **第一次真正跑起来**，MVP 的分水岭 |
| 4 | R6-05 | — | 结果可信：验收 + 越界 |
| 5 | R6-06 | — | 闭环可视化 |
| 6 | R6-07 + R6-08 | 是（不同文件） | 分析前置 + 失败重试 |
| 7 | R6-09 | — | 台账联动（可选，不阻塞发布） |

## 5. 卡片定义

> 每张卡的 `scope` 已收窄到最小改动集；动手前先读 `NB_SCOPE`，收尾跑 `scripts/verify.ps1`。
> `note` 首段是「起因/背景」，合并提交 body 四段见 `docs/task-card.md`。

### R6-01 执行定义收尾：cwd / 超时 / 轮数可编辑 + 执行页签

**目标**：让卡片能完整描述「怎么执行」——补齐 `cwd`、`timeoutMin`、`maxAttempts` 的编辑能力，
并把任务编辑弹窗拆出「执行」页签。

**要点**

- `src/TaskExec.*`：`setExec` 增补 `cwd` 校验（非空时必须是存在的目录、必须是绝对路径、规范化后不得含 `..`）；
  `cwd` 为空表示「尚未指定工作目录」，存储层不报错，但**执行按钮要求显式 cwd**（不在未知目录里跑 agent）；
- `qml/Main.qml`：任务编辑弹窗加页签「任务 / 执行」；执行页签放 goal、acceptance（多行，一行一条）、
  scope（多行，一行一条 glob）、agent（下拉，来自 profile 列表，R6-03 前先放自由文本）、cwd（目录选择）、
  超时（分钟）、轮数；保存仍走 `taskExec.setExec`；
- 执行页签底部常驻「执行」按钮，未满足最小集（goal + acceptance + cwd）时置灰并给出原因文案。

**验收**

```text
ctest --test-dir build -C Release -R tst_taskexec --output-on-failure
build/Release/NeonBoard.exe --selftest          # 退出码 0
build/Release/NeonBoard.exe --smoke             # 退出码 0，无 QML 报错
```

- 单测覆盖：cwd 为相对路径 / 不存在目录 → `setExec` 返回 false；合法绝对路径 → 落库并回读一致；
  旧卡（无 cwd）回读为空串不报错；
- 手工：新建卡 → 执行页签填全 → 关闭重开 → 字段仍在；缺 cwd 时执行按钮置灰；
- 截图：`docs/screenshots/task-exec-tab.png`。

**scope**：`src/TaskExec.*`、`qml/Main.qml`、`tests/tst_taskexec.cpp`、`docs/screenshots/**`

**依赖**：无

### R6-02 运行记录侧表与任务锁

**目标**：给每次执行一个可追溯的身份（run）与一把不可重复的锁，全部落在侧表，不动 `task` 表结构。

**要点**

- `src/Database.cpp`：`CREATE TABLE IF NOT EXISTS task_run(id TEXT PRIMARY KEY, task_id TEXT NOT NULL,
  agent TEXT NOT NULL, cwd TEXT NOT NULL, workflow TEXT NOT NULL, status TEXT NOT NULL,
  attempt INTEGER NOT NULL DEFAULT 1, started_at INTEGER, finished_at INTEGER,
  heartbeat_at INTEGER, exit_code INTEGER, last_error TEXT, log_path TEXT, scope_violation TEXT)`；
  另建部分唯一索引，保证同卡最多一个 `status IN ('queued','running')` 的 run；
  **不升 `user_version`**；
- `src/TaskRunStore.*`：创建 / 心跳 / 结束 / 查询 / 孤儿回收；`activeRun(taskId)`、`acquire(taskId)`、
  `release(runId, status, detail)`；启动时把心跳超过 90 s 的 `running` 标记为 `blocked`（原因：进程中断）；
- `src/main.cpp`：注册上下文属性 `taskRuns`；`--selftest` 断言表与索引存在、`PRAGMA user_version` 仍为 2；
- 同卡二次 `acquire` 返回明确的「已在运行」错误码，不抛异常。

**验收**

```text
ctest --test-dir build -C Release -R "tst_taskruns|tst_databasemigration" --output-on-failure
build/Release/NeonBoard.exe --selftest
```

- 单测覆盖：建 run → 二次 acquire 被拒 → release 后可再 acquire；孤儿回收（把心跳改成 120 s 前 →
  重新打开 store → 状态变 `blocked` 且锁释放）；老库（v2）打开后新表建出、老数据不受影响；
- 显式断言 `user_version == 2`。

**scope**：`src/Database.cpp`、`src/TaskRunStore.*`、`src/main.cpp`、`tests/tst_taskruns.cpp`、`tests/CMakeLists.txt`、`CMakeLists.txt`

**依赖**：无

### R6-03 Agent Profile 与执行前确认

**目标**：把 `docs/agent-profile.md` 的实测结论变成可配置的 profile，并在真正启动前把人拦一次。

**要点**

- `src/AgentProfile.*`：profile 结构 `{id, displayName, exe, args, promptMode, readonlyArgs, output, env, defaultTimeoutMin}`；
  内置 `opencode` / `claude` / `cursor-agent` 三条（**codex 未实测，不内置**）；存储走 `meta` 表
  `exec.agentProfiles`（JSON 数组），用户可增删改；
- 拼命令行：`exe + args (+ readonlyArgs) `，prompt 按 `promptMode` 走 stdin 或 argv；禁止 shell 拼接；
- `qml/components/SettingsPanel.qml`：新增「任务执行」区——执行器总开关（默认关）、
  默认 agent 下拉、profile 列表（增删改、恢复内置）；
- `qml/Main.qml`：执行前确认对话框——展示 agent、cwd、完整命令行（只读文本）、acceptance 逐条、
  预计超时；勾选「信任本目录」后写白名单；未开总开关时给出跳转设置的提示。

**验收**

```text
ctest --test-dir build -C Release -R tst_agentprofile --output-on-failure
build/Release/NeonBoard.exe --selftest
build/Release/NeonBoard.exe --smoke
```

- 单测覆盖：三条内置 profile 的命令行拼装（含只读参数）；`promptMode=stdin` 时不把 prompt 塞进 argv；
  未知 agent id 返回失败而不是崩溃；meta 读写幂等；
- 手工：设置里关闭总开关 → 卡片执行按钮提示「执行器未启用」；打开后确认框内容与实际命令行一致；
- 截图：`docs/screenshots/exec-confirm.png`、`docs/screenshots/exec-settings.png`。

**scope**：`src/AgentProfile.*`、`src/Database.cpp`、`src/main.cpp`、`qml/Main.qml`、`qml/components/SettingsPanel.qml`、`tests/tst_agentprofile.cpp`、`tests/CMakeLists.txt`、`CMakeLists.txt`、`docs/agent-profile.md`

**依赖**：R6-01

### R6-04 单次执行器：QProcess + 流式日志 + 超时/终止

**目标**：第一次真正把 agent 跑起来——受控启动、流式日志、超时与终止，进程树可回收。

**要点**

- `src/TaskRunner.*`（QObject）：`start(taskId)` 走「校验定义 → 取锁 → 建 run → 起 QProcess」；
  `cancel()` 先 `terminate()`，5 s 不退再 `kill()`，Windows 下用 Job Object 确保子进程树一起回收；
- 流式日志：stdout/stderr 合并按行读，实时追加到 `runs/<runId>.log`，并对 token / key / 32+ 位十六进制串脱敏；
  UI 通过信号增量收到最后 N 行；
- 超时：`timeoutMin` 到点强杀，run 标记 `blocked`（reason=timeout）；
- 忙策略：已有活跃 run 时 `start` 立即返回「忙」错误，不排队；
- 心跳：每 10 s 写一次 `heartbeat_at`；
- 进程退出码单独记录，**不在此卡判定成败**（验收在 R6-05）。

**验收**

```text
ctest --test-dir build -C Release -R tst_taskrunner --output-on-failure
build/Release/NeonBoard.exe --selftest
build/Release/NeonBoard.exe --smoke
```

测试用固定 fixture（`tests/fixtures/fake-agent.cmd` 或 Qt 测试内联脚本）覆盖：

- 成功：退出码 0，日志含预期行，run 状态 `running → passed`（验收器未接时先记 `passed`）；
- 失败：退出码非 0，`last_error` 含末尾 20 行摘要；
- 超时：1 s 超时用例被强杀，状态 `blocked`；
- 取消：`cancel()` 后 5 s 内进程退出，状态 `cancelled`；
- 忙：第二个 `start` 被拒，且不产生第二条 run；
- 进程树：fixture 起一个孙进程 → 取消后孙进程也不存在；
- 脱敏：日志中的伪 token 被替换为 `***`；
- 手工：真实 opencode 跑一张只改文档的卡，结束后 `git status` 无新增提交、无 push。

**scope**：`src/TaskRunner.*`、`src/TaskRunStore.*`、`src/main.cpp`、`qml/Main.qml`、`tests/tst_taskrunner.cpp`、`tests/fixtures/**`、`tests/CMakeLists.txt`、`CMakeLists.txt`

**依赖**：R6-02、R6-03

### R6-05 验收器与越界检查

**目标**：执行完不只信退出码——跑卡片 acceptance，并校验 agent 没改出 scope。

**要点**

- `src/AcceptanceRunner.*`：按顺序执行 `acceptance[]`，两条形态：
  `command`（白名单脚本路径，`QProcess` 直接起进程，**不经过 shell**，拒绝含 `|` `>` `<` `&` `;` 的条目）
  与 `assert`（内置检查器，v1 只做 `file.exists("<path>")`；http / json 留接口不实现）；
- `src/ScopeMatcher.*`：跑前记录 `git status --porcelain` 基线，跑后取差异路径集；
  用卡片的 `scope` glob 匹配，越界路径写入 `scope_violation`，整体判 `blocked`；
  **scope 为空时不检查**（存量卡不阻断）；
- 结果聚合：agent 退出码 + acceptance 全过 + 无越界 = `passed`，否则 `blocked` 并写入
  `last_error`（agent 失败摘要 / acceptance 失败项 / 越界路径三选多）。

**验收**

```text
ctest --test-dir build -C Release -R "tst_acceptance|tst_scopematcher" --output-on-failure
build/Release/NeonBoard.exe --selftest
```

- acceptance：全过 → `passed`；第二条失败 → `blocked` 且 `last_error` 指出第几条；
  含管道的命令 → 拒绝执行并报「不允许 shell 语法」；`assert file.exists` 命中 / 未命中；
- scope：改 `src/a.cpp` 命中 `src/**` → 通过；改 `docs/x.md` 越界 → `blocked` 且列出路径；
  `scope` 为空 → 永远通过；
- 单测用临时 git 仓库 fixture，不碰真实工作区。

**scope**：`src/AcceptanceRunner.*`、`src/ScopeMatcher.*`、`tests/tst_acceptance.cpp`、`tests/tst_scopematcher.cpp`、`tests/CMakeLists.txt`、`CMakeLists.txt`

**依赖**：R6-04

### R6-06 执行结果回写与执行面板

**目标**：让结果看得见、可重试，但**不替人做决定**（不改看板列、不自动 push）。

**要点**

- run 终态写回 `task_run`（状态、耗时、退出码、`last_error`、`scope_violation`、日志路径）；
- `qml/Main.qml`：卡片右键 / 详情新增「执行」入口，打开执行面板：最近一次 run 的结论徽章
  （通过 / 失败 / 超时 / 取消）、耗时、末尾日志、「查看完整日志」「重试」「标为已办结」；
- `qml/components/TaskCard.qml`：卡面增加执行态小徽章（`running` 脉冲 / `passed` 绿点 / `blocked` 红点），
  不改变卡片既有信息层级；
- 「标为已办结」= 人点后调用 `taskModel` 的既有完成路径（保留撤销），**不是自动改列**；
- 重试 = 新建一条 run（`attempt + 1`），沿用同一份定义。

**验收**

```text
ctest --test-dir build -C Release -R tst_runsummary --output-on-failure
build/Release/NeonBoard.exe --run-summary <taskId>   # 输出该卡最近一次 run 的 JSON，退出码 0
build/Release/NeonBoard.exe --smoke
```

- 单测：失败 run → 摘要含末尾日志片段与失败原因；重试生成新 run 且 `attempt=2`；取消不改看板列；
- 手工：跑一张失败卡 → 卡面红点 → 面板显示摘要 → 重试 → 成功后点「标为已办结」，列变为 done 且可 `Ctrl+Z` 撤销；
- 截图：`docs/screenshots/exec-panel.png`。

**scope**：`src/TaskRunStore.*`、`src/main.cpp`、`qml/Main.qml`、`qml/components/TaskCard.qml`、`tests/tst_runsummary.cpp`、`tests/CMakeLists.txt`、`CMakeLists.txt`、`docs/screenshots/**`

**依赖**：R6-04、R6-05

### R6-07 Triage：只读分析生成执行草案

**目标**：新建/编辑卡时先让只读 agent 分析，产出结构化草案，人确认后回填定义。

**要点**

- `src/TriageRunner.*`：复用 R6-03 的 profile 与只读参数（`cursor-agent --plan` / `claude --permission-mode plan`），
  prompt 模板见 `docs/agent-triage.md`；**强制只读**，不接受「prompt 里写别改文件」这种约定；
- 解析与校验：严格 JSON；缺必填字段 → 重跑一次，仍失败标记 `draft` 并附原始输出；
  `questions` 超过 3 条截断；非法 glob 丢弃；acceptance 为空补默认 `pwsh -File scripts/verify.ps1`；
- 只读校验：分析前后各取一次 `git status --porcelain`，有差异直接判失败（证明只读失效）；
- `src/TaskExec.*`：草案先写 `board.db`（`task_exec` 旁的草案字段或 run 记录），**不进台账**；
- `qml/Main.qml`：执行页签「先分析」按钮 → 草案预览（理解复述 / 影响文件 / scope / 验收 / 问题 / 置信度）
  → 「采纳」回填表单；`confidence < 0.5` 时禁止一键执行，只允许先拆分或先澄清。

**验收**

```text
ctest --test-dir build -C Release -R tst_triage --output-on-failure
build/Release/NeonBoard.exe --selftest
```

- 单测（fixture 固定 JSON 输出）：合法草案全字段解析；缺字段重跑一次后失败标记 `draft`；
  `questions` 5 条被截为 3 条；非法 glob 被丢弃；`confidence=0.3` 时「可执行」判定为 false；
- 手工：对一张真实卡跑分析，结束后 `git status --porcelain` 为空（只读生效）；
- 文档：`docs/agent-triage.md` 补「实现现状」一节（哪些校验已落地）。

**scope**：`src/TriageRunner.*`、`src/AgentProfile.*`、`src/Database.cpp`、`src/TaskExec.*`、`src/main.cpp`、`qml/Main.qml`、`tests/tst_triage.cpp`、`tests/CMakeLists.txt`、`CMakeLists.txt`、`docs/agent-triage.md`

**依赖**：R6-01、R6-03

### R6-08 Loop：失败摘要回灌与有限重试

**目标**：失败后带着上下文重试，轮数有上限，人可随时叫停。

**要点**

- `maxAttempts` 生效：失败后若 `attempt < maxAttempts`，把 `last_error`（末尾 200 行）注入下一轮 prompt
  的「上一轮失败摘要」段，新建 run（`attempt + 1`）；
- 每轮独立日志，`attempt` 递增，历史 run 不覆盖；
- 人可随时 `cancel()`，取消后**不再自动重试**；
- 轮数耗尽 → 最终 `blocked`，面板提示「已重试 N 轮，需人工介入」；
- 默认 `maxAttempts = 1`（等价于不循环），要循环必须人显式调高。

**验收**

```text
ctest --test-dir build -C Release -R tst_runloop --output-on-failure
build/Release/NeonBoard.exe --selftest
```

- 单测：fixture 前两轮失败、第三轮成功 → 共 3 条 run，最终 `passed`，第三轮 prompt 含第二轮摘要；
  `maxAttempts=2` 且两轮失败 → 最终 `blocked`，无第 3 条 run；取消后无新 run；
- 断言每轮日志文件互不覆盖。

**scope**：`src/TaskRunner.*`、`src/TaskRunStore.*`、`tests/tst_runloop.cpp`、`tests/CMakeLists.txt`

**依赖**：R6-04、R6-05

### R6-09 台账卡单向导入（可选）

**目标**：把台账里开发卡的 `acceptance` / `scope` / `depends` 同步进 `board.db`，避免同一定义两处手填。

**要点**

- `src/LedgerImport.*`：读 `docs/board-ledger.json`，按卡片 `code` 找 `board.db` 里标题带该短号的任务；
  找到则 upsert `task_exec` 的 acceptance / scope（goal 取 `note` 首段），**只补不覆盖非空值**；
  找不到则报告「未建卡」，不自动建卡；
- 冲突（board.db 已有不同值）→ 只报告，不覆盖；
- 幂等：同一台账连续导入两次，第二次 0 变更；
- 反向同步**不做**（ADR-0001），UI 不提供「写入台账」按钮。

**验收**

```text
ctest --test-dir build -C Release -R tst_ledgerimport --output-on-failure
build/Release/NeonBoard.exe --selftest
```

- 单测：新导入 3 张卡 → 变更 3；再导一次 → 0；board.db 已有不同 acceptance → 报告冲突且不覆盖；
  台账里不存在对应任务 → 报「未建卡」；
- 手工：导入 R5-03，卡片执行页签能看到 acceptance / scope。

**scope**：`src/LedgerImport.*`、`src/TaskExec.*`、`src/Database.cpp`、`src/main.cpp`、`qml/Main.qml`、`tests/tst_ledgerimport.cpp`、`tests/CMakeLists.txt`、`CMakeLists.txt`、`docs/adr/**`

**依赖**：R6-01

> 这张卡**不阻塞正式发布**：没有它，开发卡的定义照旧手工填；有它，只是少一次复制粘贴。

## 6. 台账登记片段（维护者执行）

> ⚠️ 按 `AGENTS.md` 条款 6，**agent 不得写台账 `cards` / `baseline`**。
> 以下片段由维护者粘贴到 `docs/board-ledger.json`，或由维护者明确授权后再由 agent 执行。
> `baseline` 全部登记为 `"todo"`，否则「已完成卡不可重新领用」的闸门会失效。

```jsonc
"R6-01": {
  "id": "90651d45-9baf-4c3a-93a6-2a8d2ff981b5",
  "stage": "R6",
  "title": "R6 执行定义收尾：cwd / 超时 / 轮数可编辑 + 执行页签",
  "note": "起因：task_exec 侧表已能存 cwd / timeout_min / max_attempts，但任务编辑弹窗只暴露 goal / acceptance / scope / agent / workflow，执行参数填不全也就无法派发。目标：补齐三项编辑能力（cwd 需校验为存在的绝对路径），把编辑弹窗拆出「任务 / 执行」两个页签，并在执行页签底部给出执行入口与最小集提示。",
  "acceptance": [
    "ctest --test-dir build -C Release -R tst_taskexec --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0",
    "build/Release/NeonBoard.exe --smoke 退出码 0 且无 QML 报错"
  ],
  "scope": ["src/TaskExec.*", "qml/Main.qml", "tests/tst_taskexec.cpp", "docs/screenshots/**"]
},
"R6-02": {
  "id": "0921b037-54ca-45e4-ab4a-7c82f484be92",
  "stage": "R6",
  "title": "R6 运行记录侧表与任务锁",
  "note": "起因：执行器需要可追溯的 run 身份与「同卡不并发」的锁，而 task 表不能加列、user_version 不能升。目标：新增侧表 task_run（id / task_id / agent / cwd / workflow / status / attempt / 时间戳 / heartbeat / exit_code / last_error / log_path / scope_violation）+ 同卡活跃 run 的部分唯一索引，配套 TaskRunStore 做创建/心跳/结束/孤儿回收，启动时把心跳超 90 秒的 running 判为 blocked（进程中断）。",
  "acceptance": [
    "ctest --test-dir build -C Release -R \"tst_taskruns|tst_databasemigration\" --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0 且断言 user_version 仍为 2"
  ],
  "scope": ["src/Database.cpp", "src/TaskRunStore.*", "src/main.cpp", "tests/tst_taskruns.cpp", "tests/CMakeLists.txt", "CMakeLists.txt"]
},
"R6-03": {
  "id": "d926c728-42bf-497d-8661-d1f093949226",
  "stage": "R6",
  "title": "R6 Agent Profile 与执行前确认",
  "note": "起因：docs/agent-profile.md 的实测结论还停留在文档里，代码不知道每种 CLI 怎么拼参数、怎么进只读模式。目标：落地 AgentProfile（内置 opencode / claude / cursor-agent，codex 未实测不内置；存 meta 表 exec.agentProfiles）+ 命令行拼装（无 shell）+ 设置面板「任务执行」区（总开关默认关、默认 agent、profile 管理）+ 执行前确认框（agent / cwd / 完整命令行 / acceptance / 超时，勾选信任目录后写白名单）。",
  "acceptance": [
    "ctest --test-dir build -C Release -R tst_agentprofile --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0",
    "build/Release/NeonBoard.exe --smoke 退出码 0 且无 QML 报错"
  ],
  "scope": ["src/AgentProfile.*", "src/Database.cpp", "src/main.cpp", "qml/Main.qml", "qml/components/SettingsPanel.qml", "tests/tst_agentprofile.cpp", "tests/CMakeLists.txt", "CMakeLists.txt", "docs/agent-profile.md"],
  "depends": ["R6-01"]
},
"R6-04": {
  "id": "e81fa56f-348d-4d47-80b6-376dd8665e64",
  "stage": "R6",
  "title": "R6 单次执行器：QProcess + 流式日志 + 超时/终止",
  "note": "起因：定义、profile、锁都齐了，缺真正拉起 agent 的引擎。目标：TaskRunner 受控启动 QProcess（校验定义 → 取锁 → 建 run），stdout/stderr 按行流式写 runs/<runId>.log 并脱敏 token / key / 长十六进制串，心跳每 10 秒；超时强杀并标 blocked（timeout）；取消先 terminate 后 kill，Windows 用 Job Object 回收整棵进程树；已有活跃 run 时新请求直接拒绝（M1 不排队）。",
  "acceptance": [
    "ctest --test-dir build -C Release -R tst_taskrunner --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0",
    "build/Release/NeonBoard.exe --smoke 退出码 0 且无 QML 报错"
  ],
  "scope": ["src/TaskRunner.*", "src/TaskRunStore.*", "src/main.cpp", "qml/Main.qml", "tests/tst_taskrunner.cpp", "tests/fixtures/**", "tests/CMakeLists.txt", "CMakeLists.txt"],
  "depends": ["R6-02", "R6-03"]
},
"R6-05": {
  "id": "dd95c213-2cba-42a5-b4cf-71773d644b96",
  "stage": "R6",
  "title": "R6 验收器与越界检查",
  "note": "起因：进程退出码 0 不等于任务完成，agent 也可能改到 scope 之外。目标：AcceptanceRunner 顺序执行 acceptance（command 走白名单脚本路径、不经过 shell、拒绝管道与重定向；assert 先支持 file.exists）+ ScopeMatcher 跑前记 git status 基线、跑后比对差异路径与卡片 scope（scope 为空不检查）；三者合并判定 passed / blocked 并写入 last_error。",
  "acceptance": [
    "ctest --test-dir build -C Release -R \"tst_acceptance|tst_scopematcher\" --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0"
  ],
  "scope": ["src/AcceptanceRunner.*", "src/ScopeMatcher.*", "tests/tst_acceptance.cpp", "tests/tst_scopematcher.cpp", "tests/CMakeLists.txt", "CMakeLists.txt"],
  "depends": ["R6-04"]
},
"R6-06": {
  "id": "a62d2df7-3073-4173-9821-b330958ad547",
  "stage": "R6",
  "title": "R6 执行结果回写与执行面板",
  "note": "起因：结果只进 run 表，人看不到也做不了决定。目标：run 终态（状态 / 耗时 / 退出码 / 摘要 / 越界 / 日志路径）回写并在执行面板展示，卡面加执行态徽章；提供查看完整日志、重试（attempt+1）与「标为已办结」（走既有完成路径、可撤销）；程序不自动改看板列、不自动 push。",
  "acceptance": [
    "ctest --test-dir build -C Release -R tst_runsummary --output-on-failure",
    "build/Release/NeonBoard.exe --smoke 退出码 0 且无 QML 报错"
  ],
  "scope": ["src/TaskRunStore.*", "src/main.cpp", "qml/Main.qml", "qml/components/TaskCard.qml", "tests/tst_runsummary.cpp", "tests/CMakeLists.txt", "CMakeLists.txt", "docs/screenshots/**"],
  "depends": ["R6-04", "R6-05"]
},
"R6-07": {
  "id": "d32f7e28-e4cc-4cb6-bb7b-6fe9db6974a9",
  "stage": "R6",
  "title": "R6 Triage：只读分析生成执行草案",
  "note": "起因：一句话需求直接派发，agent 只能猜范围与验收。目标：按 docs/agent-triage.md 的 schema 与 prompt 落地 TriageRunner，用 profile 的只读参数强制只读（分析前后 git status 必须一致，有差异即判失败）；严格 JSON 校验、questions 截断到 3 条、非法 glob 丢弃、acceptance 为空补默认；草案只写 board.db，人在执行页签预览并采纳后才回填表单，confidence 低于 0.5 时禁止一键执行。",
  "acceptance": [
    "ctest --test-dir build -C Release -R tst_triage --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0"
  ],
  "scope": ["src/TriageRunner.*", "src/AgentProfile.*", "src/Database.cpp", "src/TaskExec.*", "src/main.cpp", "qml/Main.qml", "tests/tst_triage.cpp", "tests/CMakeLists.txt", "CMakeLists.txt", "docs/agent-triage.md"],
  "depends": ["R6-01", "R6-03"]
},
"R6-08": {
  "id": "e2ef6d16-e853-4a26-b3a8-b7e5f70bcebb",
  "stage": "R6",
  "title": "R6 Loop：失败摘要回灌与有限重试",
  "note": "起因：agent 首轮失败很常见，重跑时把失败上下文带上才可能自愈。目标：maxAttempts 生效（上限 5、默认 1），失败且未到上限时把 last_error 末尾 200 行注入下一轮 prompt，新建 run 并 attempt+1，每轮日志独立；人取消后不再自动重试，轮数耗尽终态 blocked 并提示人工介入。",
  "acceptance": [
    "ctest --test-dir build -C Release -R tst_runloop --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0"
  ],
  "scope": ["src/TaskRunner.*", "src/TaskRunStore.*", "tests/tst_runloop.cpp", "tests/CMakeLists.txt"],
  "depends": ["R6-04", "R6-05"]
},
"R6-09": {
  "id": "2bcd2b26-1143-495f-991f-a56926ebd529",
  "stage": "R6",
  "title": "R6 台账卡单向导入（可选）",
  "note": "起因：开发卡的 acceptance / scope 在台账里已有一份，board.db 里还要手填一遍，容易漂移。目标：按 ADR-0001 做单向导入——读 board-ledger.json，按短号找到对应任务，只补 task_exec 里为空的 acceptance / scope / goal（note 首段），冲突只报告不覆盖，未建卡只报告不自动建；不提供反向写入台账的入口。本卡不阻塞正式发布。",
  "acceptance": [
    "ctest --test-dir build -C Release -R tst_ledgerimport --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0"
  ],
  "scope": ["src/LedgerImport.*", "src/TaskExec.*", "src/Database.cpp", "src/main.cpp", "qml/Main.qml", "tests/tst_ledgerimport.cpp", "tests/CMakeLists.txt", "CMakeLists.txt", "docs/adr/**"],
  "depends": ["R6-01"]
}
```

baseline 追加（全部 `todo`）：

```jsonc
"R6-01": "todo", "R6-02": "todo", "R6-03": "todo",
"R6-04": "todo", "R6-05": "todo", "R6-06": "todo",
"R6-07": "todo", "R6-08": "todo", "R6-09": "todo"
```

## 7. 波次与工期

```text
波次 1  R6-01 ─┬─ R6-03 ─ R6-04 ─ R6-05 ─ R6-06
        R6-02 ─┘                     ├─ R6-07
                                      └─ R6-08
波次 7  R6-09（可选）
```

| 波次 | 卡片 | 出口 | 累计 |
| --- | --- | --- | --- |
| 1 | R6-01 + R6-02 | 定义可编辑，运行态有表 | 3.5 d |
| 2 | R6-03 | 能拼出正确命令行并拦住人 | 5 d |
| 3 | R6-04 | **第一次真跑** | 8 d |
| 4 | R6-05 | 结果可信 | 10 d |
| 5 | R6-06 | 闭环可见 | 12 d |
| 6 | R6-07 + R6-08 | 分析前置 + 失败自愈 | 16 d |
| 7 | R6-09 | 台账联动（可选） | 17.5 d |

> 工期按熟悉代码库的人估算；agent 执行时主要成本在**验证**（尤其是进程树、超时、只读这三类），
> 每张卡的实际耗时可能翻倍。宁可把验收写细，也不要为赶波次缩验收。

## 8. 非目标（R6 明确不做）

| 不做 | 归属 |
| --- | --- |
| 任务队列 / 并发 > 1 / 夜间批量 | M1 之后（product-design §5） |
| 角色链 workflow（pm → architect → engineer → qa） | M4（product-design §7） |
| 自动把卡移动到 done | 不做：验收通过给人一键动作，列由人控制 |
| 自动 `git push` / 自动开 PR | 不做：push 由人决定（与 CI 回写抢主线） |
| codex profile | 未实测，不进生产 profile（agent-profile.md） |
| 多目录 / 跨仓库任务 | v1 非目标（product-design §11 第 4 条） |
| 市场签名、云端同步、跨机调度 | v1 非目标 |
| 反向写台账（board.db → ledger） | ADR-0001：必须由人确认后提交，不做按钮 |

## 9. 风险与回退

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| agent CLI 不可用 / 未登录 | 执行全部失败 | 执行前探测（`--version` 或 profile 自检），失败即提示而非挂起 |
| 进程失控 / 孙进程残留 | 机器被占满 | Job Object + terminate→kill + 超时强杀（R6-04 验收含孙进程用例） |
| 日志泄密 | 凭据进日志 / 进仓库 | 脱敏 + env 白名单 + `runs/` 落在数据目录（不进仓库） |
| schema 漂移 | 老版本打不开库 | 侧表 `IF NOT EXISTS`、不升 `user_version`、`tst_databasemigration` 覆盖 |
| agent 改出 scope | 意外改动进提交 | R6-05 越界检查；越界即 `blocked` 并列出路径 |
| 执行器把看板搞乱 | 用户不敢用 | 总开关默认关；不改列、不 push；出问题一键关闭，看板本体不受影响 |
| 卡片定义两处漂移 | 执行按错定义跑 | ADR-0001 单向导入 + 冲突只报告；台账仍是开发卡唯一事实源 |

**回退路径**：执行器相关代码全部在新增文件 + `Main.qml` 的新增区块内，总开关默认关；
若线上出问题，关开关即可回到「纯看板」，不需要回滚数据库（`task_run` 留着不影响老版本）。

## 10. 登记清单（维护者执行）

1. 把第 6 节的 9 条卡片与 baseline 追加进 `docs/board-ledger.json`（**agent 不得代劳**）；
2. 提交：`chore(board): 登记 R6 派发器卡片`；
3. 跑 `python scripts/derive_board_status.py --check`，应通过；
4. 确认 `docs/iteration-roadmap.md` 的 R6 里程碑行已就位（本分支已加）；
5. 之后每张卡按 `scripts/agent-init.ps1 -Card R6-0x` 开区，收尾用 `#R6-0x=done` 提交标记结卡。
