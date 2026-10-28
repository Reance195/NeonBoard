# NeonBoard 仓库宪章（AGENTS.md）

> 本文件是 NeonBoard 仓库的最高约束。任何人（人或 AI 代理）在本仓库中的所有改动，
> 都必须先读懂并遵守本文件。违反条款的操作会被 Git 钩子直接拦截。

---

## 一、项目速览

NeonBoard 是赛博霓虹风格的三列个人任务看板。

- 技术栈：Qt 6.8.3（MSVC 2022 x64）+ QML + C++20，本地 SQLite 持久化
- 目录约定：

  | 目录 | 内容 |
  | --- | --- |
  | `src/` | C++ 源码：数据库、模型、托盘、热键、窗口、插件宿主 |
  | `qml/` | QML 界面：主界面、霓虹背景、组件、主题 |
  | `shaders/` | GLSL 霓虹着色器 |
  | `plugins-samples/` | 示例插件（QML） |
  | `installer/` | Inno Setup 打包脚本 |
  | `scripts/` | 构建部署脚本与 Git 助手脚本 |
  | `docs/` | 设计文档、截图、插件 API 文档 |

- 构建命令（推荐走一键脚本，自动探测 CMake 与 Qt）：

  ```powershell
  pwsh -File scripts/build.ps1 -SelfTest
  ```

  新机器 / 新 clone 若提示缺件或报 `rc.exe not found`，先装配环境再构建
  （详见 `docs/git-workflow.md` 第七节）：

  ```powershell
  . ./scripts/dev-env.ps1        # 点号 + 空格：必须 dot-source
  pwsh -File scripts/build.ps1 -SelfTest
  ```

  等价手敲命令：

  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
  cmake --build build --config Release
  ```

- 自检参数：`NeonBoard.exe --selftest`（数据库增删改查自检）、`--smoke`（启动 4 秒输出指标）。

---

## 二、Git 宪章（强制条款，不可绕过）

### 条款 1 —— 改动必须先拉分支

**禁止在 `main`（及 `master` / `develop`）分支上直接修改并提交。**
每一次功能改动、缺陷修复、文档调整，都必须从 `main` 拉出一个独立分支。

```powershell
git switch main
git pull                       # 有远程时先同步
git switch -c feature/<简述>
```

### 条款 2 —— 分支命名必须带类型前缀

允许的前缀：

| 前缀 | 用途 |
| --- | --- |
| `feature/` | 新功能 |
| `fix/` | 缺陷修复 |
| `hotfix/` | 紧急修复 |
| `chore/` | 构建、脚本、依赖等杂务 |
| `docs/` | 纯文档改动 |
| `refactor/` | 重构，不改行为 |
| `perf/` | 性能优化 |
| `test/` | 仅测试相关 |
| `build/` | 构建系统 |
| `ci/` | 流水线 |
| `style/` | 纯格式 |
| `revert/` | 回滚 |
| `workbuddy/` | WorkBuddy 的 Worktree Task 自动生成（不由人命名，见 7.10） |

命名格式：`<前缀>/<简短的英文小写短横线描述>`，例如 `feature/task-tag-filter`。
一次改动只对应一个分支，分支存活期间不要混入无关改动。

### 条款 3 —— 提交信息必须规范

格式：`<type>(<scope>): <中文简述>`

- `type` 必须取自：feat / fix / chore / docs / refactor / perf / test / build / ci / style / revert / merge
- `scope` 可省略，但不得为空括号
- 主题句不超过 50 字
- 合并提交统一用 `merge: 合入 <分支名>`

示例：

```text
feat(board): 增加卡片按标签筛选
fix(model): 修复删除后统计芯片未刷新
docs(plugin): 补充插件权限清单说明
chore(git): 初始化分支工作流与钩子
```

### 条款 4 —— 改动完成后必须合并回主分支

**功能分支上的成果不得长期滞留。** 改完必须合并回 `main`，并删除已合并的本地分支。

推荐用助手脚本一步完成：

```powershell
pwsh -File scripts/git-done.ps1
```

脚本默认走 **remote 模式**：在当前 worktree 里基于 `origin/main` 建临时分支，
`git merge --no-ff` 合并后快进推送 `origin HEAD:main`。
**合并不需要在主 worktree 执行**（详见第七节 7.3），多代理并行时互不干扰。

等价的手工步骤：

```powershell
git fetch origin
git switch -c merge/<简述> origin/main
git merge --no-ff feature/<简述>     # 必须 --no-ff，保留分支合并痕迹
git push origin HEAD:main             # 快进推送，禁止强推
git branch -d feature/<简述>
```

> 禁止 fast-forward 合并（`--ff-only`），否则主分支历史会退化成分支线性堆积，
> 事后无法追溯「哪次改动来自哪个分支」。

### 条款 5 —— 主分支只接受合并提交

`main` 上只允许出现合并提交或文档/构建类的合并提交，不允许直接改代码提交。
主分支推送必须是快进，禁止 `--force`。

### 条款 6 —— 任务先行：没有任务不许改代码

**动代码之前必须先持有任务卡，并完成服务端领用占位。** 顺序不可颠倒：

1. 卡片由维护者在台账 `docs/board-ledger.json` 的 `cards` 里登记。
   **agent 不得自行加卡、改卡的标题/阶段、改 `baseline`** —— 否则就是自授权，
   所有领用闸门都会退化成自证；agent 只能写 `claims` 领用登记与卡片 `note` 交付说明。

   > **例外：维护者可显式授权代登记。** 授权必须由维护者在会话中对**具体批次**明确下达。
   > 代登记只允许追加卡片与对应的 `baseline`，不得改既有卡的 `id` / `title` / `stage`；
   > agent 不得以授权为名自行加卡，也不得通过临时 worktree、修改判定脚本或 `--no-verify` 绕过。
   > 提交信息统一用 `chore(board): ...`，并在 body 记录授权来源与批次。
   >
   > **判定凭「内容」而非「位置」**（DX-06 建立、DX-07 修正）：只要一次提交改了台账的
   > **任务定义**（新增/删除卡、改 `id`/`title`/`stage`、改 `baseline`），无论它来自哪个检出，
   > 都过 `scripts/task_gate.py message` 的三道内容校验，缺一即拒：
   >
   > 1. 类型必须是 `chore(board)` —— 用 `feat` / `docs` 等语义夹带走卡片会被拒；
   > 2. body 必须同时有 `授权：` 与 `批次：` 两行（各自后面写授权来源 / 本批卡号）；
   > 3. 不得同时改动代码路径 —— 立卡与实现分开提交，否则事后分不清「哪条被授权、哪条是实现」。
   >
   > 只改 `claims` 或卡片 `note` 不算代登记，无需凭证。判定逻辑只有一处实现
   > （`scripts/task_gate.py` 的 `audit_problems`），自带允许/拒绝各若干例的可执行自测
   > `python scripts/task_gate.py selftest`，CI 每次都跑。
   >
   > **为什么不再限定「只能在锚点区」**：锚点区检出停在 `main`、`pre-commit` 规则 1 禁止在
   > `main` 提交、§7.1 又禁止 agent 在锚点区切分支 —— 三条叠加后，「只能在锚点区代登记」
   > 等于**没有任何合法执行位置**（DX-07 实测被三道闸门连续拒）。位置不是凭证，提交信息里的
   > 授权行才是；`cmd_ledger`（`pre-commit` 阶段，手里没有提交信息）只负责提前告知要写什么，
   > 真判定留给 `cmd_message`（`commit-msg` 阶段）。

2. 用 `scripts/agent-init.ps1 -Card <短号>` 开区，脚本会推 `refs/claims/<短号>` 完成领用；
   `baseline=<短号> done` 的卡已完成，**拒绝重新领用**。
   > 这道重开门**只看 `origin/main` 那份台账**（`git show origin/main:docs/board-ledger.json`，`bd1bca7`）。
   > 锚点/工作区副本与主线分叉时，本地 `baseline` 是旧的，按本地判会把已交付的卡当成可领再重做一遍；
   > 取不到 `origin/main`（离线 / 无该引用）才退回工作区副本。
3. 提交信息必须带 `#<短号>`：触及代码路径的提交，没有短号直接在 `commit-msg` 被拒。
4. 推送 `main` 时逐个提交复核短号与 claim ref，手搓分支、手工 `merge --no-ff` 后直推同样绕不过。

「杂务分支免领用」不再靠 `-SkipClaimCheck` 自报，而是按 diff 路径自动判定：
改动全部落在 `docs/` `scripts/` `.githooks/` `.github/` 或 `*.md` 才算杂务，其余一律要有卡。
唯一的规则实现是 `scripts/task_gate.py`（`scope` / `message` / `ledger` / `push` 四个子命令），
钩子与 `git-done.ps1` 都调它，**不要在多处重复实现判定逻辑**。

**结卡自动化（DX-07 起）**：`baseline=<短号> done` 是「拒绝重新领用」的唯一门，过去靠维护者
记得批量结 —— 一定漂，每漂一次已交付的卡就能被再领一遍重做（`docs/pitfalls.md` 1.14 踩过，
7.1 曾记作「待拍板」）。现在**合入即结**：`git-done.ps1` 在合并推送成功后调
`derive_board_status.py --close-cards`，从 git 事实（最后一次 `#<短号>=` 标记）把 baseline
补成 `done`（缺 baseline 条目的卡按标记补 `done` / `todo`），作为独立的
`chore(board): 结卡 <短号>` 提交快进补推；提交或补推失败只回滚该提交，不阻断已完成的合并。

- **agent 仍不得手改 `baseline`**：自动结卡只由 `git-done` 的收尾步骤执行（提交体注明「自动来源」），
  agent 手改仍被 `task_gate.py` 拒 —— 「谁改」与「怎么改」是两件事，别混。
- `derive_board_status.py --baseline-check` 从「只报不改的待决项」降级为**回归探针**：
  漂移非零只说明那次结卡提交没成功（离线 / 撞车 / 脚本失败），CI 打警告但不阻断 job。
- 自测：`python scripts/derive_board_status.py --selftest`（有标记则结 / 无标记不动 /
  已结不重复 / 缺条目补 todo 而非 done / 幂等 / 不擅自重开），CI 每次跑。

5. 卡片应声明 `acceptance`（可执行验收项）与 `scope`（允许改动的路径 glob），
   规范见 `docs/task-card.md`。`agent-init.ps1 -Card <短号>` 会把它们注入工作区的
   `.agent-env.ps1`（`$env:NB_ACCEPTANCE` / `$env:NB_SCOPE` / `$env:NB_DEPENDS`），
   收尾统一跑 `pwsh -File scripts/verify.ps1`，CI 用 `scripts/check_scope.py` 拦越界改动。
   存量卡片未补字段时不拦截，补齐后生效（棘轮）。

---

## 三、钩子强制说明

仓库通过 `core.hooksPath = .githooks` 启用以下钩子，条款落地情况如下：

> **`core.hooksPath` 是仓库级配置，不随 clone 传递。** 新 clone 一份必须先把它装上，
> 否则下面四道钩子全部静默失效，整份宪章退化成「写在文档里的建议」——
> 这台机器上就踩过：`core.hooksPath` 为空却无人察觉，主分支保护形同虚设。
>
> ```powershell
> pwsh -File scripts/setup-git-hooks.ps1     # 幂等，随时可重跑
> ```
>
> `scripts/agent-init.ps1` 开区、`scripts/build.ps1` 构建、`.github/workflows/ci.yml`
> 都会自动带上这一步，一般不需要手动执行；加了新机器/新 clone 也只要补一次。
> 钩子文件必须是 **LF 行尾**（CRLF 会让 `sh` 找不到 shebang，表现为「装了但不跑」），
> `.gitattributes` 已固定 `eol=lf`，`setup-git-hooks.ps1` 每次也会顺手体检。

| 钩子 | 拦截的行为 | 绕过方式 |
| --- | --- | --- |
| `pre-commit` | 在 `main`/`master`/`develop` 上提交；分支名缺少规范前缀；agent 工作区改动台账 `cards`/`baseline` | `git commit --no-verify`（仅限紧急情况） |
| `commit-msg` | 提交信息不符合 `<type>(<scope>): <subject>` 格式（`merge` 类型与 Git 自动生成的合并信息除外）；代码改动未关联台账短号 | `git commit --no-verify` |
| `pre-push` | 推送尚未合并进 `main` 的分支；推送到 `main` 时非快进、快照降级；**推送范围内触及代码的提交缺短号或缺 claim ref**（无 python 时 fail-closed） | 需先领用任务卡 |
| `post-merge` | ——（仅提示可清理的已合并分支） | —— |

`pre-commit` 在**合并冲突解决**、**rebase 续行**、**cherry-pick 续行**等中间态会自动放行，
不会阻断正常合并流程。

任务闸门（`scripts/task_gate.py`）的豁免只看路径，不看人怎么声明：
`docs/` `scripts/` `.githooks/` `.github/` 下的文件与 `*.md`、`progress.json` 等算杂务；
`feat` / `fix` / `refactor` / `perf` / `test` 类型即使只改杂务文件，也必须有短号。

---

## 四、工作区卫生

- `build/`、`dist/`、`*.log`、`*.exe` 等编译产物与日志已在 `.gitignore` 中排除，不要手工 `git add -f`。
- 密钥、令牌、本地私有配置（`.env`、`*.pem`、`*.key`）**严禁入库**。
- 行尾统一：仓库内文本用 LF；`.ps1` / `.bat` / `.cmd` / `.iss` 在 Windows 工作区检出为 CRLF（见 `.gitattributes`）。
- **PowerShell 脚本必须带 UTF-8 BOM**：`.ps1` 在 Windows 上检出为 CRLF，而 Windows PowerShell 5.1
  会把**无 BOM 的 UTF-8 当 GBK 读**，中文最后一个字节会与紧随其后的引号配对、把字符串撑破，
  表现为整份脚本「批处理式语法报错」（PowerShell 7 无此问题，但 CI 与同事机器未必是 7）。
  新增脚本请以带 BOM 保存；缺 BOM 的老脚本一次性补上即可。
- 临时脚本用完即删，不要留在 `scripts/` 里。

---

## 五、代理执行约定

AI 代理在本仓库干活时：

1. **先读 `README.md` 与本文件**，再动代码。
2. **先拉分支再改代码**，不要在 `main` 上直接编辑（即便只是改文档）。
3. 改动完成后**必须合并回 `main`**，并删除分支，否则任务不算完成。
4. 合并前自检：`cmake --build build --config Release` 通过，且 `NeonBoard.exe --selftest` 无报错。
5. 不擅自提交 `build/`、`dist/`、日志；不擅自改动用户全局 Git 配置。
6. 涉及界面行为的改动，同步更新 `README.md` 中对应描述（如功能项、快捷键、已知限制）。
7. **收尾必须核对「需求卡」进度**：合并前对照 `docs/iteration-roadmap.md` 里对应阶段的验收项**逐条打勾**，把「已完成 X / 待做 Y」写回需求卡的 `note`，并同步该卡在看板上的状态。**禁止用「子任务全部 done」代替「需求已交付」** —— 子任务做完只说明父需求的某几项落地了，父需求要等验收项逐条满足才能结掉。
8. **测试与构建的三个已踩坑（R6 实测，别再花时间重新发现）**：
   - **AutoMoc 吃不熟 C++11 原始字符串**：Q_OBJECT 测试文件里写 `R"(...)"`，内容含 `)"` 会提前截断（报 missing ')'）；带花括号的 JSON fixture 会让 moc 不生成元代码，症状是链接期 `metaObject` 未解析（LNK2001），清 autogen 也未必好转。**测试数据里的 JSON / 正则一律用转义字符串拼接，别用原始字符串字面量。**
   - **改公共头文件签名后报「无法解析的外部符号」**：多半不是签名写错，是旧 `*.obj` / `*_autogen` 没重编。删对应产物再编：`Get-ChildItem build-local -Recurse -Filter 'tst_xxx*.obj' | Remove-Item`（连同其 `*_autogen` 目录），仍走 `scripts/build.ps1`，别信链接器第一眼。
   - **编辑 `.ps1` 会静默丢 UTF-8 BOM**（本会话连续丢了两次）：已由 `scripts/check_script_hygiene.py` 规则 C 在 CI 强制拦截。改完 `.ps1` 心里没底就跑一次卫生检查。

---

## 六、相关文档

- 完整操作手册（含命令示例与常见问题）：[`docs/git-workflow.md`](docs/git-workflow.md)
- 踩坑经验台账（现象/根因/解法 + 未解决清单）：[`docs/pitfalls.md`](docs/pitfalls.md)
- 贡献者约定：[`CONTRIBUTING.md`](CONTRIBUTING.md)
- 项目说明与构建：[`README.md`](README.md)

---

## 七、多代理并行约定

多个 AI 代理（或多个会话）同时改这个仓库时，必须按下述约定协作，否则改动会互相覆盖。

### 7.1 三区模型：锚点区 / 开发区 / 发布区

| 区 | 位置 | 允许的动作 | 禁止的动作 |
| --- | --- | --- | --- |
| 锚点区 | 仓库根目录 | 维护者本人看代码、跑 IDE、手动发布；`git-done.ps1` 收尾时**快进同步**（见下） | **任何 agent 在此改代码 / 切分支 / 提交 / 构建** |
| 开发区 | `.worktrees/<agent>/` | agent 的全部改动、构建、沙箱运行 | 共享 `build/` `dist/`、使用真实数据目录 |
| 发布区 | 某个 worktree 的 `dist/` | `scripts/release.ps1` 独占出包 | 两个 agent 同时出包 |

> **锚点快进例外**：`scripts/git-done.ps1` 在合并推送成功后，会对锚点区执行一次
> `git merge --ff-only origin/main`，让维护者回到根目录就能看到最新代码。该动作只允许
> **快进**：锚点检出非 `main`、有本地提交、有 `progress.json` 之外的未提交改动时一律跳过；
> `progress.json` 是应用自动写回的快照，允许临时 `git stash` 后还原；**还原撞车时主线版本优先**
> —— CI 每次回写都会动这份文件，本地快照必然冲突，此时工作区一律回退到主线版本（绝不留冲突标记，
> 应用下次写回会把 `<<<<<<<` 打进快照），本地快照仍完整留在 stash 里并打印其提交号供取回。
> **但若 `agent-workspace.json` 里 `root` 已被别的会话登记占用，只快进、不动 `progress.json`、不 stash**
> ——快照交给占用者处理，避免把别人的登记踢出去（`03b4424`，坑见 `docs/pitfalls.md` 1.16）。
> 除此之外 agent 仍不得动锚点区。
> 不需要时可加 `-SkipAnchorSync`。

锚点区的 `.git` 是所有 worktree 共享的元数据目录，但**工作区本身不属于任何 agent**。
占用登记**不再是自愿动作**（DX-05）：

- 开区即登记：`agent-init.ps1` 给开发区写 `worktree:<agent>`、锚点区原地模式写 `root`，
  有人占着同一个目标时后来者直接被拒（退出码 3），不必等出事才发现。
- 收尾即释放：`git-done.ps1` 合并成功后按当前检出的 agent 身份自动 `Release`，
  只释放自己那份（身份不符时脚本拒绝），不留僵尸登记。
- 自检即体检：`-Action Check` 一次性核对「锚点区有没有他人 in-place 环境 / 同一分支是否被两个
  检出占用 / 每个 `.worktrees/<名>` 是否各有独立 `.sandbox` / 登记与目录是否对得上」，
  有硬冲突退出码 3；登记缺项、僵尸占用只告警（回收由人拍板，§7.10）。

```powershell
pwsh -File scripts/agent-workspace.ps1 -Action Check      # 隔离自检（先跑这个）
pwsh -File scripts/agent-workspace.ps1 -Action Status
pwsh -File scripts/agent-workspace.ps1 -Action Claim  -Agent agent-c -Scope worktree:agent-c
pwsh -File scripts/agent-workspace.ps1 -Action Claim  -Agent agent-c -Scope root -Task "阶段条修复"
pwsh -File scripts/agent-workspace.ps1 -Action Release -Agent agent-c -Scope root
```

### 7.2 一 agent 一 worktree 一分支

`main` 分支已被锚点区 checkout，Git 不允许同一分支在两个 worktree 同时检出，
所以新工作区必须基于**远端引用** `origin/main` 建立。用脚本开区，别手敲 `git worktree add`：

```powershell
pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch fix/stagebar-chip-overflow -Card R3-01
cd .worktrees/agent-c
. .\.agent-env.ps1        # 注入沙箱环境变量
```

- `-Card <路线图短号>`：**必填**，开区即领用（无对应卡片时显式写 `-Card none`，
  不允许省略——「漏填」和「不需要」必须能区分）。该短号必须在台账 `cards` 里登记过；
  脚本会写 `claims[<短号>]={agent,branch,head,claimedAt}` 进**本 worktree** 的台账副本
  （随 feature 分支提交），并投递 `#<短号>=doing`（`chore(claim):` 空提交 + `--emit-hook-file` 双通道，幂等）。
  `-Card none` **只适用于纯文档/脚本杂务**：不会获得 claim ref，一旦触及代码路径，
  `commit-msg` 与 `pre-push` 会直接拒绝。
- **领用粒度**：大任务（`R#-#`）可随时领；小任务（`M##`）**只有父任务尚未被任何人认领**时才允许单独领用
  （父卡由 `board.db` 的 `task_link` 映射到台账，父卡已被领用即拒绝）。
- **卡片「进行中」不靠手工拖**：派生器按 claims 派生 doing，优先级为
  提交信息显式标记 > claims 领用 > baseline；分支被合并 / 删除 / rebase 重写时登记自动失效回退。
- 结项仍用提交标记 `#<短号>=done`；防双领：短号已被其它分支领用时开区直接报错。
- **领用的硬门在服务端**：`-Card` 会先推 `refs/claims/<短号>`（非强推；同名 ref 已存在即被拒绝），
  领子卡前先 `ls-remote` 父卡占位。本地台账 `claims` 只是投影，服务端 ref 才是唯一事实源。
  仓库 Ruleset 必须禁止 `refs/claims/**` 的强推与删除，否则可被 `--force` 绕过；领用需联网，离线直接失败（fail-closed）。
  PR 的 CI「领用闸门」（`scripts/check_claim_refs.py`）校验 claim ref 存在且指向分支历史，未领用的分支不准合入。

- **一分支多卡是合法的（DX-03）**：一条分支可以领多张卡（同一模块的顺序改动、一张卡的定义 +
  另一张卡的实现）。闸门口径是**每个出现在分支上的短号都必须各自持证** —— 多领合法，缺证照拒。
  - 补领：在分支所在检出里再跑一次 `agent-init.ps1 -InPlace -Card <另一张卡>`。脚本会判
    「本卡是否已领」，已领即跳过（否则每补领一次都会多出两条同卡号提交）。
  - 提交信息按实际改动带短号；`task_gate.py push` 本来逐提交校验，天然支持多卡。
  - 台账 `claims` 是按短号存的字典，同分支多条登记都指向同一个 `branch`，派生器与
    `release-claims.py` 已按短号遍历，无需额外登记。
  - **方向可校验但只告警**：某张卡的实现提交早于它的领用提交 = 先写代码后补领用，
    `check_claim_refs.py` 会给提示。历史分支确实需要补领，所以不阻断，但新工作请先领后写。
  - 自测：`python scripts/check_claim_refs.py --selftest`（短号提取 / 方向告警 / 多卡口径），CI 每次跑。

#### 7.2.1 认领占位 `refs/claims/*` 的生命周期与硬约束

服务端占位（`refs/claims/<短号>`）是双领防护的唯一事实源。创建 / 校验 / 释放三端均已由脚本收口，约束如下：

- **认领唯一入口**：所有认领必须且只能通过 `scripts/agent-init.ps1 -Card` 完成。**禁止手工 `git update-ref refs/claims/<短号>` 建本地占位来绕过 origin push** —— 早期部分认领这样做了，一度令 origin 无任何 `refs/claims/*`、服务端双领硬门事实上从未生效；现 `agent-init` 已强制非强推 `refs/claims/<短号>`（失败即 `exit 3`），闸门生效，origin 上只应存在**活动卡**的占位。本地 `refs/claims/*` 由 `git-done.ps1` / `task_gate.py` / `check_claim_refs.py` 执行 `git fetch origin '+refs/claims/*:refs/claims/*'` 从 origin 镜像回来，历史残留会被反复拉回，属孤儿占位，不得作为认领依据。
- **fail-closed 必须联网**：`agent-init` 推 `refs/claims/<短号>` 到 origin 失败时 `sys.exit(3)` 硬阻（已实现）。agent 运行环境**必须具备 origin 写权限**；本地存在 `refs/claims/*` **不构成**有效认领，必须以 `git ls-remote origin refs/claims/<短号>` 服务端可达为准。离线 / 无写权限时认领直接失败，不得降级为「仅本地占位」。
- **worktree 分支必须推 origin**：认领的 `fix/*` / `feature/*` 分支**必须推送到 origin** —— 不仅为 CI，更为 claim ref 指向的 commit object 在 origin 可达；否则 `ls-remote origin refs/claims/<短号>` 引用的提交在 origin 不可达，CI 领用闸门（`check_claim_refs.py`）无法校验。当前 `feature/onboarding`（R4-07）从未推 origin 即为一例。
  `agent-init.ps1` 开区时会扫 `.worktrees/*`，把「有提交不在任何 origin 引用可达范围内」的工作区**列名警告**
  （`03b4424`）——警告只负责让人看见，回收决定仍归人，不自动删（§7.10）。
- **合并后必须释放占位（已实现）**：`git-done.ps1` 的 `Invoke-ClaimRefCleanup` 在推送成功后按两条判据释放：①占位指向的提交已是 `origin/main` 祖先；②台账 `claims` 中登记的分支本地与远端均已不存在（孤儿）。都不满足的活动卡一律保留。离线时只清本地，联网后下次运行补删 origin（`git push origin --delete refs/claims/<短号>`）。
- **失效登记不再挡重新认领（已实现）**：历史缺口是「分支已合并删除、台账 `claims` 条目还留着」，这类残留曾把同一张卡的重新领用挡在防双领校验上（R4-02 踩过两次）。现 `agent-init` 在双领校验**之前**判定失效：登记分支本地 `refs/heads/*` 与 `refs/remotes/origin/*` 都不存在、且服务端 `refs/claims/<短号>` 已释放（`ls-remote --exit-code` 退出码 2），才算失效；远端不可达时保守视为仍被占用。失效项由本次领用提交顺带清理。
- **本地孤儿 ref 清理**：任何「本地有 `refs/claims/<短号>` 但 `git ls-remote origin` 无对应 ref」的状态视为异常孤儿，应 `git update-ref -d` 删除本地项，并不得在本地保留为占位依据。
- **台账过期登记一并清理（已实现）**：`git-done.ps1` 删掉认领分支后调用 `scripts/release-claims.py` 全量扫描台账 `claims`，只删「认领分支本地与远端都不存在」的条目（活动卡一律保留），作为独立的 `chore(board): 清理过期领用登记 <短号>` 提交快进补推；提交或补推失败只回滚该提交、不阻断已完成的合并。此前依赖 `claim-card.py`「下次任何人领卡时顺带清理」——顺带不保证发生，R6 收官后 `--check` 一直挂着「分支已不存在」噪声即为此。

- **`-InPlace` 的合法前提是「当前检出本身已是隔离 worktree」**（DX-05 硬门）：锚点区
  （`git-dir` 等于 `common-dir`）传 `-InPlace`，`agent-init.ps1` 直接拒绝并 `exit 3` ——
  在锚点区原地开区等于让两个会话共享 HEAD 与 `.sandbox`（2026-10-28 凌晨的现场：
  `.worktrees/` 一个都没建，双方都在根目录提交）。确需在锚点动手必须显式 `-ForceAnchor`，
  它会顺带登记 `root` 占用；检出里已留下**他人**的 `.agent-env.ps1`（`NEONBOARD_AGENT_ID`
  ≠ 当前 agent）时同样默认拒绝，同一个开关才可接管。WorkBuddy / 手工 worktree 的自动原地模式
  不受影响：它们本来就是 linked worktree。
- 同一分支禁止在两个 worktree 同时检出（Git 天然强制）。
- 每个 worktree 拥有独立的 `build/` `dist/` `.sandbox/`，体积较大，不要共享。
- worktree 用完必须回收：`git worktree remove .worktrees/<agent>`
  （`git-done.ps1 -RemoveWorktree` 会自动回收）。

### 7.3 合并不再依赖主 worktree

合并**不需要**在锚点区执行。agent 在自己的 worktree 里跑：

```powershell
pwsh -File scripts/git-done.ps1 -Branch fix/stagebar-chip-overflow
```

脚本内部流程：`git fetch origin` → 基于 `origin/main` 建临时分支 `merge/<slug>` →
`git merge --no-ff <分支>` → `git push origin HEAD:main`（**快进推送**，不是强推）。
合并前还会跑**领用闸门**：分支必须带服务端 `refs/claims/<短号>` 占位，否则拒绝合并；
纯文档/杂务分支按 diff 路径自动豁免（`scripts/task_gate.py scope` 判定为 `exempt`）。
`-SkipClaimCheck` 已废弃，保留参数只为兼容旧脚本，**不再能放行任何代码改动**。

- 串行化由远端的快进保护天然完成：两个 agent 同时推，后一个会被 non-fast-forward 拒绝，
  重新同步后再合即可 —— 因此**根目录永远不需要被任何 agent 占用**，
  也不存在「谁切了分支把别人的工作区搞乱」的问题。
- 合并完成后删除已合并的分支（`git branch -d <分支>`）并回收 worktree。
- 分支按拓扑顺序串行合并（例如 A → B → C）；合并下一个分支前，尚未合并的分支先 `git rebase origin/main`。
- **脚本定位仓库一律按脚本自身路径**（`<repo>/scripts/` 的上级目录），不按调用方当前目录：
  否则在锚点区调用 agent worktree 里的脚本，会把锚点误当成本仓库去合并 / 出包（已踩过）。
  涉及 `git-done.ps1` / `release.ps1` / `dev-run.ps1` / `agent-init.ps1`。
- **脚本里的原生命令不要让 stderr 打断流程**：MSBuild / ISCC 都往 stderr 写进度，
  在 `ErrorActionPreference = 'Stop'` 下会被当成终止性错误（已踩过），统一走带日志的调用封装。

### 7.4 热点文件行段分区

同一份文件被多个分支改动时，按行段划归不同代理，**同一时段只允许一个代理改同一段**：

| 文件 | 分区约定 |
| --- | --- |
| `src/main.cpp` | include 区 / 参数与单实例区 / 数据区 / 服务装配区 / 上下文属性区 / CLI 区 |
| `CMakeLists.txt` | 只允许在 `qt_add_executable` 源文件列表**尾部追加** |
| `qml/Main.qml` | 按界面区块（侧栏 / 设置面板 / 插件面板 / 看板列） |

冲突分三类：A 追加型（串行合并后自动消失）、B 同区块改写（靠分区规避）、C 同一行（rerere + 手工解兜底）。

### 7.5 解冲突纪律

1. 先 `git diff` 看清对方改了什么，再动手；
2. 手工合并，**禁止** `git checkout --ours/--theirs` 盲解；
3. 解完 `git add` + `git rebase --continue`；
4. 仓库需开启 `rerere`（`core.rerere.enabled true`、`core.rerere.autoupdate true`，
   仓库级配置，不动用户全局配置），同种冲突第二次会自动复用上次的解法。
   > 与 `core.hooksPath` 同构：仓库级、不随 clone 传递、漏了静默失效。所以**不要靠每个
   > agent 手工开** —— 已并入 `scripts/setup-git-hooks.ps1`（开区 / 构建 / CI 都会跑它），
   > 漏配会被自动补上。只写在文档里必然漂（R6 全程没人开、直到收官才发现）。

### 7.6 沙箱隔离开关

多个 agent 各自编译出独立的 `NeonBoard.exe`，默认会读写**同一份** `%APPDATA%\com.lin\NeonBoard\board.db`，
从而互相污染（自检任务、界面改状态、插件安装目录全部串味）。设置沙箱变量即可完全隔离：

| 环境变量 | 作用 |
| --- | --- |
| `NEONBOARD_DATA_DIR` | 数据库、插件目录、日志、界面设置（主题/画质/动效）、`hooks.db` 全部落到该目录 |
| `NEONBOARD_INSTANCE` | 单实例本地服务器 key 加后缀，允许多个实例同时跑 |
| `NEONBOARD_ROADMAP` | 路线图快照文件路径（默认从 exe 目录向上探测仓库根的 `progress.json`） |
| `NEONBOARD_ROADMAP_AUTOWRITE` | 设为 `0` 关闭看板 → 快照的自动写回 |
| `NEONBOARD_AGENT_ID` | agent 身份，供工作区占用登记与沙箱实例名使用 |
| `NB_SESSION_ARTIFACT_DIR` | （非 `NEONBOARD_*`，应用不读）会话产物落点，`agent-init.ps1` 开区时建好并导出为 `<worktree>/.sandbox/artifacts/`（`03b4424`） |

> **会话产物只往 `NB_SESSION_ARTIFACT_DIR` 写**：桌面工具的默认行为是写进**调用方 CWD**，
> 一个未登记的未跟踪目录就足以把锚点快进同步长期卡住（`docs/pitfalls.md` 1.1 / 1.17）。
> 第三方工具不认这个变量，发现一个产物目录就点名进 `.gitignore` 一条。

**别手设变量，用统一入口**（它会同时设置以上变量并把沙箱落在当前 worktree 内）：

```powershell
pwsh -File scripts/dev-run.ps1            # 沙箱启动，不污染真实数据
pwsh -File scripts/dev-run.ps1 -Wait      # 前台运行（配合 --smoke 等参数）
```

未设置时行为与原来完全一致，不影响正常用户。

> **沙箱跑应用必须设 `NEONBOARD_ROADMAP`**：路线图快照默认从 exe 目录向上找仓库根的 `progress.json`，
> 沙箱应用会把沙箱看板状态写回仓库快照（已踩过：测试卡进了仓库 `progress.json`）。
> `dev-run.ps1` 已把它固定到沙箱内，并默认关闭自动写回。

**脚本读应用落盘位置只有一个入口**：PowerShell 侧点源 `scripts/nb-paths.ps1`
（`Get-NbDataDir` / `Get-NbLogFile` / `Get-NbBoardDb`），Python 侧 `scripts/claim-card.py` 读同一组环境变量。
**不要再在任何脚本里拼一次 `%APPDATA%\com.lin\NeonBoard`** —— 写死之后沙箱运行读不到本次日志（验收假失败），
没设沙箱时又会去动真实实例正占用的文件（`verify.ps1` 的 `Remove-Item` 就被打断过）。
配套两条：

- `scripts/verify.ps1` 未设 `NEONBOARD_DATA_DIR` 时自开 `<BuildDir>\verify-sandbox`（设了则尊重），
  并把缺 ctest 的情况自行点源 `dev-env.ps1` —— 「唯一验收入口」不该要求调用方先记住别的；
- `scripts/check_script_hygiene.py` 在 CI 拦三件事：上述字面量只许出现在那两个文件，
  `git commit --no-verify` 只许出现在快照回写那一步（§11.2 第 7 条），每个 `.ps1` 必须带
  UTF-8 BOM（§4，编辑工具会静默剥掉，本会话连续丢过两次）。散文条款靠人记一定会漂。

### 7.7 验证口径

- 数据层统一用 `NeonBoard.exe --selftest` 自检（utilityMode 跳过单实例限制）。
- 构建：`cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64`，然后 `cmake --build build --config Release`。
- 涉及界面的改动在沙箱目录里验收，不要污染真实数据。
- 跑完应用检查 `git status`：沙箱跑应用若未设 `NEONBOARD_ROADMAP`，`progress.json` 会被写脏，记得 `git checkout -- progress.json` 还原。

### 7.8 发布闸门

出包只有一个入口 `scripts/release.ps1`，五道闸门任一不过立即中止，不留半成品：

1. **发布锁**：独占 `dist/`，别人正在出包时直接中止；
2. **HEAD 必须等于 `origin/main`**：发布物必须来自最新主线；
3. **工作区干净**：不允许把未提交内容打进安装包；
4. **Release 构建 + `--selftest` 自检**通过；
5. **产物带 `dist/BUILD-INFO.txt`**（commit / 分支 / 时间 / SHA256），安装包可溯源。

```powershell
pwsh -File scripts/release.ps1
```

CI 侧同样走这条路：`.github/workflows/release.yml` 在打 tag 或手动触发时出安装包并上传 artifact，
交付物只有 CI 一处产出，本地 `dist/` 仅作调试用途。

### 7.9 预览中心（多代理成果即时可见）

agent 的改动**不必等合并就能看**。主线实例内置「预览中心」面板（侧栏第 4 项），
实时列出所有 worktree，并可一键构建 / 启停。

| 层 | 位置 | 职责 |
| --- | --- | --- |
| 采集 | `src/PreviewHub` | 解析 `git worktree list --porcelain`，采集分支 / HEAD / 脏状态 / 已构建时间 |
| 构建 | `src/PreviewHub` | 异步调 cmake（configure → build 两阶段），日志进面板；另含串行「出包队列」（build → 复制 exe/示例插件 → windeployqt） |
| 实例 | `src/PreviewHub` | 以沙箱环境变量启动各 worktree 的 exe，登记 pid |
| 展示 | `qml/components/PreviewPanel.qml` | 预览中心面板（只用 `Theme.xxx` 令牌） |
| 命令行 | `scripts/preview.ps1` | `List` / `Build` / `Package` / `Start` / `Stop` 五个动作 |

```powershell
pwsh -File scripts/preview.ps1 -Action List
pwsh -File scripts/preview.ps1 -Action Build   -Agent agent-c
pwsh -File scripts/preview.ps1 -Action Package -Agent agent-c
pwsh -File scripts/preview.ps1 -Action Start   -Agent agent-c
pwsh -File scripts/preview.ps1 -Action Stop    -Agent agent-c
```

硬性约束：

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

### 7.10 与 WorkBuddy 的 Worktree Task 并用

WorkBuddy 客户端自带一套并行隔离：为某个任务开启 Worktree 开关后，会在
`~/WorkBuddy/Worktrees` 下生成独立检出，并自动建分支 `workbuddy/<基础分支>-<8位哈希>`。

它解决的是**文件层隔离**，本仓库这套解决的是**协作协议**（认领硬门、语义分支、运行时沙箱、
看板状态派生），两者互补而非二选一。并用时守住三条边界：

1. **不许套娃**：客户端已经给了隔离目录。`agent-init.ps1` 一旦检测到「脚本所属检出本身就是
   linked worktree」，就自动转**原地模式**（`-InPlace`）：只补 `.sandbox` / `.agent-env.ps1` /
   领用登记，不再往里面开一层 `.worktrees/<agent>`。确实要嵌套时用 `-ForceNested`。
2. **分支名放行 `workbuddy/`**：该前缀由客户端生成、不可人工命名，条款 2 已把它写进白名单。
   不放行的实际后果是 agent 被迫追加 `--no-verify`，反倒把台账防护、任务闸门一起绕穿。
3. **回收由人拍板**：客户端一侧有数量上限提示；本仓库侧由 `agent-init.ps1` 开区时列出
   `.worktrees/` 的数量与「多少天无提交」，超过 14 天建议回收，但**不自动删** ——
   自动清理极易端掉一个正在跑的任务。

---

## 八、计划钩子约定

「提交信息里的 `#短号` → 任务自动流转」这条链路横跨 Git 钩子与插件两端，改动时必须守住下面的边界。

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

## 九、插件库与市场约定

插件的**分发与安装**这条链路横跨 C++ 与 QML，改动时必须守住下面的边界。

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
5. **市场源可配置且可多个**：源列表存 `meta` 表 `plugin.market.sources`（JSON 数组），老版本单地址 `plugin.market.source` 自动迁移；内置默认值是**仓库简写**（`PluginMarket::defaultSource()`），用户可在插件库或设置面板改。**改默认源时同步改 README 与本节**。
6. **多源并列合并**：源地址支持仓库简写（`owner/repo`，客户端按 Gitee → GitHub 依次尝试）、完整 URL、本地目录；刷新时按源顺序逐个拉取，条目按 `id` 合并、**同 id 取版本高者**，条目带 `marketSource` 标记来源。
7. **每个源各自缓存**：缓存文件 `market-cache.json` 格式为 `{version:2, sources:[{spec,url,index,etag,lastModified,fetchedAt}]}`，**按源分条记录、互不覆盖**；请求带 `If-None-Match` / `If-Modified-Since`，`304` 直接用缓存；某个源拉取失败**只降级这个源**到自己的缓存，不得让插件库整体不可用。
8. **发布侧走脚本**：市场包由 `scripts/publish-market.ps1` 生成（读 `plugin.json` → 打 `.neon` → 算 SHA-256 → 更新 `market.json`）；索引里 `download` 可只写文件名，客户端按索引所在目录解析。索引顶层预留 `signature` 字段位，**本轮不校验签名**，以后接上不改协议。
9. **安装/卸载后必须 `PluginRuntime::reload()`**，并刷新插件库条目，否则界面与实际不一致；运行时另有目录监视自动热插拔（四个源目录 + 各插件子目录，约 400 ms 防抖），显式调用仍是兜底。
10. **官方插件不随安装包分发**：`plugins/`（官方源）不进 `dist`，发布态靠市场获取或用户手工复制；`scripts/deploy.ps1` 只复制 `plugins-samples/`。

### 9.3 验证口径

- 安装类：`--install-plugin-file <path>`（0 成功 / 2 失败）、`--market-install <id>`。
- 市场类：`--market-source <url>` → `--market-refresh` → `--market-install <id>`。
- **必须覆盖的失败用例**：篡改 `sha256` → 退出码 2 且**包未落盘**；`files` 里含 `../../evil.qml` → 退出码 2 且**没有 evil.qml 落盘**；断网 → `--market-refresh` 退出码 0 且提示离线模式、缓存文件存在。
- 起本地市场用 `python -m http.server`；测断网前先确认 8731 端口没有**残留**服务在监听（`Get-NetTCPConnection -LocalPort 8731 -State Listen`），否则断网用例会假通过。
- 日志断言要取**最后一条**匹配，不要取第一条。
- 多源专项：两个源各放一个同 id 不同版本的插件 → 刷新后条目数应为「去重后的总数」且取高版本；坏源 + 好源同时配 → 刷新仍 exit 0；只留旧的 `plugin.market.source` → 能自动迁移成一个源；`--install-plugin-url` 直链装 + `#sha256=` 校验值错 → exit 2 且不落盘。

---

## 十、主任务与子任务约定

看板支持「主任务 → 子任务」的父子关联，并在卡片上把关联关系显性化（子卡缩进 + 归属标识，父卡显示子任务完成度进度环）。

### 10.1 职责分层

| 层 | 位置 | 职责 | 不允许做 |
| --- | --- | --- | --- |
| 存储 | `board.db` 的 `task_link` 表 | 存「边」：`child_id → parent_id` | 往 `task` 表加 `parent_id` 列 |
| 门面 | `src/TaskTree.{h,cpp}` | 父卡 / 子卡查询、完成度统计、防环、增删边 | 在别处直接写 `task_link` |
| 排序 | `src/TaskFilterModel::lessThan` | 按「父卡分组」排，子任务紧跟父卡 | 改 `TaskModel::moveTask` 的既有语义 |
| 展示 | `qml/components/TaskCard.qml` | 归属竖线 + 缩进、`↳ 父卡` chip、父卡进度环 | 写死颜色（只能用 `Theme.xxx` 令牌） |

### 10.2 硬性约束

1. **关联关系只存边表**：`task_link(child_id TEXT PRIMARY KEY, parent_id TEXT NOT NULL, created_at INTEGER)`。
   **禁止**往 `task` 表加列、**禁止**升 `user_version` —— 老版本程序要能照常打开同一个库。
   建表语句在 `Database::open()` 里无条件 `CREATE TABLE IF NOT EXISTS`（`IF NOT EXISTS` 幂等，不影响旧库）。
2. **所有关联读写走 `TaskTree` 公开 API**（`setParent` / `clearParent` / `parentId` / `parentTitle` / `childStats`），
   QML 侧通过上下文属性 `taskTree` 调用；禁止在 QML 或别的类里直接拼 `task_link` 的 SQL。
3. **必须防自环与成环**：`setParent` 要沿父链上溯，发现成环直接返回 `false` 并拒绝落库。
4. **任务删除要清边**：`TaskTree` 订阅 `TaskModel::taskRemoved`，删任务时同步删掉指向它的边和它指向父卡的边。
5. **列内排序按父卡分组**：`TaskFilterModel::lessThan` 先比「父卡 order_key」，再比「是否子卡」（父在前），最后比自身 `order_key`。
   分组只影响展示顺序，**不重算 `order_key`**，也不改 `TaskModel::moveTask` 的老语义。
6. **`progress.json` 快照不含关联边**：快照只存任务字段，父子关系只存本地 `board.db`；
   跨机协作需要另行导出关联边，不要假装快照能带过去。
7. **需求卡要带进度口径**：父卡 `note` 里写「已完成 X / 待做 Y」，让进度直接落在需求卡上（配合第五节第 7 条）。

### 10.3 验证口径

- `--selftest` 能开库并建出 `task_link` 表；`NeonBoard.exe --selftest` 退出码 0。
- 建 3 个子卡 → 父卡 `taskTree.childStats(parentId)` 返回 `total=3`，`done` 随子卡状态变化。
- 成环：`setParent(A, B)` 后 `setParent(B, A)` 必须返回 `false`（不落库）。
- 删子卡 → `task_link` 里对应边消失。
- 界面：子卡显示 `↳ 父卡标题` 且左缩进，父卡右下角显示进度环 + `done/total`。

## 十一、进度快照与结卡闭环约定

`progress.json` 是路线图快照（**v3 格式**：每张卡带 `code` 字段）。**写者唯一 = 看板应用**（`src/RoadmapSync` 自动写回），
agent **禁止手改**快照文件 —— 手改已造成过 v2 覆盖 v3 的事故。

### 11.1 职责分层

| 层 | 位置 | 职责 | 不允许做 |
| --- | --- | --- | --- |
| 事件协议 | `docs/agent-task-event.md` | `agent-task-event v1` envelope 定义（agent / sessionId / items[id,title,status,note]） | 私造第二套格式 |
| 投递 | `src/main.cpp` CLI `--emit-hook-file <f> --hook-event agent.task` | 事件落 `hooks.db`（独立事件名，与 `hook.commit` 隔离） | 复用 `hook.commit`（会误伤 plan-hook） |
| 消费 | `plugins/agent-bridge/`（官方插件） | 解析 payload、维护 `agentbridge.<agent>.<sessionId>.<todoId>` → task uuid 映射、建卡/流转/对账 note | 直接写 SQL、改 `board.db` schema |
| 写回 | `src/RoadmapSync` | 看板侧改动 1.5 s 防抖后写回 `progress.json`（v3） | 手写文件 |
| 代提交 | `scripts/git-done.ps1` step1 | 提交**仅** `progress.json` 的未提交改动（固定信息 `chore(roadmap): 同步进度快照`）；提交前先跑 `derive_board_status.py --write` 把状态对齐到 git 事实 | 连同其它文件一起提交（fail-closed）；把应用写回的过期状态原样推上 main |
| 闸门 | `.githooks/pre-push` 规则 4 | 推 main 时校验快照：version ≥ 3、卡片数不减少、`code` 不丢 | 绕过（`--no-verify`） |

> **为什么代提交前要校正**：应用写回的是它内存里的状态，可能落后于本分支的 `#code=done` 标记
> （已踩过：R4-02 已 done，快照却被写回 `doing`，一路推到 main 后靠 CI 的 `derive --write` 才纠正）。
> 派生器只改状态的 `status` 字段、保留应用生成的其它卡片，无实质变化不落盘；
> 它失败时不阻断合并（落 main 后 CI 还会再纠正），但必须打警告，不许静默推过期快照。

### 11.2 硬性约束

1. **写者唯一**：快照只由看板应用写回。agent 改卡片状态/备注 → 走事件 → 插件改库 → 应用写回文件；
   **不要**用脚本直接改 `progress.json`。
2. **结卡必须走闭环**：更新需求卡的 `status` / `note`（含「已完成 X / 待做 Y」）时，投 `agent-task-event v1` 事件，
   由 `plugins/agent-bridge` 消费后写库，应用自动落快照。
3. **事件名必须隔离**：agent 事件用 `agent.task`；`hook.commit` 只归 plan-hook（`#短号` 规则）。
4. **note 语义是全量**：`item.note` 为空表示「不更新备注」，非空表示**全量覆盖**（不是追加）。
5. **快照格式不得降级**：`version` 保持 3、每张卡保留 `code`；移除的卡写 `"deleted": true` 而不是删条目。
6. **未提交只允许是快照**：`git-done.ps1` 只代提交 `progress.json`；存在其它未提交改动时**拒绝合并**（退出码 1）。
7. **CI 回写提交是唯一豁免，且只有一处**：`ad5134d` 把 `setup-git-hooks.ps1` 装进 CI 之后，
   构建 job 的检出停在 `main` 且钩子生效，「自动回写看板状态」那条提交被自家 `pre-commit`
   拒掉（10-26 之前 CI 没有钩子，回写一直是好的；`065a5e6` 起 main 常红）。
   因此全仓库只允许 `.github/workflows/ci.yml`「自动回写看板状态」这一步用
   `git commit --no-verify`，且它只提交 `progress.json`；**这条命令必须检查 `$LASTEXITCODE`**
   （`shell: pwsh` 下原生命令失败不会中断，吞掉退出码的下一个症状是 `git rebase` 报
   "index contains uncommitted changes"）。除此之外的任何地方出现 `--no-verify` 都算违规绕过。
   > 教训：**给 CI 装闸门，必须同时声明 CI 的豁免面。** 钩子与流水线各自都正确，相撞处正是
   > 「CI 既是闸门的执行环境、又是闸门的受管对象」这种双重身份；装完钩子不跑一次 main 验证，
   > 闸门就会先在自家流水线上咬人。
8. **CI 回写撞车以主线为基底重新派生**：回写提交只动 `progress.json`，而它的唯一事实源是
   「git 标记 + 台账」的派生结果。本地若已推过同派生结果（跑过 `derive --write`），CI 的重复
   回写在 rebase 时必然内容撞车 —— 正确处置是 `rebase --skip` 丢弃重叠提交，在最新主线基底上
   重新派生（派生一致则无变化早退）；**绝不手工解冲突硬拼两份快照**（会造出谁都不是的第三份
   状态）。ci.yml「自动回写看板状态」已按「先变基 → 再派生 → 提交 → 推送」的循环实现
   （R6 收官踩过：旧写法先派生后变基，撞车直接 abort，整轮红）。

### 11.3 验证口径

- 闭环：投递 `agent-task-event` → 插件日志出现「新建/更新 N」→ `board.db` 卡片 `status`/`note` 正确 →
  快照 `version=3`、`items` 增加、卡 `status=done`、`note` 与事件一致。
- 幂等：同一 `sessionId` + `todoId` 重复投递 → 卡片数不增、只对账更新。
- 提交：仅快照未提交时跑 `git-done.ps1` → 自动生成 `chore(roadmap): 同步进度快照` 提交并合并；
  混入其它文件 → 退出码 1 且不产生新提交。
- 闸门：向 main 推送 v2 快照 / 卡片数减少 / 丢 `code` → `pre-push` 拒绝并打印原因。
- CI 回写：`--check` 一致时「自动回写看板状态」应早退且不产生提交；有变化时必须留下一条 `ci(board):`
  提交并快进推上 main，提交或推送非零退出**必须让 job 立即红**（不允许拖到下一步 `rebase` 才炸）。
- **验证事件消费必须用常驻实例**：`--smoke` 的 4 秒窗口不够（`HookEventQueue` 一投出即 `markDelivered`，
  插件后台未 ready 时事件会被丢弃）。

---

## 十二、任务执行（R6）约定

R6 起看板承担「任务派发器」职责：卡片定义 → 拉起本机 agent → 验收判定。
这一段会**真正修改用户代码**，任何绕过下列边界的实现都算缺陷。

### 12.1 职责分层

| 层 | 位置 | 职责 | 不允许做 |
| --- | --- | --- | --- |
| 执行定义 | `board.db` 的 `task_exec` | goal / acceptance / scope / agent / cwd / workflow / timeout / maxAttempts | 往 `task` 表加列、升 `user_version` |
| 运行态 | `board.db` 的 `task_run` | 每次执行的 run（状态 / 心跳 / 退出码 / 日志 / 越界） | 改看板列、改卡片状态 |
| 执行器 | `src/TaskRunner.*` | 受控启动子进程、流式日志、超时与终止 | 自动 push、自动改列 |
| 验收 | `src/AcceptanceRunner.*` / `src/ScopeMatcher.*` | 跑 acceptance、比对 scope | 经过 shell 执行命令 |
| agent 适配 | `src/AgentProfile.*` | 拼命令行与只读参数 | 内置未实测的 profile |

### 12.2 硬性约束

1. **运行态独立**：`task_run` 侧表 + 同卡活跃 run 的部分唯一索引；不升 `user_version`；
   看板列仍只有 `todo/doing/done`，**程序不自动改列**，验收通过给人点「标为已办结」。
   决策见 `docs/adr/0002-run-state-and-executor-safety.md`。
2. **单飞行**：同卡最多一个活跃 run，全局默认并发 1；M1 忙时直接拒绝，不排队。
3. **默认关闭**：执行器总开关默认关；关闭时看板行为必须与未引入执行器时完全一致。
4. **只读 triage**：必须用 CLI 原生只读模式；分析前后 `git status --porcelain` 必须一致，
   prompt 里写「不要改文件」不算约束。输出 schema 见 `docs/agent-triage.md`。
5. **命令不经过 shell**：acceptance 只允许白名单脚本路径，拒绝管道 / 重定向 / 拼接字符；
   `assert` 由内置检查器执行。
6. **不自动推送**：执行完最多留下本地改动；`push` 永远由人决定。
7. **cwd 白名单**：必须命中已登记目录的绝对路径，首次登记需人勾选确认。
8. **日志脱敏**：token / key / 长十六进制串写入前脱敏；`runs/` 落数据目录，不得进仓库。
9. **定义单源**：开发卡看台账、个人卡看 `task_exec`；只允许台账 → `board.db` 单向导入，
   反向必须人确认后提交（`docs/adr/0001-task-definition-source.md`）。
10. **快照不受影响**：执行结果不写 `progress.json`；卡片状态仍由 `#短号=状态` 与台账派生（§11）。

### 12.3 验证口径

- `ctest --test-dir build -C Release -R "tst_taskruns|tst_taskrunner|tst_acceptance" --output-on-failure`；
- 禁用总开关时 `--selftest` / `--smoke` 行为与未引入执行器时一致；
- 任何新增 schema 都必须断言 `PRAGMA user_version` 未变，且 `tst_databasemigration` 通过。
