# 看板状态派生与一致性机制

本文说明 NeonBoard 自身的「路线图看板」如何与代码仓库保持一致：卡片状态从哪来、谁能改、改错了会被谁拦下。

## 一、为什么要这么设计

早期做法是**人工维护** `progress.json` 里的 `status`：每交付一个需求，就手改一次快照。
后果很直接——代码已经合并、看板还停在「待办」，或者反过来。人工记忆一定会漏。

现在改成**派生**：卡片状态是算出来的，不是手写的。

## 二、三件套的职责边界

| 文件 / 渠道 | 角色 | 谁能写 |
| --- | --- | --- |
| `docs/board-ledger.json` | 卡片台账：短号 ↔ 卡片 id / 标题 / 阶段 / 初始状态 | 人（新增卡片、修正初始状态） |
| `git` 提交信息 `#短号=状态` | 状态变更的「代码事实」 | 人（写提交信息时顺带写上） |
| `progress.json` | **派生产物**：每张卡片的 `code` 与 `status` | 派生器（`git-done` 收尾 `--write`）与看板应用；**CI 不可写** |

派生规则：

```text
status(code) = git 历史里最后一次出现的 `#code=状态` 标记
               没有标记时退回 ledger.baseline[code]
               台账里也没有时为 todo
```

因此状态永远有据可查：要么在提交信息里（代码事实），要么在台账里（显式声明）。
不存在「悄悄改了快照」这条路径——派生器 `--check` 会把不一致直接报出来。

## 三、日常怎么用

### 交付一个需求

```powershell
git commit -m "feat(board): 支持列内精确插位 #R1-04=done"
```

短号从 `docs/board-ledger.json` 里查。合并到 `main` 后，**由 `git-done` 收尾独占写**：

1. 跑 `python scripts/derive_board_status.py --write`，把 `R1-04` 的状态推导成 `done` 并写回 `progress.json`；
2. 派生结果有变化才提交（提交信息 `chore(roadmap): 同步进度快照`），推送后触发本地应用导入，
   卡片自动变成「已办结」。

这一步是 **fail-soft** 的：派生 / 提交 / 推送失败只告警，绝不阻断已经成功的合并。CI 不参与写
（`board.yml` 只有 `contents: read`），它只校验 —— **别指望「CI 会替你补」**，它只会报红。

**特性分支不要手工改 `progress.json`** —— 写者只有 `git-done` 一处，多代理并行时才零冲突。

### 新增一张需求卡

在 `docs/board-ledger.json` 的 `cards` 里加一条（短号 / 卡片 id / 标题 / 阶段），并在 `baseline` 里给初始状态。
派生器下次运行时会把这张卡补进 `progress.json`，应用导入后看板上就出现了。

### 本地先看一眼派生结果

```powershell
python scripts/derive_board_status.py --check   # 只校验，不一致退出码 1
python scripts/derive_board_status.py --write   # 把派生结果写回快照
```

### 回退一个需求的状态

同样走提交信息：`git commit -m "revert(board): 撤回列内插位 #R1-04=todo"`。
历史里最后一次标记说了算，台账里的 `baseline` 只对「从未被标记过」的卡片生效。

## 四、谁写、谁校验

| 环节 | 谁 | 做什么 |
| --- | --- | --- |
| 合并收尾 | `scripts/git-done.ps1` 的 `Sync-BoardSnapshot` | **唯一写者**：`derive --write` 后按需提交快照并推送；fail-soft，失败只告警 |
| main push / PR | `.github/workflows/board.yml` | **只校验**：`derive --check` 不一致就报红，不自愈、无推送权限（`contents: read`） |

`board.yml` 的 `paths-ignore` 只排除 `update.json`；`progress.json` 刻意**不**排除 ——
快照变更恰恰最需要被校验。合并提交那一刻红一次、紧随的收尾提交转绿，是预期节奏；
**红着不消失就是收尾失败了**，按提示在本地补跑 `derive_board_status.py --write` 后推送。

## 五、两种 `#短号` 语法不要混淆

| 语法 | 出处 | 作用 |
| --- | --- | --- |
| `#R1-04=done` | **提交信息** | 派生看板卡片的 `status`（本机制） |
| `#T12=done` | **提交信息** + 卡片**标题**内嵌 | 计划钩子把标题带 `#T12` 的任务转到指定列（见 [plan-hook.md](plan-hook.md)） |

派生器只认台账里登记过的短号。形如 `R0-01` / `M01` 的路线图短号一旦写错会直接让 CI 失败；
其它短号（例如 `#T12`）不在台账范畴，只打印提示、不拦截。

## 六、常见问题

- **CI 报「快照状态落后于派生结果」**：本地跑 `python scripts/derive_board_status.py --write` 后提交 `progress.json` 即可。
- **CI 报「台账未登记的路线图短号」**：短号拼错了，或者该卡还没登记进 `docs/board-ledger.json`。
- **应用没反映出新状态**：应用按 `progress.json` 导入，先 `git pull` 拿回最新快照（合并收尾写的），再启动应用（或等 15 秒轮询）。
- **想看某张卡的状态是从哪来的**：`python scripts/derive_board_status.py --write` 的输出会逐条打印「卡片 X 状态 A → B」。
