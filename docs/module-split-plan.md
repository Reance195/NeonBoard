# NeonBoard 模块化终极方案

> **事实基线**：2026-10-30，工作区全量扫描（`src/` `qml/` `tests/` `CMakeLists.txt`
> `docs/board-ledger.json` `docs/project-map.json` `.github/workflows/ci.yml`）。
> 所有数字由脚本算出并可复跑，口径见 §10。
>
> **版本**：v3（终极版）。v1 为勘察结论；v2 复核修正了 6 处数字与 3 处遗漏；
> **v3 新增 §4「连带影响面」——这是 v1/v2 完全没覆盖、也是真正决定能否落地的部分**：
> `src/` 一旦物理分层，仓库里有 **196 + 131 + 2** 处硬编码路径会同时失效，
> 其中一处会让 `tst_projectmap` 直接红。
>
> 本方案**只做说明与排期**，不含代码改动。落地需维护者按 §7 授权立卡。

---

## 0. 一页速览（决策者读这段就够）

| 问题 | 结论 |
| --- | --- |
| 技术上能分层吗？ | **能**。8 个模块、11 条模块级边，修正 3 个类归属后**无环**，拓扑排序已跑通 |
| 主要收益是什么？ | **约束本项目 8 个模块间的依赖方向**（linker 级强制），防止 `plugin` 被 `task` 反向依赖这类退化 |
| 是性能优化吗？ | **不是**。`core` 自己就要 link Network + Quick + Sql，编译不会变快（§3.4） |
| 最划算的第一步？ | 抽 `main.cpp` 的 CLI 命令，但可搬量是 **846 行**不是 1180 行，其中 605 行是 `--selftest` |
| 最大的落地阻塞？ | **不是架构，是路径**。`tests/CMakeLists.txt` 有 **196 处** `${CMAKE_SOURCE_DIR}/src/XXX.cpp` 硬引用 |
| 现在能直接开工吗？ | **不能**。需先做 M0.5 路径解耦，否则一次搬运会同时打断 34 个测试二进制、2 张在办卡、全景面板链路 |
| 建议排在什么时候？ | **R14-01 / R14-03 之后**（这两张卡的 `scope` 指向将被搬运的文件，见 §6.1） |

**一句话**：架构论证已完成且可靠；剩下的问题全是工程性的——把散落在 5 类文件里的
`src/XXX.cpp` 硬编码路径先收拾干净，搬运本身反而简单。

---

## 1. 定位：它在路线图的哪里

### 1.1 现状：路线图里没有这一行

`docs/iteration-roadmap.md` §9 现有阶段 `R0`–`R14` + `DX`，模块化**不在任何一行里**。

按 DX-04 落地的机制，阶段条与阶段集合取自 §9 表格行序（`ProjectMap::stageOrder()`）。
所以模块化要能被看板看见，必须：

1. 在 §9 表格**新增一行**（建议 `MOD`，主题「架构分层」）；
2. 卡片短号用 `MOD-01` ~ `MOD-09`。

> 卡号合法性：`scripts/task_gate.py` 的短号正则是 `[A-Za-z]{1,4}\d*-\d+`，`MOD-01` 合法。
> 不复用 `R14` 是因为它已有明确主题（Agent 插件「装了没生效」），混进去会让 §9 那一行失真；
> 不用 `DX` 是因为 DX 定位是「开发体验」，而模块化的收益是依赖约束，不是体验。

### 1.2 与在办卡片的关系（8 张 todo + 1 张已领用）

| 卡 | 状态 | 与模块化的关系 |
| --- | --- | --- |
| `DX-13` | **已领用**（agent-dx13） | 无交集（看板快照同步） |
| `DX-12` | todo | 无交集（`scripts/**` + 文档） |
| `DX-14` | todo | 弱交集（文档与闸门同步，M5 会动 `.github/**`） |
| `DX-15` | todo | **有交集**：「纯脚本/配置类卡片免全量编译」，与 M5 的 CI 接入是同一片区域 |
| `DX-16` | todo | 弱交集（worktree 冷启动与编译缓存；M3 改 CMake 后缓存会失效一次） |
| `R14-01` | todo | **直接冲突**：`scope` 含 `src/PluginRuntime.*` `src/PluginLoader.*` → 将搬入 `src/plugin/` |
| `R14-02` | todo | 弱交集（`CMakeLists.txt` / `installer` / `scripts`，M3 同区） |
| `R14-03` | todo | **直接冲突**：`scope` 含 `src/TaskRunner.*` → 将搬入 `src/agent/` |
| `R14-04` | todo | 无交集（纯文档核实） |

### 1.3 要不要做：三条诚实的判断

- **做的理由**：`main.cpp` 1910 行依赖 51/55 个类；`Main.qml` 4260 行（上一轮拆到 1180 又涨回来 3.6 倍）；
  加一个 QML 组件要同步改 2 处 CMake。这些是真实的日常成本。
- **不做的理由**：静态库不提速、不减 Qt 链接（§3.4）；`docs/pitfalls.md` 记过 QML 拆分引入运行时加载失败，
  重构本身有回归风险；当前还有 8 张 todo 卡，其中 4 张是**用户能感知的缺陷**（R14 Agent 插件装了不生效）。
- **折中**：**不做 M3（静态库）也行**——`MOD-06`（物理分目录）+ `MOD-05`（守卫）就够拿到大部分收益，
  `MOD-09` 可以无限延期（待拍板 P2）。

---

## 2. 现状证据

### 2.1 `src/` 零分层

110 个 `.h/.cpp`（55 个类 × 2）平铺在 `src/` 顶层，唯一子目录是 `host/`（`HostMain.cpp`，283 行）。
边界只存在于读者脑中，任何 `#include` 都能成立，编译器不会反对。

### 2.2 `main.cpp` 超载

| 指标 | 值 |
| --- | --- |
| 行数 | 1910 |
| 直接依赖的类 | 51 / 55 |
| 命令行 flag | 37 |
| CLI 派发块（花括号配对实测） | **846 行 / 10 块** |
| 其中 `--selftest` | **605 行**（531–1135），占 CLI 的 72% |
| 其余 9 块 | 241 行 |
| `setContextProperty` | **45 处**（1673–1730） |
| `return 0;` / `return 2;` | 22 / **23** |

CLI 分支穿插在对象构造之间（479–504 在构造前，1227–1630 在构造后），每个命令依赖的对象集合不同，**不能整体剪切**。

#### 2.2.1 CLI 派发块清单

| 行范围 | 行数 | 命令 |
| --- | --- | --- |
| 479–489 | 11 | `--assoc-status` |
| 490–504 | 15 | `--register-file-assoc` / `--unregister-file-assoc` |
| 531–1135 | **605** | `--selftest` |
| 1227–1317 | 91 | `--plugin-report` |
| 1360–1372 | 13 | `--market-sources` |
| 1374–1391 | 18 | `--market-refresh` |
| 1431–1436 | 6 | `--preview-list` |
| 1524–1529 | 6 | `--update-sources` |
| 1545–1568 | 24 | `--check-update` |
| 1574–1630 | 57 | `--update-apply` |

#### 2.2.2 三种派发写法（命令表必须一次设计到位）

37 个 flag 用了**三套**写法，只按第一种设计会搬到一半被迫改签名：

| 写法 | 形态 | 实例 |
| --- | --- | --- |
| ① 布尔 flag | `if (arguments.contains("--x"))` | 上表 10 块 |
| ② 可重复 flag + 取值 | `for (i) if (arguments.at(i) == "--market-source") ...at(i+1)` | 1332–1344 |
| ③ flag + 单值 | `indexOf("--market-install")` 再 `at(idx+1)` | 1393–1396；`--install-plugin-url` 1410 同款 |

失败路径**并非都返回 0**：23 处 `return 2;`。命令表返回值应为 `int`（0 / 2），
否则退出码变更会让 CI 里靠退出码判定的步骤误判。

#### 2.2.3 `utilityMode` 谓词：隐藏耦合点

`main.cpp` 152–184 是一个 **28 个 flag 的大 OR 谓词**（另有 `syncMode` 153–154、`dataMode` 155–157），
决定本次启动是否走无头模式。命令表搬走后它留在 `main.cpp`，**新增命令必须同时改两处**，
漏改则新命令不会被判为 utility 模式而**直接拉起 GUI**。

修法：`CliCommand` 带 `CliMode{Sync|Data|Utility}`，由 `allCommands()` 反推谓词，保住单点真相。

### 2.3 CMake 平铺且跨 target 重复

`CMakeLists.txt` 285 行，源文件逐个手写。两组重复登记（已核实）：

| 重复源文件 | 位置 |
| --- | --- |
| `AppPaths` / `SingleInstance` / `AppActions` | L51–55（`NeonBoard`）与 L227–229（`NeonBoardHost`） |
| `DeltaUpdate` / `BsdiffWrapper` / `thirdparty/*` | L90–100（`NeonBoard`）与 L259–265（`neonboard-delta-tool`） |

### 2.4 QML 侧：拆过，又涨回来了

`qml/Main.qml` **4260 行**，而台账「组件拆分 1605→1180」那张卡早已 `done`。
`CMakeLists.txt` 有 **31 条**手工 `QT_RESOURCE_ALIAS`（L137–167），新增组件要同步改 2 处。
**没有守卫的拆分会被日常开发吃掉**——这是 M5 必须存在的理由。

---

## 3. 目标分层

### 3.1 模块划分（55 个类全覆盖，无遗漏）

| 模块 | 类数 | 成员 |
| --- | --- | --- |
| `core` 基础设施 | 7 | `Database` `AppPaths` `SingleInstance` `AppActions` `Metrics` `UiSettings` `ScopeMatcher` |
| `task` 任务域 | 10 | `TaskModel` `TaskRunStore` `TaskExec` `TaskExtra` `TaskTree` `TaskInputParser` `TaskUndo` `TaskFilterModel` `TaskAudit` `TaskStats` |
| `plugin` 插件宿主 | 9 | `PluginRuntime` `PluginMarket` `PluginLibrary` `PluginLoader` `PluginContext` `PluginPackage` `PluginHost` `PluginEventBus` `HookEventQueue` |
| `agent` 自动化链 | 8 | `AgentProfile` `WorkflowRunner` `WorkflowPresets` `TriageRunner` `AcceptanceRunner` `Scheduler` `LedgerImport` `TaskRunner` |
| `board` 看板与呈现 | 8 | `PreviewHub` `ProjectMap` `RoadmapSync` `BoardSync` `BoardColumns` `ProjectStore` `CardDetail` `DataPort` |
| `shell` 窗口与外壳 | 8 | `WindowsIntegration` `SplashWindow` `WindowController` `TrayController` `DragFeel` `Hotkey` `KeyMove` `ReminderScheduler` |
| `update` 更新与差分 | 3 | `UpdateManager` `DeltaUpdate` `BsdiffWrapper` |
| `theme` 主题 | 2 | `ThemeMarket` `ThemePack` |

`thirdparty/`、`resources/`、`src/host/` 不在拆分范围。

### 3.2 依赖层序（已验证无环）

```
L0  core      ← 不依赖任何本项目模块
L1  task      → core
L2  theme     → core
L3  update    → core
L4  agent     → core, task
L5  board     → core, task
L6  shell     → task
L7  plugin    → core, task, shell
```

模块级边 **11 条**，实测：

```
agent  → core, task      shell  → task       theme  → core
board  → core, task      task   → core       update → core
plugin → core, shell, task
```

- `core` 被 6 个模块依赖；`Database` 入度 **23**（最高），`TaskModel` **18**（第二）
- `theme` / `board` / `update` **无人依赖**，是纯叶子

### 3.3 必须先行修正的三个归属（MOD-01）

原始聚类有 3 组双向环，全部由**单个类放错组**造成：

| 反向边 | 处置 |
| --- | --- |
| `DataPort.cpp → TaskModel.h`（`core ↔ task` 环） | `DataPort` 划归 **`board`**。它是给 QML 用的数据端口，是呈现层桥 |
| `HookEventQueue.cpp → PluginEventBus.h`（`core ↔ plugin` 环） | `HookEventQueue` 划归 **`plugin`**。它是 git 钩子事件转插件事件的桥 |
| `TaskRunner.cpp → AgentProfile.h`（`task ↔ agent` 环） | `TaskRunner` 划归 **`agent`**。它是执行器，属自动化链而非数据模型 |

修正后三组环全部消失。**只是归属声明的更正，不移动文件、不改代码。**

余下 `plugin → shell` 2 条边（`PluginRuntime.cpp` / `PluginContext.cpp` → `TrayController.h`）是
单向合法的（shell L6 < plugin L7），保留。更彻底的做法是接口反转，列为待拍板 P3。

### 3.4 预期管理：静态库 ≠ Qt 解耦

| 模块 | 需要的 Qt 组件 |
| --- | --- |
| `core` | Sql、Network（`QLocalServer`）、Quick（`QQuickWindow`）、Core（`QProcess`/`QSettings`） |
| `task` | Sql、Core |
| `agent` | Sql、Core（`QProcess`） |
| `board` | Sql、Core（`QProcess`、`QFileSystemWatcher`、`QThreadPool`） |
| `plugin` | **Qml**（`QQmlApplicationEngine`/`QQmlComponent`/`QQmlContext`/`QQmlEngine`）、Sql、Network、Core |
| `theme` / `update` | Network、Core |
| `shell` | Widgets、Quick、Core |

`core` 里的 `Metrics` 依赖 `QQuickWindow`、`SingleInstance` 依赖 `QLocalServer`——
**所谓「基础设施」并不纯净**。因此 `MOD-09` 的收益只能如实定义为「依赖方向被 linker 强制」，
不是减少 Qt 链接、也不是缩短编译时间。**别按性能立项。**

---

## 4. 连带影响面（v3 核心新增，v1/v2 完全没覆盖）

> 这是本方案最重要的部分。`src/` 物理分层**不是纯搬运**：仓库里有 5 类文件硬编码了
> `src/XXX.cpp` 形式的路径，M2 一次 `git mv` 会让它们同时失效。
> **v2 的 M2 `scope` 只写了 `src/**` + `CMakeLists.txt`，漏了 `tests/CMakeLists.txt`**——
> 按 `check_scope.py` 的精确匹配，那一批会被判越界拦截。

### 4.1 全景：5 类硬编码路径

| # | 位置 | 数量 | M2 后后果 | 严重度 |
| --- | --- | --- | --- | --- |
| 1 | `tests/CMakeLists.txt` | **196 处**引用 / 36 个去重文件 | 34 个测试二进制**链接失败**，ctest 全红 | 🔴 阻断 |
| 2 | `docs/board-ledger.json` 卡片 `scope` | **131 条**精确路径 / 33 个文件 | 在办卡改动被 `check_scope.py` 判**越界** | 🔴 阻断 |
| 3 | `docs/project-map.json` 链路证据 | `src/BoardSync.cpp` / `.h` | `tst_projectmap` **直接红**（`missing == 0` 断言） | 🔴 阻断 |
| 4 | `CMakeLists.txt` 源列表 | 285 行中的源清单 | 主程序构建失败 | 🟠 M2 本就要改 |
| 5 | 文档引用（`agent-adapters.md`、`agent-provider-api.md`、`adr/0002`、`AGENTS.md`） | 若干 | 文档过期，不阻断构建 | 🟡 低 |

### 4.2 `tests/CMakeLists.txt`：196 处引用，覆盖全部 8 个模块

443 行、**37 个测试 target**、196 处 `${CMAKE_SOURCE_DIR}/src/XXX.cpp`，去重 **36 个文件**：

| 模块 | 文件数 | 文件 |
| --- | --- | --- |
| `task` | 10 | `TaskModel` `TaskRunStore` `TaskExec` `TaskExtra` `TaskTree` `TaskInputParser` `TaskUndo` `TaskFilterModel` `TaskAudit` `TaskStats` |
| `agent` | 8 | `AgentProfile` `WorkflowRunner` `WorkflowPresets` `TriageRunner` `AcceptanceRunner` `Scheduler` `LedgerImport` `TaskRunner` |
| `core` | 5 | `Database` `AppPaths` `AppActions` `UiSettings` `ScopeMatcher` |
| `board` | 4 | `BoardColumns` `CardDetail` `ProjectMap` `ProjectStore` |
| `shell` | 3 | `DragFeel` `KeyMove` `WindowsIntegration` |
| `update` | 3 | `UpdateManager` `DeltaUpdate` `BsdiffWrapper` |
| `theme` | 2 | `ThemeMarket` `ThemePack` |
| `plugin` | 1 | `PluginPackage` |

**处置**：在 M0.5 把这些引用改成集中定义（例如在 `tests/CMakeLists.txt` 顶部定义
`NB_SRC_<MODULE>` 变量，或提供 `nb_add_test(name srcs...)` 函数），让 M2 只需改一处映射。
**这是 M0.5 的主要工作量，也是把 M2 从「高风险大爆炸」降为「纯搬运」的关键。**

### 4.3 台账 `scope`：131 条精确路径，2 张在办卡直接冲突

`docs/board-ledger.json` 的 115 张卡共 249 条 `src` 相关 `scope`，其中：

- **131 条是精确单文件路径**（如 `src/PluginRuntime.cpp`），涉及 33 个文件；
- 116 条是 glob（`src/**` 等），**不受影响**。

`check_scope.py` 用 `glob_to_regex` 把 `scope` 编译成正则做匹配，
所以 `src/PluginRuntime.cpp` **不会**匹配 `src/plugin/PluginRuntime.cpp`。

**未完成卡中会被直接打断的**：

| 卡 | 冲突的 `scope` 条目 | 文件将搬入 |
| --- | --- | --- |
| `R14-01` | `src/PluginRuntime.cpp` / `.h`、`src/PluginLoader.cpp` / `.h` | `src/plugin/` |
| `R14-03` | `src/TaskRunner.cpp` / `.h` | `src/agent/` |

> 已完成卡（107 张）的 `scope` 是历史记录，`check_scope` 只查本次 diff，不阻断；
> 但**在办卡必须改**。改 `scope` 属**任务定义变更**（DX-10），须单独一条 `chore(board):`
> 提交 + 「授权：」「批次：」两行 + 不得与代码路径同提交。

### 4.4 `docs/project-map.json`：链路证据会断链 → `tst_projectmap` 红

`chain` 的「看板 ↔ 仓库同步」环节挂的证据是 `src/BoardSync.cpp` / `src/BoardSync.h`。
`ProjectMap.cpp` L310 逐个校验证据存在性，而 `tests/tst_projectmap.cpp` L118 断言
`node.value("missing").toInt() == 0`。

`BoardSync` 属 `board` 模块 → 搬到 `src/board/` 后**该断言直接失败**。

对照：`skeleton` 的「内核 C++」组用 `QDirIterator::Subdirectories`（`ProjectMap.cpp` L282）
**递归**扫 `src`，所以骨架**不受影响**。

**处置**：M2 同批改 `docs/project-map.json` 的两条证据路径。
**M2 的 `scope` 必须包含 `docs/project-map.json`**——这是 v2 漏掉的。

### 4.5 文档引用（不阻断，但要同步）

- `docs/agent-adapters.md:144` → `src/AgentProfile.cpp:194`
- `docs/agent-provider-api.md:14` → `src/AgentProfile.cpp:194`
- `docs/adr/0002-run-state-and-executor-safety.md:5` → `src/TaskExec.*`、`src/Database.cpp`
- `AGENTS.md:423 / 695` → `src/main.cpp`（**待拍板 P4 若决定给 `main.cpp` 搬家，这两处要改**）

---

## 5. 分批计划

排期原则：**先解耦路径，再搬运；先立守卫，再大改。**

| 批次 | 内容 | 触及文件 | 风险 | 前置 |
| --- | --- | --- | --- | --- |
| **M0**（`MOD-01`） | 更正 3 个类的模块归属（仅声明，零代码） | 本文件 / 台账 | 无 | — |
| **M0.5**（`MOD-02`） | **路径解耦**：tests 196 处引用集中化 + 在办卡 `scope` 改 glob | `tests/CMakeLists.txt`、`docs/board-ledger.json` | 低 | M0 |
| **M1**（`MOD-03`） | 抽 `main.cpp`：命令表 + `AppContext` + 装配器 + 构造工厂 | `src/main.cpp`、`src/app/**` | 中 | M0.5 |
| **M1.5**（`MOD-04`） | `--selftest` 605 行与 `tests/` 34 个用例对齐 | `src/main.cpp`、`tests/**` | 低 | 可与 M1 并行 |
| **M5**（`MOD-05`） | 守卫：依赖方向 + 行数上限脚本接入 CI | `scripts/**`、`.github/**` | 低 | M1 |
| **M2**（`MOD-06`） | `src/` 物理分层为 8 个子目录 | `src/**`、`CMakeLists.txt`、**`tests/CMakeLists.txt`**、**`docs/project-map.json`** | 中 | M5 + M0.5 |
| **M4b**（`MOD-07`） | QML 多模块 URI 化，消除 31 条手工 alias | `qml/**`、`CMakeLists.txt` | 中 | M2 |
| **M4a**（`MOD-08`） | `Main.qml` 4260 → ≤1200 行 | `qml/Main.qml`、`qml/components/**` | 中 | **M4b** |
| **M3**（`MOD-09`） | 8 个 CMake `STATIC` target + 显式依赖 | `CMakeLists.txt` | 中高 | M2（可无限延期，见 P2） |

> **为什么新增 M0.5**：没有它，M2 会同时打断 34 个测试二进制 + 2 张在办卡 + 全景面板链路，
> 一次 diff 覆盖全仓库，出事无法二分回滚。有了它，M2 退化为「改一处映射 + `git mv`」。

### M0.5 —— 路径解耦（M2 的前置）

1. `tests/CMakeLists.txt`：把 196 处 `${CMAKE_SOURCE_DIR}/src/XXX.cpp` 收敛为
   「模块 → 文件列表」的集中定义（变量或函数），使 M2 只需改一处。
2. `docs/board-ledger.json`：把 2 张在办卡（`R14-01` / `R14-03`）的精确 `scope`
   改为 glob（`src/plugin/**`、`src/agent/**`）。
   **必须单独一条 `chore(board):` 提交，body 含「授权：」「批次：」两行。**
3. 可选：把 131 条历史精确 `scope` 批量改 glob，降低以后再踩的概率
   （同属任务定义变更，同样需要授权行）。

**验收**：`ctest --test-dir build -C Release` 全绿（34/34）；
`python scripts/check_scope.py` 对在办卡仍能正确判定；
`python scripts/task_gate.py selftest` 通过。

### M1.5 —— `--selftest` 与 ctest 对齐

`--selftest` 单块 605 行（531–1135），占 CLI 的 72%。它不是「命令行功能」，
而是内联的手写测试脚本（`addTask` → `taskById` → `updateTask` → `moveTask` …），
与 `tests/` 下 34 个 `tst_*.cpp` **职责重叠**。

先对齐再搬迁：能迁 `tests/` 的迁走 → M1 的搬迁量从 846 行降到 **241 行**（工作量 −70%）；
迁不走的抽成 `SelfTestRunner`；顺带消除「自检逻辑有两份」。

**验收**：`ctest` 全绿且用例数不减少；`--selftest` 输出与对齐前逐字节一致。

### M1 —— 拆 `main.cpp`

目标：**≤ 450 行**。
（v2 定的 `< 400` 在自身范围内算不出来：1910 − 846 − 104 ≈ 960，缺口没有任何批次负责；
补 M1d 后可达 ≤450。）

**M1a · `AppContext` 指针聚合** —— 覆盖 **45** 个 `setContextProperty` 注入点，不是 20 个。
不在这一步搬对象构造。

**M1b · CLI 命令表**（M1.5 之后搬迁量 241 行）

```cpp
enum class CliMode { Gui, Sync, Data, Utility };   // 反推 utilityMode 谓词，见 §2.2.3

struct CliCommand {
    QString flag;
    QString summary;
    CliMode mode{CliMode::Utility};   // 决定本次启动是否无头
    int arity{0};                     // 0=布尔 flag；n=取 n 个值
    bool repeatable{false};           // 对应写法 ②
    // 返回退出码：0=成功并消费启动，2=失败（现状 23 处 return 2），-1=未命中
    std::function<int(const QStringList &args, AppContext &ctx)> run;
};
```

三种派发写法必须一次设计到位（§2.2.2）。每搬一条跑一次 `--selftest` 并与基线**逐字节比对**。

**M1c · 连线与启动装配器**（约 104 行）：`WireUp`（1795–1866）+ `StartupSequencer`（1868–1898）。

**M1d · 对象构造工厂**（约 700 行，达成 ≤450 的关键，v1/v2 完全缺失）
抽 `buildAppObjects(AppContext &)`。**必须保留构造次序**——416 行注释
「必须声明在 `QQmlApplicationEngine` 之前」是硬约束，不得重排。

**验收**：`--selftest` / `--smoke` / `ctest` 全绿；逐条 CLI 输出与退出码（0/2）与基线一致；
新增一条命令后仍走无头模式（不拉起 GUI）；`(Get-Content src/main.cpp).Count` ≤ 450。

### M2 —— `src/` 物理分层

```
src/
  core/  task/  plugin/  agent/
  board/ shell/ update/  theme/
  app/   host/  main.cpp
```

8 个子目录全量加 include 路径，**`#include "Database.h"` 不必改**（搬运与改 include 分离）。

**M2 必须同批修改的 4 处**（v2 只列了 2 处）：

1. `CMakeLists.txt` 源列表
2. **`tests/CMakeLists.txt`**（M0.5 之后只改映射一处）
3. **`docs/project-map.json`** 的 `src/BoardSync.cpp` / `.h` 两条证据 → `src/board/`
4. 文档引用（§4.5，可另开一批）

**验收**：`--selftest` / `--smoke` / `ctest` 全绿；`git diff --stat` 主体为 rename；
`tst_projectmap` 绿（回归 §4.4）。

### M3 —— CMake 静态库

```cmake
foreach (m IN ITEMS core task theme update agent board shell plugin)
    add_library(nb_${m} STATIC ${NB_${m}_SOURCES})
    target_include_directories(nb_${m} PUBLIC src)
endforeach ()
target_link_libraries(nb_task   PUBLIC nb_core)
target_link_libraries(nb_agent  PUBLIC nb_core nb_task)
target_link_libraries(nb_shell  PUBLIC nb_task)
target_link_libraries(nb_plugin PUBLIC nb_core nb_task nb_shell)
```

落地要点：

- `CMAKE_AUTOMOC` 对每个 target 独立生效，moc 产物归属会变化，需重排 `resources/` 与编译选项；
  建议**先单独验证 `nb_core` 一个库**再推广。
- `NeonBoardHost` 改链 `nb_core`，`neonboard-delta-tool` 改链 `nb_update`，消除两组重复登记。
- **`src/DeltaUpdate.cpp` L404–478 有 `#ifdef NEONBOARD_DELTA_TOOL_MAIN` 包着的 `main()`**——
  该宏只能在 `neonboard-delta-tool` 上定义，否则静态库多出一个 `main` 符号与主程序冲突（R6）。
- **`nb_plugin` 必须 link `Qt::Qml`**（§3.4），否则链接期直接失败。
- 三个 target 都保留 `WIN32_EXECUTABLE TRUE`。

**验收**：三个 target 均构建成功；`ctest` 全绿；
`build-delta.ps1` 产出的 delta 包可被 `--update` 正常消费。

### M4 —— QML 模块化（顺序已反转）

**M4b 先做**：`NeonBoard.Core` / `NeonBoard.Components` / `NeonBoard.Theme` 各带 `qmldir`，
从根上消除 31 条手工 alias。

**M4a 后做**：`Main.qml` 4260 → ≤1200 行。现有 **10 个** `Loader` 是天然分组依据
（v2 曾写作「8 个」但列了 10 项）。

> 反过来的理由：改 URI 会动所有 `import`，若先拆 `Main.qml` 再改 URI，
> 两批改动叠加在同一批文件上，冲突概率高。先立边界，再在稳定边界内拆。

**验收**：`--smoke` 首屏无 QML 报错；
`(Get-Content qml/Main.qml).Count` ≤ 1200；`CMakeLists.txt` 中 `QT_RESOURCE_ALIAS` 语句数为 0。

### M5 —— 守卫（已提前到 M2 之前）

新增 `scripts/check_module_deps.py`：扫描模块间 `#include`，发现反向依赖即退出非 0；
`Main.qml` / `main.cpp` 行数上限作为 `-max-lines` 参数。接入 `ci.yml` 与 `verify.ps1`。

**照 `scripts/check_scope.py` 的棘轮模式写**（只检查本次改动涉及的文件，存量不阻塞），
不要全量扫描——否则首次接入就红，又会被 `--no-verify` 绕过。仓库已有
`check_scope.py` / `check_script_hygiene.py` / `check-perf.ps1` / `check-format.ps1`
四个同类脚本与对应 CI step，直接复用其形态与输出约定。

> 不能省：`Main.qml` 从 1180 涨回 4260 就是因为当年只有一次性拆分、没有持续约束。
> **提前到 M2 之前的理由**：M2 是纯搬运，正是守卫最该在场、也最能防止搬运引入反向依赖的时点。

---

## 6. 与在办卡片的冲突与排序

### 6.1 硬冲突：`R14-01` / `R14-03` 必须先于 M2

这两张卡的 `scope` 指向将被搬运的文件（§4.3）。三条路：

| 方案 | 做法 | 代价 |
| --- | --- | --- |
| **A（推荐）** | `R14-01` / `R14-03` 先做完并结卡，M2 在它们之后 | 只是排队，零额外成本 |
| B | M2 先做，同时在 M0.5 改这两张卡的 `scope` 为 glob | 需维护者在 M0.5 授权改契约；两批改动可能叠加 |
| C | 并行 | ❌ 不可行——同一批文件两个会话同时改 |

**推荐 A**：R14 是用户可感知的缺陷（Agent 插件装了不生效），优先级本来就高于架构重构；
且 R14 交付后，M2 搬运 `src/plugin/` `src/agent/` 时没有在制品。

### 6.2 弱冲突：与 `DX-15` / `DX-16` 同区

- `DX-15`（验收口径分层：纯脚本/配置类卡片免全量编译）与 M5 都要改 `verify.ps1` / `AGENTS.md`。
  建议 M5 排在 DX-15 之后，或合并讨论。
- `DX-16`（worktree 冷启动与编译缓存）与 M3 相关：改 CMake 会让 sccache 失效一次，
  属于可接受的一次性成本，记入风险 R13。

### 6.3 建议的整体顺序

```
现在  → R14-01 / R14-03（修缺陷，用户可感知）
      → MOD-01 归属更正 → MOD-02 路径解耦
      → MOD-03 拆 main.cpp（+ MOD-04 selftest 对齐，可并行）
      → MOD-05 守卫
      → MOD-06 物理分层          ← 此时 tests / 台账 / 面板三处都已解耦
      → MOD-07 QML URI → MOD-08 拆 Main.qml
      → MOD-09 静态库（可无限延期，见 P2）
```

---

## 7. 卡片登记建议

按宪章 §2 条款 6，立卡**只能由维护者执行**（或在会话中对具体批次显式授权后由 agent 代登记，
提交前缀 `chore(board):`、body 含「授权：」「批次：」两行）。

按 `docs/task-card.md` 五段式（目标 / 验收 / 边界 / 输入 / 产出）给出建议。
**建议在 `docs/iteration-roadmap.md` §9 新增 `MOD 架构分层` 一行**，否则这些卡在阶段条上不可见
（阶段集合取自 §9 行序，见 §1.1）。

| 卡号 | 标题 | `scope` | `depends` |
| --- | --- | --- | --- |
| `MOD-01` | 更正 3 个类的模块归属（仅声明） | `docs/module-split-plan.md`, `docs/board-ledger.json` | — |
| `MOD-02` | 路径解耦：tests 源引用集中化 + 在办卡 scope 改 glob | `tests/CMakeLists.txt`, `docs/board-ledger.json` | `MOD-01` |
| `MOD-03` | 抽 `main.cpp`：命令表 + AppContext + 装配器 + 构造工厂 | `src/main.cpp`, `src/app/**`, `CMakeLists.txt` | `MOD-02` |
| `MOD-04` | `--selftest` 与 ctest 对齐，605 行瘦身 | `src/main.cpp`, `tests/**`, `tests/CMakeLists.txt` | — |
| `MOD-05` | 依赖方向与行数守卫脚本 + CI 接入 | `scripts/**`, `.github/**`, `AGENTS.md` | `MOD-03` |
| `MOD-06` | `src/` 物理分层为 8 个子目录 | `src/**`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/project-map.json` | `MOD-05`, `MOD-02` |
| `MOD-07` | QML 多模块 URI 化，消除 31 条手工 alias | `qml/**`, `CMakeLists.txt` | `MOD-06` |
| `MOD-08` | `Main.qml` 编排逻辑再拆分（4260 → ≤1200） | `qml/Main.qml`, `qml/components/**` | `MOD-07` |
| `MOD-09` | CMake 拆分为 8 个静态库 target | `CMakeLists.txt` | `MOD-06` |

**关键 `acceptance` 示例**（须可执行，禁止主观描述）：

```text
MOD-02: ctest --test-dir build -C Release 全绿（34/34）
        python scripts/check_scope.py 对 R14-01 / R14-03 仍能正确判定
MOD-03: (Get-Content src/main.cpp).Count <= 450
        逐条 CLI 输出与退出码（0 / 2）与重构前基线逐字节一致
        新增一条命令后仍走无头模式，不拉起 GUI
MOD-06: --selftest / --smoke / ctest 全绿；tst_projectmap 绿
        git diff --stat 主体为 rename
MOD-08: (Get-Content qml/Main.qml).Count <= 1200
        CMakeLists.txt 中 QT_RESOURCE_ALIAS 语句数为 0
        --smoke 首屏无 QML 报错
```

### 待拍板事项

- **P1**：M1 是否引入 `AppContext` 抽象？推荐引入（否则命令表无处挂靠）。
- **P1.5**：M1d（构造工厂）做不做？做则 `main.cpp` ≤450 行，但要动次序敏感的 ~700 行；
  不做则验收线必须改成 ≤950。**不定则 MOD-03 无法立卡。**
- **P2**：`MOD-09`（静态库）是否值得做？§3.4 已说明它不提速、不减 Qt 链接，
  收益是「依赖方向被 linker 强制」。只想要可读性的话 `MOD-06` + `MOD-05` 就够了，可无限延期。
- **P3**：`plugin → shell` 的托盘回调耦合是否做接口反转？做则插件层不再依赖窗口/托盘类型，
  代价是 `PluginRuntime` 对外契约要改。
- **P4**：`src/app/` 是否为 `main.cpp` 的新家？若是，`AGENTS.md` L423 / L695 两处要同步改。
- **P5**（新增）：新阶段叫什么？建议 `MOD`，需在路线图 §9 增行才能被阶段条看见。

---

## 8. 风险登记

| # | 风险 | 等级 | 缓解 |
| --- | --- | --- | --- |
| R1 | M1b 搬迁 CLI 改变启动语义（提前 return 顺序变了） | 中 | 逐条搬迁 + 每条保存重构前 stdout 基线做逐字节比对 |
| R2 | M2 改路径后构建失败或 moc 找不到头文件 | 中 | 8 个子目录全量加 include 路径，不改动任何 `#include` 文本 |
| R3 | M3 的 AUTOMOC 边界变化导致 moc 产物丢失 | 中高 | 单独分支先行验证 `nb_core` 一个库，跑通再推广 |
| R4 | M4 的 QML 拆分引入运行时加载失败（历史踩过） | 中 | `--smoke` 作硬门禁；参考 `docs/pitfalls.md` 第 14 节 |
| R5 | 重构期间与并行会话抢同一个文件 | 高 | 分支命名 `refactor/<批次>`，用 `-Card` 领用隔离；动手前 `git worktree list` |
| R6 | `src/DeltaUpdate.cpp` L404–478 内置 `#ifdef NEONBOARD_DELTA_TOOL_MAIN` 的 `main()`；做成静态库后宏泄漏会出现重复 `main` 符号 | 中高 | 该宏只在 `neonboard-delta-tool` 上 `target_compile_definitions`；三个 exe 均须能链接启动 |
| R7 | 命令表搬走后 `utilityMode` 谓词未同步，新增命令漏判无头模式而拉起 GUI（§2.2.3） | 中 | `CliCommand` 带 `CliMode`，由 `allCommands()` 反推谓词；验收含该回归 |
| R8 | 命令表只按布尔 flag 设计，搬到可重复取值/取单值命令时被迫改签名返工（§2.2.2） | 中 | 首个命令落地前就把 `arity` / `repeatable` 设计进 `CliCommand` |
| R9 | M1 按 `<400 行` 验收，做到 ~960 行就判失败而缺口无人负责 | 中 | 补 M1d，或把验收线改为 ≤950（P1.5 必须先拍） |
| **R10**（新增） | M2 一次搬运打断 `tests/CMakeLists.txt` 196 处引用 → 34 个测试二进制链接失败 | **高** | **M0.5 先做路径解耦**；`MOD-06` 的 `scope` 必须含 `tests/CMakeLists.txt` |
| **R11**（新增） | M2 后 `docs/project-map.json` 的 `src/BoardSync.*` 证据失效 → `tst_projectmap` 红 | **高** | M2 同批改这两条证据路径；`MOD-06` 的 `scope` 含 `docs/project-map.json` |
| **R12**（新增） | 在办卡 `R14-01` / `R14-03` 的 `scope` 指向被搬运文件，改动被 `check_scope.py` 判越界 | 中 | 走 §6.1 方案 A（R14 先做），或 M0.5 授权改契约 |
| **R13**（新增） | M3 改 CMake 让 sccache 失效一次，与 `DX-16` 的冷启动优化撞车 | 低 | 记入 `MOD-09` 说明，接受一次性成本 |

---

## 9. 验收统一口径

每一批都必须通过全量：

```powershell
pwsh -File scripts/build.ps1 -SelfTest
build/Release/NeonBoard.exe --selftest
build/Release/NeonBoard.exe --smoke
ctest --test-dir build -C Release
python scripts/task_gate.py selftest
```

**M2 额外必查**（针对 §4 的连带影响）：

```powershell
ctest --test-dir build -C Release -R tst_projectmap --output-on-failure   # 面板链路不许断
python scripts/check_scope.py                                             # 在办卡不许判越界
```

**回滚**：每批一个独立分支，CI 全绿后走 `scripts/git-done.ps1` 合并。任一批失败直接弃分支。

---

## 10. 复跑清单与统计口径

**口径**（后续复跑请沿用）：

- 行数 28850 = `src/**.h` + `src/**.cpp`，**不含** `src/host/`（283 行）；含 `host/` 为 29131。
- 「110 个 `.h/.cpp`」= 55 个类的 55 `.h` + 55 `.cpp`，**不含** `main.cpp`；含 `main.cpp` 为 111。
- 类数 55（按 `.h` 计数）；`main.cpp` 不是类，单列。
- 类入度按去重后的 `#include` 关系计，含 `.h→.h` 与 `.cpp→.h`，含 `main.cpp` 的引用。

| 项 | 期望值 | 取数方式 |
| --- | --- | --- |
| 类数 | 55 | `ls src/*.h \| wc -l` |
| 类文件数（不含 main.cpp） | 110 | 55 `.h` + 55 `.cpp` |
| C++ 总行数（不含 `host/`） | 28850 | 含 `host/` 为 29131 |
| `main.cpp` 行数 | 1910 | `wc -l src/main.cpp` |
| CLI 派发块 | 846 行 / 10 块 | **按花括号配对量**，勿按行区间估算 |
| `--selftest` 单块 | 605（531–1135） | |
| `setContextProperty` | 45 | 勿沿用「约 20」 |
| `QT_RESOURCE_ALIAS` | 31（`CMakeLists.txt` 137–167） | |
| `return 0;` / `return 2;` | 22 / 23 | |
| `Database` / `TaskModel` 入度 | 23 / 18 | 去重 `#include`，含 `main.cpp` |
| 模块级边 / 类级边 | 11 / 110 | |
| `tests/CMakeLists.txt` src 引用 | 196 处 / 36 文件 / 37 target | |
| 台账 `scope` 精确 src 路径 | 131 条 / 33 文件 | |
| 台账卡片 | 115（107 done / 8 todo） | |

> **v2 的教训**：本文初版有 6 处数字错误、3 处事实遗漏，根因是「算过一次就当成事实」
> 而没有留下可复跑的脚本。**建议把扫描脚本固化为 `MOD-05` 的 `scripts/check_module_deps.py`，
> 并让它第一个检查项就是本表数字的可复跑性**——让「现在可行」变成「以后仍然可行」。
>
> **v3 的教训**：v1/v2 只盯着 `src/` 内部，没看 `src/` 被谁引用。
> 任何「大规模移动文件」的重构，**第一件事是扫全仓库的硬编码路径引用**。
