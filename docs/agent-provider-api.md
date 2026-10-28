# Agent Provider 扩展点设计（待立卡）

> 状态：**设计稿，未实现**。本文只定义协议与迁移路径，不改任何代码。
> 落地需要先立卡（动 `src/` 与 `plugins/`）。
>
> 配套的站外适配器已交付，见 [agent-adapters.md](agent-adapters.md)。

## 一、为什么要这个扩展点

出站（看板派活给 agent）的执行器 R6 已全绿，但 agent 的**定义**是硬编码的：

```cpp
// src/AgentProfile.cpp:194
QVariantList AgentProfile::builtinProfiles()   // opencode / claude / cursor-agent 三条写死
```

后果有三个，且都已在本机现形：

1. **加一个 agent 要改 C++**，或者让用户在设置里手填 argv —— 插件平台形同虚设。
2. **cursor-agent 装了但不在 PATH**，内置 profile 写 `exe: "cursor-agent"` 调不动，
   而"写绝对路径"这种本机相关的事实，内置表天然无法表达。
3. **codex 有明确的不内置理由**（未实测整轮），但它连"可被声明"的入口都没有 ——
   用户只能在 UI 里手抄参数，抄错没有任何提示。

插件平台现在能贡献事件 / 槽位 / 命令 / 设置页，唯独**不能贡献「一个 agent」**。
这就是本扩展点要补的缺口。

## 二、manifest 声明

```json
{
  "id": "com.lin.agent-claude",
  "name": "Claude Code",
  "apiVersion": 2,
  "permissions": ["exec.agentProfiles"],
  "contributes": {
    "agentProfiles": [
      {
        "id": "claude",
        "displayName": "Claude Code",
        "exe": "claude",
        "args": ["-p", "--output-format", "stream-json"],
        "promptMode": "arg",
        "readonlyArgs": ["--permission-mode", "plan"],
        "output": "stream-json",
        "env": { "NO_COLOR": "1" },
        "defaultTimeoutMin": 30
      }
    ]
  }
}
```

字段语义与 [agent-profile.md](agent-profile.md) 现有 profile 完全一致，**不新增字段**——
执行侧（`TaskRunner`）一处都不用改，改的只是 profile 从哪来。

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `id` | 是 | 唯一；与内置同 id 则覆盖内置 |
| `displayName` | 否 | 缺省用 `id` |
| `exe` | 是 | 可执行名（靠 PATH）**或绝对路径** |
| `args` | 否 | 固定参数 |
| `promptMode` | 否 | `arg` / `stdin`，缺省 `arg` |
| `readonlyArgs` | 否 | 只读模式追加参数；空数组表示"该 agent 无只读开关" |
| `output` | 否 | `stream-json` / `json` / `text`，缺省 `text` |
| `env` | 否 | 子进程附加环境变量 |
| `stdin` | 否 | `"null"` 表示必须把标准输入接到 NUL（codex 需要） |
| `defaultTimeoutMin` | 否 | 1–480，缺省 30 |

新增权限 `exec.agentProfiles`（**中风险**：声明者决定拉起哪个进程、传什么参数）。
没有该权限时宿主静默忽略这段声明，并在 `--plugin-report` 里标为"声明未授权"。

## 三、三层合并

| 层 | 来源 | 说明 |
| --- | --- | --- |
| L0 内置 | `AgentProfile::builtinProfiles()` | 保持现有三条，不动 |
| L1 插件 | 各插件 `contributes.agentProfiles` | 按插件 id 字典序排，保证顺序稳定 |
| L2 用户 | meta `exec.agentProfileOverrides` | 最高优先；`null` 表示墓碑（隐藏该 id） |

同 id 后者覆盖前者，逐条合并而不是整份替换。

**为什么不能用现有的 `exec.agentProfiles`（整份列表）当用户层**：它存的是
「用户增删改后的**整份**快照」，一旦写过，L1 插件后来声明的新 agent 会被这份旧快照
整个淹没——插件装了却在列表里看不见。所以用户层必须是**增量覆盖**而不是整份。

### 迁移（不升 `user_version`、不动 `task` 表）

已有库里 `exec.agentProfiles` 是整份快照。首次读到时：

1. 把快照逐条与 `L0 ∪ L1` 比对；
2. 与内置/插件**不同**的条目写进 `exec.agentProfileOverrides`；
3. 快照里**不存在**、但内置有的 id → 写墓碑 `null`（用户当初删了它）；
4. 迁移完成删掉 `exec.agentProfiles`，之后只认 overrides。

老版本程序打开新库：认不出 `exec.agentProfileOverrides`，回落到内置三条，
行为与今天一致 —— 与 ADR-0001 同一口径。

## 四、可用性探测

新增 `AgentProfile::isAvailable(id)`：`exe` 是绝对路径就 `QFileInfo::exists`，
否则走 `QStandardPaths::findExecutable`。插件面板与执行前确认框都用它。

> 这一条是刚需：**cursor-agent 已在列表里却调不动**，界面上必须能看出"已声明但不可用"，
> 而不是等到执行失败才暴露。

## 五、插件还能贡献什么

| 能力 | 形态 | 说明 |
| --- | --- | --- |
| 命令 | `contributes.commands` | 「派给 Claude Code」进 `Ctrl+K` 面板 |
| 设置页 | `settings.qml` | 显示 exe 是否可用、只读开关、超时 |
| 侧栏槽位 | `sidebar.qml` | 一行徽标：可用 / 不可用 / 未安装适配器 |
| 卡片槽位 | `card.qml` | 卡片上显示"本卡派给哪个 agent" |

## 六、未定的两个问题（不要在实现时临场决定）

1. **适配器安装按钮**：面板想放"一键装适配器"，但 QML 起不了进程，
   而写 `~/.claude/settings.json` 属于越权。倾向方案：宿主只显示状态，
   安装动作交给 `scripts/install-agent-adapters.ps1`，面板给出可复制的命令。
2. **`stdin: "null"` 与 `promptMode: "stdin"` 的冲突**：两者都要改子进程标准输入。
   codex 是前者（连 NUL 防卡住），某些 agent 是后者（写 prompt）。
   需要一个明确的优先级，倾向：`stdin: "null"` 只在 `promptMode != "stdin"` 时生效。

## 七、建议分卡

> **卡号已避开 R8**：R8-01~R8-06 已被「主题包 / 主题市场 / 卡片详情 / 拖拽手感 / 键盘位移」
> 占满（2026-10-29 登记），本批次顺延到 **R9**。立卡前务必再核一次台账，别照抄本文档的号。

| 卡 | 范围 | 验收 |
| --- | --- | --- |
| R9-01 | `AgentProfile` 支持插件声明 + 三层合并 + 老 meta 迁移 | `tst_agentprofile` 覆盖合并优先级与迁移；老库打开行为不变 |
| R9-02 | `plugins/agent-claude` | `--plugin-report` 能列出 claude profile；`isAvailable` 在本机为 true |
| R9-03 | `plugins/agent-opencode` | 同上 |
| R9-04 | `plugins/agent-cursor`（含绝对路径与不可用态展示） | 面板显示"已声明但 CLI 不在 PATH" |
| R9-05 | `plugins/agent-codex`（含 `stdin: "null"`） | 跑通一次以 `turn.completed` 结束的 `exec` 才允许启用 |

五个插件的 profile 数据已按现有格式备好，在 `scripts/agent-adapters/<agent>/agent.json`，
实现时直接搬进 manifest，不需要重新实测参数（参数面已实测，见 agent-adapters.md 第二节）。
