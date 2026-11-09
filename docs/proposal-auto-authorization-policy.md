# 提案：自动授权立卡（策略授权模式）

> 状态：已登记首条实策略，试运行中（`auto-sched-build-patrol`，2026-11-08 起）
> 关联条款：宪章（AGENTS.md）条款 6「任务先行」、Z5（单一事实源）、Z4（fail-closed）、Z11（权限最小）
> 日期：2026-11-08

## 1. 背景与动机

现行条款 6 只承认一种授权形态：**维护者在会话中对具体批次明确下达**。这覆盖了人在场的场景，但有两类任务天然"无人在场"：

1. **定时/自动任务**：如每日构建巡检、值守会话自动派生的小修复——每次触发都要求维护者在线说一句"授权"，实际执行中会被迫退化成两种坏结果之一：要么任务积压等人，要么 agent 冒险先写后补授权（先例：方向校验只告警的缺口）。
2. **机器来源的请求单**：由 CI / 值守脚本生成、内容完全模板化的卡片，人为逐条批准不增加任何审计价值。

本提案在**不破坏审计链**的前提下，把「授权」从"会话中的一次对话"泛化为"入库的可校验策略对象"：授权一次、多次生效，但每一张落下的卡都能机器回答"依据哪份预先批准的规则"。

## 2. 设计原则

- **授权方永远是人**。策略对象由维护者登记、走 `chore(board)` 修宪章级别的提交；agent 只是执行手（与现行代登记同构）。
- **判边界靠程序，不靠自觉**。校验逻辑只落在 `register_board_cards.py` 一处（Z5：单一实现），不在钩子/文档里抄第二份。
- **fail-closed**。策略缺失、sha 不符、字段超界一律拒绝立卡（Z4）。
- **授权来源可追溯**。回执 `authorizedBy` 写 `policy:<id>@<sha>`，事后任何一张卡都能回溯到具体策略版本。

### 2.1 与「自由立卡」的本质区别

| | 自由立卡（宪章否定的） | 策略授权（本提案） |
| --- | --- | --- |
| 授权来源 | agent 自己 | 预先登记的策略对象 |
| 能立什么卡 | 想立就立 | 策略限定：tier 上限、scope 白名单、stage、标题模板 |
| 审计 | `authorizedBy: 我自己`（自证） | `authorizedBy: policy:<id>@<sha>`（机器可校验） |
| 把关者 | 无人 | 立卡程序，与现有五段式校验同一道闸门 |

## 3. 策略对象 Schema

登记在台账顶层新增映射 `authorization.policies`（与 `numbering.mode` 同层，均为任务定义）：

```jsonc
{
  "authorization": {
    "policies": [
      {
        "id": "auto-sched-build-patrol",        // 全局唯一，[a-z0-9-]+
        "title": "定时构建巡检自动立卡",
        "enabled": true,                         // 总开关，false 即策略停用
        "source": "scheduled:每日构建巡检",      // 授权来源的人类可读描述
        "tierMax": "P3",                         // 允许的最高档位（P0 最高）
        "stage": "develop",                      // 只允许立到这个 stage
        "scopeGlob": ["src/**", "scripts/**"],   // 卡片 scope 必须逐条命中此处白名单
        "titlePattern": "^\\[auto\\] .+",        // 标题必须命中此正则
        "maxCardsPerDay": 5,                     // 每日熔断上限，超出拒绝
        "authorizedBy": "维护者 <会话授权 2026-11-08>",
        "createdAt": "2026-11-08T00:00:00Z"
      }
    ]
  }
}
```

字段语义：

| 字段 | 必填 | 校验失败行为 |
| --- | --- | --- |
| `id` | 是 | 格式不符即拒；与既有策略重名即拒 |
| `enabled` | 是 | `false` 时该策略下一切立卡被拒 |
| `tierMax` | 是 | 请求单 `tier` 档位高于上限即拒（P0 优先级卡永远必须人批） |
| `stage` | 是 | 请求单 `stage` 不一致即拒；陌生 stage 走现行 DX-110 程序落行 |
| `scopeGlob` | 是 | 请求单任一 scope 未命中白名单即拒（防 R10-02 式自扩权） |
| `titlePattern` | 是 | 标题不命中即拒，保证机器来源的卡可一眼识别 |
| `maxCardsPerDay` | 否（默认 3） | 当日已达上限即拒并告警，等维护者处理 |

## 4. 程序改动方案

### 4.1 `scripts/register_board_cards.py`

新增 CLI 入口：

```
python scripts/register_board_cards.py --policy <id> --form <请求单> --trigger <调度run标识>
```

流程在现有五段式校验**之前**插入策略校验段：

1. 读台账 `authorization.policies`，找 `id`；找不到或 `enabled=false` → 拒（exit 2）。
2. 校验请求单：`tier ≤ tierMax`、`stage` 一致、`scope` 逐条命中 `scopeGlob`、`title` 命中 `titlePattern` → 任一不过即拒（exit 2），并打印具体不满足的字段。
3. 检查 `maxCardsPerDay`：扫描回执目录 `docs/card-requests/registered/` 中当日 `authorizedBy` 为本策略的条数 → 超限拒（exit 2）。
4. 通过后走**完全相同的既有发号链**：锁内发号 → `refs/registrations/<短号>` 服务端占号 → 生成回执，唯回执中 `authorizedBy` 写 `policy:<id>@<策略对象内容sha256前8位>`，并附 `trigger: <调度run标识>` 字段。
5. `derive --write` / `--check` 自校验，同现行。

提交仍拆两段（`docs(board)` + `chore(board)`），由触发方（值守会话/CI job）执行——立卡提交本身沿既有通道，不新增提交类型。

### 4.2 `scripts/task_gate.py`

- `receipt_problems` 增加策略回执校验：`authorizedBy` 以 `policy:` 开头时，重算该策略对象内容 sha、复核请求单是否在策略边界内；不符即拒（与现行"无回执/sha 不符/缺 authorizedBy 即拒"同级）。
- selftest 补用例：策略命中放行、tier 超档拒、scope 越界拒、超每日上限拒、策略停用拒、策略内容被篡改后 sha 不符拒（预期新增 ≥6 例）。

### 4.3 不改动的东西

- 台账其余结构、`refs/registrations` / `refs/claims` 机制、自动结卡、快照链路——全部不动。
- 人在场的显式授权代登记路径原样保留，两条通道并存。

## 5. 失败与熔断

| 场景 | 行为 |
| --- | --- |
| 策略不存在 / 停用 | 拒立卡，exit 2，告警日志 |
| 任一字段超界 | 拒立卡，exit 2，点名字段 |
| 当日超 `maxCardsPerDay` | 拒立卡，exit 2，告警等维护者 |
| 离线（服务端占号失败） | 沿现行规则：须显式 `--offline`，号为暂定须重验 |
| 策略对象被篡改 | `task_gate` 回执校验 sha 不符 → commit-msg 拒 |
| 定时任务持续立卡失败 | 由调度侧自行退避；仓库侧不自动放宽任何边界 |

## 6. 落地步骤（拟）

1. `chore(board): 登记自动授权立卡策略机制` —— 本提案正文并入 `docs/task-card.md` 或宪章条款 6 附注；台账 schema 增加 `authorization.policies`（空数组起步，行为不变）。
2. `feat(board)` 或 `chore(board)` 实现章 4 程序改动（涉及 `scripts/`，属杂务豁免路径，但改动台账判定逻辑，建议仍独立提交并附授权行）。
3. selftest 补例全绿后，登记第一条实策略（如"定时构建巡检"）小范围试运行一周。
4. 试运行复盘：确认无越界案例后，再决定是否扩面（更多策略、更高 tier 上限）。

## 7. 未决问题（请维护者拍板）

1. **Q1** `maxCardsPerDay` 默认值（本提案建议 3）与超限后的告警落点（board.yml 告警 vs 仅脚本输出）。
2. **Q2** 是否允许策略间继承/组合（本提案建议**不允许**，第一版保持扁平，需要时再加）。
3. **Q3** 定时任务触发的立卡提交由哪个身份执行（值守 agent 的 worktree？还是 CI 专用 job）——涉及 §7.3 合并路径的选择。
4. **Q4** `titlePattern` 的统一前缀建议用 `[auto]`，是否接受。
