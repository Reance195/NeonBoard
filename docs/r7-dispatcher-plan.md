# R7 派发器进阶：队列编排 + 多 Agent 角色链

> 状态：**规划中（2026-10-27）**
> 上游设计：[`product-design.md`](product-design.md)（§5 队列与批量 / §7 Workflow / M4 角色链）、[`iteration-roadmap.md`](iteration-roadmap.md) §9.2
> 前置里程碑：[`r6-dispatcher-plan.md`](r6-dispatcher-plan.md)（M1 单次执行）
> 决策记录：复用 [`adr/0001-task-definition-source.md`](adr/0001-task-definition-source.md)（定义单源）/ [`adr/0002-run-state-and-executor-safety.md`](adr/0002-run-state-and-executor-safety.md)（运行态不进看板状态机）
> 卡片规范：[`task-card.md`](task-card.md)

## 0. 一页速览

R6 把「一张卡一键执行」收口（M1 单次执行）。R7 让派发器从「能跑一张」升级为「能编排一堆」：

```text
勾选多张卡 → 进入队列（FIFO）
   ↓ 调度器按并发≤2 取出、背压控速
拉起 agent（复用 R6 执行器骨架）
   ↓ 角色链：pm → architect → engineer → qa，每步一份 handoff 交接物
orchestrator 收口写 summary
   ↓ 验收（复用 R6-05）+ 越界检查
run 落库（queue / running / passed / blocked）+ 成本账（耗时）
```

**R7 出口标准**

1. 多张卡可入队，队列可暂停 / 恢复 / 调序 / 跳过；
2. 并发受控（默认 1，最高 2），不互相拖慢（背压 / 限流）；
3. 角色链按序接力，每步交接物落盘、下一步读上一步产出、验收各自独立；
4. 夜间批量：勾选 → 排队 → 次日看结果，UI 给预计总时长；
5. 成本账：每 run 记录耗时，卡片可见累计成本；
6. 执行器总开关默认关闭时，R7 全部能力不可见、不影响看板本体。

## 1. 为什么现在做

| 事实 | 含义 |
| --- | --- |
| R6 把「单次执行（M1）」收口，引擎骨架（TaskRunner / TaskRunStore / AcceptanceRunner / ScopeMatcher）已就位 | R7 直接复用，不必重新造执行器 |
| product-design §5 / §7 早已规划队列与角色链，但被 R6 计划列为「M1 之后」 | 推进顺序是产品路线既定，不是新发明 |
| 看板本体 92/92 + R6 之后，单张执行只能算「能跑」 | 批量 + 角色链才是「能编排」，才真正替代人肉把一堆需求喂给 agent |
| R6-05 的越界检查与独立验收器已可用 | 角色链每步的「独立验收」有现成能力可复用 |

方向判断：R7 是 R6 的自然延伸，复用其存储与执行层，只新增「调度」与「编排」两层。

## 2. 现状与差距

| 已有（R6 交付） | 位置 | R7 要补的缺口 |
| --- | --- | --- |
| 执行定义侧表 `task_exec`（goal / acceptance / scope / agent / cwd / workflow / timeout / maxAttempts） | `src/TaskExec.*` | 缺 `workflow` 角色步骤数组字段与 handoff 产出约定 |
| 运行记录侧表 `task_run`（queued / running / passed / blocked / cancelled） | `src/TaskRunStore.*` | 缺「队列」状态与跨 run 的编排关系（parent run / child steps）；并发 >1 的锁需从「同卡唯一」扩展为「全局并发上限」 |
| 单次执行器 `TaskRunner`（QProcess + 流式日志 + 超时 / 终止） | `src/TaskRunner.*` | 缺队列消费循环、背压、并发调度；缺角色链逐步拉起与交接物注入 |
| 验收器与越界检查 | `src/AcceptanceRunner.*` / `src/ScopeMatcher.*` | 已可复用为「每步独立验收」；需支持 step 级 scope / acceptance |
| 执行面板 | `qml/Main.qml` | 缺队列视图、并发指示、预计总时长、成本账展示 |
| 执行前确认框 | `qml/Main.qml` | 角色链需在确认框展示完整 workflow 步骤与交接物契约 |

## 3. 已定架构决策（摘要）

复用 R6 的两条 ADR，不另起炉灶。

### 3.1 运行态仍不进看板状态机（沿用 ADR-0002）

- `task_run` 侧表扩展：新增 `queue_status`（waiting / dispatching / running / done）、`parent_run_id`（角色链母 run）、`step_index`、`workflow`、`role`、`handoff_path`、`cost_ms`；
- 看板列仍只有 `todo / doing / done`，**程序不自动改列**；队列 / 角色链状态只体现在执行面板与队列视图；
- **不升 `user_version`**，老版本程序照常开库（新增列走 `IF NOT EXISTS`，缺失列读默认）。

### 3.2 队列而非拒绝（M1 之后）

- R6 忙时拒绝；R7 引入 FIFO 队列：新请求入队，调度器按并发上限（默认 1，设置可到 2）取出；
- 背压：并发达上限时队列暂停派发；长任务（>10 min）必须有心跳（R6 已有 10 s 心跳），UI 显示已运行时长 + 最近一行；
- 队列可暂停 / 恢复 / 调序 / 跳过单个 run。

### 3.3 角色链复用 R6 执行器

- `workflow` 从「单步」升级为「角色步骤数组」（product-design §7）；每步 = 一个角色 + 一份 prompt 模板 + 输入（上一步 handoff）+ 输出（本步 handoff）+ 独立 acceptance + 独立 scope；
- 每步产出一个交接物文件 `.neonboard/handoff/<card>/<role>.md`（pm→spec / architect→design / engineer→git diff / qa→qa / orchestrator→summary）；下一步 prompt 注入上一步 handoff 全文；
- `orchestrator` 步骤：选 workflow（按 triage 的 `estimate` 或卡片大小）、决策失败分支（如 qa 失败回灌 engineer）、收口写 `summary.md`；
- 预设 workflow（存 `board.db`，可编辑）：`fix`(engineer→qa) / `feature`(pm→architect→engineer→qa) / `hotfix`(engineer) / `docs`(writer) / `review`(orchestrator 只读) / `delivery`(pm→architect→engineer→qa→orchestrator)。

### 3.4 安全默认（沿用 R6 §3.4 并扩展）

| 约束 | 默认值 |
| --- | --- |
| 执行器总开关 | **关**（R7 能力默认不可见） |
| 并发上限 | 默认 1，设置可到 2（4 核 8 线程，多开只会互相拖慢） |
| 角色链每步 | 强制独立 acceptance + scope 越界检查（复用 R6-05） |
| 交接物 | 落数据目录 `.neonboard/handoff/`，不进仓库（与 `runs/` 同待遇） |
| 自动推送 | **禁止**；每步 / push 由人决定 |
| 成本账 | 先记耗时（`cost_ms`），token 计量后续；不泄露（日志已脱敏） |

## 4. 卡片拆分总览

| 短号 | 标题 | 依赖 | 预估 | 波次 |
| --- | --- | --- | --- | --- |
| R7-01 | 执行队列与调度器：FIFO + 并发≤2 + 背压 + 暂停/调序/跳过 | R6-04, R6-06 | 3 d | 1 |
| R7-02 | 队列视图与并发指示 UI | R7-01 | 2 d | 2 |
| R7-03 | 角色链 workflow 引擎：步骤数组 + handoff 交接物 | R6-05, R7-01 | 3 d | 2 |
| R7-04 | 预设 workflow + orchestrator 收口 | R7-03 | 2.5 d | 3 |
| R7-05 | 夜间批量模式：勾选多卡 → 排队 → 预计总时长 | R7-01, R7-02 | 2 d | 3 |
| R7-06 | 成本账：每 run 记耗时 + 卡片累计成本展示 | R6-02, R7-01 | 1.5 d | 4 |

合计约 14 人日（agent 实际执行会更快，按 1.5–2 倍折算为开发会话）。

**关键路径**：`R7-01 → R7-03 → R7-04`；`R7-02` / `R7-05` 可与 R7-03 之后的收尾并行；`R7-06` 收尾。

**波次说明**

| 波次 | 卡片 | 意义 |
| --- | --- | --- |
| 1 | R7-01 | 队列 + 调度器落地，MVP 分水岭 |
| 2 | R7-02 + R7-03 | 队列可见 + 角色链引擎 |
| 3 | R7-04 + R7-05 | 预设工作流收口 + 夜间批量 |
| 4 | R7-06 | 成本账 |

## 5. 卡片定义

> 每张卡的 `scope` 已收窄到最小改动集；动手前先读 `NB_SCOPE`，收尾跑 `scripts/verify.ps1`。
> `note` 首段是「起因/背景」，合并提交 body 四段见 `docs/task-card.md`。

### R7-01 执行队列与调度器：FIFO + 并发≤2 + 背压 + 暂停/调序/跳过

**目标**：把 R6 的「忙则拒绝」升级为 FIFO 队列 + 并发调度。

**要点**

- `src/Database.cpp` / `src/TaskRunStore.*`：扩展 `task_run` 增加 `queue_status TEXT`（waiting / dispatching / running / done）、`position INTEGER`、`parent_run_id TEXT`、`step_index INTEGER`、`workflow TEXT`、`role TEXT`、`handoff_path TEXT`、`cost_ms INTEGER`；全局并发上限存 `meta` 表 `exec.concurrency`（默认 1，≤2）。**不升 `user_version`**，新列 `IF NOT EXISTS`。
- `src/Scheduler.*`（新）：队列消费循环，按 `position` 取 waiting 中最早的、并发未达上限则置 dispatching → 拉起 `TaskRunner`（复用 R6-04）；达上限则暂停；心跳超时回收（复用 R6-02 孤儿回收）。
- 队列操作：`pause` / `resume` / `reorder(targetPosition)` / `skip`（标记 done 不执行，记 skipped）；均落库，UI 订阅。
- 背压：并发达上限停止派发；长任务（>10 min）心跳已存在，UI 显示已运行时长 + 最近一行（R7-02）。

**验收**

```text
ctest --test-dir build -C Release -R tst_scheduler --output-on-failure
build/Release/NeonBoard.exe --selftest          # 退出码 0，断言 task_run 新列存在
```

- 单测：入队 3 张 → 并发 1 时依次 running；并发 2 时两张同时 running；`skip` 后该 run 标记 done 不执行；`reorder` 改变派发顺序；孤儿回收（心跳 >90 s 标记 blocked）。
- 显式断言 `user_version` 未变。

**scope**：`src/Database.cpp`、`src/TaskRunStore.*`、`src/Scheduler.*`、`src/main.cpp`、`tests/tst_scheduler.cpp`、`tests/CMakeLists.txt`、`CMakeLists.txt`

**依赖**：R6-04、R6-06

### R7-02 队列视图与并发指示 UI

**目标**：让用户看见队列并能操作。

**要点**

- `qml/Main.qml` 新增「执行队列」面板（侧栏项或弹层）：列出 waiting / running 的 run（卡标题、步骤、已运行时长、最近一行日志）、并发指示（x/N）、预计剩余（基于历史耗时估算）；
- 操作：暂停 / 恢复队列、调序（上移 / 下移）、跳过单条；
- 执行中卡面徽章增加「队列中」态（时钟）；running 脉冲沿用 R6-06。

**验收**

```text
build/Release/NeonBoard.exe --selftest          # 退出码 0
build/Release/NeonBoard.exe --smoke            # 退出码 0，无 QML 报错
```

- 手工：入队多卡 → 面板显示顺序与并发指示；暂停后不再派发；调序后顺序改变；跳过单条不执行且标记。
- 截图：`docs/screenshots/exec-queue.png`。

**scope**：`qml/Main.qml`、`qml/components/TaskCard.qml`、`src/main.cpp`、`docs/screenshots/**`

**依赖**：R7-01

### R7-03 角色链 workflow 引擎：步骤数组 + handoff 交接物

**目标**：`workflow` 从单步升级为角色步骤数组，每步独立执行 + 验收 + 交接物。

**要点**

- `src/TaskExec.*` 扩展：`workflow` 字段支持 `{steps:[{role,agent,prompt,inputs,outputs,acceptance,scope}]}`；卡片可覆盖单步参数（换 agent、跳过某步），但不能跳步改变交付物契约；
- `src/TaskRunner.*` 扩展：按 steps 顺序逐步拉起 agent（复用 R6-04 单步执行）；每步前记 `git status` 基线，步后 `ScopeMatcher` 比对（复用 R6-05）；
- 交接物：每步产出 `.neonboard/handoff/<card>/<role>.md`（pm=spec / architect=design / engineer=git diff 摘要 / qa=qa / orchestrator=summary）；下一步 prompt 注入上一步 handoff 全文；
- 每步独立 run 记录（`parent_run_id` 关联，`step_index` 递增，`cost_ms` 累计）。

**验收**

```text
ctest --test-dir build -C Release -R "tst_workflow|tst_scopematcher" --output-on-failure
build/Release/NeonBoard.exe --selftest
```

- `fix` 流程 engineer → qa 两步顺序执行且 qa 读 engineer 的 git diff；`feature` 四步按序；handoff 文件落盘且内容非空；越界检查每步独立；`cost_ms` 累计。
- 单测用临时 git 仓库 fixture，不碰真实工作区。

**scope**：`src/TaskRunner.*`、`src/TaskExec.*`、`src/ScopeMatcher.*`、`src/AcceptanceRunner.*`、`tests/tst_workflow.cpp`、`tests/CMakeLists.txt`、`CMakeLists.txt`

**依赖**：R6-05、R7-01

### R7-04 预设 workflow + orchestrator 收口

**目标**：内置可编辑预设，orchestrator 决策失败分支并收口。

**要点**

- `src/WorkflowPresets.*`：预设 `fix` / `feature` / `hotfix` / `docs` / `review` / `delivery`（见 product-design §7），存 `board.db` meta，可编辑；
- `orchestrator` 步骤：按 triage 的 `estimate` 或卡片大小选 workflow；失败分支（qa 失败 → 回灌 engineer，受 `maxAttempts` 约束）；收口写 `summary.md` 并给出「建议下一步」；
- 跳过规则：小任务走 `fix` 不允许跳 qa（验收是最后防线），可跳 pm / architect（开放问题 #8）；
- 执行前确认框（R6-03）展示完整 workflow 步骤与每步交接物契约。

**验收**

```text
ctest --test-dir build -C Release -R tst_workflowpresets --output-on-failure
build/Release/NeonBoard.exe --selftest
```

- `feature` 默认四步；`hotfix` 单 engineer 跳过评审；qa 失败回灌 engineer 受 `maxAttempts` 限制；orchestrator `summary.md` 落盘。

**scope**：`src/WorkflowPresets.*`、`src/TaskRunner.*`、`src/AgentProfile.*`、`tests/tst_workflowpresets.cpp`、`tests/CMakeLists.txt`、`CMakeLists.txt`

**依赖**：R7-03

### R7-05 夜间批量模式

**目标**：勾选多卡 → 排队 → 次日看结果，UI 给预计总时长。

**要点**

- 看板多选（Ctrl / Shift 多选卡）→「加入夜间队列」→ 全部入队（R7-01）；
- 预计总时长：基于各卡历史平均耗时（或 triage estimate）求和，UI 显示「预计 X 分钟，约 HH:MM 完成」；
- 完成后通知（复用 R1 提醒 / 托盘辉光）；失败卡标记 blocked 不阻断其余。

**验收**

```text
ctest --test-dir build -C Release -R tst_nightbatch --output-on-failure
build/Release/NeonBoard.exe --selftest
```

- 多选入队顺序正确；预计时长计算合理；单卡失败不阻断队列其余。

**scope**：`qml/Main.qml`、`src/Scheduler.*`、`src/main.cpp`、`tests/tst_nightbatch.cpp`、`tests/CMakeLists.txt`

**依赖**：R7-01、R7-02

### R7-06 成本账

**目标**：记录每 run 耗时，卡片可见累计成本。

**要点**

- `task_run.cost_ms` 在 run 结束写入（running 时长，不含排队）；
- `src/TaskRunStore.*` 提供 `totalCost(cardId)` 聚合；
- 执行面板（R6-06）与卡面显示「本卡累计耗时 X」；token 计量留接口（开放问题 #6，后续）。

**验收**

```text
ctest --test-dir build -C Release -R tst_taskruns --output-on-failure
build/Release/NeonBoard.exe --selftest
```

- run 结束 `cost_ms > 0`；`totalCost` 累加正确；面板显示。

**scope**：`src/TaskRunStore.*`、`src/main.cpp`、`qml/Main.qml`、`qml/components/TaskCard.qml`、`tests/tst_taskruns.cpp`

**依赖**：R6-02、R7-01

## 6. 台账登记片段（维护者执行）

> ⚠️ 按 `AGENTS.md` 条款 6，**agent 不得写台账 `cards` / `baseline`**。
> 以下片段由维护者粘贴到 `docs/board-ledger.json`，或由维护者明确授权后再由 agent 执行。
> `baseline` 全部登记为 `"todo"`，否则「已完成卡不可重新领用」的闸门会失效。
> `id` 由维护者生成（下方为占位，替换成真实 UUID）。

```jsonc
"R7-01": {
  "id": "00000000-0000-0000-0000-000000000701",
  "stage": "R7",
  "title": "R7 执行队列与调度器：FIFO + 并发≤2 + 背压 + 暂停/调序/跳过",
  "note": "起因：R6 忙时直接拒绝，多卡只能一张张手工触发，派发器还谈不上「编排」。目标：新增 task_run 队列字段与全局并发上限（meta exec.concurrency，默认 1≤2），落地 Scheduler 消费循环（按 position 取最早、并发达上限暂停、心跳超时回收），支持 pause/resume/reorder/skip；不升 user_version，新列 IF NOT EXISTS。",
  "acceptance": [
    "ctest --test-dir build -C Release -R tst_scheduler --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0 且断言 task_run 新列存在、user_version 未变"
  ],
  "scope": ["src/Database.cpp", "src/TaskRunStore.*", "src/Scheduler.*", "src/main.cpp", "tests/tst_scheduler.cpp", "tests/CMakeLists.txt", "CMakeLists.txt"]
},
"R7-02": {
  "id": "00000000-0000-0000-0000-000000000702",
  "stage": "R7",
  "title": "R7 队列视图与并发指示 UI",
  "note": "起因：队列在后台跑，用户看不见也操作不了。目标：新增「执行队列」面板，列出 waiting/running 的 run（卡标题/步骤/已运行时长/最近一行日志）、并发指示 x/N、预计剩余；支持暂停/恢复队列、调序、跳过单条；卡面增加「队列中」态徽章。",
  "acceptance": [
    "build/Release/NeonBoard.exe --selftest 退出码 0",
    "build/Release/NeonBoard.exe --smoke 退出码 0 且无 QML 报错"
  ],
  "scope": ["qml/Main.qml", "qml/components/TaskCard.qml", "src/main.cpp", "docs/screenshots/**"]
},
"R7-03": {
  "id": "00000000-0000-0000-0000-000000000703",
  "stage": "R7",
  "title": "R7 角色链 workflow 引擎：步骤数组 + handoff 交接物",
  "note": "起因：R6 只支持单步执行，复杂需求仍要人肉拆。目标：task_exec 的 workflow 升级为角色步骤数组（pm/architect/engineer/qa/writer/orchestrator），每步独立 acceptance+scope，逐步拉起 agent，每步产出 .neonboard/handoff/<card>/<role>.md，下一步注入上一步 handoff；复用 R6-04/R6-05。",
  "acceptance": [
    "ctest --test-dir build -C Release -R \"tst_workflow|tst_scopematcher\" --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0"
  ],
  "scope": ["src/TaskRunner.*", "src/TaskExec.*", "src/ScopeMatcher.*", "src/AcceptanceRunner.*", "tests/tst_workflow.cpp", "tests/CMakeLists.txt", "CMakeLists.txt"]
},
"R7-04": {
  "id": "00000000-0000-0000-0000-000000000704",
  "stage": "R7",
  "title": "R7 预设 workflow + orchestrator 收口",
  "note": "起因：角色链需要开箱即用的组合与收口决策。目标：内置可编辑预设 fix/feature/hotfix/docs/review/delivery（存 board.db meta），orchestrator 按 triage estimate 选 workflow、qa 失败回灌 engineer（受 maxAttempts 约束）、收口写 summary.md；小任务可跳 pm/architect 不可跳 qa；执行前确认框展示完整步骤。",
  "acceptance": [
    "ctest --test-dir build -C Release -R tst_workflowpresets --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0"
  ],
  "scope": ["src/WorkflowPresets.*", "src/TaskRunner.*", "src/AgentProfile.*", "tests/tst_workflowpresets.cpp", "tests/CMakeLists.txt", "CMakeLists.txt"]
},
"R7-05": {
  "id": "00000000-0000-0000-0000-000000000705",
  "stage": "R7",
  "title": "R7 夜间批量模式：勾选多卡 → 排队 → 预计总时长",
  "note": "起因：每天收录一堆需求，手动一张张触发太低效。目标：看板多选卡 → 加入夜间队列 → 全部入队；基于历史平均耗时估算总时长并展示「约 HH:MM 完成」；完成后通知，单卡失败不阻断其余。",
  "acceptance": [
    "ctest --test-dir build -C Release -R tst_nightbatch --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0"
  ],
  "scope": ["qml/Main.qml", "src/Scheduler.*", "src/main.cpp", "tests/tst_nightbatch.cpp", "tests/CMakeLists.txt"]
},
"R7-06": {
  "id": "00000000-0000-0000-0000-000000000706",
  "stage": "R7",
  "title": "R7 成本账：每 run 记耗时 + 卡片累计成本展示",
  "note": "起因：执行了多少算不清，优化无从谈起（开放问题 #6）。目标：task_run.cost_ms 记 running 时长（不含排队），TaskRunStore 提供 totalCost 聚合，执行面板与卡面展示本卡累计耗时；token 计量留接口后续。",
  "acceptance": [
    "ctest --test-dir build -C Release -R tst_taskruns --output-on-failure",
    "build/Release/NeonBoard.exe --selftest 退出码 0"
  ],
  "scope": ["src/TaskRunStore.*", "src/main.cpp", "qml/Main.qml", "qml/components/TaskCard.qml", "tests/tst_taskruns.cpp"]
}
```

baseline 追加（全部 `todo`）：

```jsonc
"R7-01": "todo", "R7-02": "todo", "R7-03": "todo",
"R7-04": "todo", "R7-05": "todo", "R7-06": "todo"
```

## 7. 波次与工期

| 波次 | 卡片 | 出口 | 累计 |
| --- | --- | --- | --- |
| 1 | R7-01 | 队列 + 调度器可用 | 3 d |
| 2 | R7-02 + R7-03 | 队列可见 + 角色链引擎 | 8 d |
| 3 | R7-04 + R7-05 | 预设工作流收口 + 夜间批量 | 12.5 d |
| 4 | R7-06 | 成本账 | 14 d |

> 工期按熟悉代码库的人估算；agent 执行时主要成本在**验证**（并发 / 角色链 / handoff 一致性），
> 每张卡的实际耗时可能翻倍。宁可把验收写细，也不要为赶波次缩验收。

## 8. 非目标（R7 明确不做）

| 不做 | 归属 |
| --- | --- |
| 任务队列持久化跨会话恢复中断的任务 | v1 非目标：队列在内存，重启清空（与 R6 的 task_run 持久化不冲突，run 记录仍在） |
| 自动把卡移动到 done | 不做：验收通过给人一键动作，列由人控制（沿用 R6-06） |
| 自动 `git push` / 自动开 PR | 不做：push 由人决定 |
| 跨仓库 / 多目录任务并发 | v1 非目标（product-design §11 第 4 条） |
| 市场签名、云端同步、跨机调度 | v1 非目标 |
| 完整 token 成本计量 | 先记耗时，token 后续（开放问题 #6） |
| 动态增删队列中的卡定义 | 入队后定义冻结，改定义需跳过重排 |

## 9. 风险与回退

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| 并发 >1 互相拖慢 | 多开反而更慢（4 核 8 线程） | 并发上限默认 1，设置最高 2；背压暂停派发 |
| 角色链 handoff 不一致 | 下一步读错上一步产出 | handoff 路径契约固定（`.neonboard/handoff/<card>/<role>.md`），单测校验落盘与注入 |
| 长任务卡死占满并发 | 队列永久阻塞 | 心跳超时回收（复用 R6-02）+ 每步独立超时 |
| 执行器把看板搞乱 | 用户不敢用 | 总开关默认关；不改列、不 push；出问题一键关闭，看板本体不受影响 |
| 队列内存态丢失 | 重启后排队清空 | 队列元信息落 task_run（queue_status/position），重启可重建；进行中 run 走孤儿回收 |

**回退路径**：执行器相关代码全部在新增文件 + `Main.qml` 的新增区块内，总开关默认关；
若线上出问题，关开关即可回到「纯看板」，不需要回滚数据库。

## 10. 登记清单（维护者执行）

1. 把第 6 节的 6 条卡片与 baseline 追加进 `docs/board-ledger.json`（**agent 不得代劳**）；
2. 提交：`chore(board): 登记 R7 派发器进阶卡片`；
3. 跑 `python scripts/derive_board_status.py --check`，应通过；
4. 确认 `docs/iteration-roadmap.md` 的 R7 里程碑行与 §9.2 已就位（本分支已加）；
5. 之后每张卡按 `scripts/agent-init.ps1 -Card R7-0x` 开区，收尾用 `#R7-0x=done` 提交标记结卡。
