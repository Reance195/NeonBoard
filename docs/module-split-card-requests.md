# 模块化建卡请求与质疑（草案）

> **状态：草案，未立卡。** 本文只递交请求，不改 `docs/board-ledger.json` 的 `cards` / `baseline`。
> 编号是建议（沿用 `docs/module-split-plan.md` v4 §7.4 的 `MOD-xx`），**实际编号以维护者立卡时为准**。
> 依据：`docs/module-split-plan.md` v4（合入提交 `a568cf3`），基线 `origin/main`。

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

```json
"MOD-05": {
  "stage": "MOD",
  "title": "MOD 守卫：模块依赖方向与行数上限接入 CI",
  "note": "起因：src/ 目前零分层，任何 #include 都能成立，编译器不会反对；Main.qml 从 1180 涨回 4260 就是「只有一次性拆分、没有持续约束」的实证。\n\n目标：新增 scripts/check_module_deps.py，检测反向依赖（如 task → agent）与文件行数上限，接入 ci.yml 与 verify.ps1。必须棘轮式：只检查本次改动涉及的文件，存量不阻塞——否则首次接入即红，会被 --no-verify 绕过，比没有守卫更糟。\n\n边界：只加脚本与 CI 接线，不改任何 C++ 代码、不搬文件。",
  "acceptance": [
    "python scripts/check_module_deps.py --base origin/main 在未引入违规的改动上退出 0（存量不红）",
    "在 src/TaskModel.cpp 里临时加一条反向依赖 #include \"TrayController.h\"，脚本退出非 0 并打印违反的模块对",
    "行数上限先设「不得增加」（main.cpp 1910 / Main.qml 4260），存量不红、新增超限即红",
    "ci.yml 与 scripts/verify.ps1 均调用该脚本，CI 新增 step 在 PR/推送页可见",
    "python scripts/task_gate.py selftest 与 check_script_hygiene.py 均退出 0"
  ],
  "scope": ["scripts/**", ".github/**", "AGENTS.md", "docs/testing.md"],
  "depends": ["MOD-02"]
}
```

### 1.3 【建议 `MOD-06`】`src/` 物理分层

| 段 | 内容 |
| --- | --- |
| **目标** | `src/` 平铺的 112 个文件按 8 个模块归入子目录，`main.cpp` 与 `host/` 留在顶层 |
| **验收** | 见 `acceptance` |
| **边界** | `src/**`、`CMakeLists.txt`、`tests/CMakeLists.txt`、`docs/project-map.json`、`docs/*.md` |
| **输入** | `depends: ["MOD-05", "MOD-02"]`，外加 `R14-03` 必须已结（见 §2.5） |
| **产出** | 合并提交 body 四段 |

**执行顺序**（写进 `note`，这是本卡最容易做错的地方）：

1. 先搬 `core`(7 类) + `task`(10 类) —— 两个最底层模块，搬完立刻验证；
2. 再搬 `board` / `shell` / `plugin` / `agent` / `update` / `theme`；
3. **`TaskRunner` 进 `src/agent/`，不是 `src/task/`**（§3.3 的归属修正）；
4. **`DataPort` 进 `src/board/`**、**`HookEventQueue` 进 `src/plugin/`** —— 三个易错归属；
5. 同批改 4 处：`CMakeLists.txt` 源列表、`tests/CMakeLists.txt`（MOD-02 后只改 8 行）、
   `docs/project-map.json` 的两条链路证据、文档路径引用（`AGENTS.md` / `agent-adapters.md` /
   `agent-provider-api.md` / `adr/0002`）。

```json
"MOD-06": {
  "stage": "MOD",
  "title": "MOD src/ 物理分层：8 个模块子目录 + 连带 4 处",
  "note": "起因：110 个 .h/.cpp（55 个类）平铺在 src/ 顶层，唯一子目录是 host/，边界只存在于读者脑中。\n\n目标：按 docs/module-split-plan.md §3.1 的 8 模块划分建立子目录，全量加 include 路径，保持 #include \"Database.h\" 写法不变（搬运与改 include 分离）。\n\n执行顺序：先 core + task 两个底层模块验证，再搬其余六个。易错归属：TaskRunner → agent（不是 task）、DataPort → board、HookEventQueue → plugin。\n\n同批必改 4 处：CMakeLists.txt 源列表、tests/CMakeLists.txt 映射、docs/project-map.json 的 BoardSync 证据、文档路径引用。",
  "acceptance": [
    "ctest --test-dir build -C Release 全绿（34/34）",
    "build/Release/NeonBoard.exe --selftest 与 --smoke 退出码 0",
    "git diff --stat --find-renames origin/main...HEAD 中 rename 占比 > 85%（其余为主 target 的 include 新增与源列表映射改动）",
    "tst_projectmap 绿（docs/project-map.json 的链路证据已同步）",
    "ls src/*.cpp 只剩 main.cpp，8 个模块子目录均存在且非空",
    "python scripts/check_module_deps.py（全量模式）退出 0",
    "python scripts/check_scope.py --base origin/main 退出 0"
  ],
  "scope": ["src/**", "CMakeLists.txt", "tests/CMakeLists.txt", "docs/**"],
  "depends": ["MOD-02", "MOD-05", "R14-03"]
}
```

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

### 2.5 `R14-03` 必须先结（方案 §6.1 方案 A）

它的 `scope` 含 `src/TaskRunner.cpp` / `.h`，而该文件将被搬进 `src/agent/`；
不结就搬，`check_scope.py` 会在搬运提交上判越界。方案 §6.1 推荐「先做完再 M2」，我认同——
`R14-03` 是用户可感知的缺陷（cursor-agent 拉不起来），优先级本来就高于架构重构。

---

## 3. 修订后的建议序列

```
现在   → R14-03 结卡（缺陷优先，且是 MOD-06 的硬前置）
       → MOD-02（路径解耦；合并 B1 第二提交）
       → MOD-05（守卫，棘轮式）
       → MOD-06（src/ 物理分层：先 core+task，再其余六个）
       → MOD-03 / MOD-04（拆 main.cpp，与 task 分层无依赖，可穿插或延后）
       → MOD-07 / MOD-08（QML）
       → MOD-09（静态库，可无限延期）
```

与 v4 §6.3 的差异只有四处：`MOD-01` 取消/并入、`MOD-05` 依赖改为 `MOD-02`、
`BLD-02` 验收线改 130、`MOD-06` 的 rename 断言放宽到 85%。前两处是方案自身不自洽，
后两处是方案写完之后现状变了（见 §2.0）。

---

## 4. 本文件不做什么

- 不改 `docs/board-ledger.json`（不加卡、不改 `scope`/`acceptance`/`baseline`）；
- 不认领任何编号，不建分支，不跑 `agent-init.ps1`；
- 不构成对 §7.4 的替代，只是三张卡的建卡请求 + 五处质疑（§2.0 预研核查、§2.1～§2.4）。
