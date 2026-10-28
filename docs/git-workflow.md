# Git 工作流手册

> 本仓库的 Git 使用规范。核心一句话：**改动在分支上做，做完必须合并回 `main`。**
>
> 规范条款见 [`../AGENTS.md`](../AGENTS.md)，本文给出逐步命令与排错方法。

---

## 一、核心原则

```text
        ┌──────────────┐
        │     main     │  ← 受保护，禁止直接提交
        └──────┬───────┘
               │  git switch -c feature/xxx
        ┌──────▼───────┐
        │ feature/xxx  │  ← 所有改动在这里提交
        └──────┬───────┘
               │  git merge --no-ff
        ┌──────▼───────┐
        │     main     │  ← 合并后删除功能分支
        └──────────────┘
```

| 原则 | 说明 |
| --- | --- |
| 主分支受保护 | `main` 上不能直接提交代码，只能接收合并 |
| 一次改动一个分支 | 分支存活期间不混入无关改动 |
| 合并用 `--no-ff` | 保留分支合并痕迹，历史可回溯 |
| 合完即删 | 合并后立即 `git branch -d`，保持分支列表干净 |

---

## 二、日常流程

### 1. 开工：拉分支

```powershell
git switch main
git status                      # 确认工作区干净
git switch -c feature/task-tag-filter
```

分支名格式：`<前缀>/<英文小写短横线描述>`。允许前缀：

`feature` `fix` `hotfix` `chore` `docs` `refactor` `perf` `test` `build` `ci` `style` `revert`

### 2. 干活：改代码并提交

```powershell
# 改代码……
git add -A
git status                      # 复核暂存内容
git commit -m "feat(board): 增加卡片按标签筛选"
```

提交信息格式：`<type>(<scope>): <中文简述>`

| type | 用途 | 示例 |
| --- | --- | --- |
| `feat` | 新功能 | `feat(board): 增加卡片按标签筛选` |
| `fix` | 缺陷修复 | `fix(model): 修复删除后统计芯片未刷新` |
| `chore` | 构建/脚本/依赖 | `chore(git): 初始化分支工作流与钩子` |
| `docs` | 文档 | `docs(plugin): 补充插件权限清单说明` |
| `refactor` | 重构不改行为 | `refactor(db): 抽取查询构造辅助函数` |
| `perf` | 性能优化 | `perf(list): 列表批量刷新合并为一次更新` |
| `test` | 测试 | `test(db): 增加任务排序用例` |
| `build` | 构建系统 | `build(cmake): 切换到 Ninja 生成器` |
| `style` | 纯格式 | `style(qml): 统一缩进为四空格` |
| `revert` | 回滚 | `revert(feat): 撤销卡片拖拽功能` |
| `merge` | 合并提交 | `merge: 合入 feature/task-tag-filter` |

`scope` 可省略，但不允许写成空括号。

### 3. 收尾：合并回主分支

推荐一键脚本（默认 remote 模式，**不需要切到 `main`**）：

```powershell
pwsh -File scripts/git-done.ps1
```

它会自动完成：检查工作区干净 → `git fetch origin` → 基于 `origin/main` 建临时分支 `merge/<简述>`
→ `git merge --no-ff` → `git push origin HEAD:main`（快进推送）→ 删除功能分支。

> 合并动作在**当前 worktree** 内完成，仓库根目录不用被占用，多代理并行互不干扰。
> 想沿用「在 `main` 上本地合并」的旧流程，加 `-Mode local`。
> 脚本按**自身路径**定位仓库，所以从任何目录调用它，动的都是脚本所在的那个 worktree。

手工等价步骤：

```powershell
git fetch origin
git switch -c merge/task-tag-filter origin/main
git merge --no-ff feature/task-tag-filter    # 必须 --no-ff
git push origin HEAD:main                     # 快进推送，禁止强推
git branch -d feature/task-tag-filter        # 删除已合并分支
```

> 合并提交的信息统一写 `merge: 合入 <分支名>`。若手动执行 `git merge` 不带 `-m`，
> Git 会生成 `Merge branch 'xxx' into main`，钩子也会放行这种自动生成的合并信息。

合并后看一眼历史，确认分支痕迹保留：

```powershell
git log --oneline --graph -10
```

### 4.（可选）推送

当前仓库为纯本地仓库，尚未配置远程。将来接入远程后：

```powershell
git remote add origin <仓库地址>
git push -u origin main
```

推送同样受 `pre-push` 钩子约束：功能分支必须已合并进 `main` 才允许推送。

---

## 三、多代理并行协作

多个 AI 代理（或多个会话）同时改本仓库时，**仓库根目录不再归属任何代理**，改用「三区模型」：

| 区 | 位置 | 归属 |
| --- | --- | --- |
| 锚点区 | 仓库根目录 | 维护者本人（看代码 / 跑 IDE / 手动发布），**任何 agent 禁入** |
| 开发区 | `.worktrees/<agent>/` | 每个 agent 自己的分支、构建与沙箱 |
| 发布区 | 某个 worktree 的 `dist/` | `scripts/release.ps1` 独占出包 |

### 0.（新机器 / 新 clone）先确认钩子已装

```powershell
pwsh -File scripts/setup-git-hooks.ps1          # 幂等，装完 core.hooksPath = .githooks
git config --get core.hooksPath                 # 应输出 .githooks
```

`core.hooksPath` 是仓库级配置、**不随 clone 传递**。漏配一次，分支保护、提交格式、
任务闸门就全部静默失效。`agent-init.ps1` 开区、`build.ps1` 构建、CI 都会自动带上这一步，
手动补一次即可。

### 1. 开区

```powershell
pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch feature/xxx -Card R3-01
cd .worktrees/agent-c
. .\.agent-env.ps1          # 注入沙箱环境变量
```

`-Card` 是**必填**参数：确实没有对应卡片时显式写 `-Card none`（脚本会打印警告并跳过领用），
不允许省略——多代理并行时「漏填」和「不需要」必须能区分开，否则会出现两人改同一张卡却都不占位。

脚本基于 `origin/main` 建分支（**不能用 `main`**：`main` 已被锚点区检出，Git 不允许同一分支在两个 worktree 同时存在），
并顺手建好 `.sandbox/`（含 `.sandbox/artifacts/`）与 `.agent-env.ps1`。

开区时脚本会顺带做两件体检（只报不删，处置归人）：

- **未推送成果**：列出「提交不在任何 `origin` 引用可达范围内」的 worktree（宪章 §7.2.1 要求认领分支必须推 origin）；
- **闲置工作区**：超过 14 天无提交的工作区建议回收，但脚本不自动删（§7.10）。

**领任务时带上 `-Card <路线图短号>`**（如 `-Card R3-01`）：开区即领用，无需再手工把卡拖到「进行中」——

```powershell
pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch feature/task-tags -Card R3-01
```

- 短号必须在 `docs/board-ledger.json` 的 `cards` 里登记过（台账是唯一身份来源）；
- **领用粒度**：大任务（`R#-#`）可随时领；小任务（`M##`）只有在父任务**尚未被任何人认领**时才允许单独领用——父卡由 `board.db` 的 `task_link` 映射，父卡已被领用则直接拒绝；
- 登记写法：`claims[<短号>] = {agent, branch, head, claimedAt}`，落在**本 worktree** 的台账副本，随 feature 分支提交；
- 登记推送前钩子就会投递 `#<短号>=doing`（看板在跑立即生效，不在跑启动时补投递），合并回 `main` 后派生器按台账 claims 派生 `doing`；
- 派生优先级：**提交信息显式标记 > 领用登记 > baseline**；
- **释放不需要任何操作**：分支被合并进主干（head 变祖先）或分支被删除（ref 消失）、或分支被 rebase 重写（head 不再是 tip 祖先）时登记自动失效回退；
  台账里这些失效 `claims` 条目由下一次 `agent-init.ps1` 开区时统一清理，避免短号被永久占用；
- 收尾结项仍用提交标记：`feat: xxx done #R3-01=done`；
- 若短号已被另一分支领用则拒绝（防双领）。
- **服务端占位（硬门）**：领用时脚本会推 `refs/claims/<短号>`（值=领用提交，非强推）；同名 ref 已存在则 push 被服务端拒绝，
  所以「谁先占谁得」，并发领取也不会双领。领子卡前用 `ls-remote` 查父卡占位；领用需要联网，离线失败。
- **CI 闸门**：PR 会跑 `scripts/check_claim_refs.py`，要求分支历史里有领用提交、且 `refs/claims/<短号>` 指向的提交是该分支祖先；
  绕过脚本直接建分支的分支无法合入。仓库需在 Ruleset 里禁止 `refs/claims/**` 的强推/删除，锁死占位。

### 2. 沙箱运行（不污染真实数据）

```powershell
pwsh -File scripts/dev-run.ps1 -Wait -AppArgs --smoke
```

自动注入 `NEONBOARD_DATA_DIR` / `NEONBOARD_INSTANCE` / `NEONBOARD_ROADMAP` /
`NEONBOARD_ROADMAP_AUTOWRITE=0` / `NEONBOARD_AGENT_ID` 五个变量，
数据库、插件、日志、`hooks.db`、路线图快照全部落在本 worktree 的 `.sandbox/` 里。

> **会话产物（截图、导出、临时报告）写 `$env:NB_SESSION_ARTIFACT_DIR`**（`agent-init.ps1` 已建好并导出，
> 指向 `.sandbox/artifacts/`）。别往仓库根或调用方 CWD 丢——一个未登记的未跟踪目录就足以卡住锚点快进
> （`docs/pitfalls.md` 1.1 / 1.17）。

### 3. 占用登记（可选，防撞车）

```powershell
pwsh -File scripts/agent-workspace.ps1 -Action Status
pwsh -File scripts/agent-workspace.ps1 -Action Claim  -Agent agent-c -Scope root -Task "阶段条修复"
pwsh -File scripts/agent-workspace.ps1 -Action Release -Agent agent-c -Scope root
```

状态文件在 `<git-common-dir>/agent-workspace.json`（所有 worktree 共享），带文件锁；
被占用时退出码为 `3`。

`-Scope root` 的登记不只是个便条：`git-done.ps1` 的锚点快进会读它，**root 被别的会话占用时只快进、
不动 `progress.json`、不 stash**，快照让给占用者处理（`docs/pitfalls.md` 1.16）。

### 4. 合并回 main（不必占用根目录）

```powershell
pwsh -File scripts/git-done.ps1 -Branch feature/xxx -RemoveWorktree
```

在自己的 worktree 里完成 fetch → 合并 → 快进推送。两个代理同时推时，
后一个会被远端的 non-fast-forward 保护拒绝，重新同步后再合即可 —— 这就是串行化保证。

合并前自动执行**领用闸门**：`refs/claims/<短号>` 必须存在并对应分支里的领用提交，否则拒绝合并。
豁免只看 diff 路径，不看调用方怎么声明（`scripts/task_gate.py scope`）：改动全部落在 `docs/` `scripts/`
`.githooks/` `.github/` 或 `*.md` 才算杂务；`-SkipClaimCheck` 已废弃，它不再能放行任何代码改动。
分支 rebase 后领用提交被改写时，闸门按提交信息放行并提示。

#### 合并推送之后的锚点快进

`git-done.ps1` 推完 `main` 会把锚点区也跟上（`-SkipAnchorSync` 可关）。它**只允许快进**，
失败原因只会是这几类，排查顺序照抄 `docs/pitfalls.md` 1.10：

| 提示 | 含义 | 处置 |
| --- | --- | --- |
| 「锚点区有未提交改动（N 项）」并列出血缘文件 | 有工具的产物目录写进了锚点 CWD | 点名进 `.gitignore`（1.1）；agent 侧产物改投 `NB_SESSION_ARTIFACT_DIR` |
| 「root 已被 xxx 登记占用，本次只快进、不动 progress.json」 | 别的会话按 §3 占了 root，快照让给它处理 | **不是故障**，别去抢；等占用者 Release（1.16） |
| 「暂存失败 / 应用可能在运行」 | 主线实例正占着 `progress.json` | 关掉实例再重跑，或忽略（不影响远端合并） |
| `Not possible to fast-forward` | 锚点 `main` 上有只在本地的提交（分叉） | **不要自行变基或改写历史**，见 `docs/pitfalls.md` 1.10 与 7.2，由维护者处置 |

### 5. 出包

```powershell
pwsh -File scripts/release.ps1
```

五道闸门：发布锁 → HEAD 必须等于 `origin/main` → 工作区干净 → Release 构建 + `--selftest`
→ 产出 `dist/BUILD-INFO.txt`（commit / 分支 / 时间 / SHA256）。任一不过立即中止。

> CI 侧同样出包：`.github/workflows/release.yml` 在打 `v*` 标签或手动触发时生成安装包并上传制品。

### 6. 交付需求时顺手更新看板状态

看板卡片的 `status` **不是手写的**，而是从提交信息里的短号派生出来的：

```text
feat(board): 支持列内精确插位 #R1-04=done
```

- 短号查 `docs/board-ledger.json`；
- **特性分支不要手工改 `progress.json`** —— 它是派生器 + CI 独占写的派生产物，合并到 `main` 后由 CI 自动回写；
- 本地想先看派生结果：`python scripts/derive_board_status.py --check`（校验）/ `--write`（写回）；
- **台账 baseline 漂移**另有一个只读口径：`--baseline-check` 对比 git 历史里的 `#短号=done` 与台账 `baseline`，
  打印「漂移项 / 缺项」并恒退出 0，CI 里作为一步警告输出。`baseline` 归维护者，这个检查**只报不改**；
- PR 上若快照与派生结果不一致，CI 会直接拦住合并。

完整机制见 [docs/board-sync.md](docs/board-sync.md)。

### 7. 预览中心（成果即时可见，不用等合并）

agent 的改动只要**构建过**，主线实例就能直接跑起来看，不必等所有 agent 合并完。

应用内：主线窗口侧栏第 4 项「预览」→ 列出所有 worktree → 点「构建 / 启动 / 停止」。

命令行：

```powershell
pwsh -File scripts/preview.ps1 -Action List                    # 清单
pwsh -File scripts/preview.ps1 -Action Build -Agent agent-c    # 构建
pwsh -File scripts/preview.ps1 -Action Start -Agent agent-c    # 启动预览实例
pwsh -File scripts/preview.ps1 -Action Stop  -Agent agent-c    # 停止
```

- 预览实例跑在**该 worktree 的沙箱**里（`.sandbox/`），与真实数据、仓库根 `progress.json` 完全隔离。
- 窗口标题与托盘 tooltip 带 agent 标签（`NeonBoard · agent-c`），多个实例一眼分清。
- 运行登记在 `<git-common-dir>/preview-runtime.json`；`scripts/agent-workspace.ps1 -Action Status`
  的「预览实例」段落会一并显示。

---

### 8. 与 WorkBuddy 的 Worktree Task 并用

客户端自带一套并行隔离：开启任务后会在 `~/WorkBuddy/Worktrees` 下建独立检出，
自动生成分支 `workbuddy/<基础分支>-<8位哈希>`。它管**文件隔离**，本仓库这套管**协作协议**，
两者互补。`agent-init.ps1` 检测到当前检出已经是 linked worktree 时会自动转**原地模式**：
复用它、并在里面补沙箱与领用登记，不再往下再开一层 `.worktrees/<agent>`。

```powershell
pwsh -File scripts/agent-init.ps1 -Agent agent-c -Card R3-01      # 自动 -InPlace
```

三条边界（不许套娃 / 放行 `workbuddy/` 前缀 / 回收由人拍板）见 AGENTS.md 7.10。

---

## 四、钩子拦截与排查

仓库钩子位于 `.githooks/`，通过 `core.hooksPath` 启用。共 4 个：

| 钩子 | 触发时机 | 拦截内容 |
| --- | --- | --- |
| `pre-commit` | `git commit` | 主分支直接提交；分支名缺前缀 / 前缀非法 |
| `commit-msg` | `git commit` | 提交信息不符合 `<type>(<scope>): <subject>` |
| `pre-push` | `git push` | 功能分支未合并进 `main`；主分支非快进推送 |
| `post-merge` | `git merge` | 不拦截，仅提示可删除的已合并分支 |

### 常见拦截与处理

**① 报错「禁止在受保护分支 'main' 上直接提交」**

```powershell
git switch -c feature/<简述>    # 换到分支上再提交
```

若当前改动已经在 `main` 上被 `git add` 过，`git switch -c` 会带着暂存内容一起带走，无需处理。

**② 报错「分支名 'xxx' 缺少类型前缀」**

分支名必须形如 `feature/xxx`。用 `git branch -m <新名>` 改名。

**③ 报错「提交信息格式不符合规范」**

改成 `<type>(<scope>): <说明>`，例如 `fix(model): 修复删除后统计芯片未刷新`。

**④ 报错「分支 'xxx' 尚未合并进 'main'，禁止推送」**

先合并再推：

```powershell
pwsh -File scripts/git-done.ps1 -Branch xxx
git push origin main
```

### 紧急绕过

```powershell
git commit --no-verify
```

仅在热修复等特殊场景使用，且事后必须补一次规范合并。

### 钩子不生效？

```powershell
git config core.hooksPath    # 应输出 .githooks
```

Windows 上钩子文件必须是 **LF 换行 + 无 BOM**，否则 `sh` 无法解析。仓库已通过
`.gitattributes` 固定：`.githooks/* text eol=lf`。

---

## 五、常用命令速查

```powershell
# 看当前状态
git status
git branch -vv

# 看图形化历史
git log --oneline --graph --decorate -20

# 看某个文件改了什么
git diff
git diff --cached
git diff main...HEAD            # 本分支相对 main 的全部差异

# 撤销
git restore <file>              # 丢弃工作区改动
git restore --staged <file>     # 取消暂存
git reset --soft HEAD~1         # 撤销上一次提交但保留改动
git revert <commit>             # 用新提交撤销旧提交（已推送后用这个）

# 分支管理
git branch -d <branch>          # 删除已合并分支
git branch -D <branch>          # 强制删除（谨慎）
git switch -c <branch>          # 新建并切换
git merge --no-ff <branch>      # 合并（保留合并痕迹）

# 临时存放
git stash push -m "说明"
git stash list
git stash pop

# 构建环境（新机器 / 新 clone，见第七节）
. ./scripts/dev-env.ps1
pwsh -File scripts/build.ps1 -SelfTest
```

---

## 六、FAQ

**Q：为什么强制 `--no-ff`？**

快进合并会把分支提交直接平铺到 `main` 上，历史里看不出「这批改动来自哪个分支」，
出问题无法整体回退。非快进合并会生成一个合并节点，保留了「分支 → 合并」的来源关系。

**Q：能不能在 `main` 上直接改小东西？**

不行。哪怕只改一行 README，`pre-commit` 也会拦截。走 `docs/xxx` 分支，
改完合并回来，流程一致。

**Q：钩子失败了怎么查？**

```powershell
git commit -m "..."      # 看提示信息
```

钩子脚本本身在 `.githooks/` 下，可直接阅读；需要临时手工执行验证：

```powershell
sh .githooks/pre-commit
```

**Q：仓库没有远程，但想用 `git push` 做备份？**

可以先建一个本地裸仓库当"远程"：

```powershell
git init --bare D:\neonboard-backup.git
git remote add backup D:/neonboard-backup.git
git push -u backup main
```

**Q：如何确认钩子已生效？**

```powershell
git switch main
git commit -m "test" --allow-empty    # 应当被 pre-commit 拦截
```

---

## 七、构建环境（新机器 / 新 clone）

Ninja + MSVC 必须在「开发者环境」里跑，仓库提供一键装配（**点号 + 空格不能少**，
必须 dot-source，否则环境变量只存在于子进程，等于白做）：

```powershell
. ./scripts/dev-env.ps1              # 装配 MSVC / SDK / CMake / Ninja / Qt 并打印清单
. ./scripts/dev-env.ps1 -Quiet       # 只装配，不打印
pwsh -File scripts/dev-env.ps1 -Check   # 只体检，不改环境
pwsh -File scripts/build.ps1 -SelfTest  # 装好后再构建
```

### 为什么不直接跑 vcvars64.bat

`vcvars64.bat` 靠 `reg.exe` 读注册表来拼环境；受限环境里 `reg.exe` 被拦截时它
**静默少导入一批变量**，症状是 CMake 报 `rc.exe not found` 或
`CXX compiler is not able to compile a simple test program`，从报错看不出是环境没导全。
`dev-env.ps1` 直接拼 `VCINSTALLDIR / VCToolsInstallDir / INCLUDE / LIB / PATH`，不读注册表、
不需要 UAC。`build.ps1` 仍先尝试 vcvars，导入不全时自动回退到 `dev-env.ps1` 的装配逻辑。

### 缺件怎么补（全部用户级，无需管理员）

| 组件 | 装法 | 解析位置 |
| --- | --- | --- |
| Qt 6（msvc2022_64） | `pip install aqtinstall`；`python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtshadertools -O "$HOME\Qt"` | `C:\Qt\<版本>`、`$HOME\Qt\<版本>`（aqt 默认）、`%LOCALAPPDATA%\Qt` |
| CMake | 解压官方 zip 到 `%LOCALAPPDATA%\Programs\DevTools\cmake` | 同上目录，外加 PATH、VS 自带、`Program Files\CMake` |
| Ninja | 解压 `ninja-win.zip` 到 `%LOCALAPPDATA%\Programs\DevTools\ninja` | 同上 |
| MSVC | VS BuildTools 勾选「使用 C++ 的桌面开发」 | vswhere 结果、`C:\BuildTools`、`%LOCALAPPDATA%\Programs\VSBuildTools` |

Qt 装在别处时用 `-QtRoot` 指定，或设置 `NEONBOARD_QT_ROOT` / `QT_ROOT`。

> 坑：PS 5.1 的 `Expand-Archive` 解 CMake 官方 zip 会得到坏包（cmake 无法运行），
> 改用 `tar -xf` 解压。
