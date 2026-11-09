# NeonBoard 仓库宪章（AGENTS.md）

> 本文件是 NeonBoard 仓库的最高约束。任何人（人或 AI 代理）在本仓库中的所有改动，
> 都必须先读懂并遵守本文件。违反条款的操作会被 Git 钩子直接拦截。
>
> 本文件只保留规范条款；条款的背景、历史与机制细节在
> [`docs/charter-appendix.md`](docs/charter-appendix.md)（下文简称「附录」），
> 按宪章章节编号对应。改具体功能模块前先读对应附录节。

---

## 一、项目速览

NeonBoard 是赛博霓虹风格的三列个人任务看板。
技术栈：Qt（版本见 cmake/toolchain.cmake 单点）+ QML + C++20，MSVC 2022 x64，本地 SQLite 持久化。

| 目录 | 内容 |
| --- | --- |
| `src/` | C++ 源码：数据库、模型、托盘、热键、窗口、插件宿主 |
| `qml/` | QML 界面：主界面、霓虹背景、组件、主题 |
| `shaders/` | GLSL 霓虹着色器 |
| `plugins-samples/` | 示例插件（QML） |
| `installer/` | Inno Setup 打包脚本 |
| `scripts/` | 构建部署脚本与 Git 助手脚本 |
| `docs/` | 设计文档、截图、插件 API 文档 |

构建（推荐一键脚本，自动探测 CMake 与 Qt）：

```powershell
pwsh -File scripts/build.ps1 -SelfTest
```

- 新机器 / 新 clone 缺件或报 `rc.exe not found`：先 `. ./scripts/dev-env.ps1`
  （必须 dot-source）再构建；环境装配见 `docs/git-workflow.md` 第七节。
- 机器只有 Windows PowerShell 5.1（默认 Restricted）时脚本会被静默拒绝
  （无输出 + 退出码 1），处置见附录「一」与 `docs/pitfalls.md` §2.11。
- 新 worktree 首次全量构建约 20~30 分钟（零缓存，不是卡死）；纯脚本 / 文档改动
  不必全量，跑 `pwsh -File scripts/verify.ps1 -ScriptOnly`（约 2 秒）。
  怀疑变慢看 `build.ps1` 末尾打印的三段耗时与 sccache 命中率（附录「一」）。
- 等价手敲 cmake 命令与 sccache 机制见附录「一」。
- 自检参数：`NeonBoard.exe --selftest`（数据库增删改查）、`--smoke`（启动 4 秒输出指标）。

---

## 二、Git 宪章（强制条款，不可绕过）

### 条款 1 —— 改动必须先拉分支

**禁止在 `main`（及 `master` / `develop`）分支上直接修改并提交。**
每一次功能改动、缺陷修复、文档调整，都必须从 `main` 拉出一个独立分支。

### 条款 2 —— 分支命名必须带类型前缀

允许前缀：`feature/` 新功能 · `fix/` 缺陷修复 · `hotfix/` 紧急修复 · `chore/` 构建脚本杂务 ·
`docs/` 纯文档 · `refactor/` 重构不改行为 · `perf/` 性能 · `test/` 仅测试 · `build/` 构建系统 ·
`ci/` 流水线 · `style/` 纯格式 · `revert/` 回滚 · `workbuddy/` WorkBuddy 自动生成（见 7.10）。

格式：`<前缀>/<简短英文小写短横线描述>`，如 `feature/task-tag-filter`。
一次改动只对应一个分支，分支存活期间不混入无关改动。
允许表的唯一事实源是 `.githooks/pre-commit` 的 `allowed_prefixes`，改规则只改那一处。

### 条款 3 —— 提交信息必须规范

格式：`<type>(<scope>): <中文简述>`

- `type` ∈ feat / fix / chore / docs / refactor / perf / test / build / ci / style / revert / merge
- `scope` 可省略但不得为空括号；主题句 ≤ 50 字；合并提交统一 `merge: 合入 <分支名>`。
- 示例：`feat(board): 增加卡片按标签筛选`

### 条款 4 —— 改动完成后必须合并回主分支

功能分支成果不得长期滞留：改完必须合并回 `main` 并删除已合并的本地分支。统一入口：

```powershell
pwsh -File scripts/git-done.ps1
```

脚本走 remote 模式（基于 `origin/main` 建临时分支、`--no-ff` 合并、快进推送）；
合并不需要在主 worktree 执行（见 7.3）。**禁止 fast-forward 合并**，否则主分支历史
无法追溯「哪次改动来自哪个分支」。手工等价步骤见附录「条款 4」与 `docs/git-workflow.md`。

### 条款 5 —— 主分支只接受合并提交

`main` 上只允许合并提交，不允许直接改代码提交；推送必须快进，禁止 `--force`。

### 条款 6 —— 任务先行：没有任务不许改代码

**动代码之前必须先持有任务卡，并完成服务端领用占位。** 发号、回执、策略授权、
结卡自动化等机制细节见附录「条款 6」与 `docs/task-card.md`。规范如下：

1. **台账**：卡片登记在 `docs/board-ledger.json` 的 `cards` 里。
   **立卡只能走程序**：`python scripts/register_board_cards.py --form <请求单> --authorized-by <授权来源>`
   （请求单必须声明档位 `tier: P0..P4`）。手工改台账新增卡没有回执，commit-msg 阶段一律被拒。
   **agent 不得自行加卡、不得改卡的 `id`/`title`/`stage`/`scope`/`acceptance`、不得改 `baseline`**
   ——只能写 `claims` 领用登记与卡片 `note` 交付说明。
   代登记仅在维护者对**具体批次**显式授权时成立，且只允许追加卡片与对应 `baseline`。
   无人在场的定时任务可用台账策略授权（`authorization.policies`），机制见
   `docs/proposal-auto-authorization-policy.md`；agent 不得改策略。
2. **改任务定义的提交凭「内容」判定，不限检出位置**：凡新增/删除卡、改
   `id`/`title`/`stage`/`scope`/`acceptance`/`baseline`，必须单独 `chore(board):` 提交，
   body 同时有 `授权：` 与 `批次：` 两行，且不得混改代码路径。
   交付说明（`note`）单独一条 `docs(board): 补 <短号> 交付说明` 提交。
   判定逻辑唯一实现是 `scripts/task_gate.py`（`scope`/`message`/`ledger`/`push` 四个子命令，
   钩子与 `git-done.ps1` 都调它），**不得在多处重复实现**；自测 `python scripts/task_gate.py selftest`。
3. **领用**：`scripts/agent-init.ps1 -Card <短号>` 开区并推服务端占位
   `refs/claims/<短号>`（非强推，同名即拒；需联网，离线 fail-closed）。
   `baseline=<短号> done` 的卡（以 `origin/main` 台账为准）拒绝重新领用；
   短号已被其它分支领用时开区报错。触及代码路径的提交必须带 `#<短号>`，
   `commit-msg` 与 `pre-push`（逐提交复核短号与 claim ref）都拦得住。
   一分支多卡合法：分支上每个短号必须各自持证；补领用 `-InPlace -Card <另一张卡>`。
4. **杂务豁免按 diff 路径自动判定**：改动全部落在 `docs/` `scripts/` `.githooks/` `.github/`
   或 `*.md` 才算杂务（免领用），其余一律要有卡；`-Card none` 与 `-SkipClaimCheck` 都不能放行代码改动。
5. **结卡自动化**：`git-done.ps1` 合并推送成功后调 `derive_board_status.py --close-cards`，
   按 git 标记把 `baseline` 补成 `done`，作为独立 `chore(board): 结卡 <短号>` 提交
   （提交体写 `自动来源：` 行）；失败只回滚该提交，不阻断已完成的合并。
   **agent 仍不得手改 `baseline`**。
6. **验收与范围**：卡片应声明 `acceptance`（可执行验收项）与 `scope`（允许改动路径 glob），
   规范见 `docs/task-card.md`。`agent-init -Card` 注入工作区 `.agent-env.ps1`，
   收尾统一 `pwsh -File scripts/verify.ps1`，CI 用 `scripts/check_scope.py` 拦越界（棘轮）。

---

## 三、钩子强制说明

`core.hooksPath` 是仓库级配置、不随 clone 传递，漏配则下列钩子全部静默失效。
新 clone 先跑 `pwsh -File scripts/setup-git-hooks.ps1`（幂等）；
`agent-init.ps1`、`build.ps1`、CI 都会自动补。钩子文件必须 **LF 行尾**（`.gitattributes` 已固定）。

| 钩子 | 拦截的行为 | 绕过方式 |
| --- | --- | --- |
| `pre-commit` | 在 `main`/`master`/`develop` 上提交；分支名缺规范前缀；agent 工作区改台账 `cards`/`baseline` | `git commit --no-verify`（仅限紧急情况） |
| `commit-msg` | 提交信息不符合 `<type>(<scope>): <subject>`；代码改动未关联台账短号 | `git commit --no-verify` |
| `pre-push` | 推送未合并进 `main` 的分支；推 main 非快进、快照降级；推送范围内代码提交缺短号或缺 claim ref（无 python 时 fail-closed）；发布标签指向的提交不在主线上 | 需先领用任务卡 |
| `post-merge` | 仅提示可清理的已合并分支 | —— |

`pre-commit` 在合并冲突解决、rebase / cherry-pick 续行等中间态自动放行。
任务闸门豁免只看路径不看声明；`feat`/`fix`/`refactor`/`perf`/`test` 类型即使只改杂务文件也必须有短号。

---

## 四、工作区卫生

- 编译产物与日志（`build/`、`dist/`、`*.log`、`*.exe`）已被 `.gitignore` 排除，不要 `git add -f`。
- 密钥、令牌、本地私有配置（`.env`、`*.pem`、`*.key`）**严禁入库**。
- 行尾：仓库文本用 LF；`.ps1`/`.bat`/`.cmd`/`.iss` 在 Windows 检出为 CRLF（见 `.gitattributes`）。
- **PowerShell 脚本必须带 UTF-8 BOM**：PS 5.1 会把无 BOM 的 UTF-8 当 GBK 读，中文字符串会
  撑破引号、整份脚本「批处理式语法报错」。编辑工具会静默剥 BOM；CI 由
  `scripts/check_script_hygiene.py` 规则 C 强制拦截。
- 临时脚本用完即删，不要留在 `scripts/` 里。

---

## 五、代理执行约定

### 工作宗旨（Z1–Z15）

宪章管「不许做什么」（有钩子拦）；宗旨管「没人拦的时候怎么判断」。
收工前把每条的自检问句过一遍。每条先例与展开见附录「五」。

**写代码**

- **Z1 简单是默认，复杂要论证**——新抽象（类/表/脚本/配置项）先回答「现有的为什么不够用」。
  （自检：这个改动能删掉什么？）
- **Z2 改动最小，风格随邻**——diff 只含必需的行；不顺手重排、不统一别人格式、不升级无关依赖。
  （自检：diff 里有没有和本任务无关的行？）
- **Z3 不动既有语义，要动就给迁移**——改接口先枚举调用方、给兼容或迁移路径。
  先例：`moveTask` 保「插最前」老语义，追加末尾另开 `moveTaskToEnd`。（自检：谁在依赖现在的行为？）
- **Z4 失败模式写码前先定**——数据、安全、审计类一律 fail-closed；体验、收尾装饰类可 fail-soft
  但必须告警；静默吞错是缺陷。（自检：它失败的时候，谁会被瞒住？）

**做流程**

- **Z5 单一事实源**——同一条判定只实现一次；同一份共享状态只允许一个写者
  （校验者可以有一百个）。（自检：这条规则的第二份副本在哪？）
- **Z6 踩坑两次，沉淀成机器判据**——同类问题第二次出现就变成脚本、钩子或 CI 检查。
  （自检：这个坑下次靠什么拦住？）
- **Z7 流程为交付服务**——闸门反复拦正常工作时报给维护者修闸门，不习惯性地用
  `--no-verify` 绕行。（自检：我是在遵守流程，还是在熟练地绕开它？）

**验证与汇报**

- **Z8 验收看产物，不看动作**——以文件清单、日志最后一条匹配、退出码为准；
  凡「拷文件」环节必须拿产物验收，源码树里永远测不出部署缺件。
  （自检：我验收的是刚做出来的东西，还是做东西的过程？）
- **Z9 「已完成」和「已验证」是两句话**——红了就说红、贴原始输出；跳过的步骤写明跳过；
  没验证的标「未验证」。（自检：有没有把「我做了」说成「它好了」？）
- **Z10 一次性红灯是信号，常驻红灯是失败**——合并瞬间红灯、收尾转绿是预期；
  停住不消失必须处理，别屏蔽。（自检：这个红灯下次还亮吗？）

**协作边界**

- **Z11 权限最小，越界先停**——只改卡片 `scope` 路径；必须越界时停下向维护者要授权，
  不自补 scope、不自放宽 acceptance、不用临时目录或 `--no-verify` 绕闸门。
  （自检：这一步的授权在哪张卡上？）
- **Z12 拿不准就问，不猜**——验收含糊、需求二义、判断缺依据时停下来问。（自检：依据是什么？）

**流程健壮性**

- **Z13 可重入**——多步写操作按「任意一步都会死」设计：每步幂等、失败即回滚、
  恢复走状态转移表。（自检：第 N 步死掉后原样重跑会发生什么？）
- **Z14 修一处，扫一族**——修复覆盖整个同类写法，或明确说出为什么只修一处。
  （自检：同族写法还剩几处？）
- **Z15 判据钉死「读哪份事实、哪个时点」**——每条自动判定显式声明数据源与时点；
  陈旧投影、本地镜像、中间态不是判据。（自检：读的是事实源本身还是过时投影？）

### 硬性约定

1. **先读 `README.md` 与本文件**，再动代码。
2. **先拉分支再改代码**，不要在 `main` 上直接编辑（即便只是改文档）。
3. 改动完成后**必须合并回 `main`** 并删除分支，否则任务不算完成。
4. 合并前自检按 §7.7 的验收档位执行；全量口径为 Release 构建通过且 `--selftest` 无报错。
5. 不擅自提交 `build/`、`dist/`、日志；不擅自改动用户全局 Git 配置。
6. 涉及界面行为的改动，同步更新 `README.md` 对应描述（功能项、快捷键、已知限制）。
7. **收尾必须核对「需求卡」进度**：合并前对照 `docs/iteration-roadmap.md` 对应阶段的验收项
   **逐条打勾**，把「已完成 X / 待做 Y」写回需求卡 `note` 并同步看板状态。
   子任务全部 done ≠ 需求已交付，父需求要验收项逐条满足才能结掉。
8. **测试与构建的三个已踩坑（R6 实测）**：AutoMoc 与原始字符串、旧 obj 导致的伪链接错误、
   编辑 `.ps1` 静默丢 BOM——判据与展开见附录「五」，前两条动手前必读。

---

## 六、相关文档

- 宪章机制细节与历史附录：[`docs/charter-appendix.md`](docs/charter-appendix.md)
- 任务卡规范（scope / acceptance / 验收档位）：[`docs/task-card.md`](docs/task-card.md)
- 完整操作手册（命令示例与常见问题）：[`docs/git-workflow.md`](docs/git-workflow.md)
- 踩坑经验台账：[`docs/pitfalls.md`](docs/pitfalls.md)
- 贡献者约定：[`CONTRIBUTING.md`](CONTRIBUTING.md)
- 项目说明与构建：[`README.md`](README.md)

---

## 七、多代理并行约定

多个 AI 代理（或多个会话）同时改这个仓库时，必须按下述约定协作，否则改动会互相覆盖。

### 7.1 三区模型：锚点区 / 开发区 / 发布区

| 区 | 位置 | 允许的动作 | 禁止的动作 |
| --- | --- | --- | --- |
| 锚点区 | 仓库根目录 | 维护者本人看代码、跑 IDE、手动发布；`git-done.ps1` 收尾快进同步 | **任何 agent 在此改代码 / 切分支 / 提交 / 构建** |
| 开发区 | `.worktrees/<agent>/` | agent 的全部改动、构建、沙箱运行 | 共享 `build/` `dist/`、使用真实数据目录 |
| 发布区 | 某个 worktree 的 `dist/` | `scripts/release.ps1` 独占出包 | 两个 agent 同时出包 |

锚点快进同步：`git-done.ps1` 合并推送成功后对锚点执行一次 `merge --ff-only origin/main`；
只允许快进（非 `main`、有本地提交、未跟踪文件、受控文件外脏改一律跳过，三类原因分开报）。
**受控文件只有两份**（`progress.json`、`docs/iteration-roadmap.md`），允许临时 stash、
还原撞车时主线版优先；`root` 已被别的会话占用时只快进不动受控文件。完整判据见附录「7.1」。

占用登记是强制的（DX-05）：开区即登记（`agent-init.ps1` 写 `worktree:<agent>` / `root`，
同一目标被占后来者拒绝，退出码 3）、收尾即释放（`git-done` 只释放自己那份）、
自检即体检（`-Action Check`，硬冲突退出码 3）、删除判据
（`check-worktree-activity.ps1 -Audit`：未推送提交 / 未提交改动 / detached 全 0 才标可删，
只报告不删除，回收由人拍板，见 7.10）。

```powershell
pwsh -File scripts/agent-workspace.ps1 -Action Check   # 隔离自检（先跑这个）
pwsh -File scripts/agent-workspace.ps1 -Action Status
```

### 7.2 一 agent 一 worktree 一分支

`main` 已被锚点区 checkout，新工作区必须基于 `origin/main` 建立。用脚本开区，别手敲 `git worktree add`：

```powershell
pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch fix/stagebar-chip-overflow -Card R3-01
cd .worktrees/agent-c
. .\.agent-env.ps1        # 注入沙箱环境变量
```

- `-Card <短号>` **必填**：开区即领用（写 claims 投影 + 投递 `#<短号>=doing`，幂等）。
  无对应卡片时显式写 `-Card none`——只适用于纯文档/脚本杂务，一旦触及代码路径即被拒。
- 领用粒度：大任务（`R#-#`）可随时领；小任务（`M##`）只有父卡未被认领时才允许单独领用。
  卡片「进行中」由派生器按 claims 派生（优先级：提交标记 > claims 领用 > baseline），不手工拖。
- **领用硬门在服务端**：`refs/claims/<短号>` 非强推占位、Ruleset 禁强推与删除、离线 fail-closed；
  本地台账 `claims` 只是投影。CI 领用闸门（`scripts/check_claim_refs.py`）校验 ref 存在且指向分支历史。
- **认领唯一入口是 `agent-init -Card`，禁止手工 `git update-ref refs/claims/*` 绕过**；
  认领分支**不单独推送**（pre-push 规则 1 会拒：未合并进 main 的分支禁推）；claim 占位 ref
  `refs/claims/<短号>` 由 `agent-init` 领用时原子推送保证远端可达（红线实质不变），
  认领分支成果的远端可达性由 `git-done` 合并推 main 兑现。占位释放、失效登记
  与台账过期登记清理均由 `git-done.ps1` 收尾自动完成；判据细节见附录「7.2.1」。
- 一分支多卡合法（DX-03）：分支上每个短号必须各自持证；补领
  `agent-init -InPlace -Card <另一张卡>`（已领自动跳过）；实现早于领用只告警不阻断，
  但新工作先领后写。收尾自动提交必须在分离 HEAD 上做（DX-26）、`-InPlace` 合法前提是
  当前检出本身已是隔离 worktree（锚点须显式 `-ForceAnchor`）：详见附录「7.2」。
- 同一分支禁止两个 worktree 同时检出；每 worktree 独立 `build/` `dist/` `.sandbox/`；
  用完必须回收（`git-done.ps1 -RemoveWorktree` 自动做）。

### 7.3 合并不再依赖主 worktree

agent 在自己的 worktree 里跑 `pwsh -File scripts/git-done.ps1 -Branch <分支>`：基于
`origin/main` 建临时分支 `merge --no-ff`，清理登记 / 结卡 / 快照全部本地提交后对 main
**只推一次**（快进，非强推）。合并冲突仅 `progress.json` 时自动善后（取主线版重算派生）；
含其它文件（尤其台账）回滚现场转人工。合并前跑领用闸门，纯文档/杂务分支按 diff 路径
自动豁免。变基重试与机制细节见附录「7.3」。

- 串行化由远端快进保护天然完成：后推者被 non-fast-forward 拒绝，重新同步再合即可；
  根目录永远不需要被 agent 占用。合并后删除已合并分支并回收 worktree；多分支按拓扑顺序
  串行合并，合下一个前先 `git rebase origin/main`。
- 脚本定位仓库一律按**脚本自身路径**，不按调用方 cwd；原生命令的 stderr 不得打断
  `ErrorActionPreference='Stop'` 流程（MSBuild / ISCC 都往 stderr 写进度）。

### 7.4 热点文件行段分区

同一文件被多分支改动时按行段划归，同一时段只允许一个代理改同一段：

| 文件 | 分区约定 |
| --- | --- |
| `src/main.cpp` | include 区 / 参数与单实例区 / 数据区 / 服务装配区 / 上下文属性区 / CLI 区 |
| `CMakeLists.txt` | 只允许在 `qt_add_executable` 源文件列表**尾部追加** |
| `qml/Main.qml` | 按界面区块（侧栏 / 设置面板 / 插件面板 / 看板列） |

冲突三类：A 追加型（串行合并自动消失）、B 同区块改写（靠分区规避）、C 同一行（rerere + 手工解）。

### 7.5 解冲突纪律

先 `git diff` 看清对方改了什么再动手；手工合并，**禁止** `git checkout --ours/--theirs` 盲解；
解完 `git add` + `git rebase --continue`。仓库 `rerere` 已由 `scripts/setup-git-hooks.ps1`
自动配置（仓库级、不动用户全局），同种冲突第二次自动复用上次解法。

### 7.6 沙箱隔离开关

多个 agent 的 exe 默认读写同一份 `%APPDATA%\com.lin\NeonBoard\board.db` 会互相污染。
沙箱变量（`NEONBOARD_DATA_DIR` / `NEONBOARD_INSTANCE` / `NEONBOARD_ROADMAP` /
`NEONBOARD_ROADMAP_AUTOWRITE` / `NEONBOARD_AGENT_ID` / `NB_SESSION_ARTIFACT_DIR`，
逐项作用见附录「7.6」）实现完全隔离；**别手设变量，用统一入口**：

```powershell
pwsh -File scripts/dev-run.ps1            # 沙箱启动，不污染真实数据
pwsh -File scripts/dev-run.ps1 -Wait      # 前台运行（配合 --smoke 等参数）
```

- 会话产物只往 `NB_SESSION_ARTIFACT_DIR` 写——未登记目录进调用方 CWD 会卡住锚点快进。
- **脚本读应用落盘位置只有一个入口**：PowerShell 点源 `scripts/nb-paths.ps1`，
  Python 用 `scripts/nb_paths.py`；**不得再在任何脚本里拼 `%APPDATA%\com.lin\NeonBoard` 字面量**
  （CI `check_script_hygiene.py` 拦截，豁免面只有那两个文件）。
  `verify.ps1` 未设 `NEONBOARD_DATA_DIR` 时自开 `<BuildDir>\verify-sandbox`。细节见附录「7.6」。

### 7.7 验证口径

- **两档验收，按卡的 `scope` 判定**（DX-15）：`scope` 内有 `.cpp`/`.h`/`.hpp`/`.qml`/
  `CMakeLists.txt` → 代码类，跑 `pwsh -File scripts/verify.ps1`（编译 → ctest → `--selftest`
  → `--smoke` → 模块守卫 → 看板一致性）；都没有 → 跑 `verify.ps1 -ScriptOnly`（约 2 秒）。
  故意少报代码的，`-ScriptOnly` 扫改动清单见代码即要求全量。细节见 `docs/task-card.md`。
- **改台账按「改了什么」分流**（DX-25）：由 `scripts/check_board_diff.py` 判定——只动
  `baseline`/`claims` 或单纯立卡走轻档 + derive 校验；改既有卡契约、删卡、陌生 `stage` 直接要求全量。
- **Qt 路径不用手传**（DX-24）：`verify.ps1` 从 `build/CMakeCache.txt` 反推补 PATH。
  全量编译交 CI 兜底；发布前仍走 `scripts/release.ps1` 的 Release 构建 + `--selftest`（7.8）。
- 数据层统一 `NeonBoard.exe --selftest`；界面改动在沙箱验收，不污染真实数据
  （沙箱写脏仓库快照由 `verify.ps1` 的 `--smoke` 前后哈希比对兜底，无需手查 `git status`）。

### 7.8 发布闸门

出包唯一入口 `scripts/release.ps1`，五道闸门任一不过立即中止：

1. **发布锁**：独占 `dist/`；
2. **HEAD 必须等于 `origin/main`**；
3. **工作区干净**；
4. **Release 构建 + `--selftest`** 通过；
5. **产物带 `dist/BUILD-INFO.txt`**（commit / 分支 / 时间 / SHA256），安装包可溯源。

CI 侧 `.github/workflows/release.yml` 走同一条路（打 tag 或手动触发），交付物只有 CI 一处产出。
tag 闸门判据（标签指向的提交必须在 `origin/main` 上）与细节见附录「7.8」。

### 7.9 预览中心（多代理成果即时可见）

agent 改动不必等合并就能看：主线实例内置「预览中心」面板（侧栏第 4 项），实时列出所有
worktree，可一键构建 / 启停 / 出包。实现在 `src/PreviewHub` + `qml/components/PreviewPanel.qml`；
命令行 `scripts/preview.ps1 -Action List|Build|Package|Start|Stop -Agent <名>`。
红线（七条完整约束见附录「7.9」）：

1. **只有主线实例能当宿主**（设了 `NEONBOARD_AGENT_ID` 的实例不扫 worktree、不写运行时文件）。
2. **预览实例必须沙箱隔离**，绝不碰真实数据目录与仓库根 `progress.json`。
3. **出包只写 `dist/packages/<agent>/`**，串行队列、同一 agent 不重复入队。

### 7.10 与 WorkBuddy 的 Worktree Task 并用

WorkBuddy 客户端的 Worktree 开关解决**文件层隔离**，本仓库这套解决**协作协议**，两者互补。
三条边界：

1. **不许套娃**：检出本身已是 linked worktree 时 `agent-init.ps1` 自动转 `-InPlace` 原地模式
   （只补 `.sandbox` / `.agent-env.ps1` / 领用登记）；确要嵌套用 `-ForceNested`。
2. **分支名放行 `workbuddy/`** 前缀（条款 2 白名单）。
3. **回收由人拍板**：开区时列出工作区数量与无提交天数，超 14 天建议回收，**不自动删**。

---

## 八、计划钩子约定

「提交信息里的 `#短号` → 任务自动流转」链路横跨 Git 钩子与插件两端。改动
`post-commit`、`HookEventQueue`、`plan-hook`、`PluginContext`/`TaskModel` 前
**必须先读附录「八」**。红线：

- **规则只有一份，只在 `plugins/plan-hook/rules.js`**；宿主 C++ 不读 `plan.rules`、不做兜底解析。
- `post-commit` 钩子永远以 0 退出，不得中断提交；事件走独立库 `hooks.db`，不写 `board.db`。
- 规则与插件状态存 `meta` 表；不改 `board.db` schema、不升 `user_version`。
- 插件调任务接口只走 `pluginHost` 门面；`moveTask` 保「插最前」老语义，追加末尾用 `moveTaskToEnd`。
- 标记语法已冻结：`#T12` 转进行中、`#T12=done` 已办结、`#T12=todo` 转回待办；
  整词匹配、大小写不敏感，路线图短号（`#R3-01`、`#M19`）同样有效。
- 钩子文件必须 LF + 无 BOM；改 `post-commit` 时勿覆盖路线图快照同步（`--sync-roadmap`）逻辑。

---

## 九、插件库与市场约定

插件的发现、编排、打包、市场分发横跨 `PluginLoader` / `PluginRuntime` / `PluginPackage` /
`PluginMarket` / `PluginLibrary`。改动这条链路前**必须先读附录「九」**。红线：

- **只写用户安装区** `AppPaths::pluginDir()/<id 末段>`；`appDir/plugins`、`sourceDir/plugins`、
  `sourceDir/plugins-samples` 只读，卸载不得删。
- **四道校验缺一不可**（包格式 → `manifest.id` 一致 → SHA-256 与索引一致 → `files` 安全相对路径），
  任何一道不过整单失败、不落任何文件；安装必须经临时目录**原子替换**，禁止逐个写目标目录。
- `.neon` 是单文件 JSON，**不引入 zip / 外部解压命令**（零第三方依赖、跨平台）。
- 市场源可多个、按 `id` 合并取高版本、按源分条缓存；某源失败只降级该源，不得让插件库整体不可用。
- 安装/卸载后必须 `PluginRuntime::reload()` 并刷新插件库条目。
- **官方插件随分发包一起打**（`plugins\*` + `plugins-samples\*` 两批，用产物 `dist/plugins/`
  验收）；官方来源启动补齐声明权限，`appdata`/`sample` 仍走显式授权（`docs/pitfalls.md` §3.14）。
- 验证以 CLI 退出码为准（`--install-plugin-file`、`--market-refresh`、`--market-install` 等，
  必测失败用例见附录 9.3）。

---

## 十、主任务与子任务约定

看板支持「主任务 → 子任务」父子关联（子卡缩进 + `↳ 父卡` chip，父卡进度环）。
改动 `TaskTree` / `TaskFilterModel` / `TaskCard.qml` / `task_link` 前**必须先读附录「十」**。红线：

- 关联**只存边表** `task_link(child_id, parent_id, created_at)`：禁止给 `task` 表加列、
  禁止升 `user_version`（老版本必须能照常开库）；建表在 `Database::open()` 无条件幂等执行。
- 关联读写只走 `TaskTree` 公开 API（QML 经上下文属性 `taskTree`）；禁止别处拼 SQL；
  `setParent` 防环（成环返回 `false` 不落库）；删任务必须清边。
- 父卡分组排序只影响展示，**不重算 `order_key`、不改 `TaskModel::moveTask` 语义**。
- `progress.json` 快照不含关联边（父子关系只在本地 `board.db`）；界面颜色只用 `Theme.xxx` 令牌。
- 需求卡进度落在父卡 `note`（「已完成 X / 待做 Y」，配合第五节第 7 条）。

---

## 十一、进度快照与结卡闭环约定

`progress.json` 是路线图快照（**v3**：每张卡带 `code`）。写入只有两条受控路径，**都在本地**：
看板应用（`src/RoadmapSync` 自动写回）与 `git-done` 收尾的 `derive --write`（按 git 事实重算）。
**CI 不是写者**（`board.yml` 只校验）。agent **禁止手改**快照文件（曾造成 v2 覆盖 v3 事故）。

### 11.1 职责分层

链路为：事件协议（`docs/agent-task-event.md`）→ 投递（`--emit-hook-file --hook-event agent.task`
落 `hooks.db`）→ 消费（`plugins/agent-bridge` 官方插件写库）→ 写回（`src/RoadmapSync` 防抖落盘）
→ 代提交（`git-done` step1，先 `derive --write` 对齐 git 事实）→ 闸门（`pre-push` 规则 4 校验快照）。
各层「不允许做」的完整表与代提交前校正的原因见附录「十一」。

派生器只改 `status` 字段、保留其它卡片，无实质变化不落盘；它失败不阻断合并但必须告警——
**别指望 CI 事后补，它只校验**。

### 11.2 硬性约束

1. **写入路径只有两条，都在本地**（应用写回、`git-done` 收尾校正）；agent 改卡片状态/备注
   走事件 → 插件写库 → 应用落快照；**不要用脚本直接改 `progress.json`**。
2. **结卡走闭环**：更新需求卡 `status`/`note` 时投 `agent-task-event v1`，由 `agent-bridge` 消费写库。
3. **事件名隔离**：agent 事件用 `agent.task`，`hook.commit` 只归 plan-hook。
4. **note 语义全量**：`item.note` 空 = 不更新；非空 = 全量覆盖（不是追加）。
5. **快照格式不得降级**：`version` 保持 3、每卡保留 `code`；移除的卡写 `"deleted": true` 不删条目。
6. **未提交只允许是快照**：`git-done.ps1` 只代提交 `progress.json`；存在其它未提交改动拒绝合并（exit 1）。
7. **`--no-verify` 当前零豁免**：`.github/workflows/` 与脚本里都不许出现（`check_script_hygiene.py`
   规则 B/D）；历史教训与完整经过见 `docs/adr/0003`。
8. **快照只有一条写入链，CI 不参与写**：唯一写者是 `git-done` 收尾的 `Sync-BoardSnapshot`
   （fail-soft：失败只告警，不阻断已成功的合并）；`board.yml` 只是校验器。每批收尾对 main
   只推一次，中间态结构上不存在——**main tip 红灯 = 收尾真的失败了**，照提示补跑，别屏蔽。
   **同一份派生文件永远只能有一个写者。**

### 11.3 验证口径

- 闭环：投事件 → 插件日志「新建/更新 N」→ 库中 `status`/`note` 正确 → 快照 v3、`status=done`、note 一致。
- 幂等：同 `sessionId`+`todoId` 重复投递 → 卡片数不增、只对账更新。
- 提交：仅快照未提交时 `git-done` 自动 `chore(roadmap): 同步进度快照` 并合并；混入其它文件 → exit 1。
- 闸门：推 v2 快照 / 卡片减少 / 丢 `code` → `pre-push` 拒绝。
- CI 只校验：`--check` 红时不产生任何提交（`permissions: contents: read`）；看结论只看 main tip。
- **验证事件消费必须用常驻实例**：`--smoke` 4 秒窗口不够（事件投出即 `markDelivered`，
  插件未 ready 会丢）。

---

## 十二、任务执行（R6）约定

R6 起看板承担「任务派发器」：卡片定义 → 拉起本机 agent → 验收判定。
这一段会**真正修改用户代码**，绕过下列边界的实现都算缺陷。改动 `src/agent/`、
`ScopeMatcher`、`task_exec`/`task_run` 前**必须先读附录「十二」**。红线：

1. 运行态独立：`task_run` 侧表 + 同卡活跃 run 部分唯一索引；不升 `user_version`；
   **程序不自动改列**（验收通过给人点「标为已办结」，`docs/adr/0002`）。
2. 单飞行：同卡最多一个活跃 run，全局默认并发 1，忙时直接拒绝。
3. 执行器总开关**默认关闭**；关闭时看板行为必须与未引入执行器时完全一致。
4. 只读 triage 用 CLI 原生只读模式，前后 `git status --porcelain` 必须一致；
   prompt 写「不要改文件」不算约束（schema 见 `docs/agent-triage.md`）。
5. 命令不经过 shell：acceptance 只允许白名单脚本路径，拒绝管道/重定向/拼接字符。
6. **不自动推送**：`push` 永远由人决定；cwd 必须命中已登记白名单（首次登记需人确认）。
7. 日志脱敏（token/key/长十六进制串）；`runs/` 落数据目录，不进仓库。
8. 定义单源：台账 → `board.db` 单向导入，反向必须人确认（`docs/adr/0001`）；
   执行结果不写 `progress.json`，卡片状态仍按 §11 派生。
9. 验证：`ctest -R "tst_taskruns|tst_taskrunner|tst_acceptance"`；新增 schema 必须断言
   `PRAGMA user_version` 未变且 `tst_databasemigration` 通过。
