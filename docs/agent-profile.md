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
  "defaultTimeoutMin": 30               // 1–480
}
```

命令行**只按 argv 拼装**：`exe + args + readonlyArgs（只读时）+ prompt（promptMode=arg 时）`，
不经过 shell；界面上的完整命令行只是同一份 argv 的展示串（含引号转义），执行侧不使用它。

## 存储与生效

| meta 键 | 内容 |
| --- | --- |
| `exec.agentProfiles` | 整份 profile 列表（JSON 数组）；键不存在 = 用内置，删键即恢复内置 |
| `exec.enabled` | 执行器总开关，`"1"` 为开，**默认关** |
| `exec.defaultAgent` | 默认 agent id（卡片未填 agent 时使用） |
| `exec.trustedCwds` | cwd 白名单（JSON 数组），首次执行目录需在确认框勾选「信任本目录」 |

- 只写 `meta` 表，**不升 `user_version`、不动 `task` 表**。
- 内置三条：`opencode` / `claude` / `cursor-agent`（见下表）。**codex 未实测，不内置**，
  需要时在 设置 → 任务执行 里手工新增。
- 用户增删改后整份列表落 meta；`恢复内置` 即删除该键。

### 内置 profile

| id | 命令行 | 只读追加 | output | promptMode |
| --- | --- | --- | --- | --- |
| `opencode` | `opencode run --format json <prompt>` | `--agent plan` | `json` | `arg` |
| `claude` | `claude -p --output-format stream-json <prompt>` | `--permission-mode plan` | `stream-json` | `arg` |
| `cursor-agent` | `cursor-agent -p --output-format stream-json <prompt>` | `--plan` | `stream-json` | `arg` |

## 已验证的 CLI 事实（本机实测，2026-10-27）

| agent | 非交互调用 | 输出格式 | 模型/agent 选择 | 只读模式 |
| --- | --- | --- | --- | --- |
| **opencode** | `opencode run "<prompt>"` | `--format default`（格式化）或 `--format json`（原始 JSON 事件） | `--model provider/model`、`--agent <name>` | 无内置只读开关；用 `--agent` 指向一个只读 agent 配置，或 `--pure` 禁用外部插件 |
| **claude** | `claude -p "<prompt>"` | `--output-format text \| json \| stream-json` | `--model` | `--permission-mode`（含 plan）、`--allowedTools` 白名单；`--dangerously-skip-permissions` 危险，默认禁用 |
| **cursor-agent** | `cursor-agent -p "<prompt>"` | `--output-format text \| json \| stream-json` | `--model` | **`--plan`**（等价 `--mode=plan`），天然只读 |
| **codex** | `codex exec "<prompt>"`（**未实测**） | 待验证 | `-m` | 待验证（已知有 `--sandbox read-only`，需实测确认） |

> 本机未安装 codex，表中标注「待验证」的项**不要**直接写进生产 profile，先在目标机器实测再落。

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
