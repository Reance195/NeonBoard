# NeonBoard 产品设计稿：从「看板」到「任务派发器」

> 状态：草案（2026-10-27）
> 定位：**个人使用**的本机任务调度器。看板是队列和状态面板，核心是「任务 → agent 执行」这一段自动化。

## 1. 目标与非目标

### 目标

1. 每天把想到的需求**快速收录**成卡片（一句话即可）
2. 卡片能被**分析**成可执行定义（目标 / 验收 / 边界）
3. **一键执行**：自动拉起 agent，在指定目录按指定流程干活
4. 执行结果**自动判定**（验收命令 + 退出码），回写卡片状态
5. 支持 **loop**：失败后带上下文重试，直到成功、轮数耗尽或人叫停

### 非目标（v1 明确不做）

- 多用户 / 团队协作 / 云端同步
- 跨机器调度（只调度本机进程）
- 通用 CI/CD（那是 CI 的活）
- 任意命令编排语言（流程用固定模板，不做 DSL）

## 2. 概念模型

三层输入，缺一层 agent 就会猜：

| 层 | 内容 | 存放位置 | 生命周期 |
| --- | --- | --- | --- |
| **静态上下文** | 项目约定、构建/测试命令、禁止项 | 目标仓库的 `AGENTS.md` | 仓库自带，agent 每次读 |
| **任务定义** | 目标 / 验收 / 边界 / 输入 / 产出 / 依赖 | `docs/board-ledger.json`（git，可评审） | 随卡片长期存在 |
| **执行参数** | agent / cwd / workflow / 超时 / 轮数 | 卡片运行时字段（board.db）+ 全局预设 | 每次执行可调 |

**存储三分**（关键决定）：

| 存储 | 放什么 | 谁写 | 理由 |
| --- | --- | --- | --- |
| `board.db` | 卡片本体、状态、run 记录、triage 草案 | 应用 | 运行时高频写，不能进 git |
| `board-ledger.json` | 卡片的**定义**（id/title/stage + acceptance/scope/depends） | 维护者（人或经人确认的 agent） | 定义要可评审、可回滚 |
| `progress.json` | 状态快照（派生产物） | CI / 派生器 | 让状态可被 git 追述 |

> 约束：agent **不得**直接改台账里的任务定义（`AGENTS.md` 条款 6）。triage 草案先落 `board.db`，
> 人确认后由一次提交写入台账——这一步是**有意的摩擦**，防止 agent 自授权扩大 scope。

## 3. 卡片数据模型

```jsonc
{
  // —— 任务定义（台账，git）——
  "id": "R6-01",
  "stage": "R6",
  "title": "修复统计芯片删除后不刷新",
  "goal": "删除任务后统计数字立即更新",
  "acceptance": ["pwsh -File scripts/verify.ps1"],
  "scope": ["src/TaskModel.cpp", "tests/tst_taskcolumns.cpp"],
  "forbidden": ["不许改数据库 schema", "不许新增依赖"],
  "inputs": ["branch: feature/stats-chip"],
  "deliverable": "commit",
  "depends": ["R5-03"],

  // —— 执行参数（board.db，运行时可调）——
  "exec": {
    "agent": "opencode",
    "cwd": "E:/projects/NeonBoard",
    "workflow": "fix",
    "timeoutMin": 30,
    "maxAttempts": 2,
    "readonlyAnalyze": true
  },

  // —— 运行时状态（board.db）——
  "status": "draft | todo | doing | done | blocked",
  "runId": "run-20261027-0012",
  "attempts": 1,
  "lastError": "ctest 失败：tst_taskcolumns 2 条用例"
}
```

### 字段最小集

MVP 只要三个就能开工：**`goal` + `cwd` + `acceptance`**。
其余字段缺失时按所选 workflow 的角色步骤补默认值。

## 4. 状态机

```
        ┌──────── triage ────────┐
        ↓                        │
    （新建）──▶ draft ──人确认──▶ todo ──执行──▶ doing ──验收通过──▶ done
                  │                 ↑                  │
                  │                 │                  ├─验收失败─▶ blocked ──重试──▶ doing
                  └── 丢弃 ─────────┘                  │
                                                       └─人工取消─▶ todo（附日志）
```

- `draft`：triage 产出的草案，**不可执行**
- `todo`：定义完整，可执行
- `doing`：有活跃 run，**任务锁生效**（同卡不可重复执行）
- `done`：验收通过
- `blocked`：验收失败或超时，附 `lastError` 与日志路径

### 与 `progress.json` 的映射（必须显式，否则派生器会报未知状态）

| board.db 状态 | 派生快照里的 status | 说明 |
| --- | --- | --- |
| `draft` | `todo` | 未确认的草案不对外可见为「进行中」 |
| `todo` | `todo` | — |
| `doing` | `doing` | — |
| `blocked` | `doing` | 卡未完成，仍占用「进行中」语义 |
| `done` | `done` | — |

> 若派生器的状态枚举要扩展，属于独立改动；v1 用映射表即可，不动 `derive_board_status.py`。

### 任务锁的租约与崩溃恢复

锁不能只靠「正在跑」这个隐含状态：

- 锁记录带 `run_id` + `heartbeat_at`，**超过 90 秒无心跳视为孤儿**
- 应用启动时扫描 `doing` 且心跳超时的卡 → 标记 `blocked`（原因：进程中断），释放锁
- 人可在 UI 上「强制释放锁」，需二次确认

## 5. 执行流程

```
点「执行」
  ├─ 前置检查：定义完整？cwd 在白名单？没有正在跑的 run？
  ├─ 创建 run 记录（run_id、started_at、agent、cwd、workflow）
  ├─ 拼 prompt：卡片定义 + workflow（角色步骤）+ triage 结论
  │   （**不塞 AGENTS.md 摘要**——摘要会失真且浪费 token；只要求 agent 先读仓库里的 AGENTS.md）
  │   （M4 起：每步一个角色、一份交接物，下一步读上一步的产出）
  ├─ 启动子进程（QProcess），cwd = 卡片 cwd
  ├─ 流式读取 stdout/stderr → 写 run 日志 + 卡片日志面板
  ├─ 进程退出 → 执行 acceptance 命令
  │    ├─ 退出码 0 且验收 0 → done
  │    └─ 否则 → blocked（记录失败摘要，最多保留末尾 200 行）
  └─ （loop）attempts < maxAttempts → 把失败摘要注入 prompt，重跑
```

### 锁与队列

锁的语义见 §4「任务锁的租约与崩溃恢复」；同一张卡同一时刻只允许一个 run，
锁记录在 `board.db`（`card_id` 唯一索引 + `run_id` + 心跳）。
**不依赖 git ref**（`refs/claims` 是跨机器场景的方案，单机 MVP 不需要）。

### 队列与批量

「每天收录一堆需求」意味着任务会排队，不是一次一张：

- FIFO 队列，可在 UI 里暂停 / 调整顺序 / 跳过
- 并发默认 **1**（4 核 8 线程，多开只会互相拖慢），设置里可调到 2
- **夜间批量模式**：勾选多张卡 → 排队执行 → 早上看结果；UI 给出预计总时长
- 长任务（>10 分钟）必须有心跳：显示已运行时长 + 最近一行输出


## 6. Triage：执行前先分析

在 `draft` 状态调用**只读** agent，输出结构化草案，人确认后才进 `todo`。

```jsonc
{
  "understanding": "一句话复述需求",
  "affected": { "files": [], "modules": [], "risk": "低/中/高 + 理由" },
  "scope_draft": ["允许改动的路径 glob"],
  "acceptance_draft": ["可执行验收项"],
  "questions": ["需要人回答的歧义点"],
  "split": [{ "title": "子任务", "independent": true }],
  "estimate": { "workflow": "fix", "rounds": 1 },
  "confidence": 0.0
}
```

**强制只读**：不是靠 prompt 约定，而是用 agent 的只读模式（如 `--permission-mode plan` / 只读工具集），
或本机沙箱目录 + 禁止写权限。分析阶段**不允许**产生任何副作用。

**采纳规则**（两条路径，别混）：

| 采纳什么 | 写哪 | 谁做 |
| --- | --- | --- |
| `scope_draft` / `acceptance_draft` / `questions` 的答案 | 先写 `board.db` 的卡片字段（立即可用） | 应用，一键 |
| 卡片的**定义**（进 git、可评审） | `docs/board-ledger.json` | **一次提交**，由人确认（或人在应用里点「同步到台账」生成提交草稿） |

> 这是有意保留的摩擦：分析结果是 agent 的建议，进 git 前必须过一次人的确认。

### 验收的两类形态

纯命令白名单太窄（「接口返回 200」这类断言写不了），所以 acceptance 支持两类：

| 类型 | 例子 | 执行者 |
| --- | --- | --- |
| `command` | `pwsh -File scripts/verify.ps1`、`ctest -C Release` | 白名单脚本路径，**禁止管道/重定向** |
| `assert` | `http.get("http://127.0.0.1:8326/health").status == 200`、`file.exists("dist/NeonBoard/NeonBoard.exe")` | 应用内置检查器，不经过 shell |

`assert` 用固定的小语法（http / file / json 三种），由应用自己执行，避免为了跑断言而开放 shell。


## 7. Workflow：角色步骤组合

**「角色」是积木，「workflow」是积木的有序组合。** 不做固定团队——那是「0→1 交付型」产品的形态；
个人日常是「一堆大小不一的小需求」，必须按任务大小拼装。

### 角色步骤的结构

| 字段 | 说明 |
| --- | --- |
| `role` | `pm` / `architect` / `engineer` / `qa` / `writer` / `orchestrator` |
| `agent` | 该步用哪个 agent（缺省继承卡片 `exec.agent`） |
| `prompt` | 步骤模板：指令 + 产出要求 |
| `inputs` | 依赖的交接物或卡片字段 |
| `outputs` | 本步产出的交接物（路径 + 内容契约） |
| `acceptance` | 本步验收（`command` / `assert`；缺省用卡片级 acceptance） |
| `scope` | 本步允许改的路径（只读角色为空；缺省用卡片 scope） |

### 交接物（handoff）

每一步产出一个文件，就是下一步的输入：

| 角色 | 产出 | 路径约定 |
| --- | --- | --- |
| `pm` | 需求规格 | `.neonboard/handoff/<card>/spec.md` |
| `architect` | 技术方案 | `.neonboard/handoff/<card>/design.md` |
| `engineer` | 代码改动 | 工作区（`git diff` 即交接物） |
| `qa` | 测试报告 | `.neonboard/handoff/<card>/qa.md` |
| `orchestrator` | 收口摘要 | `.neonboard/handoff/<card>/summary.md` |

### 预设 workflow（可编辑，存 board.db）

| workflow | 步骤序列 | 适用 |
| --- | --- | --- |
| `fix` | engineer → qa | 一行修复、小 bug |
| `feature` | pm → architect → engineer → qa | 新功能 |
| `hotfix` | engineer | 紧急，跳过评审 |
| `docs` | writer | 纯文档 |
| `review` | orchestrator（只读） | 只看不改 |
| `delivery` | pm → architect → engineer → qa → orchestrator | 0→1 大活 |

卡片可以覆盖单步参数（换 agent、跳过某步），但不能跳步改变交付物契约。

### 主理人（orchestrator）职责

1. 任务进来时**选 workflow**（按 triage 的 `estimate` 或卡片大小）
2. 某步失败时决策：重试 / 换角色 / 降级跳过 / 升级给人
3. 收口时确认所有 acceptance 通过，写 `summary.md`
4. **不写代码**——只读角色，避免既当裁判又当运动员

> **与 WorkBuddy「软件开发团队」的对照**：对方是「1 主理人 + 4 角色」的固定编制，适配「从 0 到 1 交付一个 App」；
> 本设计把角色做成可组合步骤，适配「每天一堆大小不一的小需求」。核心差异是**固定编制 vs 按需拼装**。
> 可借鉴的是「主理人显式存在」和「交接物显式化」，不可借鉴的是固定 5 人编制与云端黑盒。

## 8. 安全边界

| 风险 | 约束 |
| --- | --- |
| 执行任意命令 | 命令来自 **workflow 角色步骤 + 卡片字段的受控插值**，卡片不能直接提供 shell 片段 |
| 目录越界 | `cwd` 必须命中**已登记的工作目录列表**（首次使用时确认一次） |
| 卡片被污染 | `acceptance` 只允许白名单命令（`verify.ps1` / `ctest` / 脚本路径），不允许管道与重定向 |
| 密钥泄露 | 日志脱敏（匹配 token 模式）；prompt 不回显环境变量 |
| 无限循环 | `maxAttempts` 上限 + 单次 `timeoutMin` + 人可随时终止 |
| 副作用 | triage 只读；执行前展示「将执行的命令 + 目录」供确认 |
| 自动推送 | 默认 `deliverable: commit`，**执行完不自动 push**；推送由人决定（避免与 CI 回写抢主线） |

## 9. 与现有实现的映射

| 现有 | 复用方式 |
| --- | --- |
| `AGENTS.md` | 静态上下文，执行 prompt 自动带摘要 |
| `docs/task-card.md` | 任务定义字段规范（已落地） |
| `agent-task-event` 协议 | 扩展事件域：`agent.triage` / `agent.run.*`，让外部 agent 也能投递状态 |
| `Plan hook` / `hook.commit` | 执行/验收结果可投递事件，驱动看板更新 |
| 插件系统 | executor 可作为**内置能力**，也可先做成 `plugins/agent-exec` 原型 |
| CI 的 `verify.ps1` | 默认 acceptance 命令 |
| `refs/claims` | MVP 不用；未来多机/多 worktree 时启用 |

## 10. MVP 拆分

### M0：卡住模型（不写执行引擎）
- 卡片新增 `goal` / `acceptance` / `scope` / `exec` 字段（存 board.db）
- 卡片详情页两个 Tab：任务 / 执行
- 状态机落库（draft/todo/doing/done/blocked）

### M1：单次执行
- `QProcess` 拉起 agent，流式日志，退出后跑 acceptance
- 任务锁 + 并发 1 + 手动终止
- 执行前确认框（命令 + 目录）

### M2：Triage
- 只读 agent 分析 → 草案 → 人确认 → 回填
- 分析结果随执行 prompt 下发

### M3：Loop
- 失败摘要回灌 + `maxAttempts` + 人可中断
- 每轮记录 attempt 日志

### M4：角色链
- `workflow` 从「单步」升级为「角色步骤数组」（§7）
- 每步一张子卡、一个交接物文件、一份独立验收
- `orchestrator` 选 workflow、决策失败分支、收口写 summary
- 预设：`fix` / `feature` / `hotfix` / `docs` / `review` / `delivery`

## 11. 开放问题

| # | 问题 | 状态 |
| --- | --- | --- |
| 1 | **agent 适配层**：opencode / claude / codex 的只读模式、非交互参数、输出格式差异，是否抽象成 `agent profile`（`{ exe, args, readonlyArgs, env }`）？ | 待定，M1 前必须定 |
| 2 | **日志存储**：建议按 run 存文件（`%APPDATA%/com.lin/NeonBoard/runs/<run_id>.log`）+ db 只存索引与摘要，避免 db 膨胀 | 倾向方案已给，待确认 |
| 3 | **执行期沙箱**：agent 可能改到 `scope` 之外的路径，是否在 run 结束后用 `scope` 做一次 diff 校验（越界即 blocked）？ | 建议做，复用 `check_scope.py` 思路 |
| 4 | **多目录任务**：一张卡跨两个仓库 | **v1 不支持**，明确写进「非目标」 |
| 5 | **与 git 流程的关系**：执行产生的提交是否自动做领用占位（多 agent 并行时） | v1 不做；单机靠 board.db 的锁 |
| 6 | **成本可视化**：记录每次 run 的耗时与 token，给卡片一个「花了多少」的账 | 建议 M4 做，先记耗时 |
| 7 | **失败后给谁**：`blocked` 卡是自动进下轮，还是必须人看一眼再重试 | 建议默认人工确认，可开「自动重试 1 次」 |
| 8 | **角色链的跳过规则**：小任务走 `fix` 时，是否允许 orchestrator 自动跳过 `qa`？ | 建议不允许跳 qa（验收是最后防线），可允许跳 pm/architect |
| 9 | **交接物版本化**：`.neonboard/handoff/` 要不要进 git（可评审）还是只留本地？ | 建议进 git 的子集：spec/design/qa 摘要，完整日志不进 |

