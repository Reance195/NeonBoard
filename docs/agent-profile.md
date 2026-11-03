# Agent Profile：把不同 CLI 的差异收敛成一个配置

> 目的：executor 不关心底层是 opencode / claude / cursor-agent / codex，
> 只消费统一的 `agent profile`。新增一个 agent = 加一条配置，不改执行引擎。

## Profile 结构

```jsonc
{
  "id": "opencode",                     // 唯一 id：[A-Za-z0-9][A-Za-z0-9._-]{0,63}
  "displayName": "opencode",
  "exe": "opencode",                    // 可执行名或绝对路径
  "args": ["run", "--format", "json"],
  "promptMode": "arg",                  // arg（拼进 argv）| stdin（由执行器写标准输入）
  "readonlyArgs": ["--agent", "plan"],  // 只读模式追加的参数；空表示该 agent 无只读开关
  "output": "json",                     // stream-json | json | text
  "env": { "NO_COLOR": "1" },
  "defaultTimeoutMin": 30,               // 1–480
  "stdin": "null",                       // 可选：把子进程标准输入接到 NUL（codex 必须）
  "enabled": false                       // 可选：false = 声明了但默认不启用，不可派活
}
```

命令行**只按 argv 拼装**：`exe + args + readonlyArgs（只读时）+ prompt（promptMode=arg 时）`，
不经过 shell；界面上的完整命令行只是同一份 argv 的展示串（含引号转义），执行侧不使用它。

标准输入的三条路（R9-05）互斥，按此优先级：

| 声明 | 执行器行为 |
| --- | --- |
| `stdin: "null"` | `setStandardInputFile(nullDevice())` —— 子进程读到 EOF，不会卡在等输入 |
| `promptMode: "stdin"` | 进程起来后 `write(prompt)` + `closeWriteChannel()` |
| 都没声明 | 不碰，交给子进程继承 |

`stdin` 只认 `"null"` 一个值，写错（如 `"nul"`）会让**整条 profile 判非法**而不是静默忽略 ——
忽略的症状是子进程卡在 `Reading additional input from stdin...` 直到超时，看起来像模型慢。

`enabled: false` 是"声明了但默认不启用"：profile 仍出现在列表里（可编辑、可改成启用），
但 `isAvailable()` 直接判否，派活时不会被选中。codex 就卡在这条上 —— 参数看过，
整轮 `exec` 没跑通，按 §12.1 不得进入可选列表。

`available`（每行的"能不能派活"）与 `enabled` 一起构成判断，两者都只进返回值**不落盘**：
PATH 会变、盘会拔，写进 meta 就成了谎话。

## 存储与生效

| meta 键 | 内容 |
| --- | --- |
| `exec.agentProfileOverrides` | 用户层**增量**覆盖（JSON 对象 `id → profile`，`null` 为墓碑）；键不存在 = 完全跟随 L0 ∪ L1 |
| `exec.enabled` | 执行器总开关，`"1"` 为开，**默认关** |
| `exec.defaultAgent` | 默认 agent id（卡片未填 agent 时使用） |
| `exec.trustedCwds` | cwd 白名单（JSON 数组），首次执行目录需在确认框勾选「信任本目录」 |

- 生效列表是三层合并（R9-01）：内置 L0 < 插件 `contributes.agentProfiles`（L1）< 用户覆盖（L2）。
  用户层只存差异，改回与下层一致即等于撤销覆盖。
- 旧版 `exec.agentProfiles`（整份快照）在首次读取时**一次性迁移**成 overrides 并删除该键；
  迁移只给内置 id 立墓碑——插件声明的 agent 在快照成形的年代还不存在，不能被当作「用户删过」。
- 只写 `meta` 表，**不升 `user_version`、不动 `task` 表**。
- 内置三条：`opencode` / `claude` / `cursor-agent`（见下表）。**codex 不内置**——
  参数面已实测（见下），但两次 `codex exec` 都没在 200 s / 420 s 内跑完一个回合，
  按 §12.1「禁止内置未实测 profile」的标准不达标；需要时在 设置 → 任务执行 里手工新增。
- 用户增删改后整份列表落 meta；`恢复内置` 即删除该键。

### 内置 profile

| id | 命令行 | 只读追加 | output | promptMode |
| --- | --- | --- | --- | --- |
| `opencode` | `opencode run --format json <prompt>` | `--agent plan`（内置，1.19.0 实测） | `json` | `arg` |
| `claude` | `claude -p --output-format stream-json <prompt>` | `--permission-mode plan` | `stream-json` | `arg` |
| `cursor-agent` | `cursor-agent -p --output-format stream-json <prompt>` | `--plan` | `stream-json` | `arg` |

## 已验证的 CLI 事实（本机实测，2026-10-27）

| agent | 非交互调用 | 输出格式 | 模型/agent 选择 | 只读模式 |
| --- | --- | --- | --- | --- |
| **opencode** | `opencode run "<prompt>"` | `--format default`（格式化）或 `--format json`（原始 JSON 事件） | `--model provider/model`、`--agent <name>` | 无内置只读开关；**`--agent plan` 是内置 primary agent（1.19.0 实测存在）**，`edit` 权限为 deny（只允许写 `plans/*.md`）→ 编辑工具拒写；bash 未单独设规则，属工具级只读，不是沙箱 |
| **claude** | `claude -p "<prompt>"` | `--output-format text \| json \| stream-json` | `--model` | `--permission-mode`（含 plan）、`--allowedTools` 白名单；`--dangerously-skip-permissions` 危险，默认禁用 |
| **cursor-agent** | `cursor-agent -p "<prompt>"` | `--output-format text \| json \| stream-json` | `--model` | **`--plan`**（等价 `--mode=plan`），天然只读 |
| **codex**（v0.163.0，参数面实测见下） | `codex exec "<prompt>"`（**必须重定向 stdin**，否则卡在读取 stdin） | `--json`（JSONL 事件流）/ `-o <file>`（末条回复）/ `--output-schema <file>` | `-m <model>`、`-c model_reasoning_effort="low"` | **`-s read-only`**（原生沙箱，实测确实拦住写文件）；`-C <dir>` 指定工作根、`--ephemeral` 不落会话文件 |

### opencode `--agent plan` 核实记录（2026-11-04，opencode 1.19.0）

R9-03 用 `--agent plan` 兜只读，但当时只看到 `~/.config/opencode/` 下没有 `agent/` 目录，
**没有核过这个 agent 到底存不存在**。本卡补上，两条路都查：

| 路径 | 命令 / 位置 | 结果 |
| --- | --- | --- |
| 命令行 | `opencode agent list` | 列出 7 个：`build` / `compaction` / **`plan`** / `summary` / `title`（primary）+ `explore` / `general`（subagent）——`plan (primary)` 在内 |
| 配置文件 | `~/.config/opencode/opencode.jsonc` | **没有 `agent` 键，也没有 `agent/` 目录** → 上面 7 个全是**内置** agent，不是本机自定义 |

`opencode run --help` 同时确认 `--agent <string>` 参数存在。

`plan` 的有效权限（`opencode agent list` 输出的 override 表，非 `external_directory` 的 15 条）：

- `edit` → **deny `*`**，只有两处 allow：`.opencode\plans\*.md` 与
  `C:\Users\<user>\.local\share\opencode\plans\*.md`
- `read` → allow（`*.env` / `*.env.*` 为 ask）
- `task` → `general` 子 agent 被 deny
- **`bash` 没有单独规则** → 落在 `*` 的 allow 上

**结论**：「plan 存在」成立；「只读」是**工具级**成立 —— 编辑类工具被拒，agent 改不了工作区文件；
但 bash 没被禁，所以它不是系统级沙箱。按 §只读三档，这属于第 2 档「工具白名单」，
不是第 3 档「外部沙箱」。要绝对保证，仍得在只读挂载 / 临时快照里跑。
`readonlyArgs` 维持 `["--agent", "plan"]` 不变。

### codex 实测记录（2026-10-28，codex-cli 0.163.0）

在仓库外的临时 git 目录里跑了两次，两次都没走完一个回合（`timeout` 200 s / 420 s，退出码均 124）：

- 会话头打印 `sandbox: read-only`、`approval: never`；事件顺序是
  `thread.started` → `item.completed{type:"error"}`（自定义 provider 的模型名没有元数据，退化到 fallback metadata）
  → `turn.started` → `item.started` / `item.completed{type:"command_execution"}`，之后没有结束事件。
- **只读沙箱是真的**：显式要求它创建一个文件，`evil.txt` 没有落盘；两次跑完 `git status --porcelain` 与跑前逐字一致。
- **两个必须写进拼装逻辑的坑**：
  1. stdin 不重定向会卡在 `Reading additional input from stdin...`，脚本调用一律把 stdin 接到 NUL；
  2. `--json` 输出的是事件流而不是最终回复，末条回复要另用 `-o <file>` 收。
- **结论**：参数面够用（`codex exec -s read-only -C <cwd> --json -o <out>`），但一回合耗时不可控，
  达不到 executor 的超时口径，**所以仍不内置**。要内置的前提是在目标机器上跑通一次以
  `turn.completed` 结束的 exec，并把实测到的耗时与失败判定补回本节。

## .cmd 包装器实测记录（2026-11-04，R14-03）

本机 PATH 上的 cursor-agent 只有 `%LOCALAPPDATA%\cursor-agent\cursor-agent.cmd`，
真身是 `versions\2026.10.19-6bbf2c4\node.exe` + `index.js`，外层 .cmd 只负责转调
`cursor-agent.ps1`。用执行器（TaskRunner）真跑 `--version`，三种 program 写法结果不同：

| 传给 QProcess 的 program | 实测结果 |
| --- | --- |
| `cursor-agent`（profile 原样，裸命令名） | **FailedToStart**，errorString＝`Process failed to start: 系统找不到指定的文件。` |
| `C:\…\cursor-agent.cmd`（绝对路径） | 拉起成功，`--version` 退出码 0，stdout `2026.10.19-6bbf2c4` |
| `C:\…\versions\2026.10.19-6bbf2c4\node.exe`（真身） | 拉起成功，同上 |

根因不在可用性判定：`QStandardPaths::findExecutable` 认 `PATHEXT`，所以面板一直显示「可用」；
错在 `setProgram` 之后走的是 `CreateProcess`，裸名只补 `.exe`，`.cmd` 根本不在候选里。
修法：`TaskRunner::setupProcess` 在 `setProgram` 前把裸命令名解析成绝对路径
（`resolveProgram`），口径与 `available` 同源 —— 面板说可用，就必须真能拉起。

Qt 6.8 对 `.cmd` / `.bat` 会自动套一层 `cmd.exe /c ""<prog>" <args>"`（实测抓到该命令行），
所以绝对路径下 .cmd 本身能拉起、退出码照常回传；作业对象按 QProcess 的 pid 挂
（.cmd 情形即 cmd.exe），子孙进程随作业一起回收。

复跑方式（会真的拉起本机 CLI，默认跳过）：

```powershell
$env:NB_REAL_AGENT_PROBE = "1"; ctest -R taskrunner --output-on-failure -V
```

**R14-05 补上的边界（2026-11-04）**：这层转调不是透明的，实测确认过不去的有两类：

- **换行 / 回车**：被 cmd 当命令分隔符，prompt 从第一行就断掉。而 `WorkflowRunner::buildStepPrompt`
  （`src/WorkflowRunner.cpp`）拼的正是多行 prompt（任务 / 上一步交接物 / 本步任务 / 交付物 / 约束）——
  也就是说 .cmd 型 agent 走 arg 模式时，交接物整段丢失且**不报错**；
- **`%VAR%`**：在双引号里也照展开成环境变量，命令行没有可靠转义（引号内的 `^` 是字面量）。

`&` `|` `<` `>` 由 Qt 加引号后不构成命令分隔，本次没有观察到失真；内嵌双引号被转义成 `\"`，
按 `CommandLineToArgvW` 解析的 CLI 仍能拿回原字符。这两项不进拒绝名单 —— 误伤的代价更高。

于是 `startRaw` 在 `acquire` 之前加了一道判据（`isCmdWrapper` + `lossyPayloadReason`）：
解析后是 `.cmd` / `.bat`、prompt 走 argv、且命中上述两类 → **直接拒绝启动**，错误信息点名
profile 与原因，并给出两条出路（exe 指向真身 `node.exe` + `index.js`，或改用 `promptMode=stdin`）。
宁可失败，也不把半句话交出去。

**为什么不用 stdin 兜底**：`cursor-agent --help` 只有 `[prompt...]` 位置参数，没有
`--input-format` / `--stdin`；包内 `index.js` 里 `stdin` 仅出现 3 次，不是输入通道。
执行器侧的 stdin 通道本身是好的（R9-05 修通），限制在 CLI 这一端。

## 只读模式怎么保证（triage 必须）

三档，从强到弱：

1. **CLI 原生只读**（首选）：`cursor-agent --plan`、`claude --permission-mode plan`
2. **工具白名单**（次选）：`claude --allowedTools "Read,Grep,Glob"`；opencode 用只读 agent 配置
3. **外部沙箱**（兜底）：在只读挂载 / 临时快照目录里跑，结束后丢弃

**不接受的方案**：只在 prompt 里写「不要修改文件」——那不是约束。

## 输出解析

统一收敛成事件流：

```jsonc
{ "type": "message", "role": "assistant", "text": "..." }
{ "type": "tool",    "name": "Edit", "summary": "src/a.cpp (+12 -3)" }
{ "type": "result",  "ok": true, "summary": "完成 3 个文件" }
{ "type": "error",   "message": "..." }
```

- `stream-json` / `json` 输出直接映射
- `text` 输出只能把整段当 `message` + 用退出码判成功

## 成功/失败判定

| 信号 | 用法 |
| --- | --- |
| 进程退出码 | 0 才算 agent 侧成功 |
| 卡片 `acceptance` | **与退出码独立**，都过才算卡 done |
| 超时 | `timeoutMin` 到点强杀，标记 `blocked`（原因：超时） |
| 无输出心跳 | 连续 N 分钟无新输出 → UI 警告，不自动杀（agent 可能在长时间思考） |

## 安全约定

| 参数 | 何时允许 |
| --- | --- |
| `--dangerously-skip-permissions` / `--auto` | **仅**当 `cwd` 在白名单且用户在确认框勾选「信任本目录」 |
| 自定义 `--append-system-prompt` | 允许（注入我们的角色/流程约定） |
| 环境变量 | 只透传白名单（`PATH`、`HOME`、agent 自己的 key），不传全部 |

## 新增一个 agent 的步骤

1. 实测：非交互调用、输出格式、只读参数、退出码行为
2. 填 profile JSON
3. 用一条最小任务验证：能跑、能出流式输出、只读模式真的写不进去
4. 记进本文「已验证」表
