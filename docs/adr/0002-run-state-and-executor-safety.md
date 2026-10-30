# 0002. 运行态与看板状态分离，执行器默认关闭且受控

- 状态：accepted
- 日期：2026-10-27
- 关联：`docs/product-design.md` §4 / §5 / §8、`src/TaskExec.*`、`src/Database.cpp`、[R6 派发器计划](../archive/r6-dispatcher-plan.md)

## 背景

派发器要回答两个问题：**一次执行处于什么阶段**，以及**允许它做什么**。

现状里有两个诱因会把这两件事搅在一起：

1. 看板只有三列（`todo` / `doing` / `done`），而 `product-design.md` §4 设计的是五状态
   （外加 `draft` / `blocked`）。如果直接把执行状态塞进卡片状态，会连带影响
   `progress.json` 派生映射、列 UI、插件事件、快照闸门（`pre-push` 规则 4）——一次改动横跨四个子系统，
   且老版本程序看到的列语义会变。
2. 执行器要记的东西（`run_id` / `attempt` / 退出码 / 心跳 / 日志路径 / 越界路径 / 失败摘要）
   是**每次执行**的属性，不是卡片的属性。写进卡片字段会让「重试」变成「覆盖历史」。

同时，执行器是本仓库第一个**可以真正改动用户代码**的能力。默认放开权限，
等于让「点错一个按钮」变成「agent 在真实仓库里乱改」。

## 决策

### 一、运行态独立成表，不进看板状态机

新增侧表 `task_run`（沿用 `task_link` / `task_extra` / `task_exec` 的侧表先例）：

```sql
CREATE TABLE IF NOT EXISTS task_run(
  id              TEXT PRIMARY KEY,
  task_id         TEXT NOT NULL,
  agent           TEXT NOT NULL,
  cwd             TEXT NOT NULL,
  workflow        TEXT NOT NULL,
  status          TEXT NOT NULL,          -- queued | running | passed | blocked | cancelled
  attempt         INTEGER NOT NULL DEFAULT 1,
  started_at      INTEGER,
  finished_at     INTEGER,
  heartbeat_at    INTEGER,
  exit_code       INTEGER,
  last_error      TEXT,
  log_path        TEXT,
  scope_violation TEXT
);
-- 同卡同一时刻最多一个活跃 run
CREATE UNIQUE INDEX IF NOT EXISTS idx_task_run_active
  ON task_run(task_id) WHERE status IN ('queued','running');
```

- **不往 `task` 表加列、不升 `user_version`**，老版本程序照常开库；
- 生命周期五个值只描述 run，不描述卡片；卡片仍只有三列语义；
- **程序不自动改看板列**：验收通过后，执行面板给「标为已办结」一键动作（走既有完成路径、保留撤销），
  要不要改由人点；
- `blocked` 的卡不会自动回到可执行：重试是显式动作（新建 run，`attempt + 1`）。

### 二、锁与崩溃恢复

- 锁就是「存在活跃 run」，由上面的部分唯一索引硬保证，不依赖 UI 状态；
- 心跳每 **10 s** 写一次 `heartbeat_at`；
- 应用启动时扫描 `status = 'running'` 且 `heartbeat_at` 超过 **90 s** 的记录 → 标记 `blocked`
  （`last_error` = 「进程中断」），释放锁，不自动重跑。

### 三、M1 忙策略：拒绝，不排队

已有活跃 run 时，新的执行请求**直接拒绝并提示**，不建队列。并发默认 1。
（4 核 8 线程的机器上多开只会互相拖慢；队列涉及排序 / 暂停 / 取消语义，属 M1 之后。）

### 四、安全默认（总开关关闭）

| 约束 | 默认值 / 行为 |
| --- | --- |
| 执行器总开关 | **关闭**（`executor.enabled = false`），关闭时看板行为与今天完全一致 |
| `cwd` | 必须是已登记工作目录列表中的绝对路径；首次登记需人勾选「信任本目录」 |
| 执行前确认 | 展示 agent、cwd、完整命令行、acceptance 全文、超时，人确认后才启动 |
| acceptance | 只允许白名单脚本路径；**不经过 shell**，拒绝 `\|` `>` `<` `&` `;` 等拼接字符 |
| 环境变量 | 白名单透传，不整包继承 |
| 日志 | 落 `%APPDATA%/com.lin/NeonBoard/runs/<runId>.log`，写入前脱敏 token / key / 长十六进制串 |
| `timeoutMin` | 1–480，默认 30；到点强杀，记 `blocked`（timeout） |
| `maxAttempts` | 1–5，默认 1（等价不循环） |
| 自动推送 | **禁止**；执行完最多留下本地改动，push 由人决定 |
| 进程回收 | `terminate()` → 5 s 后 `kill()`，Windows 用 Job Object 回收整棵进程树 |

## 理由

- **关注点分离**：卡片状态回答「这件事做完了没」，run 状态回答「这次执行怎么样了」。
  两者变化频率不同、语义不同、历史要求不同（run 要留全部尝试，卡片只要一个当前值）。
- **改动面最小**：侧表 + `IF NOT EXISTS` 不触碰任何既有字段，老版本、插件、派生器全部无感；
  对比「给卡片加 draft/blocked 状态」要同步改快照映射、UI 列、闸门、插件事件四处。
- **把「谁能改列」留给用户**：派发器的目标是减少机械劳动，不是替用户做进度判断。
  自动改列一旦出错，用户会失去对看板的信任，而看板是这个产品的根。
- **默认关闭是安全基线**：新能力默认不可用，用户主动开启才生效；出问题时关开关即可回退，
  不需要回滚数据库或降级安装包。

## 后果

**正面**

- 执行器可以独立演进（队列、并发、角色链）而不动看板状态机；
- 每次尝试都有独立记录与日志，失败可复盘；
- 老版本程序兼容，迁移成本为零；
- 默认关闭 + 确认框把「误操作」风险压到最低。

**负面**

- 同一张卡在 UI 上会出现「卡片列」与「run 状态」两套信息，需要在执行面板里解释清楚；
- 用户要手动把验收通过的卡拖到 done（我们接受：这是有意的「人确认」）；
- `task_run` 会随时间增长，需要定期清理（v1 保留最近 200 条 / 每卡 20 条，清理策略在 R6-06 落地）；
- 忙策略直接拒绝，用户连点两次会看到错误提示（好过排队造成的「以为在跑其实在等」）。

## 备选方案

| 方案 | 为什么不选 |
| --- | --- |
| 给卡片加 `draft` / `blocked` 状态 | 要同步改 `progress.json` 派生映射、列 UI、快照闸门、插件事件；且老版本读不懂新状态 |
| 运行状态写进 `task_exec` | 定义与运行态混在一行，「重试」会覆盖历史，无法追溯第几轮失败 |
| 用 git ref 做锁（`refs/claims` 思路） | 单机 MVP 不需要跨机器锁；引入 git 依赖会把「执行」与「网络/仓库状态」绑定 |
| 排队而不是拒绝 | M1 只需「一张卡一次」，队列的排序 / 暂停 / 取消语义会显著扩大范围 |
| 默认开启执行器 | 第一次使用就可能误跑；默认关闭的成本只是多点一次开关 |
| 自动 push / 自动改列 | 与 CI 回写抢主线、与用户预期冲突；出错后难以解释 |

## 验证方式

- `tst_taskruns`：部分唯一索引在二次 `acquire` 时拒绝；孤儿回收把 120 s 前心跳的 `running` 记为 `blocked`；
- `tst_databasemigration`：v2 老库打开后新表建出，`PRAGMA user_version` 仍为 2；
- `tst_taskrunner`：超时 → `blocked(timeout)`；取消 → `cancelled`；孙进程被回收；日志脱敏；
- 人工：关闭总开关时执行按钮不可用；开启后确认框展示的命令行与实际进程一致；
- 代码审查：不存在从 run 终态直接改 `task.status` / 看板列的代码路径。
