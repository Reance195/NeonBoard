# agent-task-event v1（通用 AI Agent → 看板任务协议）

> 目标：任何 AI Agent（opencode / Claude Code / 其他）都能把「当前任务清单」实时同步到
> NeonBoard 看板，不依赖标题内嵌短号，不污染计划钩子的事件域。

## 三层结构

| 层 | 位置 | 职责 | 不允许做 |
| --- | --- | --- | --- |
| 投递 | 各 Agent 的适配器（如 opencode 插件 `neonboard-todo-bridge`） | 采集自身任务清单 → 按本协议封装 → `NeonBoard.exe --emit-hook-file <path> --hook-event agent.task` | 解析规则、直接调任务 API |
| 队列 | `src/HookEventQueue`（hooks.db） | 落库、轮询转发事件总线 | 执行策略 |
| 消费 | `plugins/agent-bridge/`（官方插件） | 校验协议、按 meta 映射新建/流转卡片 | 直接写 SQL |

宿主只有一处机制改动：CLI `--hook-event <name>` 可选参数（默认仍 `hook.commit`），
让 agent 事件走独立事件域 `agent.task`，两个事件域互不掺和。

## 载荷（envelope）

投递时把以下 JSON 整体写入临时文件，经 `--emit-hook-file` 投到 `agent.task` 事件域；
宿主投递链路把该 JSON 全文作为 `payload.text` 交给插件。

```json
{
  "protocol": "agent-task-event",
  "version": 1,
  "agent": "opencode",
  "sessionId": "ses_abc123...",
  "ts": 1792654300000,
  "items": [
    { "id": "t1", "title": "修复统计芯片刷新", "status": "in_progress" },
    { "id": "t2", "title": "补一条回归测试",  "status": "pending" },
    { "id": "t3", "title": "出安装包",        "status": "completed" }
  ]
}
```

字段约束：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `protocol` | ✅ | 固定 `agent-task-event`，不符则整包丢弃 |
| `version` | ✅ | 当前 `1`；未来不兼容变更升版并保留旧解析 |
| `agent` | ✅ | Agent 标识（如 `opencode`），进 meta 映射键 |
| `sessionId` | ✅ | 会话标识，进 meta 映射键 |
| `ts` | 可选 | 毫秒时间戳 |
| `items[].id` | ✅ | 上游 todo 的稳定 id（优先上游原生 id；习惯上每轮投递保持一致） |
| `items[].title` | ✅ | 卡片标题；**不要**内嵌 `#短号` |
| `items[].status` | 可选 | 上游状态，缺省按 `pending` 处理 |
| `items[].note` | 可选 | 卡片备注全量文本（见下方 note 语义） |

## 状态映射

| 上游 | 看板 |
| --- | --- |
| `pending` | `todo` |
| `in_progress` | `doing` |
| `completed` | `done` |
| `todo` / `doing` / `done` | 原样（协议层已是看板状态） |
| 其他/缺省 | `todo` |

## 消费语义（agent-bridge 插件）

- **身份**：`meta` 键 `agentbridge.<agent>.<sessionId>.<todoId>` 存卡片 uuid。
  标题不带短号；同一 sessionId 内 id 稳定即可幂等。
- **新建**：映射不存在（或卡片已被删除）→ `addTask` 建卡，打标签 `agent`，
  备注：协议带了 `note` 就以 `note` 为准（幂等收敛），没带才写
  「来自 agent xxx · session 前8位」的归属说明；写映射。
- **流转**：映射存在 → 标题变了 `updateTask`；`note` 做全量对账——事件带了
  `note` 且与卡片当前不同才 `updateTask` 覆盖（重复投递完全收敛，永远以最新
  事件为准），事件没带 `note` 不动卡片备注（保留人工编辑）；状态变了
  `moveTaskToEnd`；已在目标列则跳过写库。
- **todo 消失不删卡**：上游清单里去掉的 item，对应卡片保留在看板上（卡片已是
  用户可见实体，不反向删除）。想清理就手工删卡，映射失效后下次投递会重建。
- **来源徽标**：卡片带 `agent` 标签（短期约定；`agent` 值未单列标签，避免标签爆炸）。

## 结卡闭环（快照自动写回）

任务卡的状态以看板为事实源。agent 结一整条需求时，在事件里投「卡片 id 映射 + status=done
+ note（进度口径文案」，插件把它写进看板 → `src/RoadmapSync` 在 1.5s 防抖后把看板状态
**自动写回仓库快照 `progress.json`**（v3 格式，程序生成，agent 不再手改该文件）。
收尾时 `scripts/git-done.ps1` 检测到 progress.json 有未提交变更会自动代提交
`chore(roadmap): 同步进度快照`。

这条链成立的前提：

1. **事件投递时应用正在运行**（`hooks.db` 只被运行中的主进程轮询消费）；
   应用没开就投递的话，事件留到下次启动补消费，快照写回要等那一刻。
2. 快照路径由 `NEONBOARD_ROADMAP` 解析；沙箱运行时写回的是沙箱文件，
   真实看板对应仓库根 `progress.json`。
3. progress.json 的写者唯一 = 看板应用。agent / 人不允许手改（曾导致 v2 覆盖 v3 格式退化）。

## 适配器契约（以 opencode 为例）

- 监听 `todowrite` 工具调用（`tool.execute.before` 捕获 args，`tool.execute.after`
  确认执行成功再投递）， batchSize = 全量清单快照（opencode 每次 todowrite 是全量）。
- `sessionId` 用 opencode 的 `sessionID`。
- 投递失败静默降级：只 warn，不影响 agent 主流程——看板是旁路可视化。

## 验证口径

```powershell
# 1. 单包投递（应用未运行 → 下次启动补投递）
NeonBoard.exe --emit-hook-file tmp.json --hook-event agent.task
# 2. 沙箱消费：dev-run.ps1 起应用，几秒内看板出现对应卡片（标签 agent）
# 3. 幂等：同一批次重复投递 → 不新建卡片、动作数 0
# 4. 流转：改 item status 重投 → 卡片换列
```
