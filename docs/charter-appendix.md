# NeonBoard 宪章细节附录

> 本文件收录 [`AGENTS.md`](../AGENTS.md) 各条款的背景、历史与机制细节。
> 宪章本体只留规范条款以控制注入体积；**规范以宪章为准，这里解释「为什么、怎么来的、边界在哪」**。
> 章节标题与宪章编号一一对应；与 `docs/pitfalls.md` 的分工：pitfalls 记「现象/根因/识别」，
> 本附录记「宪章条款背后的机制设计与历史决策」。

---

## 「一」构建与环境

### DX-12 执行策略

> 仓库里所有示例都写 `pwsh -File ...`（PowerShell 7，Windows 默认策略 RemoteSigned，
> 本地脚本直接可跑）。若机器上只有 Windows PowerShell 5.1（默认 **Restricted**），
> `& .\scripts\xxx.ps1` 与 dot-source 会被**静默拒绝** —— 表现为**没有任何输出 + 退出码 1**，
> 很容易误判成「脚本坏了」。这时改用
> `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\xxx.ps1`，
> 或一次性 `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`（只影响当前用户，不需要管理员）。
> 判据与更多现象见 `docs/pitfalls.md` §2.11。

### DX-16 构建耗时与编译缓存

> `build.ps1` 结尾会打印「配置 / 编译 / 合计」三段耗时与 sccache 命中率 —— 怀疑变慢时先看
> 这三个数字，别靠体感。新 worktree 首次全量构建约 **20~30 分钟**（零缓存），这不是卡死；
> 纯脚本 / 文档改动**不必全量**，跑 `pwsh -File scripts/verify.ps1 -ScriptOnly` 约 2 秒。
> sccache 由 `CMakeLists.txt` 的 `find_program` 自动挂 launcher，但 winget 装的它会落在
> PATH 之外（改了用户级 PATH，已跑的进程读不到），`build.ps1` 已自行探测并补齐；
> 判据见 `docs/pitfalls.md` §2.12。

### 等价手敲构建命令

默认走 Ninja（由 `build.ps1` 补 `-G Ninja` 并装配 MSVC 环境）；`-Generator VS` 时等价于：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
```

---

## 「条款 4」手工等价步骤

`git-done.ps1` 不可用时的手工合并流程（推送 main 必须快进，禁止强推）：

```powershell
git fetch origin
git switch -c merge/<简述> origin/main
git merge --no-ff feature/<简述>     # 必须 --no-ff，保留分支合并痕迹
git push origin HEAD:main             # 快进推送，禁止 --force
git branch -d feature/<简述>
```

> 禁止 fast-forward 合并（`--ff-only`），否则主分支历史会退化成分支线性堆积，
> 事后无法追溯「哪次改动来自哪个分支」。

---

## 「条款 6」立卡、发号与审计机制

### 代登记与发号（DX-98 / DX-99 / DX-74 / DX-96 / DX-105）

> **例外：维护者可显式授权代登记。** 授权必须由维护者在会话中对**具体批次**明确下达。
> 工具（`scripts/register_board_cards.py`）负责：五段式表单校验、git 通用目录**跨 worktree
> 锁内发号**（`<stage>-<max+1>`；手工「扫描时点 max+1」无预占机制，多会话并发必撞号 ——
> DX-74/96 实测）、生成含卡片规范 JSON sha256 的**回执**
> `docs/card-requests/registered/<短号>.json`、写盘后 `derive --write`/`--check` 自校验。
> 发号含**服务端占位**（DX-99）：候选号先推 `refs/registrations/<短号>` —— 新建 ref 的推送
> 在 origin 是原子 test-and-set，谁先推成谁占号，被拒即刷新占用集换号重试；占位永久留痕
> 即机器可校验的发号审计链，跨机并发窗口缩到一次原子推送（离线需显式 `--offline`，
> 号为暂定须重验）。
> 发号强度按项目协作形态可配（DX-105）：台账顶层 `numbering.mode` —— `remote`
> （默认，服务端占位）/ `local`（仅本地锁+台账，零网络，适合单机/离线项目，跨机并发不受保护）；
> CLI `--mode` 单次覆盖。改它属任务定义，必须 chore(board)+审计行。

### 策略授权立卡（无人在场的定时/自动任务）

> 授权可预先固化为台账顶层 `authorization.policies` 里的策略对象（`id` / `enabled` / `tierMax` /
> `stage` / `scopeGlob` / `titlePattern` / `maxCardsPerDay`），由维护者以 chore(board)+授权行登记；
> 之后 `register_board_cards.py --policy <id> --form <单> --trigger <run标识>` 走与显式代登记
> 完全相同的发号链，仅回执 `authorizedBy` 写 `policy:<id>@<策略sha256前8位>`。
> 任一字段超界、策略停用/缺失、当日超熔断上限一律拒立（fail-closed，exit 2）；
> `task_gate` 对 `policy:` 回执复核策略内容 sha 与边界，篡改即拒。新增/修改/停用策略本身属
> 任务定义，必须单独 `chore(board):` 提交 + 授权/批次两行；agent 不得改策略。
> 设计与 Schema 详见 `docs/proposal-auto-authorization-policy.md`。

### 回执校验与排位档位（DX-123）

> **手工改台账新增卡一律被拒**：`task_gate` 在 commit-msg 阶段核对回执，
> 无回执 / `card` 不符 / sha256 不符 / 缺 `authorizedBy` 即拒。
> **立卡必须声明排位档位**：请求单 frontmatter 写 `tier: P0..P4`，缺省或非法枚举立卡直接被拒；
> 派生器按档位给新卡算 `orderKey` 落点，`derive --check` 校验档位随 orderKey 单调。
> `tier` 属任务定义 —— 改既有卡的档位必须单独 `chore(board):` 提交，body 带「授权：」「批次：」
> 两行。规则与五档含义见 `docs/task-card.md`「排位如何生效」。
> 代登记只允许追加卡片与对应的 `baseline`，不得改既有卡的 `id` / `title` / `stage`；
> agent 不得以授权为名自行加卡，也不得通过临时 worktree、修改判定脚本或 `--no-verify` 绕过。
> 提交拆两段（工具运行完会打印完整指引）：`docs(board): 收录 <短号> 立卡请求单`（表单；
> 陌生 stage 时同带 §9 里程碑行）+ `chore(board): 立卡 <短号>`（台账+快照+回执，
> body 带 授权/批次 两行）。

### 陌生 stage 的路线图行由程序落盘（DX-110）

> `stage` 首次出现时，`register_board_cards.py` 自己把 `docs/iteration-roadmap.md` §9 的
> 里程碑行写进工作区，并列入立卡流程第一段 `docs(board)` 提交（`docs/` 是任务闸门的杂务豁免路径）。
> 立卡会话**不得手工往锚点工作区补行** —— 锚点 `pre-commit` 禁 `main` 提交，那行永远入不了库，
> 会把 `git-done` 的锚点快进永久卡死（本地 `main` 落后 `origin/main`）。
> 第二段 `chore(board): 立卡 <短号>` 仍只带台账 + 快照 + 回执：把 roadmap 行塞进去会撞
> DX-51「改任务定义的提交不得夹带流程路径之外的文件」，连杂务白名单路径也算夹带。

### 判定凭「内容」而非「位置」（DX-06 / DX-07 / DX-10）

> 只要一次提交改了台账的**任务定义**（新增/删除卡、改 `id`/`title`/`stage`、改 `scope`/`acceptance`、
> 改 `baseline`），无论它来自哪个检出，都过 `scripts/task_gate.py message` 的三道内容校验，缺一即拒：
>
> 1. 类型必须是 `chore(board)` —— 用 `feat` / `docs` 等语义夹带走卡片会被拒；
> 2. body 必须同时有 `授权：` 与 `批次：` 两行（各自后面写授权来源 / 本批卡号）；
> 3. 不得同时改动代码路径 —— 立卡与实现分开提交，否则事后分不清「哪条被授权、哪条是实现」。
>
> **`scope` / `acceptance` 属任务定义**（DX-10）：`scope` 决定 agent 被允许改哪些路径，
> `acceptance` 决定怎样算做完 —— 放任它们不进审计，等于留着一条「自我扩权、自我放宽验收且不留痕」
> 的后门（R10-02 立卡漏写三个必需路径，agent 两次自补 `scope` 做完，无人可追溯）。
> 因此改这两项与加卡同权。比对时忽略顺序与首尾空白（纯排版整理不触发）；
> 自动结卡只改 `baseline`，**不给这条通道扩权**。
>
> 只改 `claims` 或卡片 `note` 不算代登记，无需凭证。判定逻辑只有一处实现
> （`scripts/task_gate.py` 的 `audit_problems` + `receipt_problems`），自带 32 例
> （含自动结卡通道、不扩权与回执三态）的可执行自测 `python scripts/task_gate.py selftest`，CI 每次都跑。
>
> **为什么不再限定「只能在锚点区」**：锚点区检出停在 `main`、`pre-commit` 规则 1 禁止在 `main`
> 提交、§7.1 又禁止 agent 在锚点区切分支 —— 三条叠加后，「只能在锚点区代登记」等于
> **没有任何合法执行位置**（DX-07 实测被三道闸门连续拒）。位置不是凭证，提交信息里的授权行才是；
> `cmd_ledger`（`pre-commit` 阶段，手里没有提交信息）只负责提前告知要写什么，
> 真判定留给 `cmd_message`（`commit-msg` 阶段）。

### 交付说明（note）的提交与验收档位（DX-113）

> 交付说明单独一条 `docs(board): 补 <短号> 交付说明` 提交，与实现分开；`check_board_diff`
> 对只改 `note` 的台账判 derive 档、不再要求全量 —— 轻档（`-ScriptOnly`）验收在含交付说明提交的
> **最终分支态**成立，卡片验收项按最终态理解，不存在「实现态绿、最终态红」的挑时态空间
> （口径详见 `docs/task-card.md` §交付说明）。

### 重开判断只认 origin/main（`bd1bca7`）

> 「`baseline=<短号> done` 拒绝重新领用」这道门**只看 `origin/main` 那份台账**
> （`git show origin/main:docs/board-ledger.json`）。锚点/工作区副本与主线分叉时，本地
> `baseline` 是旧的，按本地判会把已交付的卡当成可领再重做一遍；取不到 `origin/main`
> （离线 / 无该引用）才退回工作区副本。

### 结卡自动化的背景（DX-07）

> `baseline=<短号> done` 是「拒绝重新领用」的唯一门，过去靠维护者记得批量结 —— 一定漂，
> 每漂一次已交付的卡就能被再领一遍重做（`docs/pitfalls.md` 1.14 踩过，宪章 7.1 曾记作「待拍板」）。
> 现在**合入即结**：`git-done.ps1` 在合并推送成功后调 `derive_board_status.py --close-cards`，
> 从 git 事实（最后一次 `#<短号>=` 标记）把 baseline 补成 `done`（缺 baseline 条目的卡按标记补
> `done` / `todo`），作为独立的 `chore(board): 结卡 <短号>` 提交快进补推；提交或补推失败只回滚
> 该提交，不阻断已完成的合并。
>
> 「自动来源」是**审计闸门认可的自动通道**（DX-07 补）：它没有「谁授权」可写（结卡是 git 事实的
> 机械投影），所以不要求 `授权：` / `批次：`；但**只对「仅改 baseline」生效** —— 一旦夹带新增/删除
> 卡片或改 `id`/`title`/`stage`，仍按人工代登记要授权行，通道不扩权。
> `derive_board_status.py --baseline-check` 是**回归探针**：漂移非零只说明那次结卡提交没成功
> （离线 / 撞车 / 脚本失败），CI 打警告但不阻断 job。
> 自测：`python scripts/derive_board_status.py --selftest`（有标记则结 / 无标记不动 / 已结不重复 /
> 缺条目补 todo 而非 done / 幂等 / 不擅自重开），CI 每次跑。

---

## 「三」钩子安装背景

> **`core.hooksPath` 是仓库级配置，不随 clone 传递。** 新 clone 一份必须先把它装上，
> 否则四道钩子全部静默失效，整份宪章退化成「写在文档里的建议」——
> 这台机器上就踩过：`core.hooksPath` 为空却无人察觉，主分支保护形同虚设。
>
> `scripts/agent-init.ps1` 开区、`scripts/build.ps1` 构建、`.github/workflows/ci.yml`
> 都会自动带上 `setup-git-hooks.ps1`，一般不需要手动执行；加了新机器/新 clone 也只要补一次。
> 钩子文件必须是 **LF 行尾**（CRLF 会让 `sh` 找不到 shebang，表现为「装了但不跑」），
> `.gitattributes` 已固定 `eol=lf`，`setup-git-hooks.ps1` 每次也会顺手体检。

分支前缀允许表的判据细节：

> 唯一事实源是 `.githooks/pre-commit` 的 `allowed_prefixes`：`scripts/agent-init.ps1` 直接读它，
> 在入口就拦掉不合规的分支名（退出码 2，不创建任何目录）。以前要等「领用空提交」被 commit-msg 拒，
> 那时 worktree / `.sandbox` / 占用登记都已建好，只能手工 `git worktree remove --force` +
> `git branch -D` 重来。改前缀规则只改钩子那一处。

---

## 「五」工作宗旨先例与已踩坑

### Z 条款的展开与先例

- **Z3** 先例：`moveTask` 保「插最前」老语义（老插件零依赖契约），追加末尾另开 `moveTaskToEnd`。
- **Z5** 补充：判定规则的第二份副本一旦出现在钩子、脚本或文档里，就意味着漂移的开始；
  校验者可以有一百个，实现和写者只能有一个——两个写者必然撞车，谁先推谁赢，另一个白干。
- **Z6** 补充：散文条款靠人记一定会漂；写得再狠的文档，不如一条退出码。
- **Z7** 补充：习惯性绕行（尤其 `--no-verify`）省下的三秒钟，迟早连本带利还回来。
  闸门拦了正常工作时报给维护者修闸门，而不是每个人各自学会绕闸门。
- **Z8** 补充：凡「拷文件」环节必须拿产物验收——源码树里永远测不出部署缺件
  （`docs/pitfalls.md` §3.14：`agent-bridge` 漏拷意味着整条链路在安装版上从未工作过）。
- **Z9** 补充：这条对机器同样成立——退出码、回执、日志就是代码的汇报，
  推送没落地就不能打印「合并完成」，验收没跑过就不能写 passed。汇报缺一半不丢人，粉饰一半才丢人。
- **Z11** 补充：闸门防的不是某一个 agent，而是「每个 agent 都可以」这件事本身。
- **Z13** 先例：开区半途失败自动回滚、合并冲突中止后收尾待办悬空（DX-107）、
  孤儿 claim 释放判据漏第三形态。恢复路径要走状态转移表，不是异常处理。
- **Z14** 先例：编码自切只治一个入口（DX-60）、`sys.path` 判据只落一个脚本（DX-84）、
  裸 git 取值三连雷。
- **Z15** 先例：契约注入读到锚点过时台账（DX-94）、验收档随提交时态漂移（DX-113）。

### 测试与构建的三个已踩坑（R6 实测）

- **AutoMoc 吃不熟 C++11 原始字符串**：Q_OBJECT 测试文件里写 `R"(...)"`，内容含 `)"` 会提前
  截断（报 missing ')'）；带花括号的 JSON fixture 会让 moc 不生成元代码，症状是链接期
  `metaObject` 未解析（LNK2001），清 autogen 也未必好转。
  **测试数据里的 JSON / 正则一律用转义字符串拼接，别用原始字符串字面量。**
- **改公共头文件签名后报「无法解析的外部符号」**：多半不是签名写错，是旧 `*.obj` / `*_autogen`
  没重编。删对应产物再编（如 `tst_xxx*.obj` 连同其 `*_autogen` 目录），仍走 `scripts/build.ps1`，
  别信链接器第一眼。
- **编辑 `.ps1` 会静默丢 UTF-8 BOM**：已由 `scripts/check_script_hygiene.py` 规则 C 在 CI
  强制拦截（实测同一会话连续丢过两次）。改完 `.ps1` 心里没底就跑一次卫生检查。

---

## 「7.1」锚点快进例外全判据

> `scripts/git-done.ps1` 在合并推送成功后，会对锚点区执行一次 `git merge --ff-only origin/main`，
> 让维护者回到根目录就能看到最新代码。该动作只允许**快进**：锚点检出非 `main`、有本地提交、
> 有未跟踪文件、有受控文件之外的已跟踪改动时一律跳过；跳过原因按「本地提交 / 已跟踪文件脏
> （逐个点名）/ 未跟踪冲突」**三类分开报**（DX-110），不再一律甩一句「本地提交 / 未跟踪文件冲突」
> 误导排查。
>
> **受控文件只有两份**：`progress.json`（应用自动写回的快照）与 `docs/iteration-roadmap.md`
> （立卡程序写进立卡提交前的 §9 里程碑行）。它们允许临时 `git stash` 后还原；**还原撞车时主线
> 版本优先** —— 每次合并收尾都会动这些文件，本地版本必然冲突，此时工作区一律回退到主线版本
> （绝不留冲突标记，应用下次写回会把 `<<<<<<<` 打进快照），本地版本仍完整留在 stash 里并打印
> 其提交号供逐文件取回。工具产物目录与其它任何文件仍跳过不碰 —— 「为了同步」不等于有权替别人
> 处置现场。**但若 `agent-workspace.json` 里 `root` 已被别的会话登记占用，只快进、不动受控文件、
> 不 stash** ——处置权交给占用者，避免把别人的登记踢出去（`03b4424`，坑见 `docs/pitfalls.md` 1.16）。
> 除此之外 agent 仍不得动锚点区。不需要时可加 `-SkipAnchorSync`。

占用登记四件套的完整口径（DX-05 / DX-23）：

- 开区即登记：`agent-init.ps1` 给开发区写 `worktree:<agent>`、锚点区原地模式写 `root`，
  有人占着同一个目标时后来者直接被拒（退出码 3），不必等出事才发现。
- 收尾即释放：`git-done.ps1` 合并成功后按当前检出的 agent 身份自动 `Release`，
  只释放自己那份（身份不符时脚本拒绝），不留僵尸登记。
- 自检即体检：`-Action Check` 一次性核对「锚点区有没有他人 in-place 环境 / 同一分支是否被两个
  检出占用 / 每个 `.worktrees/<名>` 是否各有独立 `.sandbox` / 登记与目录是否对得上」，
  有硬冲突退出码 3；登记缺项、僵尸占用只告警（回收由人拍板，§7.10）。
- 能不能删也要判（DX-23）：`check-worktree-activity.ps1 -Audit` 逐区给三项判据
  —— 未推送提交 / 未提交改动 / 是否 detached，全 0 才标「可删」并附回收命令，
  同时打印占用登记的 `agent` 与 `since`（归属有客观依据）、列出「登记还在目录已删」的僵尸占用。
  只报告不删除，恒退 0。

完整命令：

```powershell
pwsh -File scripts/agent-workspace.ps1 -Action Status
pwsh -File scripts/agent-workspace.ps1 -Action Claim  -Agent agent-c -Scope worktree:agent-c
pwsh -File scripts/agent-workspace.ps1 -Action Claim  -Agent agent-c -Scope root -Task "阶段条修复"
pwsh -File scripts/agent-workspace.ps1 -Action Release -Agent agent-c -Scope root
```

---

## 「7.2 / 7.2.1」领用与认领占位生命周期

### -Card 的投递细节

> 该短号必须在台账 `cards` 里登记过；脚本会写
> `claims[<短号>]={agent,branch,head,claimedAt}` 进**本 worktree** 的台账副本（随 feature 分支提交），
> 并投递 `#<短号>=doing`（`chore(claim):` 空提交 + `--emit-hook-file` 双通道，幂等）。
> `-Card none` 不会获得 claim ref，一旦触及代码路径，`commit-msg` 与 `pre-push` 会直接拒绝。

### 一分支多卡（DX-03）

> 一条分支可以领多张卡（同一模块的顺序改动、一张卡的定义 + 另一张卡的实现）。
> 闸门口径是**每个出现在分支上的短号都必须各自持证** —— 多领合法，缺证照拒。
> - 补领：在分支所在检出里再跑一次 `agent-init.ps1 -InPlace -Card <另一张卡>`。脚本会判
>   「本卡是否已领」，已领即跳过（否则每补领一次都会多出两条同卡号提交）。
> - 提交信息按实际改动带短号；`task_gate.py push` 本来逐提交校验，天然支持多卡。
> - 台账 `claims` 是按短号存的字典，同分支多条登记都指向同一个 `branch`，派生器与
>   `release-claims.py` 已按短号遍历，无需额外登记。
> - **方向可校验但只告警**：某张卡的实现提交早于它的领用提交 = 先写代码后补领用，
>   `check_claim_refs.py` 会给提示。历史分支确实需要补领，所以不阻断，但新工作请先领后写。
> - 自测：`python scripts/check_claim_refs.py --selftest`（短号提取 / 方向告警 / 多卡口径），CI 每次跑。

### 认领占位 `refs/claims/*` 的七条硬约束

服务端占位是双领防护的唯一事实源。创建 / 校验 / 释放三端均由脚本收口：

- **认领唯一入口**：所有认领必须且只能通过 `scripts/agent-init.ps1 -Card` 完成。**禁止手工
  `git update-ref refs/claims/<短号>` 建本地占位来绕过 origin push** —— 早期部分认领这样做了，
  一度令 origin 无任何 `refs/claims/*`、服务端双领硬门事实上从未生效；现 `agent-init` 已强制
  非强推 `refs/claims/<短号>`（失败即 `exit 3`），origin 上只应存在**活动卡**的占位。
  本地 `refs/claims/*` 由 `git-done.ps1` / `task_gate.py` / `check_claim_refs.py` 执行
  `git fetch origin '+refs/claims/*:refs/claims/*'` 从 origin 镜像回来，历史残留会被反复拉回，
  属孤儿占位，不得作为认领依据。
- **fail-closed 必须联网**：`agent-init` 推 `refs/claims/<短号>` 到 origin 失败时 `sys.exit(3)` 硬阻。
  agent 运行环境**必须具备 origin 写权限**；本地存在 `refs/claims/*` **不构成**有效认领，必须以
  `git ls-remote origin refs/claims/<短号>` 服务端可达为准。离线 / 无写权限时认领直接失败，
  不得降级为「仅本地占位」。
- **worktree 分支必须推 origin**：认领的 `fix/*` / `feature/*` 分支必须推送到 origin —— 不仅为 CI，
  更为 claim ref 指向的 commit object 在 origin 可达；否则 `ls-remote` 引用的提交在 origin 不可达，
  CI 领用闸门（`check_claim_refs.py`）无法校验（`feature/onboarding`（R4-07）从未推 origin 即为一例）。
  `agent-init.ps1` 开区时会扫 `.worktrees/*`，把「有提交不在任何 origin 引用可达范围内」的工作区
  **列名警告**（`03b4424`）——警告只负责让人看见，回收决定仍归人，不自动删（§7.10）。
- **合并后必须释放占位**：`git-done.ps1` 的 `Invoke-ClaimRefCleanup` 在推送成功后按两条判据释放：
  ①占位指向的提交已是 `origin/main` 祖先；②台账 `claims` 中登记的分支本地与远端均已不存在（孤儿）。
  都不满足的活动卡一律保留。离线时只清本地，联网后下次运行补删 origin
  （`git push origin --delete refs/claims/<短号>`）。
- **失效登记不再挡重新认领**：历史缺口是「分支已合并删除、台账 `claims` 条目还留着」，这类残留曾把
  同一张卡的重新领用挡在防双领校验上（R4-02 踩过两次）。现 `agent-init` 在双领校验**之前**判定失效：
  登记分支本地 `refs/heads/*` 与 `refs/remotes/origin/*` 都不存在、且服务端 `refs/claims/<短号>`
  已释放（`ls-remote --exit-code` 退出码 2），才算失效；远端不可达时保守视为仍被占用。
  失效项由本次领用提交顺带清理。
- **本地孤儿 ref 清理**：任何「本地有 `refs/claims/<短号>` 但 `git ls-remote origin` 无对应 ref」
  的状态视为异常孤儿，应 `git update-ref -d` 删除本地项，并不得在本地保留为占位依据。
- **台账过期登记一并清理**：`git-done.ps1` 删掉认领分支后调用 `scripts/release-claims.py` 全量扫描
  台账 `claims`，只删「认领分支本地与远端都不存在」的条目（活动卡一律保留），作为独立的
  `chore(board): 清理过期领用登记 <短号>` 提交快进补推；提交或补推失败只回滚该提交、不阻断已完成的
  合并。此前依赖 `claim-card.py`「下次任何人领卡时顺带清理」——顺带不保证发生，R6 收官后
  `--check` 一直挂着「分支已不存在」噪声即为此。

### 收尾自动提交的时序约束（DX-26）

> `git-done.ps1` 合并后的「清理过期领用登记 / 结卡 / 刷快照」三步都是 commit，而 `pre-commit`
> 规则 1 禁止在 `main` 直接提交 —— 锚点区收尾时恰好停在 `main` 上。所以三步之前**先无条件
> `switch --detach`**，「切回 `main` + `merge --ff-only`」的归位排在**所有 commit 之后**。
> 把归位提前会让三步全被自家钩子拒掉，且失败回滚若只 `checkout --`（不撤暂存区）会留 staged
> 脏文件（pitfalls 2.15）。

### `-InPlace` 的合法前提（DX-05 硬门）

> 锚点区（`git-dir` 等于 `common-dir`）传 `-InPlace`，`agent-init.ps1` 直接拒绝并 `exit 3` ——
> 在锚点区原地开区等于让两个会话共享 HEAD 与 `.sandbox`（2026-10-28 凌晨的现场：
> `.worktrees/` 一个都没建，双方都在根目录提交）。确需在锚点动手必须显式 `-ForceAnchor`，
> 它会顺带登记 `root` 占用；检出里已留下**他人**的 `.agent-env.ps1`（`NEONBOARD_AGENT_ID` ≠ 当前
> agent）时同样默认拒绝，同一个开关才可接管。WorkBuddy / 手工 worktree 的自动原地模式不受影响：
> 它们本来就是 linked worktree。

---

## 「7.3」git-done 内部流程细节

> 脚本内部流程：`git fetch origin` → 基于 `origin/main` 建临时分支 `merge/<slug>` →
> `git merge --no-ff <分支>`，清理登记 / 结卡 / 快照全部本地提交后，对 main **只推一次**
> （DX-106 起，快进推送，不是强推；推送收口在 `Push-Main` 单一实现）。撞 non-ff 时变基重试，
> 仍失败则整体回滚收尾提交、**单独推送合并**（fail-soft 交付不被收尾拖住）。
> 合并撞冲突时（DX-108 起）：冲突文件仅为 `progress.json`（纯派生物）时**自动善后**——取主线版
> 完成合并，收尾派生器整体重算，不要求人工介入；含其它文件（尤其台账，它承载任务定义）不进自动
> 通道，现场自动回滚（DX-107）后转人工。
> 合并前还会跑**领用闸门**：分支必须带服务端 `refs/claims/<短号>` 占位，否则拒绝合并；
> 纯文档/杂务分支按 diff 路径自动豁免（`scripts/task_gate.py scope` 判定为 `exempt`）。
> `-SkipClaimCheck` 已废弃，保留参数只为兼容旧脚本，**不再能放行任何代码改动**。

脚本定位仓库按自身路径的历史：在锚点区调用 agent worktree 里的脚本，会把锚点误当成本仓库去
合并 / 出包（已踩过）；涉及 `git-done.ps1` / `release.ps1` / `dev-run.ps1` / `agent-init.ps1`。
原生命令 stderr 问题：MSBuild / ISCC 都往 stderr 写进度，在 `ErrorActionPreference = 'Stop'` 下
会被当成终止性错误（已踩过），统一走带日志的调用封装。

---

## 「7.5」rerere 配置背景

> 与 `core.hooksPath` 同构：仓库级、不随 clone 传递、漏了静默失效。所以**不要靠每个 agent
> 手工开** —— 已并入 `scripts/setup-git-hooks.ps1`（开区 / 构建 / CI 都会跑它），漏配会被自动
> 补上。只写在文档里必然漂（R6 全程没人开、直到收官才发现）。

---

## 「7.6」沙箱配套判据细节

沙箱环境变量逐项作用：

| 环境变量 | 作用 |
| --- | --- |
| `NEONBOARD_DATA_DIR` | 数据库、插件目录、日志、界面设置（主题/画质/动效）、`hooks.db` 全部落到该目录 |
| `NEONBOARD_INSTANCE` | 单实例本地服务器 key 加后缀，允许多个实例同时跑 |
| `NEONBOARD_ROADMAP` | 路线图快照文件路径（默认从 exe 目录向上探测仓库根的 `progress.json`） |
| `NEONBOARD_ROADMAP_AUTOWRITE` | 设为 `0` 关闭看板 → 快照的自动写回 |
| `NEONBOARD_AGENT_ID` | agent 身份，供工作区占用登记与沙箱实例名使用 |
| `NB_SESSION_ARTIFACT_DIR` | （非 `NEONBOARD_*`，应用不读）会话产物落点，`agent-init.ps1` 开区时建好并导出为 `<worktree>/.sandbox/artifacts/`（`03b4424`） |

> `dev-run.ps1` 会同时设置以上变量并把沙箱落在当前 worktree 内；未设置时应用行为与原来
> 完全一致，不影响正常用户。

会话产物落点：

> **会话产物只往 `NB_SESSION_ARTIFACT_DIR` 写**：桌面工具的默认行为是写进**调用方 CWD**，
> 一个未登记的未跟踪目录就足以把锚点快进同步长期卡住（`docs/pitfalls.md` 1.1 / 1.17）。
> 第三方工具不认这个变量，发现一个产物目录就点名进 `.gitignore` 一条。

路线图快照写脏的历史：

> 路线图快照默认从 exe 目录向上找仓库根的 `progress.json`，沙箱应用会把沙箱看板状态写回仓库快照
> （已踩过：测试卡进了仓库 `progress.json`）。`dev-run.ps1` 已把它固定到沙箱内，并默认关闭自动写回。

路径唯一入口的原因：

> **不要再在任何脚本里拼一次 `%APPDATA%\com.lin\NeonBoard`** —— 写死之后沙箱运行读不到本次日志
> （验收假失败），没设沙箱时又会去动真实实例正占用的文件（`verify.ps1` 的 `Remove-Item` 就被打断过）。
> 配套两条：
> - `scripts/verify.ps1` 未设 `NEONBOARD_DATA_DIR` 时自开 `<BuildDir>\verify-sandbox`（设了则尊重），
>   并把缺 ctest 的情况自行点源 `dev-env.ps1` —— 「唯一验收入口」不该要求调用方先记住别的；
> - `scripts/check_script_hygiene.py` 在 CI 拦四件事：上述字面量只许出现在
>   `nb-paths.ps1` / `nb_paths.py` 两个文件、`git commit --no-verify` 的豁免面**当前为空**
>   （§11.2 第 7 条）、`.github/workflows/*.yml` 里一个都不许出现它、每个 `.ps1` 必须带 UTF-8 BOM
>   （§4，编辑工具会静默剥掉）。散文条款靠人记一定会漂，所以每条都得有机器判据。

---

## 「7.7」验收分流细节

两档验收的完整检查项：

> 代码类 `verify.ps1`：编译 → ctest → `--selftest` → `--smoke` → 模块守卫 → 看板一致性。
> 脚本/配置类 `verify.ps1 -ScriptOnly`：改动清单自查 → PS 语法解析 → `check_script_hygiene` →
> 工作流 YAML → `check_scope` → `check_path_refs` → 台账分档（约 2 秒，不触发编译）。
> 故意不按卡申报代码的，`-ScriptOnly` 的第一项会拒绝：它先扫改动清单，见代码就要求回全量。
> 细节见 `docs/task-card.md`「两类验收口径」。

台账分流的原因（DX-25）：

> `docs/board-ledger.json` 是 `tst_projectmap` 的测试输入，不是普通文档。只动 `baseline` / `claims`
> 或单纯立卡时，`-ScriptOnly` 会跑 `derive_board_status.py --check`（立卡不拦 —— 否则脚本卡的轻档
> 永远用不上）；改了既有卡的契约、删卡、或新卡带陌生 `stage` 则直接拒绝并要求全量。判定由
> `scripts/check_board_diff.py` 完成，不用记规则。

Qt 路径反推的原因（DX-24）：

> 没给 `-QtRoot` 时 `verify.ps1` 从 `build/CMakeCache.txt` 的 `Qt6_DIR` 反推并补 PATH。
> 以前在 `.worktrees/<agent>` 里必现 `0xC0000135`（缺 Qt6Core.dll，看着像代码坏了），
> 现在编译通过就能直接跑自检。

---

## 「7.8」tag 闸门对偶判据

> 发布靠 tag 触发，所以 `pre-push` 必须放行 `refs/tags/*`（它不是分支，没有「先合并回 main」
> 这回事可言）。但闸门 2 的精神要换成对偶判据留下：**标签指向的提交必须已在 `origin/main` 上**，
> 否则 CI 会拿去打一个主线里不存在的构建。附注标签的 `local_sha` 是 tag 对象，判祖先前要先
> `rev-parse <sha>^{commit}` 剥到提交，否则判据恒假、正常发布也推不上去。
> `workflow_dispatch` 空 tag 只出 artifact、不发布 Release，用于上线前验链路。

---

## 「7.9」预览中心完整约定

| 层 | 位置 | 职责 |
| --- | --- | --- |
| 采集 | `src/PreviewHub` | 解析 `git worktree list --porcelain`，采集分支 / HEAD / 脏状态 / 已构建时间 |
| 构建 | `src/PreviewHub` | 异步调 cmake（configure → build 两阶段），日志进面板；另含串行「出包队列」（build → 复制 exe/示例插件 → windeployqt） |
| 实例 | `src/PreviewHub` | 以沙箱环境变量启动各 worktree 的 exe，登记 pid |
| 展示 | `qml/components/PreviewPanel.qml` | 预览中心面板（只用 `Theme.xxx` 令牌） |
| 命令行 | `scripts/preview.ps1` | `List` / `Build` / `Package` / `Start` / `Stop` 五个动作 |

硬性约束七条：

1. **只有主线实例能当宿主**：设置了 `NEONBOARD_AGENT_ID` 的预览实例**不再扫描 worktree**，
   也不写运行时文件，避免多实例互相踩。预览中心入口也只在主线实例侧栏出现。
2. **预览实例必须沙箱隔离**：启动时注入 `NEONBOARD_DATA_DIR` / `NEONBOARD_INSTANCE` /
   `NEONBOARD_ROADMAP`（落在该 worktree 的 `.sandbox/`）/ `NEONBOARD_ROADMAP_AUTOWRITE=0` /
   `NEONBOARD_AGENT_ID`，**绝不碰真实数据目录与仓库根的 `progress.json`**。
3. **运行时文件用原子写**：`<git-common-dir>/preview-runtime.json` 用 `QSaveFile` 落盘，
   防止主线与预览实例并发写坏 JSON。
4. **脱离启动不能攥着调用方句柄**：`startDetached` 必须配 `QProcess::nullDevice()` 标准流 +
   Windows 下 `setCreateProcessArgumentsModifier` 设 `DETACHED_PROCESS` 且 `inheritHandles=false`，
   否则脚本会因管道等不到 EOF 而挂死（已踩过）。
5. **CLI 结果走 stdout**：exe 是 GUI 子系统，`qInfo()` 只进日志文件；`--preview-*` 必须用
   `emitCliLine()` 写 stdout，脚本才读得到。
6. **多实例可区分**：窗口标题与托盘 tooltip 带 agent 标签（`NeonBoard · agent-c`）。
7. **出包只写 `dist/packages/<agent>/`**：不覆盖锚点的 `dist/NeonBoard/`（避免锁住正在使用的安装目录）；
   多个出包任务必须串行（队列），同一 agent 已在队列中时不得重复入队。

---

## 「九」官方插件打包自检的教训

> R14-02 起 `scripts/deploy.ps1` 拷贝后的对齐自检以仓库 `plugins/` 的实际目录为准（不写死清单，
> 新增插件不用改这里），逐个确认 `dist/plugins/<名字>/plugin.json` 存在，缺一个就 `throw` 并点名
> 缺哪个。漏拷一个目录不会让构建变红，只能靠机器拦；**凡「拷文件」环节都必须用产物本身验收，
> 不能只跑源码树**（`docs/pitfalls.md` §3.14 —— 当年 `agent-bridge` 缺席意味着整条自动建卡链路
> 在安装版上从未工作过，而源码树里永远测不出来）。
> 市场与手工复制仍是**第三方插件**的分发路径，官方插件不再依赖它。

---

## 「十一」快照闭环背景与历史教训

### 为什么代提交前要校正

> 应用写回的是它内存里的状态，可能落后于本分支的 `#code=done` 标记（已踩过：R4-02 已 done，
> 快照却被写回 `doing`，一路推到 main 后靠 CI 的 `derive --write` 才纠正。R14-01 之后 CI 不再
> 纠正了，这条落差只能由 git-done step1 提交前的 `derive_board_status.py --write` 兜住 ——
> 这也是它为什么必须在提交前跑）。派生器只改 `status` 字段、保留应用生成的其它卡片，
> 无实质变化不落盘；它失败时不阻断合并，但必须打警告、不许静默推过期快照 ——
> **别指望 CI 事后补，它只校验**。

### §11.2 第 7 条：豁免面的由来与回收

> `ad5134d` 把 `setup-git-hooks.ps1` 装进 CI 之后，构建 job 的检出停在 `main` 且钩子生效，
> 「自动回写看板状态」那条提交被自家 `pre-commit` 拒掉（10-26 之前 CI 没有钩子，回写一直是好的；
> `065a5e6` 起 main 常红）。当时给这一步开了全仓库唯一的 `git commit --no-verify` 豁免。
> **R14-01（11-03）之后连回写本身都取消了** —— 写权整体收归 `git-done`，`board.yml` 只校验、
> 不回写，「唯一豁免」的豁免对象消失，豁免面随之清空：`.github/workflows/` 下一个
> `--no-verify` 都不许有（`check_script_hygiene.py` 规则 D），脚本里出现同样算越权（规则 B）。
>
> 教训一（仍然成立）：**给 CI 装闸门，必须同时声明 CI 的豁免面。** 钩子与流水线各自都正确，
> 相撞处正是「CI 既是闸门的执行环境、又是闸门的受管对象」这种双重身份；装完钩子不跑一次
> main 验证，闸门就会先在自家流水线上咬人。
> 教训二：**豁免要跟着实现一起回收。** 实现没了、白名单还指着它，等于给下一个想加回去的人
> 留了一道没人解释的绿灯 —— 这比一开始就不开豁免更危险。

### §11.2 第 8 条：红灯判据与双写者历史

> **红灯只可能落在 `main` tip 的那一个 run 上（DX-44；DX-106 起结构化归零）**：git-done 收尾对
> main 只推**一次** —— 合并提交与清理登记/结卡/快照全部本地提交后合成单次推送（快照已在推送前
> 刷新完毕，推送落点天然自洽；撞 non-ff 变基重试，仍失败整体回滚收尾提交、单独推合并）。
> 每批收尾只建 1 个 run，中间态在结构上不存在。`board.yml` 的 `concurrency.cancel-in-progress: true`
> 与 job 首步 ~30s 沉降等待（DX-44）降级为**纯双保险**：单推送下已无中间 run 可言，保留只为防
> 未来重新出现多次推送时回归。**main tip 红 = 收尾那几步真的失败了**（离线 / 撞车 / 脚本失败，
> R14-01 那次三件收尾全部回滚、靠人肉补 `d064c12`），照提示补跑即可；`--check` 对中间态照样返回
> 非零 —— 折叠的是「留不留红灯」，不是判据本身。
>
> 历史包袱（写权为什么要收成一处）：双写者时期两边写同一个派生文件必然撞车 —— 谁先推谁赢、
> 另一个白干，而脚本是 fail-soft 的，失败只留几行「补推失败」；撞车后 `rebase` 还会因为两份
> 内容重叠而 abort。结论：**同一份派生文件永远只能有一个写者**，校验可以有很多个。

---

## 「八」计划钩子完整约定

### 8.1 职责分层

| 层 | 位置 | 职责 | 不允许做 |
| --- | --- | --- | --- |
| 信号产生 | `.githooks/post-commit` | 从提交信息里挑出 `#短号`，投递事件 | 解析规则、直接改任务、阻塞提交 |
| 事件队列 | `src/HookEventQueue` | 落 `hooks.db`，主进程轮询消费 | 执行规则 |
| 规则执行 | `plugins/plan-hook/`（插件 QML） | 解析规则、匹配任务、调用 `moveTaskToEnd` | 直接写 SQL |
| 任务读写 | `src/PluginContext` / `src/TaskModel` | 统一入口，内存与 DB 同步 | 绕过权限校验 |

**规则只有一份，且只存在于插件 QML（`plugins/plan-hook/rules.js`）**。宿主 C++ 不读 `plan.rules`，也不做兜底解析 —— 否则会出现两套实现漂移。

### 8.2 硬性约束

1. `post-commit` 钩子**永远以 0 退出**：找不到 exe、投递失败、参数异常都不得中断提交。
2. 事件队列走**独立库** `hooks.db`（`AppPaths::hookDbPath()`），不写 `board.db` —— 投递不得污染看板库，也不得触发模型重置打断用户拖拽。
3. 规则与插件状态存在 `meta` 表（`plan.rules` / `plugins.disabled`），**不改 `board.db` 的 schema、不升 `user_version`**。
4. 插件调用任务接口一律走 `pluginHost` 门面，权限按实例校验；后台插件用**子 QML 上下文**注入自己的门面，同名 `pluginHost` 遮蔽全局代理。
5. `PluginContext::moveTask` 保持「插到目标列最前」的老语义（老插件零改动），追加末尾一律用新增的 `moveTaskToEnd`。
6. 钩子文件（`.githooks/*`）必须是 **LF + 无 BOM**；改完用 `[System.IO.File]::ReadAllBytes` 校验。
7. 改动 `.githooks/post-commit` 时注意它同时承担**路线图快照同步**（`--sync-roadmap`），不要覆盖掉那部分逻辑。

### 8.3 标记语法（已冻结）

| 写法 | 效果 |
| --- | --- |
| `#T12` | 标题含 `#T12` 的任务转到「进行中」 |
| `#T12=done` | 转到「已办结」 |
| `#T12=todo` | 转回「待办」 |

- 短号**整词匹配**（`#T1` 不得命中 `#T12`）、**大小写不敏感**（统一归一为大写）。
- 短号可带一段连字符小节（`#R3-01`、`#M19` 这类路线图短号同样有效）；路线图快照卡片的 `code` 在导入看板时自动嵌进标题尾、导出时自动剥掉（`RoadmapSync` 负责），嵌号不进快照文件。
- 任务标识来自**标题内嵌短号**，标记形如 `修复统计芯片刷新 #T12`。
- 同一短号命中多个任务时全部流转；已在目标列则跳过写库。

### 8.4 验证口径

- 端到端：`NeonBoard.exe --emit-hook-file <tmp>` 投递 → 主进程消费 → 任务换列；应用未运行时投递则下次启动补投递。
- 注意 `Start-Process -ArgumentList "a b c"` 会按空格拆词，带空格文本必须用 `--emit-hook-file`。
- 钩子报**退出码 127** 表示 exe 起不来（缺 Qt DLL），设 `NEONBOARD_QT_BIN` 指向 Qt `bin` 目录即可。

---

## 「九」插件库与市场完整约定

### 9.1 职责分层

| 层 | 位置 | 职责 | 不允许做 |
| --- | --- | --- | --- |
| 插件发现 | `src/PluginLoader` | 扫 4 个源目录、校验 manifest、去重 | 执行插件代码 |
| 插件编排 | `src/PluginRuntime` | 建上下文、加载后台入口、启停与热重载 | 绕过权限校验 |
| 包格式与落盘 | `src/PluginPackage` | 解析 `.neon`、安全落盘、原子替换 | 跳过路径与摘要校验 |
| 市场通信 | `src/PluginMarket` | 拉索引、下载包、校验 SHA-256、缓存 | 绕过 `PluginPackage` 落盘 |
| 插件库门面 | `src/PluginLibrary` | 合并本地与市场条目、装/卸/启停/更新 | 直接写插件目录以外的地方 |

### 9.2 硬性约束

1. **只写用户安装区**：安装目标固定为 `AppPaths::pluginDir()/<id 末段>`（`%APPDATA%\com.lin\NeonBoard\plugins\`）。`appDir/plugins`、`sourceDir/plugins`、`sourceDir/plugins-samples` 一律**只读**，卸载不得删它们。
2. **四道校验缺一不可**：包格式合法 → `manifest.id` 与请求 id 一致 → SHA-256 与索引一致（索引给了才校验）→ `files` 每个键是安全相对路径（禁绝对路径、反斜杠、`..`，只允许单层子目录）。任何一道不过就整单失败，**不落任何文件**。
3. **原子安装**：先写临时目录（`.neon-stage-XXXXXX` / `.copy-stage-XXXXXX`），再原子替换目标目录；旧目录先改名 `.backup`，替换失败回滚，成功后删 `.backup`。**禁止**直接往目标目录里逐个写文件。
4. **`.neon` 是单文件 JSON**：`{manifest:{...}, files:{相对路径:文件内容}}`；`manifest` 落盘为 `plugin.json`。**不要**引入 zip / minizip / 外部解压命令，保持零第三方依赖与跨平台。
5. **市场源可配置且可多个**：源列表存 `meta` 表 `plugin.market.sources`（JSON 数组），老版本单地址 `plugin.market.source` 自动迁移；内置默认值是**仓库简写**（`PluginMarket::defaultSource()`），用户可在插件库或设置面板改。**改默认源时同步改 README 与本节约定**。
6. **多源并列合并**：源地址支持仓库简写（`owner/repo`，客户端按 Gitee → GitHub 依次尝试）、完整 URL、本地目录；刷新时按源顺序逐个拉取，条目按 `id` 合并、**同 id 取版本高者**，条目带 `marketSource` 标记来源。
7. **每个源各自缓存**：缓存文件 `market-cache.json` 格式为 `{version:2, sources:[{spec,url,index,etag,lastModified,fetchedAt}]}`，**按源分条记录、互不覆盖**；请求带 `If-None-Match` / `If-Modified-Since`，`304` 直接用缓存；某个源拉取失败**只降级这个源**到自己的缓存，不得让插件库整体不可用。
8. **发布侧走脚本**：市场包由 `scripts/publish-market.ps1` 生成（读 `plugin.json` → 打 `.neon` → 算 SHA-256 → 更新 `market.json`）；索引里 `download` 可只写文件名，客户端按索引所在目录解析。索引顶层预留 `signature` 字段位，**本轮不校验签名**，以后接上不改协议。
9. **安装/卸载后必须 `PluginRuntime::reload()`**，并刷新插件库条目，否则界面与实际不一致；运行时另有目录监视自动热插拔（四个源目录 + 各插件子目录，约 400 ms 防抖），显式调用仍是兜底。
10. **官方插件随分发包一起打**（R14-02 起），拷贝与自检细节见本附录「九、官方插件打包自检的教训」。权限按来源分档：`appdir` / `official` 算官方来源（`PluginLoader::isOfficialPluginSource()`），`PluginRuntime::ensureGrantsBootstrapped()` 每次启动补齐其缺失的声明权限，用户显式撤销记在 meta 键 `plugins.grants.revoked` 且不会被补回；`appdata`（第三方）与 `sample`（示例）**仍走显式授权**，不得顺带放行。市场与手工复制仍是**第三方插件**的分发路径，官方插件不再依赖它。

### 9.3 验证口径

- 安装类：`--install-plugin-file <path>`（0 成功 / 2 失败）、`--market-install <id>`。
- 市场类：`--market-source <url>` → `--market-refresh` → `--market-install <id>`。
- **必须覆盖的失败用例**：篡改 `sha256` → 退出码 2 且**包未落盘**；`files` 里含 `../../evil.qml` → 退出码 2 且**没有 evil.qml 落盘**；断网 → `--market-refresh` 退出码 0 且提示离线模式、缓存文件存在。
- 起本地市场用 `python -m http.server`；测断网前先确认 8731 端口没有**残留**服务在监听（`Get-NetTCPConnection -LocalPort 8731 -State Listen`），否则断网用例会假通过。
- 日志断言要取**最后一条**匹配，不要取第一条。
- 多源专项：两个源各放一个同 id 不同版本的插件 → 刷新后条目数应为「去重后的总数」且取高版本；坏源 + 好源同时配 → 刷新仍 exit 0；只留旧的 `plugin.market.source` → 能自动迁移成一个源；`--install-plugin-url` 直链装 + `#sha256=` 校验值错 → exit 2 且不落盘。

---

## 「十」主任务与子任务完整约定

### 10.1 职责分层

| 层 | 位置 | 职责 | 不允许做 |
| --- | --- | --- | --- |
| 存储 | `board.db` 的 `task_link` 表 | 存「边」：`child_id → parent_id` | 往 `task` 表加 `parent_id` 列 |
| 门面 | `src/TaskTree.{h,cpp}` | 父卡 / 子卡查询、完成度统计、防环、增删边 | 在别处直接写 `task_link` |
| 排序 | `src/TaskFilterModel::lessThan` | 按「父卡分组」排，子任务紧跟父卡 | 改 `TaskModel::moveTask` 的既有语义 |
| 展示 | `qml/components/TaskCard.qml` | 归属竖线 + 缩进、`↳ 父卡` chip、父卡进度环 | 写死颜色（只能用 `Theme.xxx` 令牌） |

### 10.2 硬性约束

1. **关联关系只存边表**：`task_link(child_id TEXT PRIMARY KEY, parent_id TEXT NOT NULL, created_at INTEGER)`。**禁止**往 `task` 表加列、**禁止**升 `user_version` —— 老版本程序要能照常打开同一个库。建表语句在 `Database::open()` 里无条件 `CREATE TABLE IF NOT EXISTS`（`IF NOT EXISTS` 幂等，不影响旧库）。
2. **所有关联读写走 `TaskTree` 公开 API**（`setParent` / `clearParent` / `parentId` / `parentTitle` / `childStats`），QML 侧通过上下文属性 `taskTree` 调用；禁止在 QML 或别的类里直接拼 `task_link` 的 SQL。
3. **必须防自环与成环**：`setParent` 要沿父链上溯，发现成环直接返回 `false` 并拒绝落库。
4. **任务删除要清边**：`TaskTree` 订阅 `TaskModel::taskRemoved`，删任务时同步删掉指向它的边和它指向父卡的边。
5. **列内排序按父卡分组**：`TaskFilterModel::lessThan` 先比「父卡 order_key」，再比「是否子卡」（父在前），最后比自身 `order_key`。分组只影响展示顺序，**不重算 `order_key`**，也不改 `TaskModel::moveTask` 的老语义。
6. **`progress.json` 快照不含关联边**：快照只存任务字段，父子关系只存本地 `board.db`；跨机协作需要另行导出关联边，不要假装快照能带过去。
7. **需求卡要带进度口径**：父卡 `note` 里写「已完成 X / 待做 Y」，让进度直接落在需求卡上（配合宪章第五节第 7 条）。

### 10.3 验证口径

- `--selftest` 能开库并建出 `task_link` 表；`NeonBoard.exe --selftest` 退出码 0。
- 建 3 个子卡 → 父卡 `taskTree.childStats(parentId)` 返回 `total=3`，`done` 随子卡状态变化。
- 成环：`setParent(A, B)` 后 `setParent(B, A)` 必须返回 `false`（不落库）。
- 删子卡 → `task_link` 里对应边消失。
- 界面：子卡显示 `↳ 父卡标题` 且左缩进，父卡右下角显示进度环 + `done/total`。

---

## 「十一」职责分层表

| 层 | 位置 | 职责 | 不允许做 |
| --- | --- | --- | --- |
| 事件协议 | `docs/agent-task-event.md` | `agent-task-event v1` envelope 定义（agent / sessionId / items[id,title,status,note]） | 私造第二套格式 |
| 投递 | `src/main.cpp` CLI `--emit-hook-file <f> --hook-event agent.task` | 事件落 `hooks.db`（独立事件名，与 `hook.commit` 隔离） | 复用 `hook.commit`（会误伤 plan-hook） |
| 消费 | `plugins/agent-bridge/`（官方插件） | 解析 payload、维护 `agentbridge.<agent>.<sessionId>.<todoId>` → task uuid 映射、建卡/流转/对账 note | 直接写 SQL、改 `board.db` schema |
| 写回 | `src/RoadmapSync` | 看板侧改动 1.5 s 防抖后写回 `progress.json`（v3） | 手写文件 |
| 代提交 | `scripts/git-done.ps1` step1 | 提交**仅** `progress.json` 的未提交改动（固定信息 `chore(roadmap): 同步进度快照`）；提交前先跑 `derive_board_status.py --write` 把状态对齐到 git 事实 | 连同其它文件一起提交（fail-closed）；把应用写回的过期状态原样推上 main |
| 闸门 | `.githooks/pre-push` 规则 4 | 推 main 时校验快照：version ≥ 3、卡片数不减少、`code` 不丢 | 绕过（`--no-verify`） |

---

## 「十二」任务执行（R6）完整约定

### 12.1 职责分层

| 层 | 位置 | 职责 | 不允许做 |
| --- | --- | --- | --- |
| 执行定义 | `board.db` 的 `task_exec` | goal / acceptance / scope / agent / cwd / workflow / timeout / maxAttempts | 往 `task` 表加列、升 `user_version` |
| 运行态 | `board.db` 的 `task_run` | 每次执行的 run（状态 / 心跳 / 退出码 / 日志 / 越界） | 改看板列、改卡片状态 |
| 执行器 | `src/agent/TaskRunner.*` | 受控启动子进程、流式日志、超时与终止 | 自动 push、自动改列 |
| 验收 | `src/agent/AcceptanceRunner.*` / `src/core/ScopeMatcher.*` | 跑 acceptance、比对 scope | 经过 shell 执行命令 |
| agent 适配 | `src/agent/AgentProfile.*` | 拼命令行与只读参数 | 内置未实测的 profile |

### 12.2 硬性约束

1. **运行态独立**：`task_run` 侧表 + 同卡活跃 run 的部分唯一索引；不升 `user_version`；看板列仍只有 `todo/doing/done`，**程序不自动改列**，验收通过给人点「标为已办结」。决策见 `docs/adr/0002-run-state-and-executor-safety.md`。
2. **单飞行**：同卡最多一个活跃 run，全局默认并发 1；M1 忙时直接拒绝，不排队。
3. **默认关闭**：执行器总开关默认关；关闭时看板行为必须与未引入执行器时完全一致。
4. **只读 triage**：必须用 CLI 原生只读模式；分析前后 `git status --porcelain` 必须一致，prompt 里写「不要改文件」不算约束。输出 schema 见 `docs/agent-triage.md`。
5. **命令不经过 shell**：acceptance 只允许白名单脚本路径，拒绝管道 / 重定向 / 拼接字符；`assert` 由内置检查器执行。
6. **不自动推送**：执行完最多留下本地改动；`push` 永远由人决定。
7. **cwd 白名单**：必须命中已登记目录的绝对路径，首次登记需人勾选确认。
8. **日志脱敏**：token / key / 长十六进制串写入前脱敏；`runs/` 落数据目录，不得进仓库。
9. **定义单源**：开发卡看台账、个人卡看 `task_exec`；只允许台账 → `board.db` 单向导入，反向必须人确认后提交（`docs/adr/0001-task-definition-source.md`）。
10. **快照不受影响**：执行结果不写 `progress.json`；卡片状态仍由 `#短号=状态` 与台账派生（宪章 §11）。

### 12.3 验证口径

- `ctest --test-dir build -C Release -R "tst_taskruns|tst_taskrunner|tst_acceptance" --output-on-failure`；
- 禁用总开关时 `--selftest` / `--smoke` 行为与未引入执行器时一致；
- 任何新增 schema 都必须断言 `PRAGMA user_version` 未变，且 `tst_databasemigration` 通过。
