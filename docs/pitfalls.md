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
- **根因**：CI 每次回写都会动 `progress.json`，本地快照与主线快照必然冲突；失败的 `pop` 会把文件挂成 `UU` 并留下标记。
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

### 1.11 CI「自动回写看板状态」撞本地快照，整轮红

- **现象**：推送合并后 CI 其它步骤全绿，唯独「自动回写看板状态」失败，日志是
  `error: could not apply <sha>... ci(board): 自动回写看板状态` → `rebase 到 origin/main 失败`。
  红的那轮其实什么都没坏——快照内容在 main 上是对的。
- **根因**：旧写法「先本地派生提交，再 rebase 推送」。本地若已跑过 `derive --write` 推过快照提交
  （收官时想让 `--check` 立刻干净，很自然会这么做），CI 生成的就是**内容重叠的重复提交**，
  rebase 必撞内容冲突；旧逻辑直接 abort。另一形态：给 CI 装钩子后回写提交被自家 `pre-commit` 拒
  （§11.2 第 7 条，已修，豁免只留这一处）。
- **解法**：回写循环改为「fetch → rebase（撞车 `--skip` 丢弃重叠提交）→ 以最新主线重新派生 → 提交 → 推送」：
  派生一致则无变化早退，绝不手工解冲突硬拼两份快照（`c8419b8`，合入后首跑即绿）。条款见 §11.2 第 8 条。
- **预防**：`progress.json` 有唯一写者链（应用写回 → 派生校正 → CI 独占回写），本地手工 `derive --write`
  是插队行为——允许做（git-done 代提交本身就带校正），但要预期 CI 会把自己的重复提交跳掉；
  看到「回写步骤红 + could not apply」先按本节判重叠提交，别去解冲突。

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
  Python 侧走 `scripts/claim-card.py` 读同一组环境变量；`verify.ps1` 未设 `NEONBOARD_DATA_DIR` 时自开
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

---

## 五、网络与远程操作

### 5.1 GitHub HTTPS 443 时通时断

- **现象**：同一分钟内 `git fetch` 成功、紧接着 push 报 `Recv failure: Connection was reset` 或连不上 443（rc=128）。
- **解法**：带间隔的重试循环（3–5 次）；分支与本地合并提交不会因此丢。
  需要改走 SSH 时用**一次性环境变量**改写 URL，不动仓库或用户全局 git 配置：
  `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0='url.<ssh-url>.insteadOf' GIT_CONFIG_VALUE_0=<https-url>`。
- **预防**：故障链条与两台加速器互踩的分析见 [`handoff-network-issue.md`](handoff-network-issue.md)；
  凡「只对这台机器成立」的代理细节留本地，不进仓库。

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

### 7.2 发版类提交仍可能在锚点区产生（原分叉已解除，流程未固化）

- **现状（2026-10-28 12:56 复核）**：锚点 `main` 与 `origin/main` 已无分叉——`git rev-list --count origin/main..main`
  为 0，`git merge --ff-only origin/main` 直接成功；此前那份本地独有的 `219b64a` 已不可解析
  （`git cat-file -t` 报 `Not a valid object name`）。线上 v0.1.3 release asset 的 `update.json` 与
  `origin/main` 那份一致，不存在「一份发布、两份清单」的残留。
- **为什么曾经发生**：发版/版本号提交天然发生在根目录（维护者在那儿跑 `release.ps1`），
  而宪章条款 1/5 又禁止在 `main` 上直接提交；`Sync-AnchorWorktree` 只做快进，遇到分叉立即放弃
  （`fatal: Not possible to fast-forward`），于是锚点越漂越远。
- **识别**：`git log --oneline origin/main..main` 有输出即分叉；`git rev-list --count origin/main..main` 看本地独有几条。
- **残留风险**：`release.ps1` 本身不执行 git 提交，版本 bump 与清单同步仍靠人工提交——只要有人在锚点
  直接 `git commit`，就会再次分叉，而同步只会放弃、不会自动收敛。
- **需要谁拍板**：要不要把「版本 bump 与清单同步走独立分支 + `git-done`」写进发布流程，或在 `release.ps1`
  加护栏（检测到锚点区直接提交就报错）。属流程/脚本改造，需开卡。
- **处置记录**：2026-10-28 12:56 复核确认分叉已消失；未做任何历史改写（agent 不得在锚点变基或改写历史）。

### 7.3 codex CLI 已实测参数面，但仍不达内置标准

- **现状**：内置 profile 只有 opencode / claude / cursor-agent（§12.1 禁止内置未实测 profile）。
  2026-10-28 用 codex-cli 0.163.0 做了实测，参数面与只读沙箱已摸清并记进
  [`agent-profile.md`](agent-profile.md)（含 `exec` 必须重定向 stdin、`--json` 事件流形态、
  `-s read-only` 确实拦住写文件）；但**两次 `exec` 都在超时窗口内没跑完一个回合**（退出码 124）。
- **卡在哪**：「起得来、只读、能出结果」三条只验到前两条。内置要改 `src/AgentProfile.cpp`，
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

---

## 八、同专题的既有文档

本文不重复展开这些，遇到对应场景请直接翻：

| 文档 | 管什么 |
| --- | --- |
| [`git-workflow.md`](git-workflow.md) | 分支工作流、钩子拦截与排查（§四）、流程类 FAQ（§六） |
| [`board-sync.md`](board-sync.md) | 看板状态派生与一致性，含常见问题（§六） |
| [`handoff-network-issue.md`](handoff-network-issue.md) | 推送失败 / GitHub 连接问题的完整排查链条与结论 |
| [`handoff-r6-05.md`](handoff-r6-05.md) | R6-05 执行器交付上下文，含「踩过的坑（别再踩）」一节 |
| [`testing.md`](testing.md) | 测试与验收口径 |
| [`auto-update.md`](auto-update.md) | 升级链路与 `update.json` 清单 |
| [`adr/`](adr/README.md) | 架构决策记录（运行态与执行器安全边界见 ADR-0002） |
