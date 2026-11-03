# 模块化建卡请求与质疑（草案）

> **状态：草案，未立卡。** 本文只递交请求，不改 `docs/board-ledger.json` 的 `cards` / `baseline`。
> 编号是建议（沿用 `docs/module-split-plan.md` v4 §7.4 的 `MOD-xx`），**实际编号以维护者立卡时为准**。
> 依据：`docs/module-split-plan.md` v4（合入提交 `a568cf3`），基线 `origin/main`。
>
> **2026-11-04 已入账**：§1.3 / §1.4 / §1.5 的 `MOD-06` / `DX-17` / `R14-05` 已按维护者指令
> 「R14-04 → R14-05 → DX-17 → MOD-06」立卡（`chore(board):` 提交），台账 `baseline` 均为 `todo`。
> 下面的 JSON 片段已降级为 `text` 留档，**不再被 `board-apply.py` 解析** —— 否则再用本文件建卡
> 会因「同号卡已存在」整批被拒。卡片定义以 `docs/board-ledger.json` 为准。
> 因此本文件已不是可执行清单 —— 再跑 `board-apply.py` 会报「不是合法 JSON 片段」，属预期。
>
> **2026-11-04 刷新**：`MOD-02` / `MOD-05` / `R14-03` 均已结卡，§1.3 的验收数字（35 不是 34）、
> 文件数（111 顶层 + host/ 1）与前置状态已按实际改正；新增 §1.4 `DX-17`、§1.5 `R14-05`
> 两张请求（前者是在办卡正在踩的治理缺陷，后者是 `R14-03` 收尾时实测出的产品缺陷）。
>
> **2026-11-04 收尾**：`R14-04` / `R14-05` / `DX-17` / `MOD-06` 四张全部结卡。新增 §1.7 `DX-18`
> ——`MOD-06` 搬家留下的副作用：`--audit` 被 191 条历史卡的过期精确 scope 淹没。
> 本节与 §1.4/§1.5 一样只递交请求，不改台账。

---

## 0. 五件事的拍板结果（2026-11-03 已定）

| # | 事项 | 决定 | 理由 |
| --- | --- | --- | --- |
| P-A | `MOD-01`（归属声明更正）去留 | **取消** | 交付物已随 v4 文档入库，立卡无事可做（证据见 §2.1） |
| P-B | `MOD-05` 的 `depends` | **`["MOD-02"]`** | 守卫要在搬运前在场，不必等拆 `main.cpp`（§2.2 三个版本的自洽修法） |
| P-C | `BLD-02` 验收线「全量 TU ≤ 100」 | **改为 ≤ 130** | 按 B1 自身设计算得 126，100 不可达（§2.3） |
| P-D | `MOD-` 前缀与 `M01`～`M18` 撞车 | **保留 `MOD-`** | 撤回该质疑：`task_gate.py` 正则两形态均匹配，看板有 stage 列区分；换前缀要改 12 行，不划算 |
| P-E | `MOD-06` 的 rename 断言 | **放宽为 > 85%** | 主 target 侧要新增 8 条 include + 逐行改 64 行源列表，90% 不可达（§2.0） |

上述五项均属**任务定义**（DX-10），落地时由维护者立卡并写入 `chore(board):` 提交，
body 带「授权：」「批次：」两行。**本文件只记录决定，不执行变更。**

---

## 1. 建卡请求

### 1.1 【建议 `MOD-02`】路径解耦：测试源引用集中化

| 段 | 内容 |
| --- | --- |
| **目标** | `tests/CMakeLists.txt` 里 199 处 `${CMAKE_SOURCE_DIR}/src/XXX.cpp` 收敛为 8 个模块目录变量，使 `src/` 物理分层时只需改 8 行 |
| **验收** | 见下方 `acceptance` |
| **边界** | `tests/CMakeLists.txt` + `CMakeLists.txt`（后者仅 B1 第二提交用） |
| **输入** | 基线 `origin/main`；无前置（`MOD-01` 已定取消，本卡即链头） |
| **产出** | 合并提交 body 四段；`git diff --stat` 只出现被授权文件 |

**的做法**（写进 `note` 供执行者参考）：

```cmake
# 现在 8 个变量同值，语义是「声明模块归属」；M2 时各自改成 src/task 等
set(NB_DIR_CORE   ${CMAKE_SOURCE_DIR}/src)
set(NB_DIR_TASK   ${CMAKE_SOURCE_DIR}/src)
# ...
neonboard_add_test(tst_taskinputparser ${NB_DIR_TASK}/TaskInputParser.cpp)
```

同一提交里把 `neonboard_add_test()` 内的 `target_include_directories(... ${CMAKE_SOURCE_DIR}/src)`
收成 `NB_INCLUDE_DIRS` 列表 —— 否则 M2 后要在这里补 8 条 include。

> **已入账，不再作为待建请求**（2026-11-03）：`MOD-02` 于 `0441fe9` 立卡，
> `8879f7c` 依维护者授权 A 把 `tests/tst_projectmap.cpp` 加入 scope，
> `6b13080` 完成实现，`6cb8299` 自动结卡（`baseline = done`）。
> 卡片定义以 `docs/board-ledger.json` 为准；本节的 JSON 片段已移除，
> 否则再用本文件建卡会因「同号卡已存在」整批被拒。
> **验收项 2 的「34/34」是过期数字** —— 实际测试总数为 35（R14-01 新增 `tst_pluginruntime`），
> 该卡验收时 ctest 报 `100% tests passed out of 35`；卡面未改，留待维护者更正。

**为什么把 B1（静态库）并进来**：方案 §5 已建议合并（两次改同一个文件）。建议**同卡两个提交**：
`refactor(test): 路径收敛` → `build(test): 公共源码收敛静态库`。第二个提交失败只回退它，
不拖累第一步的收益；验收也分两段判（TU 数只属第二段）。

### 1.2 【建议 `MOD-05`】模块依赖与行数守卫（棘轮式）

| 段 | 内容 |
| --- | --- |
| **目标** | 新增 `scripts/check_module_deps.py`：反向 `#include` 与行数上限，接入 CI 与 `verify.ps1` |
| **验收** | 见 `acceptance` |
| **边界** | `scripts/**`、`.github/**`、`AGENTS.md`、`docs/testing.md` |
| **输入** | `depends: ["MOD-02"]`（建议值，待 P-B 拍板） |
| **产出** | 合并提交 body 四段 |

**关键设计**（写进 `note`）：**必须照 `check_scope.py` 的棘轮模式写** —— 只检查本次 diff 涉及的
文件，存量不阻塞。`src/` 分层前模块归属靠映射表（文件 → 模块），分层后靠目录推导；
两种形态都要能吃，否则守卫会在 M2 之前满屏红，然后被 `--no-verify` 绕过。

> **已入账，不再作为待建请求**（2026-11-04）：`MOD-05` 于 `242e230` 立卡，
> `d67b0c8` 新增 `scripts/check_module_deps.py`，`1e9bd5e` 接入 `ci.yml` 与 `verify.ps1`，
> `4e2dcc7` 自动结卡（`baseline = done`）。卡片定义以 `docs/board-ledger.json` 为准；
> 本节的 JSON 片段已移除，否则再用本文件建卡会因「同号卡已存在」整批被拒。
>
> 实施时对验收的两处**补充**（不是改验收，是把实测写下来）：
> - 方向判据落成「层号更小才允许」，而不是「只允许实测那 11 条边」——后者会把
>   `shell → core` 这类合法方向也判红。`--base HEAD~200` 回归 183 文件 / 86 条边，0 误报。
> - 验收项 3 的 1910 / 4260 与实测一致，无需修正。
>
> 未做：`AGENTS.md`（与 DX-12/14/15/16 争同一路径，说明改写在脚本 docstring）、
> `docs/testing.md`（scope 允许但验收不要求，不新建）。

### 1.3 【建议 `MOD-06`】`src/` 物理分层

| 段 | 内容 |
| --- | --- |
| **目标** | `src/` 顶层平铺的 111 个 `.h/.cpp` 按 8 个模块归入子目录，`main.cpp` 留在顶层，`host/` 不动 |
| **验收** | 见 `acceptance`（2026-11-04 复核：测试总数 35，不是 34） |
| **边界** | `src/**`、`CMakeLists.txt`、`tests/CMakeLists.txt`、`docs/**` |
| **输入** | `depends: ["MOD-02", "MOD-05", "R14-03"]` —— **三张全部已结**（2026-11-04 核实），本卡即就绪 |
| **产出** | 合并提交 body 四段 |

**执行顺序**（写进 `note`，这是本卡最容易做错的地方）：

1. 先搬 `core`(7 类) + `task`(10 类) —— 两个最底层模块，搬完立刻验证；
2. 再搬 `board` / `shell` / `plugin` / `agent` / `update` / `theme`；
3. **`TaskRunner` 进 `src/agent/`，不是 `src/task/`**（§3.3 的归属修正）；
4. **`DataPort` 进 `src/board/`**、**`HookEventQueue` 进 `src/plugin/`** —— 三个易错归属；
5. 同批改 5 处（2026-11-04 逐项复核过数量）：`CMakeLists.txt` 源列表（**64 行** `src/` 引用 +
   主 target **需要新增** 8 条 include，它现在一条都没有）、`tests/CMakeLists.txt`（MOD-02 后
   **只改 8 行**）、`docs/project-map.json` 的 chain 证据、文档路径引用（`AGENTS.md` + `docs/`
   下 **12 个**非归档文档命中，`docs/archive/` 3 个历史归档不改）、卡片 scope 里的精确路径
   （**在办卡 0 条**，已核，见下）。

**两个易混点，别当成一回事**：

- `docs/project-map.json` 的 **skeleton 是递归扫**（`QDirIterator::Subdirectories`），拆子目录**不受影响**；
  **chain 是逐条校验证据存在性**，拆了就断链（`tst_projectmap` 会红）。
- 卡片 `scope` 里 **glob（`src/**`）不受影响，精确路径（`src/X.cpp`）才受影响**。当前 6 张在办卡
  的 scope 没有一条精确 `src/` 路径（已核），所以搬运不会让谁越界；但**改别卡的精确 scope 属任务
  定义变更（DX-10）**，要 `chore(board):` + 授权/批次两行 + 不混代码。

**时序风险（新）**：`docs/**` 与 6 张在办卡的 10 个精确路径重叠（`DX-12` 3、`DX-14` 2、
`DX-15` 1、`DX-16` 1、`R14-02` 1、`R14-04` 2），且 `CMakeLists.txt` 与 **`R14-02`（打包产物）**
争同一精确路径。`board-apply.py` 会报 `scope.clash` / `scope.cover` 两条提示。建议 **MOD-06 排在这些卡之后**，
或协调为「路径引用段由 MOD-06 独占一个提交」，避免同一文件双改。

```text
"MOD-06": {
  "stage": "MOD",
  "title": "MOD src/ 物理分层：8 个模块子目录 + 连带 5 处引用",
  "note": "起因：111 个 .h/.cpp 平铺在 src/ 顶层（另有 host/HostMain.cpp 一个子目录），边界只存在于读者脑中。\n\n目标：按 docs/module-split-plan.md §3.1 的 8 模块划分建立子目录，全量加 include 路径，保持 #include \"Database.h\" 写法不变（搬运与改 include 分离）。\n\n搬运前先扫 5 类硬编码引用（2026-11-04 复核）：tests/CMakeLists.txt（MOD-02 后只需改 8 行）、CMakeLists.txt 源列表（64 行 src/ 引用，且主 target 需从无到有新增 8 条 include）、docs/project-map.json 的 chain 证据（逐条校验存在性，拆目录即断链；skeleton 是递归扫，不受影响）、文档路径引用（AGENTS.md + docs 下 12 个非归档文档命中，docs/archive 3 个历史归档不改）、卡片 scope 的精确路径（在办卡 0 条，已核；glob 不受影响）。\n\n执行顺序：先 core + task 验证，再搬其余六个。易错归属：TaskRunner → agent（不是 task）、DataPort → board、HookEventQueue → plugin。\n\n时序：docs/** 与 6 张在办卡的 10 个精确路径重叠（DX-12/14/15/16、R14-02、R14-04），CMakeLists.txt 与 R14-02 争同一路径；建议排在这些卡之后，或把文档路径引用段独占一个提交。",
  "acceptance": [
    "ctest --test-dir build -C Release 全绿（35/35）",
    "build/Release/NeonBoard.exe --selftest 与 --smoke 退出码 0",
    "git diff --stat --find-renames origin/main...HEAD 中 rename 占比 > 85%（其余为主 target 的 include 新增与源列表映射改动）",
    "ctest -R projectmap 绿（docs/project-map.json 的 chain 证据已同步到新路径）",
    "ls src/*.cpp 只剩 main.cpp；src 下 core/task/plugin/agent/board/shell/update/theme 八个子目录均存在且非空",
    "python scripts/check_module_deps.py --base origin/main 退出 0",
    "python scripts/check_scope.py --base origin/main 退出 0"
  ],
  "scope": ["src/**", "CMakeLists.txt", "tests/CMakeLists.txt", "docs/**"],
  "depends": ["MOD-02", "MOD-05", "R14-03"]
}
```

### 1.4 【建议 `DX-17`】目录式 `scope` 静默失效：6 处尾斜杠一条文件都盖不住

| 段 | 内容 |
| --- | --- |
| **目标** | 卡片 `scope` 写 `plugins/x/`（尾斜杠）时能覆盖该目录下全部文件；并让「写了但没生效」可被机器发现 |
| **验收** | 见 `acceptance` |
| **边界** | `scripts/check_scope.py`、`scripts/board-apply.py`、`docs/task-card.md`、`.github/workflows/ci.yml` |
| **输入** | 无前置；与在办卡无路径冲突 |
| **产出** | 合并提交 body 四段 |

**为什么这不是笔误，是缺陷**：`check_scope.glob_to_regex("plugins/agent-opencode/")` 编译出
`^plugins/agent\-opencode/$` —— 只能匹配「字面量等于目录名」的路径，真实文件一个都不命中。
台账里 6 处同一写法：`R9-02` / `R9-03` / `R9-04` / `R9-05` / `R10-04`（`docs/`）/ **`R14-04`（在办）**。
也就是说 `R14-04` 现在改 `plugins/agent-opencode/main.qml` 会被判越界，而它自己并不知道。

`R14-03` 已把本卡的 `plugins/agent-cursor/` 手工改成 `plugins/agent-cursor/**`（`5974de0`），
但那是逐卡打补丁，不解决写法本身。

**修法**：在 `check_scope.py` 里对 scope **先规范化**（尾斜杠补 `**`），**不要改 `glob_to_regex`
的语义** —— `board-apply.py` 复用它做建卡校验，改语义会波及那 16 项自测。同时把匹配逻辑抽成
可测函数（`matches(scope, path)`），让脚本第一次有 `--selftest`。

**顺带要的 `--audit`**：遍历台账所有卡片的每条 scope，统计命中文件数，把「命中 0」的列出来。
这类「写了但没生效」人眼看不出来 —— 6 处尾斜杠活了这么久就是证据。CI 的 scope step 目前是
`continue-on-error`，只在信号层红，更不该指望人盯。

```text
"DX-17": {
  "stage": "DX",
  "title": "DX 目录式 scope 静默失效：6 处尾斜杠写法一条文件都盖不住",
  "note": "起因：卡片 scope 写 plugins/agent-opencode/（尾斜杠）时，check_scope.glob_to_regex 只按 glob 编译，得到 ^plugins/agent-opencode/$，只能匹配字面量为目录名的路径，一条真实文件都盖不住——等于这张卡没有 scope 保护。\n\n全仓 6 处：R9-02/03/04/05、R10-04（docs/）、R14-04（在办）。R14-03 已把本卡的 plugins/agent-cursor/ 手工改成 /**，但那是逐卡打补丁，不解决写法本身。\n\n修法：在 check_scope.py 里对 scope 先规范化（尾斜杠补 **），不改 glob_to_regex 的语义——board-apply.py 复用它做建卡校验，改语义会波及那 16 项自测。把匹配逻辑抽成可测函数 matches(scope, path)，让脚本第一次有 --selftest。\n\n顺带加 --audit：遍历台账所有卡片的每条 scope，统计命中文件数，列出命中 0 的条目。CI 的 scope step 是 continue-on-error，红灯只在信号层，不能指望人盯。",
  "acceptance": [
    "python scripts/check_scope.py --selftest 退出 0，含断言：目录式 plugins/agent-opencode/ 能匹配 plugins/agent-opencode/main.qml",
    "python scripts/check_scope.py --audit 输出中「命中 0 文件」的 scope 条目为 0（修复前为 6）",
    "python scripts/board-apply.py --selftest 退出 0（glob_to_regex 语义未变）",
    "python scripts/check_script_hygiene.py 退出 0",
    "docs/task-card.md 的 scope 段写明两种等价写法（plugins/x/ 与 plugins/x/**），并说明 CI 仍为信号"
  ],
  "scope": ["scripts/check_scope.py", "scripts/board-apply.py", "docs/task-card.md", ".github/workflows/ci.yml"],
  "depends": []
}
```

### 1.5 【建议 `R14-05`】`.cmd` 转调下 prompt 只送第一行，特殊字符被 cmd 二次解释

| 段 | 内容 |
| --- | --- |
| **目标** | 程序是 `.cmd`/`.bat` 包装器时，prompt 不再静默失真：能安全送达就送达，不能就在启动前明确失败 |
| **验收** | 见 `acceptance` |
| **边界** | `src/TaskRunner.*`、`src/AgentProfile.*`、`tests/tst_taskrunner.cpp`、`plugins/agent-cursor/**`、`docs/agent-profile.md`、`docs/agent-adapters.md` |
| **输入** | `depends: ["R14-03"]`（已结：`resolveProgram` 解决了「拉得起来」） |
| **产出** | 合并提交 body 四段 |

**这是 `R14-03` 收尾时量出来的第二层，不是猜的**。Qt 6.8 对 `.cmd` 会自动套
`cmd.exe /c ""prog" args"`（实抓命令行），于是参数被 cmd **再解释一遍**：

| prompt 内容 | 经 `.cmd` 的结果（`build/probe` 实测） |
| --- | --- |
| 含换行（`\n`） | **在第一个换行处截断**，后面的内容根本没到 CLI |
| 含 `%PATH%` 这类 `%VAR%` | 被展开成环境变量值 |
| 含 `&` `\|` `<` `>` | 被当成命令分隔符/重定向 |
| 含内嵌双引号 | 变成 `\"` |

**这不是边缘场景**：`WorkflowRunner::buildPrompt`（`src/WorkflowRunner.cpp:137`）用 `\n` 拼
多行 prompt（任务 / 上一步交接物 / 本步任务 / 交付物 / 约束）。`promptMode=arg` 的 profile
走 `.cmd` 时，agent 只收到第一行 —— 交接物整段丢失，且**没有任何报错**。

**三条出路（按推荐序，写进 note）**：

1. **真身直连**：profile 声明解释器 + 脚本（`node.exe` + `index.js`），执行器用它代替包装器，
   不经 shell。`docs/agent-profile.md` 已写的「彻底解法」；代价是版本目录会随升级变，要有解析与回退。
2. **自拼命令行**：`setCreateProcessArgumentsModifier` 自己拼 cmd 行并转义（`^` 转义 `& | < > ^`、
   `%` 用 `^%`、`"` 用 `\"`）。能救单行，**救不了换行**（cmd 没有换行转义）。
3. **兜底（本次至少要落地）**：启动前检测「程序是 `.cmd`/`.bat` 且 prompt 含换行或 NUL」→
   直接失败，错误信息指明 profile id 与原因，**绝不静默截断**。

**已排除的方案**：`promptMode=stdin`。`cursor-agent --help` 只有 `[prompt...]` 位置参数，
无 `--input-format` / `--stdin`；包内 `index.js` 里 `stdin` 仅出现 3 次，不是输入通道。
（`TaskRunner` 侧 stdin 通道本身是好的 —— R9-05 已修通，问题在 CLI 不读。）

```text
"R14-05": {
  "stage": "R14",
  "title": "R14 .cmd 转调下 prompt 只送第一行：换行截断与 cmd 二次解释",
  "note": "起因：R14-03 修好「拉得起来」后留下的一层。cursor-agent 在 PATH 上只有 .cmd 包装器，Qt 6.8 给它套 cmd.exe /c \"\"prog\" args\"，参数因此被 cmd 二次解释。实测（build/probe，2026-11-04）：换行→prompt 在第一个换行处截断；%VAR%→展开成环境变量；& | < >→当命令分隔符；内嵌双引号→\\\"。\n\n不是边缘场景：WorkflowRunner::buildPrompt（src/WorkflowRunner.cpp:137）用 \\n 拼多行 prompt（任务/上一步交接物/本步任务/交付物/约束），arg 模式经 .cmd 时只有第一行送达，且无任何报错。\n\n三条出路（推荐序）：1) 真身直连：profile 声明解释器+脚本（node.exe + index.js），不经 shell，代价是版本目录随升级变，需解析与回退；2) 自拼命令行 + cmd 转义（^ 转义 & | < > ^、% 用 ^%、\" 用 \\\"），救得了单行救不了换行；3) 兜底：启动前检测「.cmd/.bat 且 prompt 含换行/NUL」→ 明确失败并指明 profile id，绝不静默截断。\n\n本次至少落地 3)；1) 若时间允许一并做，并把 available 判定从「包装器可拉起」升级为「prompt 可安全送达」。\n\n已排除：promptMode=stdin——cursor-agent --help 只有 [prompt...] 位置参数，无 --input-format/--stdin，index.js 内 stdin 仅 3 次且非输入通道。\n\n时序：与在办卡 R14-04 争 docs/agent-profile.md 与 docs/agent-adapters.md，建议 R14-04 先结。",
  "acceptance": [
    "ctest -R taskrunner 全绿，含新用例：.cmd + 含换行 prompt 在启动前失败，错误信息含换行与 profile id，且不残留 running 锁",
    "ctest -R taskrunner 含新用例：.cmd 回显脚本收到含 %PATH% 与 a&b 的参数时，stdout 原样含这些字面量（不展开、不切分）",
    "NB_REAL_AGENT_PROBE=1 ctest -R taskrunner -V 真跑 cursor-agent 单行 prompt，退出码 0",
    "python scripts/check_module_deps.py --base origin/main 退出 0",
    "docs/agent-profile.md 的「仍未解决」段改为结论，含三种程序写法的实测结果表"
  ],
  "scope": [
    "src/TaskRunner.cpp",
    "src/TaskRunner.h",
    "src/AgentProfile.cpp",
    "src/AgentProfile.h",
    "tests/tst_taskrunner.cpp",
    "plugins/agent-cursor/**",
    "docs/agent-profile.md",
    "docs/agent-adapters.md"
  ],
  "depends": ["R14-03"]
}
```

### 1.6 建卡工具报出的 5 条路径冲突（排期先定序）

`python scripts/board-apply.py docs/module-split-card-requests.md` 的结论是
「可以建卡，就绪 3 张（`MOD-06`、`DX-17`、`R14-05`）」，同时给出 5 条提示。都不是阻塞项，
但都要人工定序 —— 否则同一个文件被两条分支各改一遍，合并时必然手工解冲突。

| # | 类型 | 本卡 | 与谁 | 建议顺序 |
| --- | --- | --- | --- | --- |
| 1 | `scope.clash` | `MOD-06`（`CMakeLists.txt`） | `R14-02` 打包产物 | `R14-02` 先结 |
| 2 | `scope.cover` | `MOD-06`（`docs/**` 盖住 6 卡的 10 条精确路径） | `DX-12/14/15/16`、`R14-02`、`R14-04` | `MOD-06` 最后做 |
| 3 | `scope.clash` | `DX-17`（`docs/task-card.md`） | `DX-15` 验收口径分层 | 任一先结，另一个 rebase |
| 4 | `scope.clash` | `R14-05`（`docs/agent-profile.md`） | `R14-04` opencode 只读参数 | `R14-04` 先结 |
| 5 | `scope.clash` | `R14-05`（`docs/agent-adapters.md`） | `R14-04` | 同上 |

即：**`MOD-06` 是三张新卡里最该往后放的**（它要动的 `CMakeLists.txt` 与 `docs/**` 恰好是
在办卡最密集的两处）；`DX-17` 纯脚本、无依赖，随时可插；`R14-05` 排在 `R14-04` 之后。

### 1.7 【建议 `DX-18`】`--audit` 被历史卡的过期 scope 淹没（MOD-06 的直接后果）

| 段 | 内容 |
| --- | --- |
| **目标** | `check_scope.py --audit` 默认只审「在办卡」，让「写了但没生效」重新可见；`--include-closed` 才列已结卡 |
| **验收** | 见 `acceptance` |
| **边界** | `scripts/check_scope.py`、`docs/task-card.md` |
| **输入** | `depends: ["DX-17"]`（已结：`--audit` 的判据与退出码由它定义） |
| **产出** | 合并提交 body 四段 |

**触发证据（2026-11-04 实测，`main` `5a0fee6`）**：

```
审计：120 张卡 / 793 条 scope / 仓库 342 个文件
  目录式（尾斜杠）5 条 → 命中 0 的 0 条
  其它 scope 命中 0 的 191 条（多为「卡要建的文件还没建」，仅提示）：
```

这 191 条**不是**「卡要建的文件还没建」，而是 `MOD-06` 搬家后的历史残留：已结卡写死的
`src/Database.*`、`src/TaskRunner.cpp` 这类精确路径，文件现在在 `src/core/`、`src/agent/` 下。
`DX-01` 一条卡就贡献了几十条。真实含义被这句「多为…」盖住了 —— 工具自己在撒谎。

已核**在办卡零失效**：结 `MOD-06` 前 6 张在办卡、结后 5 张（`DX-12`/`DX-14`/`DX-15`/`DX-16`/`R14-02`），
scope 里没有一条指向不存在的文件。也就是说 191 条是**纯噪声**，一条实情都没有。

**为什么不改台账**：已结卡的过期路径属历史记录，改它们是任务定义变更（`DX-10`），且 191 条
分散在 100 多张卡上，等于一次全量改写；`DX-17` 的 `--audit` 本来就是给「在办卡」用的监视器。

**修法**：

1. `--audit` 默认跳过 `baseline` 为 `done` 的卡，末尾补一行
   「已结卡另 N 条命中 0，已折叠（`--include-closed` 查看）」—— 不丢信息，只是不糊屏；
2. `--include-closed` 恢复当前全量行为（给做历史考古时用）；
3. 退出码语义不变：只有「目录式命中 0」才退 1；
4. `--selftest` 补两条断言：已结卡的失效路径默认被折叠、加 `--include-closed` 后能列回。

```text
"DX-18": {
  "stage": "DX",
  "title": "DX --audit 被历史卡过期 scope 淹没：191 条噪声盖住在办卡",
  "note": "起因：MOD-06 把 110 个文件搬进 8 个模块子目录后，已结卡里写死的精确路径（src/Database.*、src/TaskRunner.cpp 等）全部指向不存在的文件。2026-11-04 在 main 5a0fee6 实测 check_scope.py --audit：120 张卡 / 793 条 scope / 仓库 342 个文件，目录式 5 条全命中，其它命中 0 的 191 条——输出里把这 191 条解释成「多为卡要建的文件还没建」，实际全是搬家残留，工具在自我误述。\n\n已核在办卡零失效（DX-12/14/15/16、R14-02 共 5 张，scope 无一指向不存在的文件），所以 191 条是纯噪声，一条实情都没有；它把 DX-17 刚做出来的审计能力吃掉了。\n\n不改台账的理由：已结卡的过期路径属历史记录，改它是任务定义变更（DX-10），且分散在 100+ 张卡上，等于一次全量改写。--audit 本来就是给在办卡用的监视器。\n\n修法：--audit 默认跳过 baseline=done 的卡，末尾补一行「已结卡另 N 条命中 0，已折叠（--include-closed 查看）」；加 --include-closed 恢复全量；退出码语义不变（只有目录式命中 0 退 1）；--selftest 补两条断言（默认折叠、加开关能列回）。",
  "acceptance": [
    "python scripts/check_scope.py --audit 默认输出中不再列出已结卡的命中 0 条目，且末尾含折叠计数与 --include-closed 提示",
    "python scripts/check_scope.py --audit --include-closed 的条目数与改动前一致（当前基线 191 条，随卡增减）",
    "python scripts/check_scope.py --selftest 退出 0，新增两组断言：已结卡失效路径默认折叠；加 --include-closed 后列回",
    "目录式命中 0 仍退 1，--audit 对在办卡的判定未放松",
    "docs/task-card.md 的 --audit 段写明默认折叠语义与 --include-closed 用途"
  ],
  "scope": ["scripts/check_scope.py", "docs/task-card.md"],
  "depends": ["DX-17"]
}
```

**时序**：与在办卡 `DX-15` 争 `docs/task-card.md`（它要改验收口径那段，本卡改 `--audit` 段），
同文件不同段，仍建议 `DX-15` 先结、本卡后做；`scripts/check_scope.py` 无冲突。

---

## 2. 对 v4 的质疑（附核实证据）

### 2.0 预研核查：数字已过期，且「改 8 行」只对测试侧成立

立卡前对现状做了一次静态核查（2026-11-03，`origin/main` `8eb7193`），三条结果都会影响执行：

| 核查项 | 实测 | 影响 |
| --- | --- | --- |
| `tests/CMakeLists.txt` 内的源引用 | **199 处 `.cpp` + 0 处 `.h` + 1 处 include 目录**（第 18 行），去重 **37** 个文件 | 形态单一（全部 `${CMAKE_SOURCE_DIR}/src/XXX.cpp`），8 变量方案可行 |
| 与方案 §4.2 记的「196 处 / 36 个文件」 | 差 3 处 / 1 个文件 | 方案写于 10-30，之后 R14-01 加了 `tst_pluginruntime`（`Database.cpp` `AppPaths.cpp` `PluginLoader.cpp`）——**方案数字当时对，现在过期**，正是 DX-14 要治的那种漂移 |
| 主 target `NeonBoard` 的 include 路径 | **没有** `target_include_directories(... src)`（只有 `NeonBoardHost` 与 `neonboard-delta-tool` 有） | 现在跨文件 `#include "Database.h"` 靠「同目录」解析；**M2 必须为 8 个子目录新增 include**，否则跨模块引用全断 |

**这条最需要修正认知**：方案反复说「M2 退化为改一处映射 + `git mv`」——
那只对**测试侧**成立。主 target 侧是另外两件事：

1. `CMakeLists.txt` 的源列表是 **64 行逐文件相对路径**（`src/Database.h src/Database.cpp` 这种写法），
   无法用 8 个变量收敛，得逐行改（可脚本批量，但要人工核对）；
2. 主 target 的 include 目录是**从无到有**地新增 8 条，不是改造既有的一处。

所以 `MOD-06` 的真实工作量比「纯搬运」多两块，验收里那条「`diff` 主体为 rename」也必然
被这两块打破——**建议把该断言放宽为「rename 占比 > 85%，其余为 CMake 与 JSON 的映射改动」**。
（这一条要不要改，同属 `acceptance` 变更，见 P-E。）

### 2.1 `MOD-01` 很可能是空卡

方案 §3.3 定义它是「只是归属声明的更正，不移动文件、不改代码」。核实结果：

| 可能的载体 | 核实结果 |
| --- | --- |
| `docs/module-split-plan.md` §3.1 表 | **已经是修正后的版本**（`TaskRunner` 在 `agent`、`DataPort` 在 `board`、`HookEventQueue` 在 `plugin`） |
| `docs/project-map.json` skeleton | 只按目录分组（`src` 整体一组），**无类级模块归属** |
| 其他文档提到这三个类 | 全是功能描述（`src/TaskRunner.*` = 执行器、`src/HookEventQueue` = 事件队列、`src/DataPort` = 导出），**不是归属声明** |

即：v3/v4 写作时就把归属写对了，`MOD-01` 的「交付物」已经随方案文档一起进了仓库。
**建议**：取消，或把它的内容改成「MOD-06 搬运时同步文档里的路径引用」，并入 MOD-06。

### 2.2 `MOD-05` 的依赖写了三个版本

| 位置 | 写的 |
| --- | --- |
| §5 分批计划总表 | `MOD-05` depends `MOD-03`（拆 `main.cpp`） |
| §5「M5 —— 守卫」章节 | 「已提前到 M2 之前」 |
| §6.3 建议整体顺序 | 排在 `MOD-03` 之后、`MOD-06` 之前 |

三者只有「§6.3」与「§5 章节」勉强兼容。建议明确为 `depends: ["MOD-02"]`，
理由：守卫要在搬运前在场；`main.cpp` 的行数上限可以先设「不得增加」，不需要等拆完。

### 2.3 `BLD-02` 的验收线算不出来

§7.4 写「`ninja` 全量构建的编译单元总数 ≤ 100（原 286）」，但按 B1 自身设计：
36（静态库自身 TU）+ 34（测试各 1 TU）= 测试侧 70，加主程序 56 = **126**。
§5 说「测试侧 230 TU → 34 TU」时**漏算了库自身的 36 TU**。

这跟 v2 写「`main.cpp` < 400 行」是同一类问题——验收线没做过算术自洽。
建议改 **≤ 130**；若要真的到 100 以内，得让主程序也链静态库，那已经是 `MOD-09` 的范围。

### 2.4 `MOD-` 前缀与既有 `M01`～`M18` 撞车

台账现有 `M01`～`M18` 共 18 张里程碑卡（形态 `M\d+`，靠 `task_gate.py` 正则的
**第二个分支**匹配）。新增 `MOD-01` 走第一个分支（`[A-Za-z]{1,4}\d*-\d+`），**校验能过**，
但看板/文档里 `MOD-01` 与 `M01` 只差两个字母，人工核对时极易看错。
`ARCH`（4 字母，正好在前缀上限）或 `LAY` 都不冲突。

### 2.5 ~~`R14-03` 必须先结~~（已解除，2026-11-04）

它的 `scope` 含 `src/TaskRunner.cpp` / `.h`，而该文件将被搬进 `src/agent/`；
不结就搬，`check_scope.py` 会在搬运提交上判越界。

**结论**：`R14-03` 已于 2026-11-04 结卡（`a1e23a8`，实现 `57ac656`，本地 main `5456b8d`）。
顺带把它失效的目录式 scope 改成 `plugins/agent-cursor/**`（`5974de0`）——同一类问题
在台账里还有 6 处，已单独立 §1.4 的 `DX-17`。

---

## 3. 修订后的建议序列

```
已完成 → R14-03 结卡（缺陷优先，且是 MOD-06 的硬前置）
已完成 → MOD-02（路径解耦，199 处 → 8 个变量）
已完成 → MOD-05（依赖与行数守卫，棘轮式）
已完成 → R14-04（opencode --agent plan 核实：内置 primary agent，只读为工具级）
已完成 → R14-05（.cmd 下 prompt 失真：启动前拒绝换行/%VAR%）
已完成 → DX-17（目录式 scope 生效 + --audit + --selftest）
已完成 → MOD-06（src/ 物理分层：110 文件 / 8 子目录 / rename 87.3%）
待立卡 → DX-18（--audit 折叠已结卡，见 §1.7）
可穿插 → MOD-03 / MOD-04（拆 main.cpp，与 task 分层无依赖）
       → MOD-07 / MOD-08（QML）
       → MOD-09（静态库，可无限延期）
```

2026-11-04 收尾状态：四张卡全部结卡（`R14-04` `e75259e`、`R14-05` `09fd434`、`DX-17` `3204d3a`、
`MOD-06` `112b284` → 结卡 `8e4cae0` → 快照 `5a0fee6`），台账 120 张 / 已结 115。
在办只剩 `DX-12` / `DX-14` / `DX-15` / `DX-16` / `R14-02`，与本文档的卡无路径冲突。

`MOD-06` 实际执行后，§1.3 里那两处「预测」都成立：`tests/CMakeLists.txt` 只改 8 行、
`skeleton` 递归扫不受影响；但多冒出一条当时没预料到的连带改动 —— 格式脚本把纯改名当改动
（119 个文件里 89 个被误判），以及本节的 `DX-18`。

与 v4 §6.3 的差异只有四处：`MOD-01` 取消/并入、`MOD-05` 依赖改为 `MOD-02`、
`BLD-02` 验收线改 130、`MOD-06` 的 rename 断言放宽到 85%。前两处是方案自身不自洽，
后两处是方案写完之后现状变了（见 §2.0）。

---

## 4. 本文件不做什么

- 不改 `docs/board-ledger.json`（不加卡、不改 `scope`/`acceptance`/`baseline`）；
- 不认领任何编号，不建分支，不跑 `agent-init.ps1`；
- 不构成对 §7.4 的替代，只是四张卡（`MOD-06` / `DX-17` / `R14-05` / `DX-18`）的建卡请求
  + 五处质疑（§2.0 预研核查、§2.1～§2.4）。前三张已由维护者立卡并结卡；`DX-18` 立卡中。
