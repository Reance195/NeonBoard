# 问题与解决方案台账（pitfalls）

> 这份文档只干一件事：**把踩过的坑变成可复用的判据**，避免同一片坑被不同 agent 反复踩。
> 与 `AGENTS.md` 的分工：宪章写「必须怎么做」（规范），这里写「为什么会踩、怎么识别、怎么绕」（经验）。
> 已有专门文档的，这里只留一条索引并指过去，不重复展开。

## 维护约定

1. 新坑按分类追加，每条写 **现象 / 根因 / 解法 / 预防**，解法已进代码或脚本的就标出处（提交号或文件）。
2. **还没有解法的**进[第七节](#七尚无解决方案需人拍板)，写清「卡在哪、谁拍板」；不要混在已解决条目里。
3. 只写对**任何机器都成立**的经验。只对这台机器成立的运维便条（代理端口、杀软排除、证书开关）不进仓库。
4. 涉及「绕过闸门」的做法一律不写命令原文，只写「由维护者授权后手工处置」——闸门本身以 `AGENTS.md` 为准。
5. 与宪章的边界：`AGENTS.md` §五.8 已列的三条构建/测试坑（AutoMoc 原始字符串、旧 `*.obj` 造成 LNK2001、
   编辑 `.ps1` 静默丢 BOM）**以宪章为准**，本文只补「怎么识别」与出处，不复述规则原文。

---

## 一、Git 与多代理并行

### 1.1 锚点区长期落后 `origin/main`，越攒越多

- **现象**：agent 每次合并都跑过 `git-done.ps1`，但回到仓库根目录看到的还是几十个提交前的代码。
- **根因**：锚点快进的前提是「锚点区干净，或只有 `progress.json` 被抓脏」（`Sync-AnchorWorktree`）。
  桌面工具习惯把产物写进**调用方 CWD**（本仓库实际踩的是 Qoder 的画布与额度报告 `.qoder-credits/`），一旦这些目录既没入库也没被忽略，
  锚点区就**永久处于「有未提交改动」状态**，快进每次被跳过——跳过只是灰字提示，没人回头翻。
- **解法**：把工具产物目录写进 `.gitignore`（`.qoder-credits/`、`.workbuddy/` 同源），见 `5b245a8`。
- **预防**：判断标准是「这个目录是不是某个工具的输出」——是就进 `.gitignore` 并加一行注释说明它为什么会拖脏锚点。
  新增此类工具前先想清楚：落在锚点的未跟踪文件，等于给所有 agent 的收尾流程装了个隐形开关。
  「隐形」这半截已由 `b395aa5` 消掉：跳过时不再只报数量，而是**直接列出前 10 项脏文件**并提示补 `.gitignore`。
  agent 侧的显式落点由 1.17 的 `NB_SESSION_ARTIFACT_DIR` 提供。

### 1.2 应用在跑，快照文件把锚点同步卡死

- **现象**：`.gitignore` 补好后锚点仍然不同步，日志停在「临时暂存 progress.json → 暂存失败，跳过快进」。
- **根因**：主线实例常驻运行，`RoadmapSync` 会随时写 `progress.json`；旧逻辑**无条件先 `git stash`**，
  而 stash 的 checkout 步骤要写这个文件，撞上应用占用就失败。
- **解法**：改成**先直接 `git merge --ff-only origin/main`**——上游没动 `progress.json` 时本地快照抓脏根本不挡快进；
  确实动了才临时暂存后重试，失败时提示「应用可能在运行」（`2b2538c`）。
- **预防**：任何「为了绕开脏文件而先写这个文件」的设计，都要先问一句：这次写会不会和常驻进程抢句柄。
  能用一次原子操作解决的，不要拆成 stash + merge + pop 三步。

### 1.3 stash pop 撞车，把冲突标记留在快照里

- **现象**：锚点 `progress.json` 里出现 `<<<<<<<`，下次应用写回会把标记打进快照文件，脏检查从此一直误判。
- **根因**：每次合并收尾都会动 `progress.json`（`git-done` 的 `Sync-BoardSnapshot`），本地快照与主线快照必然冲突；失败的 `pop` 会把文件挂成 `UU` 并留下标记。
- **解法**：撞车时**主线版本优先**（`git checkout HEAD -- progress.json`），本地快照留在 stash 里并打印其提交号供取回（`9da6f06`）。
- **预防**：`progress.json` 是快照不是数据源，冲突时永远向主线让步；要保的是本地内容**可找回**，不是本地内容**覆盖上去**。

### 1.4 合并推送被 non-fast-forward 拒绝

- **现象**：`git push origin HEAD:main` 报 `cannot lock ref 'refs/heads/main': is at <新哈希> but expected <旧哈希>`。
- **根因**：多 agent 并发推主线，串行化本来就靠远端快进保护（`AGENTS.md` §7.3），这是**预期行为**不是故障。
- **解法**：重跑 `git-done.ps1`（会重新 fetch 并基于最新 `origin/main` 建临时分支）。
  若脚本已把当前 worktree 切到临时分支，先 `git switch <原分支>` 再 `git branch -D merge/<slug>`，
  否则会报 `cannot delete branch ... used by worktree`。
- **预防**：动手前 `git ls-remote origin` 现取基线，别拿几分钟前的快照当事实；重写历史类操作更需要现取现用。

### 1.5 功能分支直推被 pre-push 拦下

- **现象**：`git push origin feature/xxx` 被拒：「尚未合并进 main，禁止推送」。
- **根因**：钩子按设计拦未合并分支；推分支只为让 claim ref 指向的提交在 origin 可达（`AGENTS.md` §7.2.1）。
- **解法**：正常收尾一律走 `git-done.ps1`；确实需要先让远端能看到提交时，用**领用/闸门放行**的方式，而不是手工绕过。
- **预防**：分支存活期短、一次一事（条款 2），就不会出现「攒了一堆没合的分支」需要直推。

### 1.6 `git worktree remove` 报 Permission denied

- **现象**：`git worktree remove --force .worktrees/<agent>` 失败，看起来回收不掉。
- **根因**：调用方的当前目录就在那个 worktree 里（Windows 目录被进程占用）；元数据其实已摘除。
- **解法**：先 `cd` 出该目录，`git worktree prune`，再删空目录即可。
- **预防**：脚本与 agent 不要在准备回收的工作区内当 CWD 执行删除命令。

### 1.7 同一脚本被两个 agent 同时改写

- **现象**：`git-done` 合并阶段在 `scripts/git-done.ps1` 上冲突。
- **根因**：热点文件没有行段划归（`AGENTS.md` §7.4），两人改同一段。
- **解法**：按 §7.5 手工合并——**禁止 `--ours/--theirs` 盲解**；本仓库这条冲突的实际处置是「保留双方改进」，
  即直接快进（1.2）与 pop 撞车回退（1.3）两段逻辑合并共存（`2b2538c`）。
- **预防**：改 `Sync-AnchorWorktree` / `Release-ClaimRef` / 任务闸门这类共享函数前，先看 `git log -- <文件>` 最近有没有别人的手。
  `rerere` 现在由 `scripts/setup-git-hooks.ps1` 自动开启（§7.5，不再指望每个 agent 手工配），同类冲突第二次会复用上次解法。

### 1.8 台账里留着「分支已不存在」的过期领用登记

- **现象**：卡片早就合入了，派生器 / `--check` 一直报认领分支不存在。
- **根因**：过去只有「下一个人领卡时顺带清理」这条隐式路径，顺带不保证发生。
- **解法**：`git-done.ps1` 收尾时调用 `scripts/release-claims.py` 全量扫描，只删「本地与远端都没有认领分支」的条目，
  并作为独立的 `chore(board): 清理过期领用登记 <短号>` 提交快进补推；失败只回滚该提交，不阻断已完成的合并（`c8419b8`）。
- **预防**：清理走脚本，不要手删台账；`baseline` 仍归维护者所有（见 1.9）。

### 1.9 台账 baseline 落后于卡片交付说明

- **现象**：卡片 `note` 已写「交付（日期）+ 验收结果」，`baseline` 里同一条还标着 `todo`。
- **根因**：`baseline` 归维护者所有（agent 不得改），结项靠 `#短号=done` 提交标记 + `derive_board_status.py` 校正；
  其它会话合入的代码若没带结项标记，note 与 baseline 就会漂。
- **解法 / 预防**：结项必须带结项标记；发现漂移**不要顺手补 baseline**，漂移检查与代登记的落地见 1.14，
  台账缺项与结卡权仍卡在 7.1。

### 1.10 锚点同步没生效时的三步定位

- **第一步看原因**：`Sync-AnchorWorktree` 只会打这几类提示——锚点不在 `main` / 有未提交改动（会连带列出脏文件清单）/ 暂存失败 / 无法快进 / root 被占用只快进。
- **脏文件类**：清单已经打在日志里；要复核再 `git -C <锚点> status --porcelain`。只有 `progress.json` 是被允许的；冒出未跟踪目录就按 1.1 补 `.gitignore`。
- **分叉类**：`git log --oneline origin/main..main` 有输出，说明锚点有本地提交（见 7.2），**不要自行变基或改写历史**。
- **占用类**：只有 `progress.json` 脏却不进 stash、日志写「root 被占用」，是别的会话按 §7.1 登记了 root（见 1.16）；
  这类是**设计让路**，不是故障，快照交给占用者处理。

### 1.11 CI「自动回写看板状态」撞本地快照，整轮红 —— **已失效（R14-01 之后不会发生）**

> **这条现在不会再发生**：回写职责已整体取消 —— `.github/workflows/board.yml` 只校验，
> `permissions: contents: read`，写权唯一归 `git-done`。**没有第二个写者，就没有「两份快照撞车」。**
> 保留本节是因为它还解释了一件事：为什么写权最后必须收成一处。看到「回写步骤红」先确认
> 自己读的是不是 11-03 之前的构建日志。

- **现象**（历史）：推送合并后 CI 其它步骤全绿，唯独「自动回写看板状态」失败，日志是
  `error: could not apply <sha>... ci(board): 自动回写看板状态` → `rebase 到 origin/main 失败`。
  红的那轮其实什么都没坏——快照内容在 main 上是对的。
- **根因**（历史）：旧写法「先本地派生提交，再 rebase 推送」。本地若已跑过 `derive --write` 推过快照提交
  （收官时想让 `--check` 立刻干净，很自然会这么做），CI 生成的就是**内容重叠的重复提交**，
  rebase 必撞内容冲突；旧逻辑直接 abort。另一形态：给 CI 装钩子后回写提交被自家 `pre-commit` 拒
  （§11.2 第 7 条）。
- **当时的解法**：回写循环改为「fetch → rebase（撞车 `--skip` 丢弃重叠提交）→ 以最新主线重新派生 →
  提交 → 推送」（`c8419b8`）。治标；真正的解法是 R14-01 把写权收归一处。
- **现在的预防**：`progress.json` 唯一写者 = `git-done` 的 `Sync-BoardSnapshot`。本地手工
  `derive --write` 仍允许（它只是提前算出同一个结果），但**别因此以为 CI 会替你补** —— 它只会报红。
  锚点区看到快照落后时：`python scripts/derive_board_status.py --write` 后提交推送。

### 1.12 用 `fix` 类型提交纯脚本修复，被任务闸门拒绝

- **现象**：只改 `scripts/check_scope.py`（明明是杂务路径），提交却被 `commit-msg` 拦下：
  「本次改动触及代码路径，提交信息必须带任务号」。
- **根因**：闸门对豁免是**类型 + 路径双条件**——`feat` / `fix` / `refactor` / `perf` / `test` 类型即使只改
  杂务文件也必须有短号（宪章 §三末段）。设计意图：行为修复应当可追溯，「修脚本」听起来像 fix，
  但杂务分支要用 `chore` / `docs` / `ci` / `style` 类型来声明「这就是杂务」。
- **解法**：无卡提交改用 `chore(<域>):` / `ci(<域>):` / `docs(<域>):` 前缀重提（内容不变）。
- **预防**：写提交信息前先自问「这算不算行为修复」——算，就去领卡；不算（编码适配、文档、流水线配置），
  就用 chore/ci/docs 类型。别拿 `--no-verify` 硬闯，那会把台账保护一起绕穿（§7.10 教训同源）。

### 1.13 从锚点区跑 worktree 里的脚本，验的是旧版本

- **现象**：给 `agent-init.ps1` 加完「未推送成果」警告，从仓库根目录调用 worktree 里那份脚本试，警告没出现，
  一度判定「改动没生效」。
- **根因**：脚本按**自身路径**定位仓库（`AGENTS.md` §7.3），锚点区与 `origin/main` 落后且分叉时（见 1.1、7.2），
  从锚点目录发起的执行拿的是锚点检出里的旧代码，新改动根本不在执行路径上。
- **解法**：在改动所属的 worktree 里跑它自己的脚本，跑前先 `git rev-parse --show-toplevel` + `git log -1 --oneline`
  确认这份检出确实带着改动。
- **预防**：「验证没触发」先怀疑**跑的是哪份代码**，再怀疑改动本身——假阴性最容易引发二次返工。

### 1.14 已交付卡的 `baseline` 停在 `todo`，重复领用门失效

- **现象**：R6-01 ~ R6-06 代码都在主线、卡片 `note` 也写了交付，台账 `baseline` 里还是 `todo`；
  DX-01 / DX-02 / DX-04 更怪——`git log` 里有 `#短号=done`，`baseline` 干脆没有条目。
- **根因**：`baseline` 归维护者，agent 不得改；结项靠提交标记 + 派生器校正，但**派生器只校正 `progress.json` 的
  `status`，不动台账 `baseline`**（1.9），两条链长期各走各的。
- **风险**：「`baseline=<短号> done` 即拒绝重新领用」是条款 6 的防重复实现门；停在 `todo` 等于任何人
  都能把一张已交付的卡重领一遍，做出重复实现。
- **解法**（三处，均已上线）：
  1. **漂移检查进 CI**：`derive_board_status.py --baseline-check` 只报不改、退出码恒 0，
     CI「台账 baseline 漂移提示（只报不改）」逐步打印漂移与缺项清单（`05819b9`）；
  2. **授权代登记结卡**：维护者对具体批次明确授权后，在锚点区把 29 张已交付卡的 `baseline` 翻成 `done`
     （`1f0dbae`，只动既有条目、不补缺项，见条款 6 例外）；
  3. **领用重开门改读主线台账**：`claim-card.py` 用 `git show origin/main:docs/board-ledger.json` 判定，
     调用方工作区副本过期时不会把已交付的卡当成可领（`bd1bca7`）。
- **预防**：发现漂移**不要顺手补 baseline**（缺项另说，见 7.1）；代登记只允许翻授权批次内的既有条目，
  不得改 `id` / `title` / `stage`。

### 1.15 本地有提交、origin 无对应分支的工作区无人报警

- **现象**：某会话的实现只存在于 `.worktrees/<agent>`，origin 上没有同名分支，16 小时无活动，
  全靠维护者翻 `git worktree list` 才发现；该成果最终由人工合入（`6c235f1`）。
- **根因**：宪章 §7.2.1 要求认领分支必须推 origin（否则 claim ref 指向的提交在远端不可达、CI 领用闸门没法校验），
  但仓库里没有任何检查会主动列出「有未推送提交」的工作区。
- **解法**：`agent-init.ps1` 开区时扫一遍 `.worktrees/*`，用 `rev-list --count HEAD --not --remotes=origin`
  判定孤儿提交，命中即打 `⚠ 未推送成果：N 个工作区的提交不在任何 origin 引用可达范围内`，
  逐个列名称 / 分支 / 提交数 / 闲置天数，并指回 §7.2.1（`03b4424`）。
- **预防**：警告只负责让人看见，**回收决定仍归人**，脚本不自动删（§7.10，自动清理极易端掉正在跑的任务）；
  看到警告先确认该工作区有没有独有内容，再谈回收。

### 1.16 root 被占用时锚点同步照旧 stash 快照，踢掉别人的登记

- **现象**：另一会话已 `agent-workspace.ps1 -Action Claim -Scope root` 登记占用，`git-done.ps1` 的锚点快进
  仍把它的 `progress.json` 已暂存改动 stash 走。
- **根因**：`Sync-AnchorWorktree` 从不读 `agent-workspace.json` 里的 root 占用登记，只按工作区脏/净决策。
- **解法**：新增 `Get-AnchorRootClaim`——**按锚点路径**解析 `--git-common-dir`，读 `<common-dir>/agent-workspace.json`
  （显式 UTF-8，见 2.1）；root 被占用时**只快进、不动 `progress.json`、不 stash**，
  收尾原因改写「root 被占用（按登记让给占用者处理快照）」（`03b4424`）。
- **预防**：读跨 worktree 共享的元数据一律按目标仓库路径解析，**不要按调用方 CWD**——按 CWD 解析会把真实锚点的
  登记读进离线的假仓库验证里，造出假阳性（本条解法第一版就是这么错的）。

### 1.17 会话产物的落点没有口径，靠 `.gitignore` 逐个点名兜着

- **现象**：桌面工具的默认行为是往**调用方 CWD** 写产物，每接入一个新工具就多出一个未登记目录，
  在补进 `.gitignore` 之前它先拖脏锚点（1.1）。
- **解法**：`agent-init.ps1` 开区时在沙箱下建 `<worktree>/.sandbox/artifacts/` 并导出 `NB_SESSION_ARTIFACT_DIR`，
  agent 侧的会话产物有了显式落点（`03b4424`）。
- **预防**：agent 写临时产物走这个变量，别往仓库根丢；第三方工具不认它，治本约定还没人拍板（见 7.5）。

### 1.18 代登记规则写成了「只能在锚点区」，但锚点区根本不允许 agent 提交

- **现象**：按 DX-06 落地的口径开区登记新卡，被三道闸门连续拒 ——
  `agent-init -Card <新卡>` 报「台账未登记短号」（这是对的，卡还没登记）；
  在开发区 `git commit` 代登记，`pre-commit` 的 `task_gate ledger` 报「agent 工作区不允许改台账任务定义」；
  改到锚点区提交，`pre-commit` 规则 1 报「禁止在受保护分支 'main' 上直接提交」。
- **根因**：三条规则各自都对，叠起来没有交集 ——
  「代登记只能在锚点区」（条款 6 例外）＋「锚点区检出停在 `main`」＋「`main` 禁止直接提交」
  ＋「§7.1 禁止 agent 在锚点区切分支」＝ 代登记**没有任何合法执行位置**。
  DX-06 只做了内容审计，没验证过这条路径能不能走通；`task_gate ledger` 在 `pre-commit`
  阶段也**拿不到提交信息**，本来就没法判「授权行写了没有」，只能靠位置替代判断。
- **解法**（DX-07）：位置不是凭证，**内容才是**。`cmd_ledger`（`pre-commit`）背后不再判死，
  只提前打印「即将被审计什么」；真正判定交给 `cmd_message`（`commit-msg`，那里才有提交信息），
  任何检出里只要能给出 `chore(board)` + `授权：` + `批次：` 且不混代码，即放行。
- **教训**：**给规则加一个「必须在哪里做」的限制之前，先实测那个位置真的能提交。**
  与 1.13（从锚点跑 worktree 脚本）、§11.2 第 7 条（CI 既是闸门执行环境又是受管对象）同族：
  规则与环境相撞时，坏的不是规则本身，而是没人跑一遍。

### 1.19 `scope` / `acceptance` 不进审计，等于留了一条自我扩权的后门

- **现象**：R10-02 立卡时漏写 `src/main.cpp`、`src/board/BoardColumns.*`、`tests/` 三个实现必需路径，
  实现到一半才发现 —— 于是 agent 自己写了两条 `chore(board)` 把 `scope` 补上，继续做完。
  整个过程没有任何「谁授权」的记录，事后翻 git 也看不出这两次扩权是维护者同意的还是 agent 顺手加的。
- **根因**：`task_gate.py` 的 `ledger_shape()` 只提取 `id` / `title` / `stage` / `baseline` 做判定，
  `scope` 与 `acceptance` 不参与 —— 它们被当成「备注」而不是「契约」。而 `scope` 恰恰决定
  `check_scope.py` 拦不拦得住越界：改它等于改自己的活动边界；`acceptance` 决定验收口径：
  改软它等于自己判自己及格。两条都是零成本、零留痕。
- **解法**（DX-10）：把两者纳入 `ledger_shape()`，比较时去空白、忽略顺序（纯排版不触发）；
  改动必须单独 `chore(board)` + `授权：`/`批次：` 两行，且不得与代码同提交。
  自动结卡通道仍只认 `baseline`，夹带 `scope` 改动照拒。
- **教训**：**「哪些字段算任务定义」不能按字段名拍脑袋，要按「改它能不能改变闸门行为」来定。**
  凡是能改变闸门判定结果的字段（边界、验收口径），都必须和加卡同等审计；
  否则规则只约束了「加任务」，没约束「改任务的边界」。

### 1.20 判断卡是否被占用，别信本地 claims 镜像

- **现象**：一次会话误开了已结卡（R5-06）的工作区重做一遍；另一次差点抢走并行会话正在做的卡（R12-01）。
- **根因**：本地的 `refs/claims/*` 只是**镜像**，会话开着没推、或已释放没同步，按它判断占用必然失真。
- **解法 / 预防**：动手前现查远端——`git ls-remote origin 'refs/claims/*'`；「重开门读主线台账」那道门
  （1.14 第 3 条）防的是已交付卡，防不了「别人正拿着做」，两道检查各管各的。

---

### 1.21 结卡后快照停在旧状态，`derive --check` 报「待同步」

- **现象**：刚结完卡，`python scripts/derive_board_status.py --check` 退出 1，报
  「待同步：卡片 X 状态 todo → done」；台账里 `baseline` 明明已是 done。
- **判据**：只有这一个判据。不要凭「看板显示不对」猜，也别把派生器提示里的
  `#RGB` / `#ERROR` 当问题 —— 那是提交信息里的十六进制串被误认成卡号。
- **根因（两处叠加，2026-10-30 在 GitHub 上实测）**：
  1. `scripts/git-done.ps1` 的快照校正整段包在「progress.json 已脏」的条件里 ——
     应用（RoadmapSync）没写回快照的会话直接跳过；而结卡（`--close-cards`）只改
     `docs/board-ledger.json` 的 baseline，从不刷新快照。
  2. CI 兜不住：`ci.yml` 的 `paths-ignore` 含 `docs/**`，结卡这类 docs-only 提交
     **完全不触发流水线**（`gh api repos/<owner>/<repo>/commits/<sha>/check-runs`
     返回空即可证实）；而原「看板状态一致性校验」步骤带
     `if: github.event_name == 'pull_request'`，本仓库零 PR
     （`gh pr list --state all` 为空），实测该步骤结论恒为 skipped。
- **处置**：本地跑 `python scripts/derive_board_status.py --write` 并提交；
  DX-13 起 `git-done` 收尾自动刷一遍，`.github/workflows/board.yml` 作确定性兜底
  （main push 回写、PR 只校验）。与 1.11 是同一文件的两面：那里是「两边同时写快照撞车」，
  这里是「谁都不写」。
- **排查命令**：`gh api repos/<owner>/<repo>/commits/<sha>/check-runs`（提交有没有触发 CI）、
  `gh api repos/<owner>/<repo>/actions/runs/<run-id>/jobs`（看步骤结论是不是 skipped）。

### 1.22 残留工作区只抛一句「请先 remove」，人没法决定删不删

- **现象**：`agent-init.ps1` 撞到已存在的 `.worktrees/<agent>`，只抛
  「工作区已存在…（如需重建请先 git worktree remove）」；分支重名时更糟，`git worktree add -b`
  直接失败、没告诉你那个分支上有多少东西。
- **判据**：§7.10 明令「删不删由人决定」，而做决定要三个数：**有没有未提交改动**、
  **有没有未推送提交**、**相对 origin/main 领先几个**。缺这三个数就是在逼人闭眼删。
- **处置**：DX-12 起两个函数给齐材料 —— `Show-StaleWorktree`（分支 / HEAD / 未提交文件清单 /
  未推送提交 / 可直接复制的 `git worktree remove --force` 与 `git branch -D`）与
  `Show-StaleBranch`（领先 origin/main 的提交数、未推送提交、`git branch -D`）。退出码 4，
  **不自动删除任何东西**。
- **预防**：写「拒绝并让人处理」的分支时，把「人需要知道什么才能决定」列出来 —— 只报错的
  门等于把人挡在门外还不给钥匙。

### 1.23 `pre-push` 把发布标签当成分支，正常发布推不上去

- **现象**：`git push origin v0.1.4` 被自家钩子拒了，报「分支 `refs/tags/v0.1.4` 尚未合并进
  `main`，禁止推送」，还建议 `git merge --no-ff refs/tags/v0.1.4` —— 一个 tag 没法「合并进 main」，
  这条建议在语义上就不成立。
- **根因**：规则 1 的循环里只豁免了 `refs/heads/*` 与 `refs/claims/*`，`refs/tags/*` 落进了
  「按分支处理」的默认路径。标签推的是 tag 对象，`local_sha` 不是提交。
- **为什么一直没人踩到**：增量链路那次验收走的是 `workflow_dispatch`（空 tag 只出 artifact、
  不发布 Release），**恰好绕开了唯一会撞这道门的动作**。用一条不走 tag push 的路径去验「tag 触发
  的发布」，等于只验了一半 —— 另一半第一次真发版才炸。
- **处置**：`.githooks/pre-push` 对 `refs/tags/*` 换成**对偶判据**而不是放行 ——
  标签指向的提交必须已在 `origin/main` 上（取不到才退回本地 `main`，与「重开门只看 origin/main」
  同源），否则拒绝。判祖先前用 `git rev-parse -q --verify "<local_sha>^{commit}"` 把附注标签剥到
  提交对象，漏了这一步判据恒假、连正常的发布标签也推不上去。
- **验证**：三例探针（附注标签@主线 / 轻量标签@主线 ⇒ 退 0；标签指向主线外的提交 ⇒ 退 1 并给出
  重打标签的命令）。探针 stdin 形如 `<sha> refs/tags/<名> refs/tags/<名> <zero>`，直接喂给钩子即可。
- **预防**：闸门按 ref 类型分流时，把「这个 ref 是什么」写进注释并为其一种形态单独造一个反例；
  只测「正常分支」的闸门，会在第一次非常规但合法的发布动作上咬人。

### 1.24 卡片 `scope` 越界不会被任何一道门拦住

- **现象**：R16-05 的分支声明 scope 是 `plugins-samples/today/`、`plugins-samples/surfaces/`、
  `plugins/plan-hook/` 三个目录，实际还改了 `tests/tst_pluginruntime.cpp`。`git-done` 一路绿，
  合入主线、结卡、释放占位，全程没有任何一道门提到越界。而手工跑
  `python scripts/check_scope.py --card R16-05` 会明确报「✗ 改动越出卡片 R16-05 的 scope」。
- **根因**：`check_scope.py` 只挂在 `.github/workflows/ci.yml`（任务卡范围检查那一步），
  且带着 `continue-on-error: true` —— 棘轮期只作信号不阻断；`git-done.ps1` 合并前跑的是
  **领用闸门**（短号有没有持证），不是范围闸门。两者覆盖面不同，却容易被当成同一道门。
- **判据**：`grep -n check_scope scripts/git-done.ps1 .githooks/*` 为空 = 本地与推送期都不查；
  CI 那一步的 `continue-on-error` 存在 = 主线上的红线也只是一行日志。
- **这意味着什么**：条款 6 里「`scope` 决定 agent 被允许改哪些路径」目前只是**评审可见的声明**，
  不是硬门。R16-05 那次是正当改动（测试夹具跟着实现走），所以无人受伤；
  但同一道缺席的门，也放行过一次真正的误改（R10-02 立卡漏写三个必需路径，
  agent 两次自补 `scope` 做完，见 1.19）。
- **收紧的前提**：直接去掉 `continue-on-error` 或把 `check_scope` 接进 `git-done`，
  会立刻把所有历史存量卡（未补 scope 或补得不全）挡在合并门外 —— 这是「先清存量再收门」的
  老次序，需要人拍板节奏，不由 agent 自行决定。存量清零前，改卡片 scope 仍要走
  `chore(board)` + 授权/批次两行（1.19），因为**门越松，声明就越要留痕**。

### 1.25 领用幂等门只看分支历史：占位释放后同一张卡再也领不回来

- **现象**（2026-11-05，R5-10 现场）：卡合并、占位被 `git-done` 释放后，从 main 新开分支
  想重领同一张卡，`agent-init` 判「已在本分支领用，跳过重复登记」直接跳过 —— 领用登记、
  占位推送全被跳掉，只能直调 `claim-card.py` 绕过唯一入口。
- **根因**：幂等门只查「分支历史里有没有那条 `chore(claim)` 提交」。新分支从 main 切出来
  会继承 main 的**全部历史**，合并过的卡的领用提交永远在里面；而「是否仍被占用」的真正
  事实在服务端 `refs/claims/<短号>` —— 占位已被释放，本地历史却还留着旧痕迹。历史是投影，
  服务端才是事实（与 1.20「别信本地 claims 镜像」同源）。
- **解法**（DX-35）：幂等门命中历史后**再查一次服务端占位**——
  `git ls-remote --exit-code origin refs/claims/<短号>`：退 2（占位已释放）→ 放行重新登记
  并由 claim-card 重推占位；退 0（占位仍在）→ 维持跳过（DX-03 原意：重复跑不产生第二条
  领用提交）；其它退出码（网络不可用）→ 保守当仍被占用，与「领用需联网」的 fail-closed 同向。
- **验证**：占位在时重复 `-InPlace -Card 同卡` 不产生第二条领用提交；占位删掉后同命令
  放行重领，claim-card 重写登记并重推占位。
- **教训**：凡是拿「本地有没有做过 X」当「X 是否仍然成立」的判定，都要再问一句：
  这个事实的**唯一事实源**在哪，退化投影会不会比事实活得久。

### 1.26 拿注定短暂的中间态套最终状态判据，每次收尾固定产 1~3 封失败邮件

- **现象**（2026-11-05，两组收尾实测）：每次合并收尾后，维护者收到 Board 流水线的一串失败邮件，
  下一分钟又自己变绿。计数：R16-05 3 封、R5-08 2 封、R5-10 2 封、DX-40 立卡 1 封 ——
  **数量恰好等于那次收尾推了几个不自洽提交**，且每天都稳定复现。
- **这不是竞态，是确定性的**：机制四条叠加 ——
  ① `git-done` 分步快进补推（合并 / 清理登记 / 结卡 / 快照各一次 `push origin HEAD:main`）；
  ② `board.yml` 每次 main push 都跑 `derive_board_status.py --check` 且**不回写**（R14-01 收权）；
  ③ 合并提交推上去那一刻，`#短号=done` 标记即已生效而 baseline / 快照还是旧的；
  ④ 快照**必须**最后刷新（派生器要读前两步写完的 git 事实）。
  所以第 ①②③ 步的每一个 run 都必然测出一个「不一致」，而这个不一致几分钟后必然消失。
- **判据**：对一次收尾的三个提交逐个 `git checkout` 后跑 `--check`，退出码固定是
  **1 / 1 / 0**（实测基线 R5-10 收尾 `9e43177→47d1ced→3a78cc8`；R5-08 收尾 `52c908f=1`、
  `4a2a6bb=1`、`7116780=0`）。**红的是「时序」，不是「内容」** —— 拿单个中间态的 run 当结论，
  就是在要求主线在任何两次推送之间都必须自洽，而收尾的定义就是分几次推送完成一件事。
- **解法（DX-44）**：`board.yml` 的 `concurrency.cancel-in-progress` 由 `false` 翻成 `true`，
  让后一个推送取消前一个还在跑的 run，**判据落点自动挪到 main tip 那一次**。但取消只在「上一个
  run 还在跑」时才成立，**前提是 job 比推送间隔久**：起初按「job 约 40 秒、推送间隔约 8 秒」成立，
  实测 job 掉到 ~10s、推送间隔 6~13s 后前提失效 —— 中间 run 在下一个推送建 run 之前就跑完，
  按 failure 结算、无法再取消（2026-11-05 起又漏邮件）。**补齐**：job 首步加 ~30s 沉降等待，
  把「job 比推送间隔久」重新坐实，中间 run 在新推送到来时仍 in-progress 被取消。
  校验逻辑、`--check` 的严格程度、`permissions: contents: read` 一字未动；
  main tip 红才等于收尾真的失败了（离线 / 撞车 / 脚本失败）。
- **双保险的另一翼（DX-96）**：结构上把注定失败的中间态 run 从 3 个砍到 1 个 —— git-done
  收尾的清理登记 / 结卡 / 快照不再各自补推，改为本地提交后**单次补推**（重试集中、失败
  整体回滚到合并点，fail-soft 交付的合并不受波及）。沉降等待负责「取消得掉」，补推合并
  负责「本来就少」；叠加后每批收尾固定恰好 2 个 run（合并 + 收尾），收尾 run 落点必为
  自洽 tip。次数由 verify.ps1 的**静态推送点断言**守着（对 main 恰 2 处）—— 动态
  dry-run 统计进不了常规验收：git-done 拒脏树，而验收恰在提交前跑，动态口径永远走不到。
- **终局（DX-106，A2b）**：合并提交与清理登记 / 结卡 / 快照提交全部落本地后合成对 main 的
  **唯一一次推送**（收口在 `Push-Main` 单一实现），每批收尾只建 1 个 run，中间态在结构上
  **不存在**。撞 non-ff 变基重试（上限 3 次）保留且集中；仍失败则整体回滚收尾提交到合并点、
  **单独推送合并**（fail-soft 重排：交付不被收尾拖住，原 DX-96 的「合并先推」语义由回退路径
  承接）。DX-44 的 cancel-in-progress 与 ~30s 沉降等待降级为**纯双保险**（单推送下无中间
  run 可言，保留只为防未来重新分多次推送时回归）；静态推送点断言改为「Push-Main 定义 1、
  收口推送语句 1、游离推送语句 0」（每 run 恰 1 次；fail-soft 回退与主路径共用 Push-Main，
  两路互斥）。
- **DX-106 连带的时序调整（值得记住的坑）**：单次推送下 worktree 回收与分支删除发生在推送
  **之前**（release-claims 判台账登记失效要靠「分支已删」，必须排在它前面），于是旧的回收
  安全判据「还有未推送提交（`--not --remotes=origin`）」在这里**恒真**，每个工作区都会被判成
  不可回收、永远留僵尸。判据改成「分支提交是否已全部进本次合并」（`rev-list --count HEAD
  --not <合并提交>`）—— 刚做的 `--no-ff` 合并就是收纳证据，不在其中的提交才是真会丢的东西。
  改「防丢」类判据时先想清楚它依赖的推送时序，时序变了判据要跟着换锚点。
- **为什么不是别的修法**：
  - 「让 `--check` 区分『滞后』与『矛盾』」——**否决**。收尾失败的唯一外部表现就是快照一直滞后，
    把这个判据拆掉等于把眼睛挖了：以后收尾静默回滚，没人知道。
  - 「三步合并成一次补推」（方案 A）——列二期。它动 `git-done.ps1`，不在本卡 scope，
    而且它消灭的是中间态的**存在**，本卡消灭的是中间态的**发言权**；两者不冲突，先做风险小的。
- **一般化**：**给「最终状态」写的校验器，触发时机必须落在「只有最终状态才会出现的那个点」上。**
  只要一个不变量是分几步提交的，任何挂在「每次提交」上的判据都会稳定地误报，
  而稳定误报的闸门和没有闸门一样没用 —— 更糟，因为它教会所有人不读它的输出。
  修这类问题的顺序也固定：先问「判据该由谁触发」，再问「判据本身是不是太松」；
  一上来就放松判据，通常是在用第二种病治第一种病。

### 1.27 Bash CWD 静默漂移：跨检出调用脚本现在默认中止（DX-49）

- **现象**：多会话并行时，终端 / Bash 工具的 CWD 会在锚点区与各 worktree 之间静默漂移——
  连续的相对路径命令一部分落在锚点、一部分落在 worktree，表现为「文件改了又消失」
  「status 互相矛盾」。更隐蔽的形态是从 A 检出调用 B 检出里的脚本：脚本按自身路径作用于
  B（宪章 §7.3 的定位设计），而调用方以为作用在 A —— R16-05 实测把示例插件改动误写进
  锚点区并 stage。
- **根因**：「脚本作用于哪棵树」（按脚本自身路径定位）与「调用方站在哪棵树」（CWD）是两个
  独立事实，两者不一致时此前没有任何声音；状态误读比错误本身更耗时。
- **解法（DX-49）**：`agent-init.ps1` 与 `git-done.ps1` 入口新增**检出漂移守卫**——调用方
  CWD 所在检出与脚本所属检出是**同一仓库的不同工作树**（`git rev-parse
  --path-format=absolute --git-common-dir` 相同）时默认中止，退出码 5，打印两棵树路径与
  两条出路：cd 到目标检出再跑；或加 `-AllowCrossTree` 显式放行并打显著告警。
  调用方不在 git 检出里、或属于别的仓库时不拦（没有漂移信号，守卫自身 fail-open，
  不替代审计闸门）。
- **预防**：跨区调用从「静默成立」变成「必须显式表态」；§7.3 的按脚本路径定位模式不变，
  有意跨区时多敲一个参数即可。注意 `git-done` 内部本就 `Set-Location $repoRoot`，
  守卫拦的是「调用方的认知与实际作用树不符」，不是脚本内部的路径解析。

### 1.28 收尾顺序没有自动化测试：git-done -DryRun 档（DX-50）

- **现象**：git-done 的收尾时序（三步自动提交在分离 HEAD、归位在最后）「要真 push 才跑得到」，
  没有任何检查能在合并前发现顺序错误 —— DX-20 把归位提前时，所有检查全绿，
  端到端才炸，一天误判两次（2.15）。
- **根因**：收尾序列的每一步单看都对，「顺序」才是被测对象；单元测试测不到
  「这一步此刻跑在哪个分支上」这种位置前提。
- **解法（DX-50）**：`git-done.ps1` 新增 `-DryRun`：fetch/push 全部打桩不触网，
  合并与三步收尾提交在本地真实走一遍，逐步断言「提交发生在分离 HEAD」、
  「归位在所有提交之后」，结束切回进入时的分支、演练提交全部丢弃；
  `verify.ps1 -GitDoneDryRun` 接入该档，对当前分支预演「这次收尾能不能过」。
- **预防**：改收尾顺序前先跑一次演练；断言的是**位置前提**（在哪个分支上做），
  不是结果 —— 位置错了，后面的每一步都会错。

### 1.29 交付合并但没投 `#=done` 标记的卡永久滞留，还能被重复领用（DX-62）

- **现象**：DX-38/DX-44 的交付提交早在 origin/main 全历史里，提交信息也带着裸 `#短号`，
  但没有任何一处 `#短号=done` —— `--close-cards` 只认带状态的标记，这两张卡的
  baseline 永远停在 todo，防重复领用的门（baseline=done）永远关不上。
  2026-11-06 核实：DX-38/DX-44/DX-60/DX-61 四张卡同症，全靠人工记得补结才没重做。
- **根因**：自动结卡（DX-07）的输入是「最后一次 `#短号=状态` 标记」，它对
  「交付了但漏投标记」的卡天然无感 —— 没有标记可读，就不会被结，也不会被任何
  探针点名。漂移探针（`--baseline-check`）只报「有 done 标记但 baseline 没跟上」，
  覆盖的恰好是另一半。
- **解法（DX-62）**：`--baseline-check` 增加第三类点名「滞留」—— 交付提交已合入
  主干（裸 `#短号`、非领用/立卡流程提交）但全历史无 `#短号=done`、baseline 仍未结；
  另给 `--stuck-codes` 机器可读出口，`agent-init.ps1` 领用前按主线台账核对并显式
  警告「交付疑似已在 main，防重做」。补结仍走人工确认通道
  （`--close-cards --close-explicit`），不放宽自动结卡的判定。
- **预防**：裸标记与状态标记的口径差是这类坑的温床 —— 裸 `#短号` 只在 subject 认
  （body 是散文，交叉引用会误报），流程提交（`chore(claim)` / `chore(board)`）的
  裸标记一律不算交付。判定的单一实现在 `derive_board_status.py`，自测里复刻了
  DX-38/DX-44 的真实历史形态做回归。

### 1.30 开区是半途可失败的多写动作序列，却按「一步成功」写（DX-66）

- **现象**（2026-11-06 领 DX-69 实测）：`agent-init.ps1` 里 worktree add → 占用登记 →
  领用空提交 → 服务端占位推送是四步独立的写动作，任何一步失败都把前面建好的产物留在现场 ——
  DX-69 被并行会话抢占，推送 `refs/claims/DX-69` 被拒时 worktree / 分支 / 占用登记
  **三处残留**，全靠手工 `git worktree remove` + `git branch -D` + `Release` 对账清完。
  网络故障同理：fetch 在前、领用推送在很后面，炸的位置越靠后留的残局越大。
- **根因**：脚本按「顺序执行、每步自洽」写，没有把「一系列写动作」当成一个事务 ——
  失败路径只负责报错，不负责把它创建了一半的东西收回去。凡是「多个写动作 + 中途可失败」
  的序列，缺回滚就等于把清理成本转嫁给每一个撞上失败的调用方。
- **解法（DX-66）**：`agent-init.ps1` 开区事务化，三层防线：
  ① **预检前置** —— 任何写动作之前先 `ls-remote` 探 origin 可达性（3 次带退避），
  全失败 exit 6，此刻零产物；② **领用前置探针** —— 新开 worktree 领卡前先探
  `refs/claims/<短号>`，占位已存在（双领）或不可达（fail-closed）直接拒绝，
  不再「建完才发现被抢」；③ **事务回滚** —— 从第一个写动作起登记本次创建的产物
  （worktree / 分支 / 占用登记 / 领用提交 / 服务端占位），任一步失败按固定顺序
  （提交 → 服务端占位 → 占用登记 → 工作区 → 分支）逆向回滚并逐项打印。
  服务端占位按 **sha 归属**判定后才删：等于本次领用提交头才释放（本次的孤儿占位），
  指向别人一概不碰 —— claim-card 是「先推占位、后做扫描」，exit 3 既可能是没推成
  也可能是推成了才被扫描拒，只有 sha 能区分。
- **实测**（隔离 clone 六场景）：断网 exit 6 三处零残留；双领探针 exit 3 零残留；
  占用被拒回滚后他人登记原样保留；claim-card exit 2 全量回滚；
  原地补领幂等重跑 HEAD 不变；占位推成后崩溃 → sha 命中 → 服务端占位自动释放
  （`ls-remote` 复查 0 条）。
- **预防**：给「多写动作序列」加步骤时，先问失败回滚清单里有没有它；
  退出码新增要写进脚本头部的出口码表。模拟断网别用 `https_proxy` 环境变量 ——
  全局 `.gitconfig` 的 `http.proxy` 优先级更高，要用
  `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=http.proxy GIT_CONFIG_VALUE_0=<死端口>` 一次性覆写。

### 1.31 投影重建不消费服务端领用占位：已领用卡被重建成 created，report/close 全被拒（DX-73）

- **现象**（2026-11-06 DX-66 实录）：`nbflow dispatch` 回执明确输出
  「DX-66: created → dispatched」，`refs/claims/DX-66` 已推上 origin（`a91f544`），
  分支带两条领用提交；随后 `nbflow status` 却显示 created、state.json 无 DX-66 条目，
  report 被拒「DX-66 不在可执行态（当前：created）」，只能绕过 nbflow 走 git-done 裸收尾。
- **根因**：投影重建（DX-65）只吃两样事实——台账 `baseline` 与会话 `head` 是否并入
  `origin/main`——**服务端 `refs/claims/*` 这个领用唯一硬门反而不在事实集里**。
  state.json 是并行会话共享的读改写文件（DX-66 的条目丢失即并发覆盖），条目一丢，
  补影就把已领用卡当成「从没人动过」重建成 created；report/close 只认 dispatched 及之后，
  回路就此断头。补影漏掉的不是边角数据，是**闸门自己认的事实源**。
- **解法（DX-73）**：重建消费占位，三条路径：
  ① 补影时占位仍在的卡直接重建为 `dispatched`（`claim_refs()`：`ls-remote origin refs/claims/*`，
  离线退本地镜像并声明可能含孤儿）；② 会话滞留 created 但占位仍在 → 对账升回 dispatched
  （`reconcile_plan` 新规则）；③ 升级/补影同时尽力恢复领用现场——扫**锚点区**（git common dir
  父目录，不是 REPO：`.worktrees` 只在锚点存在，从 linked worktree 里跑 nbflow 按 REPO 扫必扫空）
  各 worktree 的 handoff.json 恢复 branch/worktree/agent/sessionId，扫不到再看「占位指向的提交
  未并入 main 且仍被某分支包含」取分支名。已并入 main 的领用现场**不猜**——之后从 main 新切的
  分支都「包含」它，恢复出来的分支名会指错人。
- **预防**：selftest 固化四条断言（占位仍在的缺会话卡重建 dispatched + 恢复现场 / 无占位仍补影
  created / 滞留 created 升回 dispatched / 纯函数无占位不动）；实机口径 = 删 state.json 后
  `nbflow status`，已领用卡必须带完整现场回来。判据写法上记一条：**凡闸门认的事实源，投影重建
  就必须消费；新增事实源（如未来的占位字段）时先过一遍 reconcile 三条路径**。

### 1.32 nbflow dispatch 把建区脚本指向调用者检出：§7.10 自动原地模式反噬派发链（DX-89）

- **现象**（2026-11-06 DX-73 交付实录）：close 从 linked worktree 里探测到台账冲突，自动建工单
  DX-85 并 approve 合回主线成功；随后的自动派发却撞「检出里已有他人 in-place 环境」守卫 exit 3，
  工单卡死 created。工单没派出去，冲突也没人接。
- **根因**：`cmd_dispatch` 用 `ps_script(..., root=REPO)` 调 `agent-init.ps1`——REPO 是**调用者**
  所在检出。从 linked worktree 里跑 nbflow（report/close 的常态位置）时，REPO = worktree，
  agent-init 检测到「脚本所属检出本身就是 linked worktree」按 §7.10 自动转原地模式（-InPlace），
  把领用往调用者检出里塞，撞 in-place 守卫被拒。领卡语义是「在**锚点**建 `.worktrees/<agent>`」，
  建区脚本的定位与调用位置无关；`handoff.json` 记录的现场路径同样用了 `REPO/.worktrees`，
  会让 report/close 在错误的检出下找派发包。与 DX-49 的区别：DX-49 治「调用方检出与脚本所属
  检出不一致要响亮失败」（防跨区误操作），本卡治「程序自己把建区动作指到了错误检出」。
- **解法（DX-89）**：`dispatch_argv()` 统一以 `ctx.common.parent`（锚点区根）定位 agent-init.ps1
  与 handoff 现场路径，cmd_dispatch 单一调用方（`raise_conflict_ticket` → `cmd_dispatch` 同一
  实现，不复制第二份 root 判定）；从锚点跑时锚点 = REPO，行为不变。
- **预防**：selftest 断言「dispatch root=锚点而非调用者检出」（stub common-dir 指向临时目录，
  命令行必须出现锚点的 agent-init.ps1、不得出现 REPO 的副本）；实机口径 = 在 linked worktree 里
  `nbflow dispatch --dry-run`，打印的脚本路径必须是锚点那份。判据写法上记一条：**「在锚点建区」
  类动作（开区、出包、立卡合回主线）定位脚本时一律按锚点/目标检出，绝不按调用方 REPO**——
  调用方 REPO 只适合「作用于调用方自己」的动作（verify、report 的验证）。

### 1.33 取号占用集只看 origin/main 台账：锚点未推的立卡挡不住同号再分配（DX-92）

- **现象**（2026-11-06 DX-88 实录）：维护者会话在锚点本地立卡 DX-74「git-done 收尾补推 3→1」
  （`75cd573`，合并提交 `45e1e93` 停在锚点未推送）；另一会话 nbflow approve 以 origin/main 台账
  取号，DX-74 被另一任务「剩余脚本裸 git 取值清理」（`6505441`）占用——同一卡号挂了两个不同任务，
  锚点 main 与 origin/main 分叉（领先 3 提交 / 落后 80+）。
- **根因**：取号占用集 = `ledger(origin/main) | 活队列 | 在途会话`，本地 main（锚点检出）的台账
  不在内。锚点区只要有未推送的立卡，任何会话的下一次取号都会把同号配给新任务。孤儿任务内容
  事后按 DX-88 保全重立，但**撞号判据不补，锚点再有未推立卡就重演**。
- **解法（DX-92）**：`ledger_card_ids()` 收口为 origin/main 与本地 main 的卡号**并集**
  （`ledger_ids_from_docs` 纯函数 + `local_main_doc()` 读 `git show main:docs/board-ledger.json`，
  取不到退 None）；`next_conflict_card` 的自带取号与 `known_titles` 查重语料同源消费（Z5：
  单一实现），本地 main 独有卡立即被拒 / 改配。并集是保守方向——多拒一个号，换号即可。
- **预防**：selftest 断言并集含本地 main 独有卡号（纯函数 + 全局注入双台账）；实机口径 = 锚点
  main 领先 origin/main 且台账有本地独有卡时，`nbflow request` 不得取到该号。判据写法上记一条：
  **凡是「从台账推导的占用/查重集合」，来源必须枚举全部本地可见的 main 引用（origin/main +
  main），只写一个来源就是把分叉检出当不存在**。

### 1.34 半删除的 worktree 尸体被当活检出复用：git -C 沿目录树落到锚点，锚点检出被静默切走（DX-93）

- **现象**（2026-11-06 实录两段）：①DX-89 收尾 `git worktree remove` 撞 Permission denied，git
  注销了 worktree 但目录半删除（文件残缺、`.git` 指针指向已清理的管理目录）；②随后 DX-92 的
  approve 走「目录存在即复用」路径，在尸体里跑 `git -C <尸体> checkout -B`——git 沿目录树向上
  搜索落到**锚点 .git**，把锚点检出从 main 静默切到 `chore/board-2026-11-06`（当日两次，均手工
  切回，锚点未推提交未受损）；board-apply 也因尸体 scripts 残缺报 can't open file 而失败。
- **根因**：cmd_approve 复用立卡 worktree 只判 `wt.exists()`——**目录存在 ≠ 检出可用**。
  半删除尸体没有 `.git` 指针，`git -C <尸体>` 不报错、沿目录树向上找最近的 `.git`，命中的是
  锚点仓库：后续命令全部落在锚点上执行。broken 检出是跨仓库误操作入口，正撞 §7.1 要防的事故面。
- **解法（DX-93）**：`worktree_alive()` 三重验活（scripts/board-apply.py 在 → `git -C wt
  rev-parse --git-common-dir` 可达 → 注册指向**本仓库** common dir）；验活失败走
  `discard_corpse_worktree()`（prune 注销 + 删目录），删不掉（被占用）**fail-closed 中止并点名
  路径**，绝不带着尸体继续。approve 的复用路径与 probe_merge 的探测区复用同源消费同一 helper。
- **预防**：selftest 两条负例（缺脚本判死 / 目录非注册 worktree 判死）；实机口径 = 造一具尸体
  （删 worktree 管理目录留壳）后重跑 approve，必须看到「尸体目录删不掉/已清理重建」而不是继续
  在壳里跑。判据写法上记一条：**任何 `git -C <dir>` 之前，先确认 <dir> 是本仓库注册的 worktree
  （注册可达且 common dir 对得上）——`-C` 从不保证你站在你以为的仓库里**。

### 1.35 dispatch 失败停不进 blocked：失败处理自己撞「非法状态转移」（DX-71/DX-75）

- **现象**（2026-11-06 DX-71 领卡实测）：origin 抖动使 dispatch（agent-init）失败，nbflow 想
  把卡停进 blocked，却打出「非法状态转移：DX-71 created → blocked（允许：abandoned, dispatched,
  requested）」——失败处理逻辑被自己的转移表拒绝，异常裸抛、卡悬在 created，没有任何
  「这一步失败了」的状态留痕。
- **根因**：blocked 早已是一等状态（approve 落盘失败用，DX-72），但 `TRANSITIONS["created"]`
  没放它进来——**停放态的入口没有跟着新的使用方一起扩**。更隐蔽的是出口：就算进得去，
  `cmd_dispatch` 只认 created，blocked → dispatched 重跑会被入口再拒一次，停进去就出不来。
- **修复（DX-75）**：① `created → blocked` 合法化（dispatch 失败可停放）；②
  `blocked → dispatched` 合法化且 `cmd_dispatch` 接受 created|blocked（出路对应入局原因：
  哪步失败停在哪步后面，重跑哪步）；③ status 对 blocked 卡打印明确出路行；④ selftest
  钉死三件事：created→blocked 合法、blocked→dispatched 合法、blocked 不可直达 closed
  （「未 report 不能 close」不被停放态绕过）。
- **判据**：`python scripts/nbflow.py --selftest` 全过（含 DX-75 场景演练
  created→blocked→dispatched）；真实失败路径不再抛异常，卡停在 blocked 且 status
  给出重跑指引（DX-112 abandon 前的 blocked 态即实测样本）。
- **一般化**：**给状态加语义时，必须同时回答「怎么进得去、怎么出得来」**。转移表是
  按使用方逐个扩出来的，新使用方接入失败路径时，入口出口都要验——只有入口没有出口的
  停放态，是把「步骤失败」变成「永久卡死」。
### 1.36 手工立卡无凭证也无闸：授权靠提交信息两行自证，撞号后无从对账（DX-98）

- **现象**（2026-11-06，DX-74 现场）：立卡靠人/agent 手工改 `board-ledger.json`，卡号 =
  「扫描时点 max+1」（1.33 的取号竞态同源）；提交信息里的 `授权：`/`批次：` 两行是唯一
  凭证，谁都可以写，没有机器可校验的登记记录 —— 撞号后只能靠翻会话记录对账谁先谁后。
- **根因**：立卡是「读台账→本地决策→写回」，既没有临界区（发号竞态，见 1.33），也没有
  不可抵赖的产物（凭证自证）。凡是靠提交信息文本自证的授权，删掉两行重写毫无成本。
- **解法（DX-98）**：立卡收口为程序唯一入口 `scripts/register_board_cards.py`——
  ① git 通用目录跨 worktree 文件锁内发号（锁内重读台账再 max+1，同机并发串行化）；
  ② 每张新卡生成**回执** `docs/card-requests/registered/<短号>.json`，携带卡片规范 JSON 的
  sha256 与 `authorizedBy`；③ `task_gate` 在 commit-msg 阶段核对：新增卡无回执 / `card`
  不符 / 哈希不符 / 缺授权来源一律拒绝 —— **手工改台账的立卡通道就此关闭**，授权来源
  从「可自证的散文」变成「机器可校验的凭证」。自动结卡（仅 baseline、`自动来源：`）不产
  新卡，不走回执，通道不受影响。同源残余「**跨机竞态**」（锁只覆盖本机）由 DX-99 闭环：
  ④ 发号在锁内先推**服务端占位** `refs/registrations/<短号>` —— 新建 ref 的推送在 origin
  是原子的 test-and-set，推成即占号、被拒即刷新占用集换号重试；占用集 = 本地台账 ∪
  origin 台账 ∪ 占位全集，占位永久留痕（顺带补全 1.33「占用集来源」的病根）；离线
  fail-closed，显式 `--offline` 降级为暂定号并写入回执。两处收尾修正（DX-104）：
  ⑤ 占位 sha 必须是 commit-tree 直造的**唯一 off-main 空提交**——v1 指向 HEAD 时，
  别人的主线后代可 plain push 快进顶掉占位，test-and-set 失效；off-main 尖端无人可
  为其后代，异 sha 重推必 non-FF 被拒，plain push 即严格仅创建。⑥ pre-push 钩子须
  对 `refs/registrations/*` 给出与 `refs/claims/*` 同款豁免（服务端占用 ref 非分支
  推送）——缺豁免时占位被「必须已合并 main」拦死，且失败曾被误报成撞号：**闸门
  放行名单和重试归因要跟着新 ref 命名空间一起改，漏一处整套机制就是摆设**。
  发号强度本身可配（DX-105）：台账 `numbering.mode`（`remote` 默认 / `local`
  单机零网络，适合没有 origin 的纯本地仓库），CLI `--mode` 单次覆盖；改配置 =
  改任务定义，同走 chore(board)+审计行 —— **强度是治理参数，不能成为免审计的后门**。
- **预防**：授权类机制要区分「凭证」与「证词」——提交信息是证词，谁都能写；
  只有带密度的产物（哈希回执）才能进闸门。给闸门新增判据时同步给 `selftest`
  补三态（有凭证放行 / 无凭证拒 / 凭证与内容不符拒），回执路径要进流程合法路径集，
  否则立卡提交会栽在自己的越面判定上。

### 1.37 单次推送下 `branch -d` 恒拒：分支删除判据还锚在 origin/main（DX-109）

- **现象**（2026-11-06 DX-106 收尾实录）：每次 `git-done.ps1` 收尾都打印
  `error: the branch 'fix/...' is not fully merged`，并留下 `（分支可能仍被某个 worktree 占用…）`
  提示；合并早就成功，分支却要手工 `git branch -D` 补删。DX-106 每批收尾都复现一次。
- **根因**：DX-106 把 worktree 回收与分支删除都挪到推送**之前**（`release-claims` 要靠
  「分支已删」判台账登记失效），worktree 回收的安全判据已经换成「提交已全部进本次合并」，
  **分支删除那半边没跟着换**：`git branch -d` 的安全判据是「已合并到 upstream」，而功能分支
  由 `agent-init` 建成时跟踪 `origin/main`；推送前 `origin/main` 还没含本次合并，判据恒假，
  git 必然拒删。DX-106 的 worktree 判据换对了，同一个时序变化下的另一处判据却漏了。
- **解法（DX-109）**：`git-done.ps1` 新增 `Remove-MergedBranch`，判据与 worktree 回收同锚点
  ——`git rev-list --count <分支> --not <本次合并提交>` 为 0 才删；确认已收纳后用 `-D`
  （`-d` 会再跑一遍同一套 upstream 检查，照样误拒）。remote 与 local 两个模式共用这一个
  实现；`-KeepBranch` / `-DryRun` 的分支保留路径不受影响。分支若在合并后被别处推进
  （计数 > 0）或被其它 worktree 占用，仍然保留并点名，不盲删。
- **预防**：改「防丢」类判据时，先列出**所有**依赖同一时序前提的删除/回收动作，逐个问
  「它的判据锚在哪」——DX-106 换了 worktree 那一处，漏掉分支这一处，症状就一直挂着。
  验收要拿产物说话：正常收尾后 `git branch --list <分支>` 必须为空，而不是只看脚本没报错。

### 1.38 同一失败类别在两处推送只修一处必复发；点源带 param 的脚本会覆写宿主同名参数（DX-78）

- **现象**（两条，2026-11-06/07 实录）：①git-done 的主线推送撞 non-ff 只在**结卡补推**那半边
  有自动重试（DX-58），主合并推送失败直接退出并打印手工恢复步骤（DX-72 会话实录过一次）——
  DX-106 单次推送后合并与收尾走同一条管道，但重试逻辑内联在收尾段，没有抽成单一实现；
  ②git-done 加 `-SelfTest` 自测开关后**永远进不去自测分支**——参数明明绑定了，`if ($SelfTest)`
  却恒假。
- **根因**：①Z5 老病：同一失败类别（non-ff 重试）的两处推送步骤，修复只落在其中一处
  （DX-58 修结卡），另一处（主推送）照旧 —— 与 §2.23/§2.24 同源：按「现场」修不按「类别」收口。
  ②PowerShell 点源共享作用域：git-done 入口 `. git-helpers.ps1`，而 git-helpers 的
  `param([switch]$SelfTest)` 在点源瞬间于**当前作用域**重绑同名变量，把宿主刚绑定的
  `-SelfTest` 静默冲成 `$false` —— 参数同名即冲突，点源一个带 param 的脚本等于声明了一次覆写。
- **解法（DX-78）**：①重试抽成单一实现 `Invoke-MainPushWithRetry`（fetch → 变基 → 重推，
  上限 3 次），全仓唯一调用点消费——再出现第二处推送重试需求时只许调用它；②入口在点源
  **之前**把 `-SelfTest` / `-StubPushFailures` 真值存进 `$script:` 作用域，块内一律读
  `$script:`；③`-SelfTest` 落成机器判据：临时裸仓 + `-StubPushFailures` 打桩跑完整 remote
  收尾链，场景 A（首拒重试落地：退出码 0、bare main = 检出 HEAD、检出归位不在 merge/*）、
  场景 B（连续拒绝：退出码非 0、补推指引、检出不在 merge/*）；④残留 merge/* 临时分支的
  清理先脱离自家检出占用再 `branch -D`（重跑永远走通，DX-74 会话实录过 `cannot delete
  branch 'used by worktree'`）；⑤推送未落地（含 fail-soft 补推仍被拒）退出码 1，不再以 0 假装成功。
- **判据**：`pwsh -File scripts/git-done.ps1 -SelfTest` 8 项断言全过（含 A/B 两场景）；
  `pwsh -File scripts/git-done.ps1 -DryRun -Branch <分支>` 演练口径不回归；
  真实收尾撞 non-ff 时输出「重取主线变基后重试」且最终单次推送落地。
- **一般化**：**修复跟着失败类别走，不跟着现场走**（§2.23 的重复教训）；副作用式的
  作用域共享（点源、环境变量、全局状态）里的同名成员就是隐式接口——接入点要么避开命名，
  要么在入口显式快照真值。自测开关「存在但进不去」比没有更糟：它给维护者绿灯的假象。

---


### 1.39 从自身 worktree 收尾永远回收不了自己：反查在摘走分支之后才跑（DX-120）

- **现象（DX-109 收尾实测）**：在 `.worktrees/agent-dx109` 里跑 `git-done.ps1 -Branch fix/dx109-branch-delete-anchor`，
  EXIT=0、分支已删、推送成功，但 `git worktree list` 里 `agent-dx109` 残留（detached HEAD），输出里既没有
  「回收 worktree」也没有「目标 worktree 就是当前目录，跳过回收」任何一句 —— 回收段被静默跳过。
- **根因**：remote 模式先 `git switch -c merge/<slug> origin/main` 把当前检出从目标分支摘走，回收段**之后**才按
  `branch refs/heads/<Branch>` 去 `worktree list --porcelain` 反查目标检出 —— 此刻已没有任何 worktree 检出该分支，
  `$wtPath` 恒空，`elseif ((-not $KeepWorktree) -and $wtPath)` 整段不进，连自删保护那句提示都轮不到。
  §7.3 的标准姿势（agent 在自己的 worktree 里跑收尾）与 AGENTS §7.2「worktree 用完必须回收」在同一条路径上
  双双落空；DX-21 的验收项「当前目录就是目标 worktree 时不尝试自删」固化了跳过，但没有覆盖「跳过后谁来收」。
- **解法（DX-120）**：反查提前到 `switch -c` 之前（此刻分支仍被检出，反查才有效）；反查到「目标就是当前检出」时
  不在推送前删 —— 三步收尾提交与回执都要靠这个检出完成，且 Windows 不允许进程删除 CWD 所在目录 —— 改为记下
  路径，待单次推送落地、结单对账与回执完成之后 `Set-Location` 让位到锚点区再 `git worktree remove`；删除失败
  不无脑 `--force`（避免 DX-93 的半删除尸体），只告警并打印可复制的命令。`-DryRun` 演练只打印判定（将回收 /
  将跳过 + 原因），不动手。
- **预防**：回收类逻辑的判定对象（按分支反查检出）与改变该对象状态的动作（switch 摘走分支）在同一脚本里有
  先后依赖时，反查必须排在动作之前；验收用产物断言（`git worktree list --porcelain`），不以脚本是否打印
  「已回收」为准。他区回收（DX-21 路径）与自身回收（DX-120 路径）要各端到端一次，不能拿一条路径的成功推断
  另一条。

### 1.39 立卡补的 roadmap 行永远入不了库：一个已跟踪脏文件把锚点快进永久卡死（DX-110）

- **现象**（2026-11-06 R19-1 立卡 / DX-110 收尾实录）：锚点区
  `docs/iteration-roadmap.md` 因一行 `| **R19** | ... |` 长期处于未提交修改态；
  `git-done.ps1` 每次收尾都打印「锚点区有未提交改动（1 项），跳过快进」，本地 `main`
  严格落后 `origin/main`（`origin/main..main` 为空，却落后一大串收尾提交），维护者回到
  根目录看到的还是旧代码（1.1）。更坑的是失败原因一律报「本地提交 / 未跟踪文件冲突」
  —— 实际既没有本地提交也没有未跟踪文件，当日两个会话先后被这句带偏。
- **根因**：三条叠加。① `register_board_cards.py` 对陌生 `stage` 强制「确认 roadmap 有
  对应行」（`--allow-new-stage`），却**没有任何受控通道落这行** —— 立卡会话
  （agent-r19，R19-1）只能把它手工写进锚点工作区，而锚点 `pre-commit` 规则 1 禁止在
  `main` 提交，该行永远无法入库；② `Sync-AnchorWorktree` 只认 `progress.json` 脏
  （`snapshotOnly`），遇任何其它已跟踪文件脏就**整体放弃快进**（1.2）；③ 跳过 / 失败
  原因分类把「已跟踪文件本地修改、本地零提交」误报成「本地提交 / 未跟踪文件冲突」。
  三条单独都不致命，叠起来就是「立一次新 stage 的卡 = 锚点从此不再同步」。
- **解法（DX-110）**：① `register_board_cards.py` 在 `stage` 首次出现时自己写 §9 里程碑行，
  并把它列入立卡流程**第一段** `docs(board)` 提交（`docs/` 是任务闸门的杂务豁免路径；
  塞进第二段 `chore(board)` 会撞 DX-51「改任务定义的提交不得夹带流程路径之外的文件」，
  见 2.21）—— 行随提交入库，锚点不再因它留脏；② `Sync-AnchorWorktree` 把受控文件泛化为
  `progress.json` 与 `docs/iteration-roadmap.md` 两份，按同款 stash / 还原 / 主线优先口径
  处理（1.3），工具产物与其它文件仍跳过不碰；③ 跳过原因按「本地提交 / 已跟踪文件脏
  （逐个点名）/ 未跟踪冲突」三类分开报。
- **预防**：「工具要求人手工补一行」和「人所在位置不允许提交这一行」撞在一起时，
  结果不是「人记得补」，而是「这行永远脏在那里」—— 凡是要落盘的产物，必须有一个
  **能提交它的程序化通道**，且通道终点落在合法提交位置。失败原因要按可观测事实分类，
  别把所有失败折叠成一句笼统文案：那是排查时最贵的一行字。锚点那份 R19 残留行由
  R19-01 实现分支收编（不在本卡 scope）。

---
## 二、脚本与编码（PowerShell / Python）

### 2.1 `.ps1` 少了 UTF-8 BOM，整份脚本报「批处理式」语法错

- **现象**：Windows PowerShell 5.1 下中文字符串像被撕开，引号配对错乱；PowerShell 7 却正常。
- **根因**：`.ps1` 在 Windows 工作区检出为 CRLF，无 BOM 的 UTF-8 被 5.1 当 GBK 读，中文最后一个字节会与紧随其后的引号配对。
- **解法 / 预防**：新增脚本带 BOM 保存（`AGENTS.md` 第四节已定为硬约定）；读无 BOM 的数据文件必须显式 `-Encoding utf8`
  （`agent-init.ps1` 读台账就是栽在这，`c471a5d` 修）。注意**编辑工具会静默剥掉 BOM**（一晚上丢过两次），
  已由 `scripts/check_script_hygiene.py` 规则 C 在 CI 拦（§五.8 第三条）——改完 `.ps1` 不放心就跑一次卫生检查。

### 2.2 临时脚本里的中文注释把下一行吞掉

- **现象**：临时探针脚本莫名少一行、或字符串跨行。
- **根因**：同 2.1 的 GBK 误读，且临时脚本往往忘了带 BOM。
- **预防**：临时 `.ps1` 一律**纯 ASCII**，用完即删（`AGENTS.md` 第四节）。

### 2.3 原生命令的 stderr 把脚本拦腰打断

- **现象**：MSBuild / ISCC / git 明明成功，脚本却在这一步抛终止性错误。
- **根因**：`$ErrorActionPreference = 'Stop'` 下 `2>&1` 会把 stderr 的进度行升级成错误（NativeCommandError）。
- **解法 / 预防**：原生命令统一走带日志的调用封装（`git-done.ps1` 的 `Invoke-Cmd` / `Invoke-GitQuiet`），**只看退出码**判成败（§7.3）。
  另：`git-done.ps1` 用 pwsh 7 跑，5.1 在 `2>&1` 处更容易炸。

### 2.4 带空格的参数被拆词

- **现象**：钩子投递/CLI 传带空格的标题文本，结果只剩第一个词。
- **根因**：`Start-Process -ArgumentList "a b c"` 按空格拆词。
- **解法 / 预防**：带空格文本改走文件通道（`--emit-hook-file`，§8.4）。

### 2.5 本机安全策略拦住 `reg.exe` / `.NET Process.Start`

- **现象**：`vcvars64.bat` 拿不到 SDK 路径，`build.ps1` 失败；或 PowerShell 起进程被拒。
- **解法**：`scripts/dev-env.ps1` 点号装配环境后再构建（§一）；手工装配时 `PATH`/`LIB`/`INCLUDE` 必须
  是 **Windows 风格路径 + `;` 分隔**，用 `/c/...` 会 `LNK1104 无法打开 kernel32.lib`。
- **预防**：GUI 子系统 exe 的 stdout 在 PowerShell 里拿不到，验证一律改看应用日志（见 4.1）。

### 2.6 Python 脚本打印中文或符号时 GBK 崩

- **现象**：`check_scope.py` 报 `UnicodeEncodeError: 'gbk' codec can't encode character '✓'`——判定其实是对的，
  只是打印炸了，看起来像检查失败；临时靠 `PYTHONIOENCODING=utf-8` 顶着。
- **根因**：Windows 上 stdout 默认跟随 ANSI 代码页，输出被重定向进管道（CI、`Invoke-Cmd`）时尤其明显。
- **解法**：脚本开头对 `sys.stdout` / `sys.stderr` 做 `reconfigure(encoding="utf-8", errors="replace")`，
  已统一进 `check_scope.py` / `check_script_hygiene.py` / `task_gate.py`（`c8419b8`）。
- **预防**：新增 Python 工具凡要打印中文或符号就自带 reconfigure，**不要把编码要求推给调用方的环境变量**。

### 2.7 `Write-Host` 单引号字符串里的变量不展开

- **现象**：日志打出字面量 `退出码 $rcRes.Code`，排障时看不到实际数字。
- **根因**：PowerShell 单引号字符串是字面量，不做变量/子表达式插值；双引号才插值。
- **解法**：需要插值的消息用 `-f` 格式化 + 单引号模板（`("· …（退出码 {0}）…" -f $rcRes.Code)`），
  既避开双引号嵌套转义，也保证变量真展开（`c8419b8` 修入 git-done 的领用清理消息）。
- **预防**：脚本里凡是「消息带变量」的 Write-Host，写完扫一眼用的是哪种引号。

### 2.8 PowerShell 里写 here-doc / 管道喂解释器，炸出乱码 ParserError

- **现象**：`python - <<'EOF' … EOF` 之类的 POSIX 惯用法在 pwsh 里报「重定向运算符后面缺少文件规范」，
  错误信息里还带乱码。
- **根因**：PowerShell 没有 here-doc；`<<` 被当重定向解析。
- **解法 / 预防**：临时逻辑**直接写成脚本文件**（用完即删，§2.2），别试图管道喂解释器；
  同理 PowerShell 内嵌 `python -c "…$变量…"` 时引号与 `$` 的转义极易拼坏，复杂一律落地成 `.py` 再调。

### 2.9 两种「BOM 该不该有」方向相反：`.ps1` 要，提交信息文件不要

- **现象**：用 PowerShell 写临时提交信息文件（`Out-File -Encoding utf8`），`commit-msg` 一直拒，
  报的是格式不符——信息内容明明是合规的 `<type>(<scope>): ...`。
- **根因**：PowerShell 5.1 的 `-Encoding utf8` 会写**带 BOM** 的文件；钩子按行首正则匹配 type 前缀，
  BOM 三个字节卡在行首，正则就不命中。与 2.1 恰好相反：`.ps1` 缺 BOM 才出事，提交信息文件**多** BOM 才出事。
- **解法 / 预防**：写提交信息、JSON、台账这类**给程序读**的文件，不用 PowerShell 的 Out-File，
  用 Python `io.open(..., newline='\n')`（LF、无 BOM），或直接在 Git Bash 里写。
  判别口诀：给人看的脚本怕被当 GBK，要 BOM；给解析器吃的数据怕前缀被污染，不要 BOM。

### 2.10 环境块里有「只差大小写」的变量，`Start-Process` 直接抛「已添加项」

- **现象**：`Start-Process` / `[Diagnostics.Process]::Start(...UseShellExecute=$false)` 报
  `ArgumentException：已添加项。字典中的关键字:“HTTP_PROXY”所添加的关键字:“http_proxy”`；
  整个阶段失败、退出码 1，而构建与 ctest 全绿 —— 看起来像验收脚本坏了。
- **判据**：同一台机器上 `Get-ChildItem Env:` 也抛同样的异常（PS 5.1），
  而 `Get-Help` 之类正常。这一条就能确认是环境块的问题，不是脚本。
- **根因**：机器上同时存在 `HTTP_PROXY` 与 `http_proxy`（还有 `HTTPS_PROXY`/`https_proxy`），
  来自不同层级的注入（用户注册表一份、工具/终端再注入一份）。.NET 建环境字典时按**大小写
  不敏感**加键，于是第二个被当成重复键。PowerShell 7 自己处理了，5.1 没有。
- **解法**：别用会重建环境块的 API。要退出码与输出 → 用调用运算符
  `$out = & $exe --args 2>&1; $code = $LASTEXITCODE`（子进程直接继承父环境块）；
  要超时与强杀 → `Process.Start` 配 `UseShellExecute=$true`（走 ShellExecuteEx，不重建环境）。
  两代 PowerShell 实测均可（DX-12，`scripts/verify.ps1` 的 `--selftest` / `--smoke` 已照此改）。
- **预防**：凡是「拉子进程」的脚本，先问一句「这个 API 会不会重建环境块」；
  报错里出现「已添加项 / 已添加了具有相同键的项」先怀疑环境变量，不是数据结构写错了。

### 2.11 无输出 + 退出码 1：先怀疑执行策略，不是脚本坏了

- **现象**：`& .\scripts\build.ps1`（或 dot-source `. .\scripts\dev-env.ps1`）什么都没打印，
  退出码 1。文档里的示例照抄也一样。
- **判据**：`Get-ExecutionPolicy` 返回 `Restricted`（Windows PowerShell 5.1 的默认；
  `Get-ExecutionPolicy -List` 全 `Undefined` 时按 Restricted 处理）就是它。
  **没有任何输出**是关键特征 —— 脚本被策略挡住时一行都不会执行。
- **解法（任选其一，都不改机器语义）**：
  ```powershell
  pwsh -File scripts/<脚本>.ps1                                            # PowerShell 7，默认 RemoteSigned
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts/<脚本>.ps1   # Windows PowerShell 5.1
  ```
  想一劳永逸：`Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`（只影响当前用户，不需管理员）。
- **预防**：仓库里的示例统一写 `pwsh -File ...`；`scripts/setup-git-hooks.ps1`
  检测到 Restricted/AllSigned 会打印上面这段指引（DX-12）。别为了「能跑」去把脚本改成
  `.bat` 或让人关掉 UAC —— 那是拿系统安全换个人省事。

### 2.12 装了 sccache 也不生效：winget 改的 PATH，当前进程读不到

- **现象**：`winget install Mozilla.sccache` 报「已成功安装」，`CMakeLists.txt` 里也有
  `find_program(sccache)` 自动挂 launcher 的逻辑，可每次配置的输出里都**没有**
  `NeonBoard: 启用 sccache 编译缓存` 这行。缓存从未生效，每个新 worktree 都老老实实付一次
  冷编译。
- **判据（三条，任一条成立就说明没挂上）**：
  1. `Get-Command sccache` 解析不到 —— winget 改的是**用户级** PATH，已经跑起来的终端 / IDE /
     宿主进程不会重新读（重启宿主才恢复）；
  2. `$env:LOCALAPPDATA\Microsoft\WinGet\Links\sccache.exe` 存在但**是 0 字节** —— 那是
     App Execution Alias，`Test-Path` 为真、`& sccache --version` 却报「不是有效的应用程序」；
  3. `cmake -S . -B <dir>` 的输出里 grep 不到 `sccache` —— 配置阶段就没挂 launcher。
- **解法**：让脚本自己找，别指望 PATH。`scripts/build.ps1` 的 `Resolve-Sccache`（DX-16）依次
  探 PATH → `WinGet\Packages` → `~\.cargo\bin` → chocolatey，**按大小过滤 0 字节别名 + 实跑
  `--version` 验证**，命中就把所在目录补进 `$env:PATH`。
  **补 PATH 必须放在 CMake 配置之前** —— 配置跑完 `CMAKE_C/CXX_COMPILER_LAUNCHER` 已经定死，
  之后再补只会得到「日志说有缓存、实际没走缓存」的假象。
- **冷启动成本清单**（2026-11-04 实测：8 核 / MSVC 19.44 / Ninja / 519 个目标）：
  - 新 worktree **首次全量构建 801.8 秒 ≈ 13.4 分钟**（其中配置 16.1 秒，编译 785.7 秒）；
    这是「缓存为空」的数字，sccache 首次必然全 miss（398 次编译请求 / 0 命中）；
  - **换一个 build 目录重编（= 新 worktree 冷启动）229.8 秒 ≈ 3.8 分钟，命中率 90.7%**
    （398 次请求里 361 次命中）—— 相比首次的 13.4 分钟快 **3.5 倍**。这才是 sccache 的主要价值：
    源码没变，换个目录不必重新付出全部编译成本；
  - **改一个 core 源文件后重编 + 链接只要 2.9 秒**，同一份源码被多个目标编译时会重复命中
    （实测 4 次 hit）—— 缓存的价值不在首次，在第二次以后；
  - 无改动时 `ninja: no work to do`，秒退；
  - 8 核会被并行 worktree 分摊：同时编两个就都变慢，**串行开区不比并行差多少**；
  - **零 C++ 改动不必全量**：脚本 / 文档类改动跑 `pwsh -File scripts/verify.ps1 -ScriptOnly`
    （约 2 秒，DX-15），代码类才需要 `verify.ps1` 全量；**但改台账要看改了什么**
    —— 改既有卡的契约、删卡、或新卡带陌生 stage 仍是全量的活，只动 `baseline` / `claims`
    和单纯立卡才留在轻档（DX-25 的分流规则，`-ScriptOnly` 会自动判并拦）；
  - 构建被中断后重跑**只补剩余目标**（Ninja 有 `.ninja_log`），不需要 `-Clean`；
  - `build.ps1` 结尾现在会打「配置 / 编译 / 合计」三段耗时与 sccache 命中率 ——
    怀疑变慢时先看这三个数字，别靠体感。
- **`-NoCache` 以前根本没关掉 sccache（DX-52 一并修掉）**：旧实现里 `-NoCache` 只是
  「不把 sccache 补进 PATH」，而 `CMakeLists.txt` 里的 `find_program(sccache)` **照样找得到它**
  （winget 装的 sccache 在用户级 PATH 里，新开的终端就看得见）—— 于是「关缓存」的构建其实
  一直在走缓存，**拿它做 A/B 对比测出来的数是假的**。
  现在 launcher 由构建脚本**显式指定**（`-DNEONBOARD_SCCACHE_LAUNCHER`），`-NoCache` 真的关得掉；
  顺带保证「探针实测前缀用的 launcher」与「真正编译用的 launcher」是同一个
  —— 两边不是同一条链路，测到的前缀就对不上，依赖条目又空了（实测：`-NoCache` 下 12/12 个 obj
  的 `#deps` 全是 0）。
- **一个已知副作用**：挂了 sccache 之后，MSVC 的 `/showIncludes` 输出会被透传到控制台
  （中文机器上是成片的 `注意: 包含文件: ...`，一次全量构建能刷出 **10 MB 以上**日志）。
  要干净的日志就重定向到文件（`> build.log 2>&1`）再 tail，别在 CI 里直接刷屏。
  > 旧版本在这里写着「它**不影响**依赖解析 —— 实测构建完 `ninja -n` 仍报 `no work to do`」——
  > **这句是错的**，害人不浅：`no work to do` 只说明「.cpp 没变」，恰恰是依赖条目全空时的表现。
  > 真正的判据是 `ninja -t deps` 里 obj 的 `#deps` 是不是 0（DX-52，见 §3.16）。
- **缓存不再跨 worktree 共享（DX-52 起，这是有意的）**：sccache 的缓存按**内容**寻址，
  同样的源码在哪个目录编译都命中同一条目；而条目里存着**当初那个目录**的 `/showIncludes` 输出，
  里面的头文件路径属于别的 worktree。Ninja 把那些路径当依赖 → 改本地头文件不重编（陈旧 obj），
  那个 worktree 被回收后构建还会直接报错。DX-52 让构建脚本给每个**源码树**生成一份
  「编译身份头」并强制包含它 —— 预处理结果随源码树而不同，缓存因此按源码树分桶，
  回放的路径也就永远属于当前这棵树。
  **代价**：换个 **build 目录**（同一棵树）仍然共享缓存（3.8 分钟那个数字不变），
  但**换一棵源码树**（新 worktree）就是冷启动 —— 与 AGENTS.md 里「新 worktree 首次全量构建
  约 20~30 分钟（零缓存）」的既有预期一致。**用「同一棵树换 build 目录」测缓存命中率才有意义**，
  拿新 worktree 测会得到全 miss，那是设计如此，不是缓存坏了。

### 2.13 验脚本改动时，在锚点区跑的是 main 上的旧版本

- **现象**：改完 `scripts/git-done.ps1` 后，为了「顺便验证锚点区归位」而在锚点区执行它 ——
  日志里没有一行新逻辑的输出，收尾照旧把锚点留在 detached HEAD、工作区照旧没回收。
  改动明明已经提交，`git log` 里也看得到。
- **根因**：锚点区的**工作区文件**属于 `main`，不是待合并分支。在锚点区执行 `scripts/*.ps1`
  跑的是已合入的那一版；改动还躺在特性分支的 worktree 里。而 `git-done` 恰恰可以在两处执行，
  只有「改动所在的工作区」才有新代码。
- **判据**：跑之前 `git -C <执行目录> status --short` 看不到本次改动、`git log -1 --stat`
  里也没有它 —— 说明要验的那一份不在当前检出里。真验证要在**改动所在的工作区**执行。
- **本次代价**（2026-11-04，DX-20/21/22）：三张卡已标 done、合并已推送，但新逻辑一行没跑；
  工作区、分支、占用登记三处残留要手工清，等于把 DX-21 想消灭的僵尸登记亲手造了一遍。
- **附带结论**：`git-done` 的「锚点归位」路径（DX-20 的 `$atAnchor` 分支）只能在锚点区跑，
  天然要等新代码进了 `main` 才有机会验证 —— 验收顺序应是「先在 worktree 验能验的，
  合入后再补验锚点路径」，别指望一次收尾验完全部。

### 2.14 冷启动构建偶发 moc / qmlcachegen 启动失败（0xC0000142、「拒绝访问」）

- **现象**：新 worktree 首次全量构建，autogen 阶段报 `AutoMoc subprocess error` +
  `Process failed with return value 3221225794`（= `0xC0000142` = `STATUS_DLL_INIT_FAILED`），
  或 qmlcachegen 报 `Error compiling qml file: 拒绝访问。`。重跑同一条命令即通过。
- **不是代码问题**：失败点在 `moc.exe` / `qmlcachegen.exe` 的**进程启动**，还没碰到源码。
  8 核机器并行几百个目标、几十个 moc 同时拉起时，桌面堆 / 句柄瞬时不够。
- **处理**：直接重跑 `build.ps1` / `verify.ps1`，Ninja 有 `.ninja_log` 会从断点续；
  **不要加 `-Clean`** —— 那会把已完成的几百个目标全废掉。
- **判据**：`0xC0000142`（DLL 初始化失败）与 `0xC0000135`（找不到 DLL）都是**加载期**错误，
  与编译错误（`C` 开头的四位诊断码）不是一类；先看是不是这两串，再决定怀不怀疑代码。
  `0xC0000135` 在 worktree 里是常态缺 Qt DLL，DX-24 之后 `verify.ps1` 会自己从
  `CMakeCache.txt` 反推 QtRoot 补 PATH；`0xC0000142` 则是偶发，重跑即可。

### 2.15 收尾的自动提交全失败：归位提前到提交之前，撞上 main 保护

- **现象**：`git-done` 收尾的日志一路绿（合并成功、已推送），随后「台账领用登记清理 /
  结卡 / 快照」三步各打一行温和的「提交失败，留待下次合并」，`exit 0`。但 `git status`
  里躺着 staged 的台账改动，下次提交会被顺带夹带（P0 收尾时就这样把代码文件带进了
  `chore(board)` 提交）。
- **根因有两层**：
  1. **时序**：`DX-20` 为了让锚点区不留在 detached HEAD，把「切回 `main` + `merge --ff-only`」
     提前到了三步提交之前。而 `.githooks/pre-commit` 规则 1 禁止在 `main` 直接提交 ——
     它只在 `symbolic-ref` 读得到分支名时生效，detached HEAD 读不到，所以**原来**在分离
     HEAD 上提交是合法的，改完归位后就全被自家钩子拒了。
  2. **回滚不彻底**：失败处理写的是 `git checkout -- <file>`，它只从 index 还原工作区、
     **不动 index**，add 过的结果留在暂存区。
- **判据**：`git status --porcelain` 显示 `M  docs/board-ledger.json`（`M` 在**第一列** =
  改动已暂存）而不是 ` M`（第二列 = 仅工作区）。收尾后看到第一列有东西，就是回滚没撤暂存区。
- **修复（DX-26）**：三步提交前**无条件** `switch --detach`，归位挪到所有 commit 之后；
  失败回滚统一用 `git restore --staged --worktree -- <file>`；commit 失败时把 git 的原始
  输出打出来（此前只有一句「提交失败」，根因被吞掉，同一缺陷一天误判两次）。
- **一般化教训**：钩子规则往往**带位置前提**（在哪个分支、是不是中间态）。重排脚本步骤时
  要问一句「这一步此时跑在哪个分支上」—— 把「提交」和「切分支」调换顺序，语义就变了。
  另外，「失败要回滚」不能只还一半：index 与工作区是两个地方。

### 2.16 `--smoke` 在仓库里跑一次，就把 `progress.json` 写脏一次

- **现象**：手工在仓库目录跑 `build\Release\NeonBoard.exe --smoke`（排查启动问题、复现
  用户场景），跑完 `git status` 多出 `M progress.json`。派生产物混进实现提交是常事 ——
  提交前没看清就被顺手 `git add .` 带走了。
- **判据**（可靠、可复现）：往 `progress.json` 的某个 item 标题里注入一个 `PROBE` 标记，
  跑 `--smoke`，然后看标记还在不在 —— 消失 = 快照被应用导出覆写；还在 = 没有写。
  只比对哈希不够：内容恰好相同（导出结果与文件一致）时哈希也不变，会让人误判成「没写」。
- **根因**：`RoadmapSync::resolvePath()` 从 `applicationDirPath()` 向上找「同时含 `.git`
  与 `progress.json`」的目录，在仓库里跑必然命中；`autoExportEnabled()` 原本只看
  `NEONBOARD_ROADMAP_AUTOWRITE` 与 `QSettings`，默认开着。`--smoke` 是一次性启动探测，
  没有用户在等结果，写下的任何东西都是垃圾。
- **修复（DX-28）**：`autoExportEnabled()` 在命令行含 `--smoke` 时恒返回 false
  （判据放在这里而不是 `main.cpp`：开关只在这一个函数里判定，且 `main.cpp` 行数已顶格）。
  `verify.ps1` 的冒烟步骤另加一道「跑前跑后 `progress.json` 哈希一致」的守卫 —— 这条路径
  没有单测覆盖（要真启动 GUI 才跑到），别处再长出写路径只有它能抓到。
- **一般化**：**诊断模式不该有持久副作用**。凡是 `--selftest` / `--smoke` / `--export-*`
  这类「为了一次性观测而启动」的模式，都要问一句「它会不会写用户的真实数据」。
  与 `--selftest` 用独立快照文件（`neonboard-selftest-snapshot.json`）是同一个道理。

### 2.17 派生器把提交正文里的 `#IFDEF` / `#RRGGBB` 当成写错的卡号

- **现象**：每次 `git-done` 收尾都刷 8 行「提交信息里的 #ERROR / #IFDEF / #IFNDEF /
  #INCLUDE / #R / #R7-0X / #RGB / #RRGGBB 不在看板台账中」。全是同一个提交
  （`182bf2b` 主题包格式）正文里的技术散文：C++ 预处理宏、`#RRGGBB` 颜色写法、
  `#R7-0X` 示例占位符。不是有人写错卡号，是解析器把散文当指令读了。
- **根因**：`MARKER` 正则匹配任意字母开头的 `#词`，而原实现把 `subject + "\n" + body`
  拼成一段一次扫完 —— 正文里每个 `#xxx` 都进了 `warnings`。
- **别这么修**：**「只扫 subject」会改掉派生语义**。全历史里有 15 个提交把状态标记写在
  body，其中 7 张卡（DX-20/21/22/23/24/25、R7-04）的 derived 状态只有那一处；
  只认 subject 时这 7 张直接丢状态。改之前先把两种口径的派生结果 diff 一遍。
- **修复（DX-31）**：标记分类抽成纯函数 `scan_markers()`，按信号性质分层 ——
  状态标记 subject/body 都认；`unknown`（形如路线图短号，写错必须拦）两处都收；
  只有纯提示性质的 `warnings` 收紧为「只来自 subject 且形似短号」（字母段 + 数字）。
  `--selftest` 用构造的 subject/body 把这三类口径各断言一遍，不依赖 git 历史。
- **一般化**：**告警和拦截不能用同一把尺**。拦截类判据要宁可多报（漏拦代价高），
  提示类判据要宁可少报（噪声会让人学会无视整个输出）。把两者混在一个集合里，
  结果一定是「真错误被淹没在假提示里」。

---

### 2.18 领了卡却没写 `#<短号>=done`，收尾一声不响就放行

- **现象**：一条分支领了卡、代码合了、`git-done` 全绿退出，卡还挂在 `todo`。
  屏幕上看不出任何异常 —— 因为 `--close-cards` 只处理「已经写了完成标记」的卡，
  没写的它不认识，也就不输出；而收尾只在「结掉 N 张」时才打印一行，结 0 张什么都不说。
- **代价**：`baseline=<短号> done` 是「拒绝重新领用」的唯一门（条款 6）。卡停在 `todo`，
  下一个人（或下一个会话）会把它当没开工的活再领一遍重做，而 git 里其实已经有实现。
  `1.14` 记的就是这个。
- **根因**：「结卡」被实现成「读标记」，而不是「对账」。标记是可选的书写习惯，
  对账是必须闭合的流程 —— 用前者代替后者，漏写就等于没结，且没有任何声音。
- **修复（DX-33）**：`git-done` 收尾比对「本次分支领用的卡」（合并前从
  `chore(claim): 领取 #<短号>` 抓）与「实际结掉的卡」，差集非空就逐张打印卡号、
  标题与 `acceptance` 全文，并给出 `-Close` 用法。`-Close` 只受理本分支领过的卡
  （否则退 8）—— 凭空结卡会让已交付的卡被重新领、没做完的卡显示完成。
- **为什么不在合并前抓就晚了**：合并一推送，分支的所有提交都成了 `origin/main` 的祖先，
  `origin/main..<分支>` 变成空集，「本次领的」和「历史上领过的」再也分不开。
- **一般化**：**「没做」和「做错了」是两种失败，只有后者会留下痕迹**。闸门通常盯着后者
  （写错的卡号、越界的路径），而前者表现为「什么都没发生」—— 不报错、不变红、不留日志。
  凡是「可选输入决定流程是否执行」的地方，都要配一条不依赖该输入的独立对账。

### 2.19 `$repoRoot` 在 linked worktree 里指向那个 worktree，物证跟着回收一起消失

- **现象**：`git-done` 的结构化回执（DX-33 产出，用来回答「那次收尾到底结了什么、
  漏了什么、清掉了什么」）一直写在 `$repoRoot\.sandbox\receipts\` 下。`.gitignore`
  认了 `.sandbox/`，屏幕上还会打一行完整路径，看起来完全正常 —— 但文件实际落在
  `.worktrees\<agent>\.sandbox\receipts\`，而 §7.2 规定 worktree 用完即回收，
  `git worktree remove` 连带把整个目录删掉。回执活了不到一小时。
- **根因**：`$repoRoot` 由 `PSScriptRoot` 的上级推出来，语义是「**这份脚本所属的检出**」。
  在锚点区跑 = 锚点区；在 linked worktree 跑 = 那个 worktree。它从来不是「仓库根」，
  只是锚点区恰好在仓库根，所以绝大多数时候两者看不出区别。
- **判据**：凡是「要活过 worktree 回收」的产物（回执、审计记录、取证快照），落点必须
  显式取锚点区 —— `Get-AnchorWorktree` 解析 `git worktree list --porcelain` 的**第一条**
  `worktree` 行（主检出永远排第一），定位不到就返回 `$null`。
- **一般化**：**脚本里所有「根目录」类的变量都要问一句它在哪个检出里取值**。同一个名字
  （`$repoRoot`、`$ProjectRoot`）在锚点与 worktree 两种检出下指向不同目录，而这个差别
  只在「产物需要跨检出存活」时才暴露 —— 而这恰好是物证类文件的共同要求。
  顺带一条同构的坑：这一段在脚本主体里（所有 `function` 都在主流程之前结束），
  主体里的 `return` 会直接结束整个脚本，把后面的成功横幅与 `exit 0` 一起吞掉；
  「跳过这一步继续往下」要用 `throw` 交给该步自己的 `catch`。

---

### 2.20 PowerShell 管道捕获 `git show` 中文输出按 GBK 解码，JSON 结构性断裂

- **现象**：`$raw = & git show origin/main:docs/board-ledger.json` 后
  `($raw -join "`n") | ConvertFrom-Json` 报「After parsing a value an unexpected
  character was encountered: s. Path 'cards.DX-01.note'」——318KB 的台账在第一条中文
  note 附近就断了，同一份文件 python `json.load` 一直正常。
- **根因**：PowerShell 管道捕获 native 命令输出按 `[Console]::OutputEncoding`
  （中文 Windows = GBK/cp936）解码，而 git 输出是 UTF-8。CJK 的 UTF-8 尾字节
  （0x80-0xBF）大多落在 GBK trail 字节合法范围（0x40-0xFE，**含 0x5C 反斜杠**）：
  某个多字节序列把紧随其后的 `\` 吞进自己的字形，`\"` 退化成裸 `"`，JSON 字符串
  提前闭合，下一个内容字符成了「意外的 s」。引号本身（0x22）不会被吞，所以报错点
  漂在第一个「中文后恰好跟转义符」的位置——看似随机损坏，实则确定性复现。
- **解法**：拿原始字节，不过文本管道。两条实测可行：① `Start-Process -FilePath git
  -ArgumentList ... -RedirectStandardOutput <文件> -Wait` 落盘后
  `[IO.File]::ReadAllText(<文件>, [Text.Encoding]::UTF8)`；② 换 python 读
  （`subprocess` 显式 `encoding="utf-8"`，nbflow 的 `run()` 即此做法）。
- **判据**：PS 脚本里凡「native 命令输出 → JSON/结构化解析」的组合必须走字节落盘或
  显式 utf-8；python 侧 `subprocess` 的 `text=True` 不带 `encoding` 同样中招（cp936）。
  nbflow `run()` 已显式 utf-8；board-apply 同类问题由 DX-46 修复。
- **一般化**：这不是 git 的坑，是「中文控制台 × UTF-8 世界」的坑——凡 PowerShell
  捕获含非 ASCII 的 native 输出都适用（日志断言、diff 解析、JSON/YAML 解析）。
  同构坑：`StringBuilder.AppendLine()` 写 `
`，会让 `(?m)^...$` 行锚匹配落空，
  自测断言与钩子正则双双失灵——手工拼接一律用反引号 n。

### 2.21 流程豁免的洗白有第二层：杂务白名单文件被领用/立卡提交卷走，两道闸门照样不可见

- **现象**：脏树里留着实现改动（哪怕是 `scripts/*.py`、`docs/*.md` 这类白名单路径），
  裸跑 `git commit -m "chore(claim): 领取 #<短号> 实施任务"`，卷入的文件随领用提交
  合进 main —— commit-msg 与 pre-push 双闸门全程绿灯。R16-05 的 `032efd8` 实测
  卷走 7 个文件（pitfalls 记于 DX-40 的 agent-init 脏树守卫）。
- **根因**：DX-40 只把豁免收口到「非代码路径」，而杂务豁免白名单（`docs/`、`scripts/`、
  `.githooks/`、`.github/`、`*.md`）比「流程自身产物」宽得多。流程提交（claim / roadmap /
  board）的合法产物只有台账与路线图快照两个文件，卷入同在白名单里的其它文件时，
  「没有代码路径」的判据照样放行 —— 实现改动换了个入口继续失去短号凭证。
- **修复（DX-51）**：`task_gate.py` 新增 `PROCESS_PATHS = (docs/board-ledger.json,
  progress.json)`。① `process_commit_exempt` 要求流程提交的改动**全部**落在
  `PROCESS_PATHS` 内，越面即失效；② 失效后不落「非 HARD 类型 + 无代码路径 → 放行」
  的杂务兜底（`flow_scope_beyond_paths`），必须带短号并过 claim 校验；③ 代登记审计的
  「混提交」判定从「代码路径」收紧到「流程路径之外」，立卡提交夹带 `docs/pitfalls.md`
  也算混提交。commit-msg 与 pre-push 双侧同口径，自测 26 例（含 7 例 DX-51）。
- **判据**：`python scripts/task_gate.py selftest`；端到端负测 —— 暂存一个白名单脚本
  后以 `chore(claim)` 提交，commit-msg 必须拒绝并要求任务号。agent-init 侧的
  领用前脏树拒（exit 3）是第一道，本条是它被绕过（手搓提交）后的第二道。
- **一般化**：豁免面要按「这个通道的合法产物」划定，不能按「看起来无害的路径」划定 ——
  白名单是给杂务提交的，不是给流程提交的；两个通道共用一份宽白名单时，宽的那份
  会替窄的那个背书。凡「A 通道的豁免判据」里出现了「B 通道的合法面」，就是洗白孔。

### 2.22 快照漂移的归因收错对象：分支上带 `#卡=done` 时 `-ScriptOnly` 必然误报滞后

- **现象**（DX-64 前每个脚本卡收尾都撞）：分支上带着 `#DX-xx=done` 标记，`verify.ps1
  -ScriptOnly` 的台账分档一步必红——派生器按 git 事实算出 done，快照还停在 doing，
  报「快照状态落后于派生结果」，逼人每卡收尾前手工跑一次 `derive_board_status.py
  --write`（一个会话连撞三次）；`progress.json` 的 rebase/合并冲突还要跳
  「取主线为底加重算」的手工舞步。
- **根因**：两个设计各自都对——快照的唯一写者是 `git-done` 收尾（§11.2 第 1 条，
  分支上就不该有新快照提交）；派生 `--check` 要求快照与 git 事实一致。相撞点在
  **归因**：`check_board_diff.py` 只看到「不一致」，不问「这不一致会不会自愈」。
  分支自身的 `#卡=done` 标记保证合入收尾时派生必然把快照收敛过去——这类漂移是
  中间态的必然，不是缺陷；归因不了的漂移（与分支无关的真漂移、写错的短号）才需要红灯。
- **修复（DX-64）**：① `check_board_diff.py` 在 derive `--check` 失败后做归因：
  漂移卡号集合（复用派生器自己的 `derive_status` / `claims_doing` / `status_of`，
  不重复实现）⊆ 本分支 `base..HEAD` 提交里的 `#短号=done`（复用
  `collect_delivery_signals`）时降为提示退出 0，其余维持非零；`base..HEAD` 在 main
  上是空区间，CI 口径不变。② `merge-resolve.ps1 -Merge` 新增 `progress.json` 冲突
  自动分支：`git checkout origin/main -- progress.json` 取主线为底，合并提交后
  `-Recover` 的派生重算收敛（提交体带自动来源行留痕）——「取主线为底加重算」
  的手工舞步从此是脚本的一步。
- **补口（DX-81）**：DX-64 只认 `#卡=done`，领用登记派生的 doing 漂移仍会红——
  领卡后还没写代码先跑 `-ScriptOnly`、或并行合并把 claims 带进来时必现，照样逼人手工
  `derive --write`。归因证据扩为两路：本分支 `base..HEAD` 的 done 标记 ∪ 本分支相对
  base **新增的 claims**（且派生状态确为 doing，复用 `claims_doing` / `status_of`）。
  两路都解释不了的漂移照旧红灯；CI 在 main 上 `base..HEAD` 为空，口径不变。
- **判据**：`python scripts/check_board_diff.py --selftest`（归因解析与判定谓词，
  含 claims 命中 / 无 claims 反例 / done+claims 混合归因）；
  端到端正例——分支上带 `#短号=done` 或新增 claim 且快照滞后时 `-ScriptOnly` 绿；
  反例——把台账里一张与分支无关的卡 baseline 改错再跑，必须仍红。
- **一般化**：**判据要收紧的是归因，不是松紧**。看到「检查失败」先问「这次失败由谁
  解释、会不会自愈」：能被本分支自身动作解释的中间态失败降为提示，解释不了的才
  是闸门对象。直接放宽阈值（跳过检查）会把真漂移一起放走，直接维持红灯则把
  正常流程逼成手工补丁——两个看似矛盾的诉求，分开归因后同时成立。

### 2.23 git 取值的空值崩溃三连：同一写法在三个脚本里各埋一颗雷

- **现象**（三次同型）：DX-49 漂移守卫两处、DX-50 演练自抓两处、DX-59 agent-init
  空引用一处 —— 全是「`& git <取值>` 无输出时对空引用调 `.Trim()`」或「`[string](& git …)`
  把错误吞成空串继续往下跑」。每次修都只修现场那一行，下一个脚本再写一次再炸一次。
- **根因**：PowerShell 里 `& git` 无输出时是 `$null`，对 `$null` 调方法当场崩；先
  `[string]` 强转则把「git 失败」静默折成空串 —— 两种写法一个崩得太响、一个错得太静，
  共同点是**空值语义每个调用点各写一遍**。`Get-NbCommonDir` 甚至在 agent-init 与
  git-done 里整函数逐字重复（Z5 破戒），修 DX-59 时要改两处就是现报应。
- **修复（DX-63）**：① 取值收口单一实现 `scripts/git-helpers.ps1`
  （`Get-NbGitLine` 核心 + `Get-NbSymRef` / `Get-NbBranch` / `Get-NbToplevel` /
  `Get-NbCommonDir` 四个语义封装，空输出返回空串、多行取首行、非仓库返回空/null，
  行为与被删的两份本地实现逐字一致）；② agent-init / git-done / verify 的漂移守卫
  与符号引用取值全部换用封装，两份重复的 `Get-NbCommonDir` 删除；③
  `check_script_hygiene.py` 规则 E：裸 `[string](& git …)` 模式只允许出现在
  git-helpers.ps1，反向探针自测（`--selftest`）钉死；④ 封装自带 `-SelfTest`
  四态自测（空输出 / 多行 / 分离 HEAD / 非仓库目录），并接进 `verify.ps1 -ScriptOnly`。
- **判据**：`pwsh -File scripts/git-helpers.ps1 -SelfTest` 退出码 0；
  `python scripts/check_script_hygiene.py --selftest` 通过；
  `pwsh -File scripts/git-done.ps1 -DryRun -Branch <功能分支>` 全绿（dry-run 断言
  走的就是新封装）。
- **一般化**：**同一类 bug 出现第三次，收口的不该只是现场那行，而是「这类写法本身」**。
  修一行是止血，把空值语义收成单一封装 + 用卫生闸门禁掉裸写法才是止血带 —— 否则
  第四颗雷照埋。判据要落在「模式被禁」而不是「这几行改好了」，后者防不住下一个文件。

### 2.24 done 卡的服务端 claim 占位永久滞留：两道释放判据同时打不中的第三形态

- **现象**（2026-11-06 实测）：origin 存活 17 条 `refs/claims/*`，其中 13 条（DX-38/41/44/46/50/51/58/59/62/63/67/71/84）
  指向的卡 `baseline` 早已 `done`。git-done 的 `Invoke-ClaimRefCleanup` 明明存在，
  每次合并也确实在跑，但这批占位一次都没被它释放。
- **根因**：两条既有释放判据对「rebase 后合并」的占位同时失效——
  ① 判据 1（占位提交是 origin/main 祖先）要求占位指向的领用提交原样进了主线；但并行推主线竞态下
  分支合并前要 `git rebase origin/main`（§7.3），`chore(claim)` 提交被改写，占位仍指向**重写前**的
  旧提交（Ruleset 禁强推，占位无法跟随重写），于是永远不是 main 祖先；
  ② 判据 2（台账 claims 有登记且分支已删）要求台账 `claims` 里还有该卡的条目；但 release-claims
  在收尾更早的一步已把条目清掉，等判据 2 评估时 `ContainsKey` 恒假。两个「保守保留」分支
  叠出一个谁都够不着的死角，占位永久滞留。
- **危害**：占位指着的旧领用提交不可达，闸门校验拿它比对必然失配；占位号本身也永久堵死
  「释放后重新领用」的语义。更要命的是**会持续新增**——每张走 rebase 合并的卡都会留下一条。
- **修复（DX-76）**：巡检判据单一实现进 `scripts/check_claim_refs.py`（`classify_claim` 纯函数）：
  卡已 done 且（占位是 main 祖先 ‖ 占位不被任何本地/远端分支引用）→ 点名「应释放」；
  台账无卡 / 对象不可达 / 离线一律保守跳过并告警，绝不误删活跃占位（R18 这类在办卡的占位
  仍被在办分支引用，判据天然保留）。CI 侧 `board.yml` 新增巡检步骤（只点名拦截，无写权限不代删）；
  存量按巡检输出逐条 `git push origin --delete` 补清。收尾侧 DX-90 把同一判定接入
  `Invoke-ClaimRefCleanup`（`check_claim_refs.py --release-zombies`，不在 PS 复制判据），
  rebase 重写占位在收尾时自动释放，不再靠巡检追着手工补。DX-95 补齐 remote 模式的第二次
  清理调用：747 行那次跑在 close-cards 之前，本次合并自身的卡 baseline 还是 todo，
  判据 B 必打不中（DX-90 交付当日实测 DX-76/90/91 三条滞留）——台账三步完成后必须再清一次。
- **判据**：`python scripts/check_claim_refs.py --selftest`（含僵尸判定正反例）通过；
  `python scripts/check_claim_refs.py --audit-zombies` 对真实 origin 实跑，点名数与预期存量一致、
  补清后转绿（退出码 1 → 0）；`board.yml` 巡检步骤随 push 生效。
- **一般化**：**「保守保留」分支叠加「前置清理」，会让清理对象掉进没人负责的缝隙**。两条判据各自
  单看都无害（宁滥勿缺），但一条失效对象、一条被前手清空，联合起来就是永久死角。给「该被清掉
  的东西」配巡检时，判据必须按**最终实态**（对象还在不在、还被不被引用）设计，不能只按
  「某次动作有没有跑到」设计——动作会漏跑、会打不中，实态不会撒谎。

### 2.25 虚假结卡（DX-79 实录）：`#短号=done` 标记谁都能写，零实现也能关掉防重领的门

- **现象**（2026-11-06 会话发现）：DX-79 baseline=done，但其 acceptance 全部四项
  （孤儿释放第三判据进 release-claims.py、离线保守跳过、实测释放、selftest 进 CI）
  一项都没交付 —— `git log origin/main --grep '#DX-79'` 只命中立卡（`chore(board): 立卡 DX-79 DX-80`）
  与结卡（`chore(board): 结卡`）两类流程提交，非流程实现提交为**零**。auto-close 后防重复领用的
  门（baseline=done）反而把这个没做过的卡永久关上了。
- **根因**：结卡自动化（DX-07）只认提交信息里的 `#短号=done` 标记，而标记由提交作者自己写
  ——写标记与验卡是同一人，缺一道「活儿真干了」的机器判据（Z6）。结卡提交自身的 body 还会
  写入 `#短号=done`（`close_commit_marker`），一旦错误结卡，标记自我 perpetuate，
  事后没有任何信号能发现。
- **修复（DX-91）**：在唯一写者处 fail-closed —— `derive_board_status.py --close-cards`
  结卡前校验**交付证据**：候选结卡的短号必须被至少一条非流程提交（`ADMIN_SUBJECT` 排除
  chore(claim)/chore(board)）在 subject 或 body 里引用过；缺失即逐卡点名并整轮拒绝落盘
  （退出码 2，git-done 按派生器失败回滚，留待补引用后重跑收尾）。body 也算证据是刻意的：
  状态标记本来就认 body（DX-31，7 张卡的派生状态只有 body 一处），只认 subject 会把合法
  交付误拦；闸门拦的是「零提交引用」的机械假结，不是散文交叉引用。
  `--close-explicit`（DX-33）不经此门 —— 人工确认通道本来就是给无标记可读的卡留的。
- **判据**：`python scripts/derive_board_status.py --selftest`（含 DX-79 形态正反例）通过；
  实跑验证：DX-79 存量形态（全流程引用）被拒、正常实现提交形态放行。
- **存量豁免**：已 done 卡不追溯重开（幂等口径不变，`--check` 不新增红灯）；DX-79 本身
  作为虚假结卡实态保留在台账（其验收项由 DX-76/DX-90 补交付，见 2.24）。
- **一般化**：**自动化的闸门只认「谁写的」，不认「写的对不对」；凡是「作者即验者」的信号，
  都要配一道独立于作者的证据判据**。结卡闭环缺的不是流程，是证据——补证据的位置选在
  唯一写者（fail-closed 阻断落盘），而不是 CI 事后报红（红完 baseline 已经错了，防重领的
  门已经关上了）。

### 2.26 规则 E 只禁强转捕获：同族裸取值还剩两种写法闸门看不见（DX-74）

- **现象**：DX-63 落规则 E 后全仓 dry-run 实测（DX-74 触发证据），同族裸 git 取值在
  六个脚本里还有 17 处绕过单一封装：`(& git … | Select-Object -First 1)` 手工首行
  （agent-workspace / dev-run / preview / release / agent-init）、
  `(Invoke-Git …).Output | Select-Object -First 1` wrapper 值捕获（merge-resolve /
  git-done）、以及 `(& git …).Trim()` 直捕获（release）。规则 E 现口径只认
  `[string](& git …)` 强转，这三类一个都抓不到 —— 封装收口了，雷还在埋。
- **根因**：闸门跟着「上一次事故的现场写法」走，而不是跟着「事故类别」走。
  空值崩溃的共同起点是「git 单值取值没走空值安全封装」，强转只是这个类别里
  恰好先炸出来的那一种；按写法枚举禁令，永远比写法进化慢一步。
- **修复（DX-74）**：① 规则 E 扩到两类新模式——E2 手工首行（`& git` 与
  `| Select-Object -First 1` 同行，`[^|]*` 卡第一根管道避免把集合过滤误伤）、
  E3 wrapper 值捕获（`.Output | Select-Object -First 1`，全仓校准 `.Output` 只在
  git wrapper 上出现）；② 六个脚本（agent-workspace / check-worktree-activity /
  dev-run / preview / release / merge-resolve）的 17 处同族取值全部迁入
  git-helpers 封装，动作类 `Invoke-Git` 调用（看退出码）保留；③ 扩闸前先全仓
  dry-run 校准：迁移后剩余命中恰好 8 处，全部落在两张在办卡的 scope 文件里
  （agent-init 6 处归 DX-94、git-done 2 处归 DX-88），进 `GIT_CAPTURE_PENDING_ALLOW`
  豁免面（棘轮，随卡清空）；E1 强转捕获不进豁免，保持最紧口径。
- **判据**：`python scripts/check_script_hygiene.py --selftest` 覆盖
  E1/E2/E3 三类违规被抓、非 git 的 `Select-Object -First 1` 不误伤、豁免文件
  E2 放行且 E1 仍拒；主扫描退出码 0（除豁免面外零命中）；
  `pwsh -File scripts/git-helpers.ps1 -SelfTest` 通过。
- **一般化**：**给「写法」立禁令之前，先把「类别」的边界写清楚**。规则扩面前必须
  全仓校准零误伤，校准剩下的存量不能假装不存在——要么迁掉，要么点名进有主豁免面
  （写明哪张卡负责清空），两头都不沾的扩闸是给未来埋假绿灯。

### 2.27 合并冲突退出不回滚现场：merge/* 临时分支 + 冲突态 + 停在临时分支（DX-107）

- **现象**（2026-11-06 DX-106 收尾实录）：`git-done.ps1` 的合并撞 `progress.json`
  冲突后，remote 模式的 catch 直接 `exit 1`：HEAD 停在 `merge/<slug>` 临时分支、
  工作区挂着未合并冲突态、临时分支也没删；后续命令全部撞墙，得手工
  `git merge --abort` + `git switch <原分支>` + `git branch -D merge/<slug>`。
  `-DryRun` 更矛盾——头部文档承诺「结束切回原分支、演练提交全部丢弃」，冲突退出
  同样不兑现，DX-106 实测留下 `merge/chore-board-dx-106-zip-push` 残留。local 模式
  则把冲突态留在 `main` 上。
- **根因**：冲突路径只打印指引就退出，没有任何清理；成功路径末尾的
  `switch --detach` / dry-run 归位块都在冲突 exit 之前到不了。清理顺序还容易做反：
  不先 abort 就切不走，不先切走就删不掉当前检出的分支。
- **修复（DX-107）**：`Restore-ConflictScene` 收口两种模式的冲突退出，按
  「abort 合并 → 切回进入时检出 → 删 merge/* 临时分支」顺序回滚，逐步打印、
  失败只告警并给手工命令。全程非破坏性：`git merge --abort`（等价
  `reset --merge`，保留未提交改动）+ `git switch`（非强制）+ `git branch -D`；
  绝不 `reset --hard` / `checkout -- .`，用户未提交改动一律不碰。进入时是分离
  HEAD 就按进入时 HEAD 回 `--detach`；无 `MERGE_HEAD`（合并未真正开始）跳过 abort。
- **判据**：在会与 `origin/main` 冲突的分支上跑
  `pwsh -File scripts/verify.ps1 -GitDoneDryRun`（回归用例；演练因冲突退 1 是预期，
  看的是退出后的现场），退出后 `git status --porcelain` 为空、
  `git symbolic-ref --short HEAD` 仍是进入时的分支、`git branch --list 'merge/*'` 为空、
  `MERGE_HEAD` 不存在；真实模式（含 `progress.json` 待提交改动）与 local 模式同样不留现场，
  且用户改动仍在、主线未被推送；`pwsh -File scripts/verify.ps1 -ScriptOnly` 全绿。
  回归在隔离临时仓里造「两侧各改同一 `docs/` 文件」的必冲突分支，`origin` 指向临时裸仓，
  不触真实远端（脚本留档：`.sandbox/artifacts/dx107-regression/`）。
- **一般化**：**失败路径也要有和成功路径对称的回滚**。凡是中途 `exit 1` 的分支，
  先问「这时检出在哪、有哪些临时产物」；只给手工指引等于把现场留给用户。
  回滚动作必须按「先解状态、再切走、最后删引用」排序，且只许用不吞改动的命令。

### 2.28 锚点检出台账是共享陈旧投影：领卡注入的契约与重开门判据口径分裂（DX-94）

- **现象**（2026-11-06 agent-dx74 领 DX-74 实录）：`.agent-env.ps1` 注入的 `NB_SCOPE`
  （`git-done/verify/pitfalls/AGENTS`）与 `NB_ACCEPTANCE` 全是 DX-88 的契约。锚点区本地
  `main` 停在分叉提交 `45e1e93`，那份台账里的 DX-74 是改名前旧收尾补推卡，主线上早已
  改配成 DX-88。agent 不逐字核对就拿着错契约开工，`check_scope` 必拒、白干一轮。
- **根因**：同一份台账被两个读者按不同口径读 —— `agent-init.ps1` 的契约注入（旧实现
  `$ledgerPath = Join-Path $repoRoot 'docs\board-ledger.json'`）读**检出本地文件**，
  而「拒绝重新领用」的 `--stuck-codes` 判据读 `--ledger-ref origin/main`。`$repoRoot`
  又是按脚本自身路径推出来的锚点根，锚点一旦分叉，注入的就是过期投影。
  「位置不是凭证」在这里反过来：**不声明读哪一份台账，读到的就是随手那一份**。
- **解法（DX-94）**：台账读取收口进 `derive_board_status.py`（新增
  `--contract <短号> --out <文件>`，只认 `--ledger-ref`，默认 `origin/main`），
  `agent-init.ps1` 改为调用它并把结果**落盘后按字节读**
  （`[System.IO.File]::ReadAllText(..., UTF8)`），绕开 PowerShell 捕获 Python stdout
  按控制台代码页 GBK 解码的坑（2.6 / 2.20 / DX-83）。worktree 开区与 `-InPlace`
  原地两条路径共用同一段注入代码，同源；读不到契约宁可显式告警跳过，也**不回落
  本地台账** —— 分叉副本正是要根治的陈旧源。
- **判据**：`python scripts/derive_board_status.py --selftest` 的「契约源」用例在临时 git
  仓库里造「ref 台账真值 + 工作区台账 SENTINEL」分叉，断言 `--ledger-ref` 读 ref、
  不读工作区；实测把工作区 `docs/board-ledger.json` 的 DX-94 三字段改成 SENTINEL 后
  重跑 `agent-init.ps1 -InPlace -Card DX-94`，`.agent-env.ps1` 的 `NB_SCOPE` /
  `NB_ACCEPTANCE` 与 `origin/main` 台账逐字一致、不含 SENTINEL。
- **一般化**：**读共享状态必须声明读哪一份**，尤其当另一条判据已经钉死某一份时 ——
  口径分裂不会报错，只会让人按过期定义干活。文件在磁盘上不等于它是事实源。
### 2.29 控制台编码自切只治一个入口：UTF-8 的三跳（代码页 / 解码 / 喂入）各留一份副本必漂

- **现象**（同类三连）：DX-60 修 build.ps1（chcp 65001 保 CMake file(READ) 字节保真）之后同类
  继续复发——verify.ps1 尾行输出「閫€鍑虹爜: 0」、nbflow status 授权段整段 mojibake（DX-77，
  CP 936 控制台直读 UTF-8 字节）；git-done / agent-init 捕获 python 子进程的 UTF-8 输出按 GBK
  解码后原样转发，乱码落地（DX-83，claim-card 的「[棰嗙敤] 鏈嶅姟绔...」实录，2026-11-06 多个
  会话现场复现）。
- **根因**：中文控制台上 UTF-8 化要同时管三跳——①控制台代码页（chcp 65001：子进程与直写
  控制台的字节按 65001 解释）；②`[Console]::OutputEncoding`（PS 解码 native 子进程 stdout，
  §2.20 的病根）；③`[Console]::InputEncoding`（PS 向 native 子进程喂输入）。§2.6 的 python
  reconfigure 只管「写侧出 UTF-8 字节」，读方（控制台 / PS 捕获）仍按 GBK 就照花。每个入口
  只修自己踩到的那一跳，其余跳下次准炸——同类第三次才收口，已经晚了两轮。
- **修复（DX-77/DX-83）**：唯一实现 `scripts/nb-console.ps1` 的 `Set-NbUtf8Console`——三跳
  一次到位、幂等（Get-Variable 探测，兼容宿主的 Set-StrictMode）、无控制台（计划任务 / CI
  服务会话）时告警放行（fail-soft，硬门在 build.ps1 的前缀逐字节核对）；build.ps1 /
  git-done.ps1 / agent-init.ps1 / verify.ps1 四个入口点源调用（build.ps1 的 DX-60 段迁移为
  调用，判据与注释留在原位）；nbflow.py 入口自切控制台 CP（GetConsoleWindow 探测，无控制台
  跳过——管道消费者本就按 UTF-8 解码）。机器判据：`check_script_hygiene.py` 规则 F——四个
  入口缺点源或缺调用即拒，反向探针入自测。
- **判据**：`python scripts/check_script_hygiene.py --selftest`；实测——CP 936 控制台下经封装
  捕获 python 中文输出与 python 直出 UTF-8 归一行尾后逐字节一致（行尾是捕获路径固有差异：
  python 文本模式写 CRLF、PS 管道捕获剥行尾；解码差异体现在内容字节），负对照（不接封装）
  必不相等；nbflow 跑过后控制台 CP 探针为 65001。
- **一般化**：§2.6（python 写侧）+ §2.20（字节落盘绕开解码）+ 本条（控制台三跳收口）是同一
  坑的三个面：中文 Windows 上任何一方的编码假设没有显式声明，就默认有人按 GBK 读。新入口
  脚本先点源 nb-console.ps1，新 python 工具先 reconfigure——两条都已有闸门盯着，别靠记性。

### 2.30 交付说明提交被判「改契约」：验收档随提交时态漂移，轻档结论靠挑时态自证（DX-113）

- **现象**（2026-11-06 DX-94 交付实录）：实现提交 `8505f61`（含 `#DX-94=done`）跑
  `check_board_diff.py --base origin/main` 判 derive、`-ScriptOnly` EXIT=0；追加
  `docs(board): 补 DX-94 交付说明`（仅改 `cards[DX-94].note`）后同一命令判 full、EXIT=2，
  轻档直接 Fail「台账的 cards 结构有变化」。卡片验收项写着「`-ScriptOnly` 全绿」，
  却只在中间态成立 —— 要拿到绿只能 detached 回实现提交跑，验收口径不可机器复核。
  同型非个例：DX-81（`9ba62fd`）、R11-11/12（`1be772b`）的交付说明提交都在最终分支态红。
- **根因**：`CARD_FIELDS` 把 `note` 与 stage/title/scope/acceptance 同列，`diff_cards`
  比整个 card dict，`classify` 见「改既有卡」即判 full。但 `note` 是交付说明，不进
  `tst_projectmap` 读的阶段 / 范围 / roadmap 行 —— 把每张卡收尾的必经动作当成改契约，
  等于让验收结论取决于提交顺序而不是改动内容。
- **修复（DX-113）**：`split_changed` 纯函数把既有卡变动拆「契约变化」（`CONTRACT_FIELDS`
  = CARD_FIELDS 去掉 note，外加自定义键）与「仅交付说明」；`classify` 只对前者判 full，
  note-only 维持 derive 档并在说明行点名。title 虽非派生输入，改标题仍属任务定义，按契约
  对待（既有「改 title → full」断言不破）。
- **判据**：`python scripts/check_board_diff.py --selftest` 新增 6 例（只改 note → derive、
  只改 note 不算未改动、note+scope 同改 → full、split_changed 两向归属）；端到端 ——
  本卡自身的交付说明提交在最终分支态跑 `-ScriptOnly` 必须 EXIT=0（修复前该形态必红）。
- **一般化**：**验收档位必须由改动内容决定，不能由提交顺序决定**。当一条流程动作
  （交付说明）必然触发某个保守闸门时，闸门该识别它并放行到正确的档位，而不是让执行者
  用提交切分去绕 —— 那会把「验收绿」变成时态巧合。

---

## 三、构建与 Qt / QML

### 3.1 `rc.exe not found` / `type_traits` 找不到

- **根因**：新机器或新 worktree 没装配 MSVC/SDK 环境，或直接手敲 cmake 跳过 `vcvars64`。
- **解法 / 预防**：永远走 `scripts/build.ps1`（自动探测），新 clone 先 `. ./scripts/dev-env.ps1`（§一、§7.7）；
  配置缓存坏了用 `-Reconfigure`。

### 3.2 `LNK1104` 无法打开 `NeonBoard.exe`

- **根因**：上一次沙箱实例还在跑，链接器写不进目标文件。
- **解法**：只结束**本 worktree** 的进程（按可执行路径过滤，例如路径含该 agent 名），不要按进程名一把杀——会误杀主线实例和同事的预览实例。
- **预防**：`dev-run.ps1` 起的沙箱实例用完就退，预览实例用 `scripts/preview.ps1 -Action Stop` 停干净；
  多实例本身是允许的（`NEONBOARD_INSTANCE` 加后缀），别为了「怕冲突」而全局杀进程。

### 3.3 AutoMoc 与 C++ 原始字符串：链接期才炸

- **现象**：测试类里用 `R"(...)"` 写 JSON fixture，编译一路过，链接报 `metaObject` / `qt_metacast` 未解析（LNK2001），
  生成的 moc 文件是空的；清 `*_autogen` 也不见得好。
- **根因**：moc 不认原始字符串——内容里有 `)"` 会提前截断（报 missing ')'），带花括号的 JSON 会让它干脆不生成元代码。
- **解法 / 预防**：测试数据里的 JSON / 正则一律用**转义字符串拼接**（`AGENTS.md` §五.8 第一条已定为约定）。

### 3.4 改了公共头文件签名，却报「无法解析的外部符号」

- **现象**：签名明明改对了，链接器说符号找不到，看起来像签名写错。
- **根因**：旧 `*.obj` / `*_autogen` 没重编，链接器拿的是改签名之前的目标文件。
- **解法**：删掉对应测试的产物（`*.obj` 连同它的 `*_autogen` 目录）再走 `scripts/build.ps1`，别急着回改签名（§五.8 第二条）。

### 3.5 新增 `qml/components/*.qml` 后 `Theme` / `Motion` ReferenceError

- **现象**：组件单独看没问题，一挂上去就报单例找不到，面板整块空白。
- **根因**：没在 `CMakeLists.txt` 里设 `QT_RESOURCE_ALIAS`，QML 文件脱离模块上下文。
- **预防**：新增组件必须同步 `set_source_files_properties(... QT_RESOURCE_ALIAS "<Name>.qml")`；
  界面令牌只能用 `Theme.xxx`，不写死颜色（§10.1）。

### 3.6 `ComboBox.textRole` 覆盖导致整块设置面板加载失败

- **现象**：改一处下拉框，整个 SettingsPanel 白屏，日志只有一行 QML 错误。
- **根因**：`textRole` 是 FINAL 属性，不能覆盖；`onActivated: { ... index ... }` 的信号参数注入也已弃用。
- **解法**：自定义显示走 `contentItem` + 从 `model[currentIndex][labelRole]` 自取标签（`displayText` 对 `QVariantList<map>` 会退化成空串），
  信号用显式形参 `onActivated: (index) => ...`。
- **预防**：Qt Quick Controls 的 FINAL 属性先查文档再覆盖；QML 报错要看**日志里最后一条**并顺带确认它前面是否有加载失败的兄弟节点。

### 3.7 控件状态「看起来不同步」多为误读

- **现象**：以为 `Switch` 没跟着 meta 值走。
- **结论**：Basic style 深色面板里**滑块靠右 = 选中**；`checked` 绑定 + `onToggled` 写回是正确用法。
- **预防**：判定界面状态要截图放大核对指针位置，或直接读 `meta` 表值，别凭缩略图印象改代码。

### 3.8 双击 `build\NeonBoard.exe` 连弹四个「找不到 Qt6xxx.dll」

- **现象**：装好 Qt 的开发机上双击构建产物，按顺序弹出 `Qt6Sql.dll` → `Qt6Widgets.dll` →
  `Qt6QuickControls2.dll` → `Qt6Quick.dll` 四个系统错误框，点四次「确定」才退出。
- **根因**：`build/` 只是编译产物目录，CMake 里没有 windeployqt 的 POST_BUILD，exe 旁边**没有**
  任何 Qt DLL；只有 `dist/NeonBoard`（`scripts/deploy.ps1` 出包）才自带运行时。同理，
  `build\Release\NeonBoard.exe`（VS 多配置）与 `build\NeonBoard.exe`（Ninja 单配置）都不能直接双击。
- **解法**：要能双击就用便携版 —— `pwsh -File scripts/deploy.ps1`，然后开 `dist\NeonBoard\NeonBoard.exe`；
  要跑构建产物就让它带上 Qt bin —— `pwsh -File scripts/dev-run.ps1` 会自动解析 Qt bin 并前置到 PATH
  （顺序：本工作区 `build\CMakeCache.txt` → 锚点区同名文件 → `NEONBOARD_QT_BIN` → `QT_ROOT_DIR` →
  `C:\Qt\<版本>\msvc*_64\bin` 取最高版本），也可手动 `$env:NEONBOARD_QT_BIN='C:\Qt\6.8.3\msvc2022_64\bin'`。
- **预防**：别把「exe 双击能开」当成构建成功的证据 —— 构建只证明编译链接过了；
  运行验证走 `--selftest` / `--smoke`，出包验证走 `dist\NeonBoard`（DX-11）。

### 3.9 `property Item` 收不下 ApplicationWindow

- **现象**：详情抽屉组件声明 `property Item hostWindow`，主窗口传 `hostWindow: root` 直接报
  `Cannot assign ...`，组件整块加载失败。
- **根因**：`ApplicationWindow` 不是 `Item`，是 `Window` 派生；`Item` 类型属性在绑定期就做类型校验。
- **解法 / 预防**：要「窗口或 Item 都能收」的宿主引用用 `property var`；需要往里挂子项时
  `parent: hostWindow.contentItem`（`34d0b9b`）。声明属性类型前先想清楚调用方会传什么。

### 3.10 QML 启动即绑定的 context property，注册时机错半步就是 null

- **现象**：QML 单例方法报 `Cannot call method of null`，但 C++ 对象明明构造了。
- **根因**：`engine.rootContext()->setContextProperty(...)` 写在了 `QQmlApplicationEngine` 构造**之后**
  （或声明顺序被调整过），QML 加载时属性还不存在。
- **解法 / 预防**：凡是 QML 第一帧就要用的单例，**注册必须先于 engine 构造**——C++ 声明顺序本身就是约束
  （`DragFeel`，`109ef92`）。代码评审看到 engine 与 context property，先看谁先谁后。

### 3.11 `Q_INVOKABLE` 方法在绑定里按属性读，渲染出的是函数体

- **现象**：界面上显示一串 JS 函数源码文本，而不是数字。
- **根因**：QML 绑定里写 `obj.someCount`（无括号），而 `someCount` 是 `Q_INVOKABLE` 方法不是属性——
  取到的是方法对象，塞进 Text 就是函数体。
- **解法 / 预防**：二选一：绑定处写调用形式 `someCount()`；要属性语义（变更自动刷新）就暴露
  `Q_PROPERTY` + `NOTIFY`（主题市场的 `revision` 属性就是为此，`f865de8`）。QML 引用 C++ 成员前，
  先确认它是属性还是方法。

### 3.12 侧表 nullable 列没定好，事件写入静默丢失

- **现象**：审计侧表只记到 created/moved，完成与删除两类事件凭空消失，无任何报错。
- **根因**：`record()` 的 detail 参数默认 `nullptr`，绑成 SQL `NULL`，撞上 `NOT NULL` 约束整条写入失败，
  而调用侧没有检查失败——错误被静默吞掉。
- **解法 / 预防**：事件类侧表的可选字段，要么建表时允许 NULL，要么绑定层默认给**非 null 空串**
  （`TaskAudit::record`，`4390ff0`）。审计/日志类写入失败必须留痕，否则丢的恰是出问题那一刻的证据。

### 3.13 编辑态脏检查的比对粒度，必须与存储精度一致

- **现象**：详情抽屉编辑态一打开，「有未保存修改」就亮着，用户什么都没改。
- **根因**：脏比对把 `QDateTime` 按秒比，而库里的到期日只有**天**精度、提醒只有**分钟**精度——
  展示层截断的精度差被当成了用户修改。
- **解法 / 预防**：补丁式保存的脏检查：到期按天（`toJulianDay`）、提醒按分钟比；逐字段生成 patch、
  空集直接返回不写库（`CardDetail`，`34d0b9b`）。比对粒度跟着**存储精度**走，不跟显示格式走。

### 3.14 打包只拷了示例插件，官方插件一个都没进包

- **现象**：安装版 / 便携版打开插件面板是空的，`plugins/` 下的 6 个官方插件一个都没有；
  而开发机跑 `scripts/dev-run.ps1` 一切正常 —— 因为开发时读的是**仓库目录**，不是打包产物。
- **判据**：`ls dist/NeonBoard/plugins` 只有 `surfaces` / `today` 两个示例，没有
  `agent-bridge` / `agent-claude` / `agent-codex` / `agent-cursor` / `agent-opencode` / `plan-hook`。
  更硬的判据是用产物本身跑 `NeonBoard.exe --plugin-report`，看 `declared[]` / `effective[]`
  的 `source` 是不是 `appdir`（安装目录 = 唯一随分发的那份；`official` 是源码树的 `plugins/`，
  `sample` 是示例，`appdata` 是用户自己装的）。
  > 这段原写作「source 是否出现 `plugin`」—— 没有这个取值，照它验永远对不上。
  > 已在 DX-19 更正为四值口径，别再改回去。
- **根因**：`scripts/deploy.ps1` 里只写了 `Copy-Item plugins-samples\*`，官方插件那一行从来没写。
  构建不会因此变红 —— 它只是少拷了一个目录。
- **后果比「少几个插件」严重**：`agent-bridge` 缺席意味着「agent 投 `agent-task-event` → 自动建卡 /
  流转 / 对账」整条链路在安装版上**从未工作过**；而开发机因为读仓库目录，永远测不出来。
- **解法 / 预防**：`deploy.ps1` 同时拷 `plugins\*` 与 `plugins-samples\*`，打包后立刻跑
  `scripts/check_dist.py` —— 一份**显式清单**，缺一即 `throw`。清单分两类来源：
  - 插件两批**以磁盘为准**（`plugins/` 与 `plugins-samples/` 下每个目录都要在产物里且有
    `plugin.json`），所以以后新增插件不用改清单，也就不会「忘了同步清单」；
  - Qt 运行时以**最小集**为准（exe、Qt6Core/Gui/Qml/Quick/QuickControls2/Network/Sql、
    `platforms/qwindows.dll`、`sqldrivers/qsqlite.dll`、`qml/QtQuick`），因为 windeployqt
    的产物集合随版本变，写死全量会天天误报。
  `deploy` / CI / release 三处共用这一份判据；它也能对任意已存在的 dist 目录单独复跑
  （`--dir`），不必重跑构建 —— 产物验收的前提就是检查器能脱离构建过程独立执行。
- **判断这类问题的通用办法**：**验收必须用产物本身，不能只跑源码树** —— 凡是「拷文件」的环节
  （插件、资源、翻译、运行时 DLL），源码树里一切正常而产物里缺东西，是同一个失效模式。
  边界要说清：`shaders/` 与 `qml/` 已由 `qt_add_shaders` / `qt_add_qml_module` 内嵌进
  `qrc`，不需要随包拷；真正需要额外拷贝的只有两批插件。

### 3.15 Qt 探测跑在 configure 之前，新工作区首次验收必卡 180 秒

- **现象**：新 worktree 第一次跑全量 `verify.ps1`，编译顺利通过，紧接着
  `--selftest 超时（180 秒未退出）`。加 `-QtRoot C:\Qt\6.8.3\msvc2022_64` 重跑，
  27 秒全过 —— 同一个工作区、同一份代码，只差一个参数。
- **判据**：日志开头是「未探测到 Qt」。180 秒不是卡死：exe 找不到 `Qt6Core.dll` 时
  Windows 会弹「无法启动此程序」的系统对话框并**一直等待**，超时机制只是把它杀掉。
  判据链：`--selftest` 超时 + 日志无「Qt（…探测）」+ `build/CMakeCache.txt` 是这次
  构建才生成的。
- **根因**：DX-24 的探测读 `build/CMakeCache.txt` 的 `Qt6_DIR`，但这一步排在**编译之前**。
  新工作区没有 `build/`，文件不存在 → 探测静默跳过（当时的卡面把「缺失就跳过」写成了
  预期行为，验收又在已 configure 过的目录里做，落差被完全掩盖）。
- **修复（DX-30）**：反推逻辑抽成 `Resolve-QtRootFromCache`，在**构建之后**再调一次。
  编译由 `build.ps1` 完成，它自己解析了 Qt 并写进 CMakeCache —— 信息早就落在磁盘上，
  差的只是「什么时候读」。补探仍失败时如实提示要传 `-QtRoot`，不假装成功。
- **一般化**：**读「构建产物里的信息」的代码，必须排在构建之后**。顺序错了往往不报错，
  而是静默降级（这里降级成「不补 PATH」），再由下游以一种完全无关的症状报出来
  （「测试超时」）。另一条：验收要单独覆盖**冷启动**这一档 —— 只在已 configure 过的目录
  里验，会漏掉所有「第一次」才有的问题。

### 3.16 Ninja 依赖条目全空：改公共头文件不重编，陈旧 obj 打出堆损坏

> **DX-52 已根治。** 本节原先写的根因（「本仓库 Ninja 构建没有开 `/showIncludes`」）
> **是错的** —— `/showIncludes` 一直开着，`deps = msvc` 也在。下面是真正的根因与解法。

- **现象**：改了 `src/shell/SplashScene.h` 里的类成员（加字段 / 改签名）后重新编译，
  链接期报「无法解析的外部符号」，或更邪门的：运行期堆损坏，崩溃点报在
  `QImage::~QImage`（与真实原因毫无关系），`gdb` 里看到的是「释放了别人的内存」。
- **真根因（三层叠加，缺一不可）**：
  1. **CMake 的编译器探测绕开启动器**：`CMAKE_DETERMINE_MSVC_SHOWINCLUDES_PREFIX` 直接调
     `cl.exe`，不走 `CMAKE_<LANG>_COMPILER_LAUNCHER` —— 它记下的是「干净 cl.exe」的前缀；
  2. **sccache 改写了本地化前缀**：编译器输出经 sccache 后，前缀字节变成
     `D7 A2 EF BF BD 3A 20 EF BF BD 3A 20 20`（含 U+FFFD），两种控制台代码页下完全一样；
     CMake 记的干净前缀因此永远对不上 Ninja 实际看到的；
  3. **代码页漂移**是另一个独立故障：build 目录若在 CP 936 下配置、却在 CP 65001 下编译，
     即使没有 sccache 也对不上。
  三者叠加的结果：Ninja 一个依赖条目都解析不出来 → `#deps 0` → 它认为「只有 .cpp 变了才重编」
  → 改了头文件却不动引用它的 TU：**旧 obj 用旧类布局、新实现按新布局写** → 堆损坏。
- **判据**：`ninja -t deps` 输出里 obj 的 `#deps` 全是 0（应按 include 数量为正）。
  另一个判据：改完头文件后 `obj` 文件的时间戳没变。
  > **别用 `ninja -n` 判**：`no work to do` 只说明「.cpp 没变」，依赖条目全空时也是这个表现。
- **反证清单（都试过，全部无效，别再走一遍）**：
  - 「没开 `/showIncludes`」—— 错，开着的；
  - `/utf-8` —— **不改变**诊断输出编码（两种代码页下输出字节完全相同）；
  - `CMAKE_NINJA_CMCLDEPS=ON` —— 无效，CMake 4.4 对 MSVC 不生成 `/sourceDependencies`；
  - `CL_SHOWINCLUDES_PREFIX` 环境变量 —— 对 sccache 无效；
  - `VSLANG=1033`（切英文输出）—— 无效，本机没装英文语言包，仍输出中文前缀；
  - 把 launcher 提前到 `project()` 之前 —— 无效，探测仍绕开 launcher。
- **解法（DX-52）**：**别让 CMake 自探测前缀，改成实测**。`scripts/build.ps1` 用**同一条编译链路**
  （含 sccache 启动器）跑一次探针编译，取 include 行里「已知路径」之前的原始字节，
  把这个前缀**按字节**交给 CMake（`project()` 之后覆盖 `CMAKE_CXX_CL_SHOWINCLUDES_PREFIX` /
  `CMAKE_CL_SHOWINCLUDES_PREFIX`）—— 让 **CMake 记录值 == Ninja 实际看到的值**。
  前缀按字节经**文件**传递而不是命令行：本地化前缀在中文机器上可能是 GBK，不是合法 UTF-8，
  走命令行会被替换成 U+FFFD，字节一失真就又对不上。
  前缀一变就强制重新配置，并让编译命令行带上前缀哈希 —— 否则旧 obj 不会被重编，
  「陈旧 obj」的隐患原样留着。
  > **「同一条链路」包括 launcher**：探针不带 sccache、编译却带（或反过来），测到的前缀照样对不上。
  > 所以 launcher 也由构建脚本显式指定（`-DNEONBOARD_SCCACHE_LAUNCHER`），两边共用同一个值。
- **踩过的三个坑（照着抄就行）**：
  1. `CMAKE_<LANG>_CL_SHOWINCLUDES_PREFIX` 是**普通变量**（不是缓存变量），
     `CMakeCXXCompiler.cmake` 里刚设过 —— **必须在 `project()` 之后覆盖**才生效；
     用 `-DCMAKE_CXX_CL_SHOWINCLUDES_PREFIX=...` 传是无效的（缓存变量会被普通变量盖住）。
  2. PowerShell 里 `,` 的优先级**高于** `+`：`@("-D" + $a, "-D" + $b)` 会算成「字符串 + 数组」，
     两个 `-D` 被拼成**一条**传给 cmake（缓存里会看到两个值连在一起）。每个元素都要加括号。
  3. 复用 CMake 缓存时 **cmake 根本不会被调用**，新加的 `-D` 进不了缓存 ——
     实测「编译身份头」就是这样丢的，症状是「参数写了但命令行里没有」。
     解法：把 `-D` 参数集合的指纹也存进缓存，对不上就强制重配。
- **配套的一条（不做就等于没修）**：sccache 的缓存**按内容寻址、跨工作区共享**，
  命中时回放的是**当初那个目录**的 `/showIncludes` 输出 —— 里面的头文件路径属于别的 worktree。
  Ninja 把那些路径当依赖 → 改本地头文件不重编（陈旧 obj），那个 worktree 被回收后构建还会直接
  报「missing and no known rule to make it」。**实测**：同样的源码在另一个 worktree 编译，
  `ninja -t deps` 里赫然是 `../../agent-dx38/src/...`。
  解法：给每个源码树生成一份带「源码根路径」的**强制包含头**（`/FI`）——
  预处理结果随之按源码树分桶，sccache 的 key 也随之分桶，回放的路径就永远属于当前这棵树。
  - `/FI` 的写法有坑：MSVC 只把「`/FI` 之后那个 token 的剩余部分」当文件名，
    **Ninja 不经过 shell，带引号的路径会把引号当成文件名的一部分**；
    选项里一旦有空格，**CMake 会给整个选项加引号**，MSVC 就把「/FI 名字」整个当文件名了。
    实测三种写法（带引号绝对路径 / 带空格 / 相对路径）都能踩到，最后用的是
    「不含空格的绝对路径」（必要时用 8.3 短路径）。
  - sccache 的 key **不吃 `-D` 定义、也不吃源文件路径**（实测：换个 `-D` 仍然命中）——
    所以「用宏给缓存分桶」这条路是不通的，必须让内容本身变。
  - `SCCACHE_DIR` **客户端改不动**（缓存目录归服务端配置管）—— 「每 worktree 一个缓存目录」
    这条路也走不通。
- **验收怎么算过**：`ninja -t deps` 里 obj 的 `#deps` 为正且列出真实头文件；
  改一个公共头文件后重新构建，对应 TU **确实重编**（日志里出现该源文件名）；
  `ninja -t deps` 里**不出现别的 worktree 的路径**。
### 3.17 包间 diff 结构性地给不出「两个包都没有」的文件删除动作

- **现象**：2026-11-05 真机 v0.1.4 → v0.1.5 增量升级成功，日志干净。但装完之后对着发布目录比：
  R5-11 好不容易清掉的 1136 个弃用样式文件（六套 Qt 控件样式目录 1126 个 + root 级 10 个 dll）
  一个都没少，`install-receipt.json` 也不存在。
- **判据**：`delta.json` 的 `changes[]` 里 **0 条 `Deleted`**，全是 `Modified`。这不是 diff 算错了 ——
  这批文件在 0.1.4 和 0.1.5 两个包里**都不存在**，而 diff 只会对「旧包有、新包没有」出删除动作。
  换句话说：任何一次包间断层都不可能删掉它们，无论版本跳几次。
- **根因（是设计边界，不是 bug）**：文件级 diff 的事实源是「两个包的目录快照」，
  它天生不知道「曾经发布过、后来不再发布」的东西长什么样。清理这类残留**只能靠一份显式清单**，
  而这份清单原先只活在安装器里：`[InstallDelete]` + 安装器的 `ssPostInstall` 回执。
  增量路径走的是自更新 bat（`launchDeltaSelfUpdater`），**压根不跑安装器** ——
  于是清理与回执两件事对增量用户同时失效。
- **后果比「多占 100 MB」严重**：从 0.1.3 一路增量升上来的用户永久背着这批文件，
  而且升级后**拿不到任何物证** —— 没有回执，R5-12 的「上次更新未完成」警示条在这些人身上永远亮不起来，
  R5-09 / R5-11 的修复等于只对「跑过完整安装包」的人生效。
  宿主清理那次没暴露同一问题，是因为 bat 里自己写了 `taskkill`（把安装器侧的动作在增量侧复刻了一遍）——
  **复刻一次侥幸对一次，两处各写必然漂**，这正是这次要避免的模式。
- **解法（DX-38）**：清单收敛成单一文件 `installer/obsolete-files.txt`，两个消费者读它 ——
  安装器用 `[Files]` 把它带进 `{tmp}`、`[Code] CleanObsoleteFiles` 运行期逐行删
  （ISPP **没有**编译期读文本文件的函数，实测 `LoadStringsFromFile` / `LoadFileToString` /
  `LoadFileToStringList` / `ReadFileToString` 四个候选全报 `Undeclared identifier`，
  所以「编译期把清单读进 [InstallDelete]」这条路根本不存在）；
  `build-delta.ps1` 把它打进 `delta.zip`，客户端 `DeltaUpdate::cleanupObsoleteFiles` 读同一份语义。
  回执同理由 `DeltaUpdate::writeInstallReceipt` 在增量侧写，`source` 标 `delta`。
- **一般化**：**「清理 / 退役」类动作不能由 diff 推导，必须有显式清单；而清单只要有两个消费者，
  就必须只有一个文件源。** 判据是问一句「这条信息在两个包里都在吗」——不在，diff 就永远给不出它，
  无论是删除还是回执。新增弃用条目时只改那一份 txt，两条路径自动同时生效；
  反过来，哪天发现「安装版正常、增量版不生效」，先怀疑的就是这类只有单侧消费者的清单。

### 3.18 预校验对缺失文件硬失败，会反噬这台机器后续所有升级

- **现象**：给增量清单塞进 `Deleted` 条目本身很容易（`buildDelta` 会算），但真正装过完整安装包
  的机器上，那些文件早已被 `[InstallDelete]` 删干净 —— 增量升级在预校验阶段直接报
  「旧文件不存在，无法应用增量」，整单失败并回退全量。
- **判据**：`DeltaUpdate.cpp` 的预校验循环里，`Modified` 与 `Deleted` 走的是同一个
  `QFile::exists` 分支，缺席一律 `return false`。
- **为什么这是必须修的结构性问题，而不是「多绕一次全量下载」**：预校验失败发生在
  **任何写盘之前**，所以它不会留下坏树；但它把「这台机器上这批文件已经清理过」这个
  **正常状态**判成了**升级障碍**。清理越干净的用户，越升不了级 ——
  一个只做对了事情的状态，被永久锁死在失败路径上，而且每次检查更新都再失败一遍。
- **解法（DX-38）**：把「旧文件必须存在」的适用范围收窄到 `Modified`（bsdiff 确实需要旧内容，
  缺席就是版本不符）；`Deleted` 条目缺席 = 已清理，放行。备份环同样跳过缺席条目
  （原先每个 `Deleted` 文件先复制一份备份，1136 条会先把 `.backup` 目录撑爆）；
  应用阶段 `Deleted` 只在「文件存在且删除失败」时报错。
  清理弃用文件走**完全独立于预校验/备份/应用**的通道（`cleanupObsoleteFiles`），
  缺席静默跳过、被占用只记 `skipped`，永不影响升级成败。
- **一般化**：**预校验的强度要按「这一步是否真的依赖该文件」定级，而不是按条目类型一刀切。**
  凡是「缺席其实意味着上一轮已经做过」的条目（删除、清理、幂等的退役动作），
  硬失败等于把成功状态判成故障；写预校验时先问一句：这条目失败之后，
  用户还有没有一条能自愈的路径？没有，就必须降级为跳过或告警。

### 3.19 `/utf-8` 散落在各处：漏一个目标就静默按本地代码页解析中文

- **现象**：仓库里的 `/utf-8` 曾散落在四处 —— 顶层 `CMakeLists.txt` 里三个
  `target_compile_options`（NeonBoard / NeonBoardHost / neonboard-delta-tool），加上
  `tests/CMakeLists.txt` 的 `neonboard_add_test` 一处。每加一个目标都得记得再写一遍，
  漏写就静默回退：MSVC 按**本地代码页**（中文机器上是 CP936）解析源码里的中文字面量，
  编译器一声不吭，症状是「断言莫名失败」/「中文常量乱码」，查起来毫无线索。
- **为什么一直没出事**：因为覆盖是**搭便车**来的 —— `Qt6::Platform` 的
  `INTERFACE_COMPILE_OPTIONS` 带 `-utf-8`（`Qt6Targets.cmake:64`），`Qt6::Core` 的
  `INTERFACE_LINK_LIBRARIES` 又链了 `Qt6::Platform`，于是凡是链 `Qt6::Core` 的目标都白捡
  了这个开关。那是 Qt 的行为，不是本仓库的约定：哪天多一个不链 Qt 的目标（纯 C 工具、
  第三方库），或者 Qt 改掉这个选项，覆盖就静默消失，而**没有任何一处代码会报错**。
- **解法（DX-53）**：收敛成顶层 `CMakeLists.txt` 唯一一处**目录级**
  `add_compile_options(/utf-8)`，撤掉四处重复声明。目录级声明对本目录以及**此后**
  `add_subdirectory()` 引入的子目录里所有目标生效，新增目标不用再写第二遍。
  位置有讲究：必须落在所有 `add_subdirectory()` 之前，否则子目录拿不到目录属性。
- **机器守卫**：`scripts/check_utf8_flag.py`，两层口径 ——
  源码层查「只有一处、且是顶层目录级、且早于所有 `add_subdirectory`」；
  构建层实测生成物（Ninja 的 `build.ninja` 按 obj 规则、VS 生成器的 `*.vcxproj` 按项目级
  `ClCompile/AdditionalOptions`），确认**每一个**编译单元实际拿到的选项里都带 utf-8。
  源码层合规不代表生成物真的带上了，两层都要跑。已接入 CI 与 `scripts/verify.ps1`。
- **一般化**：**「靠人记的约定」和「结构性保证」要分开看。** 判断某项配置是否真的被
  全覆盖，别看「现在有没有生效」，要看**它靠什么生效**：靠某个第三方库的顺带行为 =
  随时可能消失；靠自己代码里的一处声明 + 一道机器守卫 = 可持续。
  同理，验收要看**生成物**（实际发生的事），不能只看**源码**（声明的样子）。

### 3.20 前缀探针被 sccache 的缓存污染：第二个工作区里依赖条目又全空

- **现象**：DX-52 把「改公共头文件不重编 → 陈旧 obj → 堆损坏」根治了，验收也过了。可到了
  **下一个**工作区（2026-11-05 实施 DX-53 时实测），同一个坑原样复发：`ninja -t deps` 里
  418/418 个 obj 的 `#deps` 全是 0。构建脚本只打印一行黄字警告就继续，产出的是一个
  「看起来成功、实际埋雷」的构建 —— 编译器不报错、Ninja 不报错、构建照样成功。
- **根因**：前缀探针（`scripts/build.ps1` 的 `Get-NbShowIncludesPrefix`）靠「探针日志里出现
  **当前工作区**的头文件路径」来定位 `/showIncludes` 前缀。但探针源码是**逐字固定**的 →
  预处理结果逐字相同 → sccache 的 key 按**内容**寻址（源文件与头文件路径都不参与）→
  **必然命中缓存** → 命中时 sccache 回放的是当初编译那棵树的输出，里面是**别的工作区**
  的路径 → 探针对不上当前路径 → 返回空 → 退回 CMake 自探测（记的是干净 cl.exe 的前缀，
  与 Ninja 经 sccache 实际看到的对不上）→ 依赖条目全空。
  - 独立复现：在 A 目录编译过探针源码后，在 B 目录编译**逐字相同**的源码，
    sccache 命中并回放 **A 目录**的路径。
  - DX-52 当时是在「首次、缓存未命中」的工作区里验证的，所以没暴露 ——
    这是**验证方法**的漏洞：验收只在「恰好没命中缓存」的那一次成立。
- **这是 DX-52 自己的思路漏了一处**：它给**主构建**加了按源码树分桶的身份头（`/FI`），
  却把**探针自己**漏在外面。探针也是一次「经启动器的编译」，同样会被缓存命中污染。
- **解法（DX-54）**：
  ① 探针源码里嵌入**随目录变化的标识**，让预处理结果按源码树分桶 → key 不同 →
  必然 miss → 真实编译 → 日志里是当前工作区的路径。**必须是字符串字面量** ——
  注释会被预处理器删掉，预处理结果不变，key 也就跟着不变（改了等于没改）。
  ② 探针失败**不再静默降级**：直接中止构建 —— 依赖条目空着的构建不该被当成成功的构建
  交出去。
  ③ 新增 `scripts/check_ninja_deps.py`，构建后实测 `ninja -t deps`：依赖条目全空、
  或指向别的 worktree，当场拦下（判定逻辑只有一处实现）。
- **一般化**：**「靠人记的约定」和「结构性保证」要分开看**（同 §3.19）。更进一步：
  **验收要在「最可能失败的那次」成立才算数。** DX-52 的验收是在「恰好缓存未命中」的
  工作区里做的，条件一换（第二个工作区）就塌 —— 只验一次、且那次恰好是特例，等于没验。
  同理，**静默降级是最坏的失败模式**：它不报错，只是把一个隐患埋进去，等它以别的形式
  （堆损坏）冒出来时现场早已没了。宁可中止。

### 3.21 前缀交接的最后一跳失真：CP 936 下 file(READ) 把前缀写成「??」，deps 又全 0

- **现象**（2026-11-06，R5-13 v0.1.6 发版实测，新 worktree 首次全量验证）：探针正常
  （`showincludes-prefix.bin` 字节正确）、构建 547 目标全部成功，**构建末尾**依赖守卫
  报 419/419 编译单元 `#deps` 全 0，DX-54 闸门硬失败。查 `build/CMakeFiles/rules.ninja`：
  `msvc_deps_prefix = ??: ?:  ` —— 探针实测的前缀字节 `D7 A2 EF BF BD 3A 20 ...` 在
  build.ninja 里变成了 ASCII `?`。
- **根因（DX-52 机制的第四层）**：前缀按字节走「探针 → prefix.bin → CMakeLists 的
  `file(READ)` → build.ninja」，前三跳都字节保真，最后一跳 `file(READ)` 在 **CP 936
  控制台**下把非 UTF-8 字节写成 ASCII `?`（pip 装的 CMake 4.4.4 实测）；**CP 65001
  会话下同一份 prefix.bin 逐字节保真**。cl 的重定向输出编码跟系统 ANSI（936）、不跟
  控制台 CP，所以两次探针落盘的字节完全相同——变的只是 CMake 这一跳的保真度。
  DX-52 在「恰好缓存未命中」的工作区验证、DX-53 在「恰好 CP 65001」的会话里跑，
  都没踩中这一层。
- **反证清单（都试过，别再走一遍）**：
  - `VSLANG=1033` —— 已在 §3.16 反证过：本机没装英文语言包，cl 仍输出中文前缀；
  - `chcp 65001` 改变 cl 的重定向输出 —— 无效，cl 跟系统 ANSI 不跟控制台 CP
    （两次探针字节一致即为证据）；它改变的只是 `file(READ)` 这一跳；
  - 换 CMake 版本 / `-Generator VS` —— 前者治标不治本，后者绕开 Ninja 但 verify.ps1
    不支持生成器直通。
- **解法（DX-60）**：把「调用方恰好开了 CP 65001」这个偶然条件变成构建的确定前提：
  1. `scripts/build.ps1` 的 Ninja 分支在装配 MSVC 环境后自设 `chcp 65001`；
  2. **秒级闸门**：configure 之后立即把 `rules.ninja` 里的 `msvc_deps_prefix` 与
     `prefix.bin` **逐字节比对**（Latin-1 方式读，任何代码页都不改写字节），不一致当场
     指名报错 —— 否则要烧完 20-30 分钟全量构建才由依赖守卫兜底。
- **判据**：`build/CMakeFiles/rules.ninja` 的 `msvc_deps_prefix` 里出现 `3F`（`?`）字节
  即中招；健康状态是它与 `build/.nb-deps-probe/showincludes-prefix.bin` 逐字节相等。
- **一般化**：**多跳字节传递的链路，每一跳都要有自己的保真判据** —— 前三跳有探针
  自证，第四跳直到今天仍靠「恰好没失真」撑着。凡「A 进程写字节、B 进程读字节」的
  接缝（这里 A=探针/PowerShell，B=CMake），验收要在这道接缝上单独设卡，而不是只验
  链路终点的行为。附带副作用说明：build.ps1 会把宿主控制台代码页留在 65001，本仓库
  脚本与日志全按 UTF-8，这是更对的状态，未做还原。

### 3.22 Unicode Inno 的文件读写 API 参数是 AnsiString，不是 String

- **现象**（v0.1.5 发布 run 41870082136）：`iss:117 Column 71 Type mismatch` ——
  `LoadStringFromFile` / `SaveStringToFile` 用 `String` 变量传参，本地全绿
  （verify 不跑 ISCC、单测只 grep 文本），打 tag 出包才炸。
- **根因**：Inno Setup 6 只有 Unicode 版，但这两个文件读写的字符串参数是
  **AnsiString**；`String`（Unicode）变量传进去在**编译期**报 Type mismatch。
  而 iss 的 `[Code]` 段只有 ISCC 会编译 —— 任何「文本级检查」都到不了这一层。
- **解法**：一律走 `TArrayOfString` 的 `LoadStringsFromFile` / `SaveStringsToFile`
  （installer/neonboard.iss 头部注释已固化）；配合 DX-36 的 verify.ps1 ISCC 编译闸门
  （临时假 payload 真编译一次），这类错误在本地验收就红。
- **判据**：负测——在 iss 里加一个用 `String` 传参调 `LoadStringFromFile` 的过程，
  `pwsh -File scripts/verify.ps1 -ScriptOnly` 的「安装器编译闸门」必须红。

### 3.23 Pascal 注释里写花括号：第一个 `}` 提前闭合注释，后半句变代码

- **现象**（v0.1.5 发布 run 41877631595）：`iss:126 Column 26 Syntax error` ——
  注释文本里写了 Inno 常量 `{app}`，`{...}` 注释在第一个 `}` 处就结束，
  后半句被当成代码。
- **根因**：Pascal 的 `{ }` 注释**不支持嵌套**：从 `{` 到第一个 `}`。
  注释里再写 `XX}` 形态的内容（Inno 常量、花括号枚举）都会提前闭合。
- **解法**：iss 注释里不写花括号（要提常量就用不带花括号的词，如「app 常量」）；
  DX-36 的 ISCC 编译闸门会在本地把这类语法错误当场拦下。
- **判据**：负测——在 iss 注释里写 `{ 用 {app} 常量拼接 }`，
  `-ScriptOnly` 的「安装器编译闸门」必须红。

---

## 四、界面验收与自动化

### 4.1 GUI 子系统 exe 拿不到 stdout

- **现象**：`--selftest` / `--smoke` 在 PowerShell 里看不到输出，误判成没跑。
- **解法 / 预防**：看 `.sandbox/neonboard.log`；测试用 `-o out.txt,txt`；CLI 需要回传结果时走 stdout（`emitCliLine()`，§7.9）。
  日志断言取**最后一条**匹配。

### 4.2 `verify.ps1` 在 worktree 里找不到运行日志

- **现象**：验收脚本报「未生成运行日志」，可应用明明跑起来了。
- **根因**：脚本按 `%APPDATA%\com.lin\NeonBoard` 拼日志路径，而 worktree 里的 exe 被 `AppPaths` 护栏强制写到本工作区 `.sandbox`；
  更糟的是没设沙箱变量时，脚本会去动**真实实例正在占用**的文件（`Remove-Item` 直接被打断过）。
- **解法**：读应用落盘位置收口到唯一入口——PowerShell 点源 `scripts/nb-paths.ps1`（`Get-NbLogFile` / `Get-NbBoardDb`），
  Python 侧唯一实现是 `scripts/nb_paths.py`（`claim-card.py` 等 import 它）；`verify.ps1` 未设 `NEONBOARD_DATA_DIR` 时自开
  `<BuildDir>\verify-sandbox`，路径口径随之正确（§7.6）。
- **预防**：**不要在第三个文件里再拼一次路径**——`scripts/check_script_hygiene.py` 会在 CI 拦下那个字面量。
  手工跑 exe 而不带沙箱变量时，日志仍在 `%APPDATA%`，这不算脚本坏，是环境变量没设。

### 4.3 Qt 的 ScrollView 不响应模拟滚轮

- **现象**：`PostMessageW` 发滚轮消息，界面不动，截图截不到面板下半部分。
- **解法**：用 `SendMessageW(WM_MOUSEWHEEL)` 且 `lParam` 传**屏幕坐标**；`PostMessage` 版本对 Qt 不可靠。
- **预防**：设置面板是固定尺寸上限（约 520×560 逻辑像素），**放大窗口不会放大面板**，长内容必须滚动后才能截全。

### 4.4 界面自动化不能抢焦点、还要过 DPI

- **解法**：`SetProcessDpiAwarenessContext` + `ShowWindow(SW_SHOWNOACTIVATE)` + `SetWindowPos(SWP_NOACTIVATE)`，
  取图用 `PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)` 走 GDI；点击用消息投递而不是移动真实光标。
- **预防**：验收点击要有**新鲜截图确认坐标**——坐标过期会点到别的控件（曾把 `exec.defaultAgent` 误改成别的 profile，
  事后要回读 `meta` 表核对并改回）。

### 4.5 `--smoke` 的 4 秒窗口不足以验证事件消费

- **根因**：`HookEventQueue` 一投出就 `markDelivered`，插件后台还没 ready 时事件被丢弃。
- **解法 / 预防**：验证事件链路必须用**常驻实例**（§11.3）；只跑 `--smoke` 通过不代表插件收到了事件。

### 4.6 截图按「尺寸」选窗，会截到并行会话的实例

- **现象**：R10-05 验收截到一张「三列全空 + 首启引导弹窗」的图，同一份日志却写着
  `ONBOARD skip tasks 139`。据此怀疑启动期 model reset 与 QML 绑定有竞态，还立了 R13-01。
- **根因**：截图脚本按「窗口尺寸 > 1200×700 且句柄连续 2 秒稳定」挑主窗口。并行会话的 NeonBoard
  用**不同的 instance key**（worktree 里是 `com.lin.NeonBoard.single-instance.agent-cat`，锚点区不带后缀），
  两个实例可以同时跑；窗口尺寸又完全一致（1180×640 @ dpr 1.5 → 1770×960），按尺寸挑谁都可能中。
  空数据目录的那个实例显示 0 条 + 首启引导是**正确行为**，被当成了 bug。
- **判据**：拿**截图文件 mtime** 去对日志里的运行区间。那张图 21:44:03，落在 21:43:54 那次运行的
  窗口生命周期内（`BOOT shown` 21:43:57），而该进程 21:43:55 明确记着 `ONBOARD skip tasks 139` ——
  同一个进程「日志说不弹、截图却弹」，只能是截错了窗口。
- **解法 / 预防**：选窗别只看尺寸。给实例起不同名字（`NEONBOARD_INSTANCE`），截图时按
  **进程 PID 或窗口标题**锁定自己拉起的那个，并在图旁记下 PID 与 `AppPaths::dataDir()`。
- **教训**：**「日志说 A、界面显示 B」时，先怀疑观察对象不是同一个，再怀疑程序有 bug。**
  排查多实例程序，第一件事是确认「我眼前这个窗口是谁」。

### 4.7 QML 各组件的 `Component.onCompleted` 全部同毫秒，逐组件打点无法归因

- **现象**：想定位启动慢在哪个 QML 块，给每个组件加打点，时间戳全部落在同一毫秒，比不出快慢。
- **根因**：QML 引擎在一个批次里集中触发这些回调，时钟分辨率撑不起归因。
- **解法 / 预防**：改用**消融法**——清空候选块 → 重建 → 比对 `BOOT loaded` 差值，一次只减一块
  （R8 主题包验证用此法定案）。同毫秒批次内的时间戳不构成证据，做减法才有结论。

### 4.8 启动性能对比只认 CPU 时间，墙钟会得出反向结论

- **现象**：本机与 CI 各跑 A/B 对比，墙钟中位数**方向相反**，同一份数据能支撑两个结论。
- **根因**（R0-11 诊断实录）：CI 冷启动 6010ms 里 `engine.load()` 占 5316ms，而全程 CPU 只用 702ms——
  88% 的墙钟在等 35 个 Qt DLL + 7 套 QML 样式的**冷磁盘 I/O**。墙钟测的是「这台机器此刻的磁盘状态」，
  不是代码快慢；最初「QML 树构建慢 8 倍」的结论就是这么错的，拿到 CPU 口径后整体推翻。
- **解法**：`Metrics` 增加 `startupCpuMs`（`GetProcessTimes`），METRICS 行尾补 `cpu=` 字段；
  对比只认 **CPU 时间 + BOOT 分段**（app → engine → loaded），墙钟只用来感知用户体验（`perf(boot)`）。
- **预防**：任何「性能变快/变慢」的断言，先问口径是什么。墙钟噪声主要来自 I/O 与调度，
  CPU 时间天然免疫这两样；另外四个执行类 Dialog 共 1200 行清空后 loaded 仅 730→726ms——
  Popup 懒建这类「看起来该优化」的点，先消融再动手，别按直觉立项。

### 4.9 主线程上的定时器动画会被任何同步阻塞冻住 —— 「进度条卡顿连带动画卡顿」是同一个原因

- **现象**（DX-39 报障原文）：「启动动画，在进度条卡顿的时候，会一起卡顿」。
  直觉会当成两个 bug：进度条慢是「真慢」，动画卡是「渲染慢」。实测是**同一个原因**。
- **根因**：旧 SplashWindow 是**主线程上的 QWidget**，动画由 16ms `QTimer` 驱动。
  启动路径上有三段主线程同步阻塞（模型层构造 + 卡片 load、`engine.load()`、窗口 attach），
  期间事件循环不转 → 定时器不触发 → **整幅画面（含进度条）冻成静止图**。
  进度条「该停就停」是对的（真实工作本来就没进展），但动画不该跟着停。
- **判据（可复现，别靠肉眼）**：`scripts/probe-splash-frames.ps1` 逐帧哈希闪屏窗口，
  「连续 ≥120ms 画面逐像素不变」= 冻结。改动前实测 **1120ms**（闪屏存活 2.3s 的一半），
  改后 **0ms**（连续三轮）。探针本身要 DPI 感知，否则 1.5x 屏上只采到窗口左上角一块。
- **解法（DX-39）**：闪屏渲染搬进**专属线程**（载体是 Win32 分层窗口：Qt 窗口不能在非 GUI
  线程创建，本机实测卡 3.2s），主线程只推阶段值。语义随之正确：
  **进度条该停就停，动画不再跟着停。**
- **附带发现**：渲染线程**不能碰字体**。QML 引擎启动期主线程长时间持有字体库锁，
  渲染线程一画字就被整段阻塞（实测单帧 252ms）。所以文字素材一律**主线程渲染好送进去**
  （数字图集、阶段标签字图），渲染线程只负责按格贴图。
- **一般化**：「界面卡顿」类报障先分诊：*是真的慢*（工作没进展）还是*画面冻住*（渲染被饿着）。
  两者可以同时成立，但解法不同 —— 前者要治工作量，后者要治线程归属。

### 4.10 图层化之后坐标系混用：元素被画到图层缓冲外面裁没了

- **现象**（DX-39 实施中）：闪屏搬进渲染线程 + 图层缓存之后，**LOGO 字形与标题整段消失**，
  但背景、轨道环、进度条、HUD 全都正常。画面看着像「字体没加载」。
- **误判过程**：先怀疑「渲染线程画字被字体库锁住」（这个方向**部分成立**：文字确实不能在线程里画），
  写了用例验证 `QPainterPath::addText` 在工作线程上**照样拿到 34 个元素的路径** → 假设被否。
  再打埋点看图层构建：路径 34 个元素、字形图 183×188、标题 9 张图 —— **全都建出来了**。
  说明不是「没生成」，是「生成完画到别处去了」。
- **根因**：图层化的 `renderLayer(image, dpr, origin, draw)` 把画笔放在**场景坐标**
  （`device = scale·(user − origin)`）。但两种元素的路子不一样：
  - LOGO 字形路径是**字形局部坐标**（`addText` 的基线在原点）→ 画之前必须 `translate(logoOrigin)`；
  - 标题路径是**场景坐标**（`addText` 时就带上了基线位置）→ 画之前**不能再平移**。
  混用时**不报错、不崩**：元素只是被画到图层缓冲外面裁掉了。
- **解法**：把两种约定写进 `SplashScene.h` 的类注释（图层化前先统一坐标系），
  并补两条**像素级回归用例**（LOGO 墨迹区 / 标题行的亮像素数 > 阈值）——
  「画面中央有东西」这种断言抓不住这种坑，因为轨道环与网格也在中央有东西。
- **一般化**：把绘制代码从「一处直画」改成「画进图层再贴」时，**坐标系是最容易混的地方**：
  原来的 `painter` 上下文悄悄变了（逻辑→设备、场景→图层局部），
  而这类改动**没有任何编译期或运行期报错**。搬完之后要逐个元素核对位置，
  并用像素断言把位置钉死。

### 4.11 设备像素图集按逻辑像素采样：高 DPI 下百分比读数碎成一片

- **现象**（DX-69，2026-11-06 报障）：启动闪屏的进度百分比读数在 **125%/150% 缩放的屏上**
  变成一堆青色碎片（截图里看着像进度条右端有个发光斑 + 乱码字形），
  **100% 缩放的屏完全正常**。看着像字体问题，实际跟字体没关系。
- **根因**：数字图集 `renderDigitAtlas(dpr, &advance)` 内部 `painter.scale(dpr, dpr)`，
  产出的是**设备像素**图：宽 `advance × 11 × dpr`，第 i 格在设备像素里位于
  `[i·advance·dpr, (i+1)·advance·dpr)`。而 `Scene::drawProgress()` 用
  `QRectF source(index * cell, 0, cell, atlas.height() / dpr)` 去采样 ——
  **源矩形用的是逻辑像素**。于是 dpr=1.5 时横向错位半格（取到隔壁格）、
  纵向只截到图集 2/3 高度，字形被横向错位 + 纵向截断。
- **为什么只有高 DPI 复现**：dpr=1.0 时 `×dpr` 与 `/dpr` 恰好抵消，源矩形正好对上；
  dpr>1 才暴露。**同一份代码在 100% 屏上测不出来** —— 这类 bug 必须显式跑 dpr>1 的用例。
- **解法**（DX-69）：源矩形改用设备像素尺度（`cell × dpr` 宽、图集完整设备高），
  目标矩形仍是逻辑坐标（画笔自带 dpr 变换，落到设备像素正好 1:1）。
  配套补一条**像素级回归用例**：同一场景在 dpr=1.0 / 1.5 下各画两遍
  （有图集 / 无图集），差值即纯读数墨迹，比较两者「逐逻辑列的墨迹纵向覆盖」分布，
  相关性必须 > 0.8（实测修好 0.95、没修 0.38）。
- **一般化**：`QImage` 图集与 `drawImage(target, image, source)` 的**源矩形永远在
  图集的设备像素坐标系里**，与目标矩形所在的坐标系无关。手写「按格取图」的代码
  （数字表、表情、图标条、序列帧）都要先问一句：**这格在图集里的设备像素位置是哪里？**
  凡是「逻辑宽度 × dpr」生成、却按逻辑宽度去采样的，都是同一类错。

---

## 五、网络与远程操作

### 5.1 GitHub HTTPS 443 时通时断

- **现象**：同一分钟内 `git fetch` 成功、紧接着 push 报 `Recv failure: Connection was reset` 或连不上 443（rc=128）。
- **解法**：带间隔的重试循环（3–5 次）；分支与本地合并提交不会因此丢。
  需要改走 SSH 时用**一次性环境变量**改写 URL，不动仓库或用户全局 git 配置：
  `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0='url.<ssh-url>.insteadOf' GIT_CONFIG_VALUE_0=<https-url>`。
- **预防**：凡「只对这台机器成立」的代理细节留本地，不进仓库（完整排查链条原存交接文档，已随清理移除）。

### 5.2 领用与闸门需要联网

- **根因**：认领占位 `refs/claims/<短号>` 推 origin 才算数，本地存在同名 ref 不算（fail-closed，§7.2.1）。
- **预防**：离线时不要指望认领成功；本地孤儿 `refs/claims/*` 应清掉，不作为认领依据。

---

## 六、应用运行时与沙箱

### 6.1 沙箱实例把看板状态写回仓库快照

- **现象**：测试卡出现在仓库 `progress.json` 里。
- **根因**：路线图快照默认从 exe 目录向上探测仓库根，沙箱实例没设 `NEONBOARD_ROADMAP` 就会写回仓库文件。
- **解法 / 预防**：跑沙箱应用统一用 `scripts/dev-run.ps1`（固定快照路径到沙箱内并默认关闭自动写回）；
  验收完 `git status` 若发现 `progress.json` 被写脏，还原它（§7.6、§7.7）。

### 6.2 运行态数据与看板库混淆

- **预防**：钩子事件走独立库 `hooks.db`，不写 `board.db`（§8.2）；执行运行态在 `task_run` 侧表 + `runs/` 落数据目录，不进仓库（§12.2）。
  新增侧表/键位一律不升 `user_version`，且要断言它没变（`tst_databasemigration`）。

### 6.3 自动更新静默安装「悄悄放弃」

- **现象**：升级后注册表版本没变，安装器却没报错。
- **根因**：残留的 `NeonBoardHost.exe` 进程占着目标文件。
- **解法 / 预防**：升级前先清掉残留 Host 进程；链路验证用 `--update-apply`，可配 `--update-source` 指向本地清单（`docs/auto-update.md`）。

### 6.4 启动期 `TaskModel::load()` 会被调两次，count 从 0 跳到实际条数

- **现象**：空数据目录首启的日志里有两行 `TaskModel: loaded`：`loaded 0 tasks` → 426 ms 后
  `loaded 139 tasks`。任何「只看一眼 count 就下结论」的判定都可能读到中间那个 0。
- **根因**：第一次来自 `src/main.cpp` 的初始加载；第二次是 `RoadmapSync::importFrom()` 末尾
  的 `m_tasks->load()`（`src/board/RoadmapSync.cpp:323`）—— 路线图快照导进库之后要整体重建一次模型。
  两次都走 `beginResetModel()` / `endResetModel()`，是完整的 reset，不是增量。
- **实测时序**：`loaded 0`（.656）→ 导入 139 项（.085 `应用 139、跳过 0`）→ `BOOT engine`（2.188）
  → `BOOT loaded`（3.375）→ `ONBOARD skip tasks 139`。QML 加载比导入晚 1.3 秒，所以正常的
  启动路径踩不到那个 0；但只要有人把判定提前、或 QML 预热变快，就会踩。
- **解法 / 预防**（R13-01）：把依赖 count 的判定做成**可重算**的派生状态，挂在
  `taskStore.onCountsChanged` 上，不要只在 `Component.onCompleted` 里算一次。
  纯函数判定（`UiSettings::shouldOnboard`）不许加缓存 —— `tst_uisettings` 有专门一条守着。
- **教训**：**「启动瞬间读到的 count」不是事实，是快照。** 凡是要拿它做判断的地方，
  都得回答一个问题：数据后来变了，这个判断会跟着变吗？

---

## 七、尚无解决方案（需人拍板）

> 本节只留**还没有落地方案**的条目。已交付的迁进对应分类并标提交号：
> 原 7.1 的漂移检查与代登记（1.14）、原 7.2 未推送成果警告（1.15）、原 7.3 root 占用让路（1.16）、
> 原 7.7 产物落点变量（1.17）、代登记位置死锁（1.18）；现 7.1 的结卡权亦已解决 ——
> 结卡改为合入即自动结（DX-07），条目保留在本节仅因为它是本轮决策的记录，不再有「等谁拍板」的成分。

### 7.1 结卡权：已解决 —— 合入 `#短号=done` 即自动结（DX-07）

- **曾经的形态**：`baseline` 靠「维护者批量结」。2026-10-28 补过一次（`ac3332c`，见 1.14），
  但**根因没动**：只要还靠人记得结，交付一批就漂一批。2026-10-29 又漂了 ——
  `DX-03` / `DX-05` / `DX-06` 三张卡已按 `#<短号>=done` 交付并合入 main，baseline 仍停在
  `todo`；`R5-06` 甚至没有 baseline 条目。每漂一次，**已交付的卡就能被再领一遍重做**。
- **为什么这不是「数据脏了」而是机制缺陷**：`baseline=<短号> done` 是「拒绝重新领用」的
  唯一门（条款 6）。人记性的可靠性不构成一道门，所以它必然反复失效。
- **解法（DX-07）**：`git-done.ps1` 合并推送成功后调 `derive_board_status.py --close-cards`，
  从 git 事实取最后一次 `#<短号>=` 标记，把 baseline 补成 `done`（缺条目的卡按标记补
  `done` / `todo`），作为独立的 `chore(board): 结卡 <短号>` 提交快进补推；失败只回滚该提交、
  不阻断已完成的合并。**agent 手改 baseline 仍被拒** —— 自动结只由收尾步骤做，提交体注明
  「自动来源」，「谁改」与「怎么改」分开管。
- **口径变化**：`--baseline-check` 从「只报不改的待决项」降为**回归探针**：
  漂移非零 = 那次结卡提交没成功（离线 / 撞车 / 脚本失败），CI 打警告但不阻断 job。
  自测 `derive_board_status.py --selftest` 6 例（有标记则结 / 无标记不动 / 已结不重复 /
  缺条目补 todo 而非 done / 幂等 / 不擅自重开），CI 每次跑。
- **顺带修掉一个真死锁**：DX-06 把「代登记只能在锚点区」写进闸门，但锚点区停在 `main`、
  `pre-commit` 规则 1 禁止在 `main` 提交、§7.1 又禁止 agent 在锚点区切分支 —— 三条叠加后
  代登记**没有任何合法执行位置**（DX-07 实测连拒三次）。改为**凭内容而非位置**：任何检出里
  只要能给出 `chore(board)` + `授权：` + `批次：` 且不混代码，即可代登记。

### 7.2 发版类提交在锚点区分叉：已解决 —— 闸门 2 报错给出出路（DX-48）

- **现状（2026-10-28 12:56 复核）**：锚点 `main` 与 `origin/main` 已无分叉——`git rev-list --count origin/main..main`
  为 0，`git merge --ff-only origin/main` 直接成功；此前那份本地独有的 `219b64a` 已不可解析
  （`git cat-file -t` 报 `Not a valid object name`）。线上 v0.1.3 release asset 的 `update.json` 与
  `origin/main` 那份一致，不存在「一份发布、两份清单」的残留。
- **为什么曾经发生**：发版/版本号提交天然发生在根目录（维护者在那儿跑 `release.ps1`），
  而宪章条款 1/5 又禁止在 `main` 上直接提交；`Sync-AnchorWorktree` 只做快进，遇到分叉立即放弃
  （`fatal: Not possible to fast-forward`），于是锚点越漂越远。
- **识别**：`git log --oneline origin/main..main` 有输出即分叉；`git rev-list --count origin/main..main` 看本地独有几条。
- **解法（DX-48，2026-11-05 落地）**：分叉不再只被「拒绝」——闸门 2 的报错直接给出出路：
  版本 bump / 清单同步先落独立分支（`build/version-bump`），经 `git-done` 合回主线后重跑
  `release.ps1`；完整步骤固化进 `docs/git-workflow.md` §三.5。属报错指引而非硬拦截：
  直接在锚点 `main` 提交仍可能发生，但制造分叉的人会当场拿到正确路径，而不是一句
  「git fetch && git rebase」的模糊建议。
- **处置记录**：2026-10-28 分叉确认消失（未做历史改写）；2026-11-05 DX-48 落地出路指引。

### 7.3 codex CLI 已实测参数面，但仍不达内置标准

- **现状**：内置 profile 只有 opencode / claude / cursor-agent（§12.1 禁止内置未实测 profile）。
  2026-10-28 用 codex-cli 0.163.0 做了实测，参数面与只读沙箱已摸清并记进
  [`agent-profile.md`](agent-profile.md)（含 `exec` 必须重定向 stdin、`--json` 事件流形态、
  `-s read-only` 确实拦住写文件）；但**两次 `exec` 都在超时窗口内没跑完一个回合**（退出码 124）。
- **卡在哪**：「起得来、只读、能出结果」三条只验到前两条。内置要改 `src/agent/AgentProfile.cpp`，
  属代码改动 → **需要一张新卡片**（§条款 6），不能顺手写进生产 profile。
- **需要谁拍板**：是开一张卡专做 codex 内置（含超时上限与一次真实回合验证），
  还是继续让「用户在 设置 → 任务执行 手工新增 profile」这条通道兜着。

### 7.4 执行面板截图仍需 GUI 手工补拍

- **现状**：R6-06 遗留 `docs/screenshots/exec-panel.png` 未出（自动化验收项不含截图）。
- **已做**：唯一引用它的 `docs/r6-dispatcher-plan.md` 已改指已有截图（`task-exec-tab.png` /
  `exec-confirm.png` / `exec-settings.png`），文档里不再挂不存在的图。
- **还卡在哪**：真正的「执行中面板」整屏截图要在沙箱里跑一次执行流程由人拍；
  补拍后按 §五 第 6 条同步 README 功能项——README 目前并未引用这张图，所以不补也不产生坏链，
  要不要补属产品取舍，等人定。

### 7.5 外部工具的产物落点没有治本约定

- **现状**：`.gitignore` 逐个点名拦住（`.workbuddy/`、`.qoder-credits/`），agent 侧已有显式落点
  `NB_SESSION_ARTIFACT_DIR`（1.17），但第三方工具不认这个变量。
- **卡在哪**：「会话产物一律写到仓库外的显式目录」这条约定要落地，得先定**由谁注入**
  （`.agent-env.ps1`？工具自身配置？）以及**只认默认路径的工具能不能挪**。没人拍板之前，
  只能维持「发现一个、点名一个」，名单本身就是我方的一项负债。

### 7.6 R0-11 冷启动：指标与减法已落地，治本改动不在卡 scope 内

- **现状**（2026-10-30 诊断定案）：启动 `startup=6010ms / cpu=702ms`，分段 `app 15 → engine 310 →
  loaded 5316`——瓶颈是 portable 包带 7 套 QML 样式 + 35 个 Qt DLL 的冷 I/O，不是 QML 树构建。
  卡 scope 内能做的已做完：`Metrics` 增 `startupCpuMs`、splash 五处 330ms→160ms 总预算
  （`NEONBOARD_SPLASH_BUDGET` 可覆盖，预算用尽仍保证进度条重绘）。
- **真正解法在哪**：`scripts/deploy.ps1` 只随包 Basic 样式 + CMake 改 DELAYLOAD 链接，预计能砍掉
  大头冷加载。但这是**部署行为改动**，不在 R0-11 声明的 scope（`src/core/Metrics.*` / `src/main.cpp`）内，
  不能顺手做。
- **需要谁拍板**：扩 R0-11 的 scope（`scope`/`acceptance` 改动按 1.19 要单独 `chore(board)` + 授权行），
  还是另立一张卡。拍板前该卡保持未结，由认领会话继续持有。

---

## 八、同专题的既有文档

本文不重复展开这些，遇到对应场景请直接翻：

| 文档 | 管什么 |
| --- | --- |
| [`git-workflow.md`](git-workflow.md) | 分支工作流、钩子拦截与排查（§四）、流程类 FAQ（§六） |
| [`board-sync.md`](board-sync.md) | 看板状态派生与一致性，含常见问题（§六） |
| [`testing.md`](testing.md) | 测试与验收口径 |
| [`auto-update.md`](auto-update.md) | 升级链路与 `update.json` 清单 |
| [`adr/`](adr/README.md) | 架构决策记录（运行态与执行器安全边界见 ADR-0002） |
- **实测（2026-11-06，DX-96 交付演练）**：用仅对 `derive_board_status.py` 失效的
  python 垫片（PATH 注入 `python.cmd`，按参数命中即 exit 9，其余原样转发）注入
  结卡/快照失败后真跑 git-done：合并照常推送（fail-soft ✓）、两步只告警不阻断、
  EXIT 0、无收尾补推；main tip 因「标记 done / baseline todo」报红一次（眼睛 ✓），
  按提示补跑收尾即转绿。注入走运行时 PATH 而非改文件 —— git-done 拒脏树，
  改源码注入根本进不了收尾。
- **实测（2026-11-06，DX-106 交付当日三次撞快照冲突）**：合并收尾连续三次在
  `progress.json` 上撞冲突，每次都要手工五步善后（`merge --abort` → 切回分支 → 合主线 →
  `--ours` 取版 → `derive --write` 重算 → 提交），步骤全靠人记、无任何脚本兜底 —— 第三次
  甚至发生在「为这个问题立卡」的合并收尾里。**解法（DX-108）**：git-done 合并冲突路径加
  自动善后 —— 冲突文件**仅** `progress.json` 时取主线版完成合并、收尾派生器整体重算；
  含其它文件（尤其台账）不进通道，仍走人工。两个口径要说清：
  - **这不是 §7.5 禁止的 `--ours` 盲解**：盲解指「拿一侧内容当最终答案」；这里取来的内容
    只是让合并落地的占位，收尾的 `Sync-BoardSnapshot`（derive --write）必然按 git 事实
    整体重算 —— 内容的最终来源是派生器，不是 `--ours`。台账 `docs/board-ledger.json`
    不享受同等待遇：它承载任务定义（加卡/改契约要授权），不是纯派生物，取单侧会静默
    丢掉另一边的立卡，含它的冲突必须人工。
  - **领用占位的教训（同日实测）**：agent-init 报「服务端占位 sha 不符」时，先 `ls-remote`
    核对 sha 再动占位 —— 本次误判成「自己上次运行的孤儿」，把别人（agent-dx107）刚领的
    `refs/claims/DX-107` 删了，靠本地镜像 ref 重推才恢复。占位是双领防护的唯一事实源，
    释放动作前先证明「这确实是我的」，证不出来就不动。
- **实测（2026-11-06，DX-106 交付演练，同技巧复用）**：同一垫片注入后跑
  `verify.ps1 -GitDoneDryRun`（派生器 exit 9，结卡与快照两步只告警）：
  合并落本地 → 收尾两步告警跳过 → 合并+收尾单次推送桩成功 → EXIT 0、现场回滚干净。
  无垫片的常规 `-GitDoneDryRun` 同轮通过：合并+收尾一次桩推送、结单对账、归位最后。
  另记一个演练自身的坑（**已由 DX-107 修复，见 2.27**）：旧版 dry-run 撞合并冲突时
  **不会回滚现场**（冲突退出路径直接 exit 1，留下 merge/<...> 临时分支与未合并状态）。
  现在冲突退出会 abort 合并、删临时分支、切回进入时检出，演练后现场即干净；
  垫片留档：`.sandbox/artifacts/shim/python.cmd`。
