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

- 构建命令：

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

---

## 三、钩子强制说明

仓库通过 `core.hooksPath = .githooks` 启用以下钩子，条款落地情况如下：

| 钩子 | 拦截的行为 | 绕过方式 |
| --- | --- | --- |
| `pre-commit` | 在 `main`/`master`/`develop` 上提交；分支名缺少规范前缀 | `git commit --no-verify`（仅限紧急情况） |
| `commit-msg` | 提交信息不符合 `<type>(<scope>): <subject>` 格式（`merge` 类型与 Git 自动生成的合并信息除外） | `git commit --no-verify` |
| `pre-push` | 推送尚未合并进 `main` 的分支；推送到 `main` 时非快进（含 `git push origin HEAD:main` 的合并推送） | 需先合并回 `main` |
| `post-merge` | ——（仅提示可清理的已合并分支） | —— |

`pre-commit` 在**合并冲突解决**、**rebase 续行**、**cherry-pick 续行**等中间态会自动放行，
不会阻断正常合并流程。

---

## 四、工作区卫生

- `build/`、`dist/`、`*.log`、`*.exe` 等编译产物与日志已在 `.gitignore` 中排除，不要手工 `git add -f`。
- 密钥、令牌、本地私有配置（`.env`、`*.pem`、`*.key`）**严禁入库**。
- 行尾统一：仓库内文本用 LF；`.ps1` / `.bat` / `.cmd` / `.iss` 在 Windows 工作区检出为 CRLF（见 `.gitattributes`）。
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

---

## 六、相关文档

- 完整操作手册（含命令示例与常见问题）：[`docs/git-workflow.md`](docs/git-workflow.md)
- 贡献者约定：[`CONTRIBUTING.md`](CONTRIBUTING.md)
- 项目说明与构建：[`README.md`](README.md)

---

## 七、多代理并行约定

多个 AI 代理（或多个会话）同时改这个仓库时，必须按下述约定协作，否则改动会互相覆盖。

### 7.1 三区模型：锚点区 / 开发区 / 发布区

| 区 | 位置 | 允许的动作 | 禁止的动作 |
| --- | --- | --- | --- |
| 锚点区 | 仓库根目录 | 林先生本人看代码、跑 IDE、手动发布 | **任何 agent 在此改代码 / 切分支 / 提交 / 构建** |
| 开发区 | `.worktrees/<agent>/` | agent 的全部改动、构建、沙箱运行 | 共享 `build/` `dist/`、使用真实数据目录 |
| 发布区 | 某个 worktree 的 `dist/` | `scripts/release.ps1` 独占出包 | 两个 agent 同时出包 |

锚点区的 `.git` 是所有 worktree 共享的元数据目录，但**工作区本身不属于任何 agent**。
需要登记占用时走脚本，别口头约定：

```powershell
pwsh -File scripts/agent-workspace.ps1 -Action Status
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

- `-Card <路线图短号>`：开区即领用。该短号必须在台账 `cards` 里登记过；
  脚本会写 `claims[<短号>]={agent,branch,head,claimedAt}` 进**本 worktree** 的台账副本
  （随 feature 分支提交），并投递 `#<短号>=doing`（`chore(claim):` 空提交 + `--emit-hook-file` 双通道，幂等）。
- **领用粒度**：大任务（`R#-#`）可随时领；小任务（`M##`）**只有父任务尚未被任何人认领**时才允许单独领用
  （父卡由 `board.db` 的 `task_link` 映射到台账，父卡已被领用即拒绝）。
- **卡片「进行中」不靠手工拖**：派生器按 claims 派生 doing，优先级为
  提交信息显式标记 > claims 领用 > baseline；分支被合并 / 删除 / rebase 重写时登记自动失效回退。
- 结项仍用提交标记 `#<短号>=done`；防双领：短号已被其它分支领用时开区直接报错。
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
4. 仓库需开启 `rerere`（`git config core.rerere.enabled true`、`core.rerere.autoupdate true`，
   仓库级配置，不动用户全局配置），同种冲突第二次会自动复用上次的解法。

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

**别手设变量，用统一入口**（它会同时设置以上变量并把沙箱落在当前 worktree 内）：

```powershell
pwsh -File scripts/dev-run.ps1            # 沙箱启动，不污染真实数据
pwsh -File scripts/dev-run.ps1 -Wait      # 前台运行（配合 --smoke 等参数）
```

未设置时行为与原来完全一致，不影响正常用户。

> **沙箱跑应用必须设 `NEONBOARD_ROADMAP`**：路线图快照默认从 exe 目录向上找仓库根的 `progress.json`，
> 沙箱应用会把沙箱看板状态写回仓库快照（已踩过：测试卡进了仓库 `progress.json`）。
> `dev-run.ps1` 已把它固定到沙箱内，并默认关闭自动写回。

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
| 构建 | `src/PreviewHub` | 异步调 cmake（configure → build 两阶段），日志进面板 |
| 实例 | `src/PreviewHub` | 以沙箱环境变量启动各 worktree 的 exe，登记 pid |
| 展示 | `qml/components/PreviewPanel.qml` | 预览中心面板（只用 `Theme.xxx` 令牌） |
| 命令行 | `scripts/preview.ps1` | `List` / `Build` / `Start` / `Stop` 四个动作 |

```powershell
pwsh -File scripts/preview.ps1 -Action List
pwsh -File scripts/preview.ps1 -Action Build -Agent agent-c
pwsh -File scripts/preview.ps1 -Action Start -Agent agent-c
pwsh -File scripts/preview.ps1 -Action Stop  -Agent agent-c
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
| 代提交 | `scripts/git-done.ps1` step1 | 提交**仅** `progress.json` 的未提交改动（固定信息 `chore(roadmap): 同步进度快照`） | 连同其它文件一起提交（fail-closed） |
| 闸门 | `.githooks/pre-push` 规则 4 | 推 main 时校验快照：version ≥ 3、卡片数不减少、`code` 不丢 | 绕过（`--no-verify`） |

### 11.2 硬性约束

1. **写者唯一**：快照只由看板应用写回。agent 改卡片状态/备注 → 走事件 → 插件改库 → 应用写回文件；
   **不要**用脚本直接改 `progress.json`。
2. **结卡必须走闭环**：更新需求卡的 `status` / `note`（含「已完成 X / 待做 Y」）时，投 `agent-task-event v1` 事件，
   由 `plugins/agent-bridge` 消费后写库，应用自动落快照。
3. **事件名必须隔离**：agent 事件用 `agent.task`；`hook.commit` 只归 plan-hook（`#短号` 规则）。
4. **note 语义是全量**：`item.note` 为空表示「不更新备注」，非空表示**全量覆盖**（不是追加）。
5. **快照格式不得降级**：`version` 保持 3、每张卡保留 `code`；移除的卡写 `"deleted": true` 而不是删条目。
6. **未提交只允许是快照**：`git-done.ps1` 只代提交 `progress.json`；存在其它未提交改动时**拒绝合并**（退出码 1）。

### 11.3 验证口径

- 闭环：投递 `agent-task-event` → 插件日志出现「新建/更新 N」→ `board.db` 卡片 `status`/`note` 正确 →
  快照 `version=3`、`items` 增加、卡 `status=done`、`note` 与事件一致。
- 幂等：同一 `sessionId` + `todoId` 重复投递 → 卡片数不增、只对账更新。
- 提交：仅快照未提交时跑 `git-done.ps1` → 自动生成 `chore(roadmap): 同步进度快照` 提交并合并；
  混入其它文件 → 退出码 1 且不产生新提交。
- 闸门：向 main 推送 v2 快照 / 卡片数减少 / 丢 `code` → `pre-push` 拒绝并打印原因。
- **验证事件消费必须用常驻实例**：`--smoke` 的 4 秒窗口不够（`HookEventQueue` 一投出即 `markDelivered`，
  插件后台未 ready 时事件会被丢弃）。
