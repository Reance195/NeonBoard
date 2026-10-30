# NeonBoard 模块化终极方案

> **事实基线**：2026-10-30，工作区全量扫描（`src/` `qml/` `tests/` `CMakeLists.txt`
> `docs/board-ledger.json` `docs/project-map.json` `.github/workflows/ci.yml`）。
> 所有数字由脚本算出并可复跑，口径见 §10。
>
> **版本**：v4。v1 为勘察结论；v2 复核修正了 6 处数字与 3 处遗漏；
> **v3 新增 §4「连带影响面」**——v1/v2 完全没覆盖、也是真正决定能否落地的部分：
> `src/` 一旦物理分层，仓库里有 **196 + 131 + 2** 处硬编码路径会同时失效，
> 其中一处会让 `tst_projectmap` 直接红。
> **v4 新增 §7「卡片体系」**（维护者反馈整合 + 外部参照）：责任边界、多套编号、
> 派发态与接手、多项目隔离与全局视图。**它与「怎么拆代码」正交——
> 即使模块化不做，这一章的四条照样适用。**
>
> 本方案**只做说明与排期**，不含代码改动。落地是**三方分工**：
> **维护者（人）**只做立卡、编号与拍板授权；**派发程序**负责开/关分支、开/回收 worktree、
> 注入环境变量、登记领用，并把整套环境作为「派发包」交给 agent；
> **agent**只在交给它的边界内执行具体内容、回报进度（§7.0）。
> 换言之 `agent-init.ps1` / `git-done.ps1` 的**调用方是派发程序，不是人、也不是 agent 自己**。

---

## 0. 一页速览（决策者读这段就够）

| 问题 | 结论 |
| --- | --- |
| 技术上能分层吗？ | **能**。8 个模块、11 条模块级边，修正 3 个类归属后**无环**，拓扑排序已跑通 |
| 主要收益是什么？ | **约束本项目 8 个模块间的依赖方向**（linker 级强制），防止 `plugin` 被 `task` 反向依赖这类退化 |
| 是性能优化吗？ | **本身不是**。但顺带能修掉一个真实的性能问题：测试侧 **160 次重复编译**（§2.5）；且 **B0/B1 不必等模块化**，建议先做 |
| 最划算的第一步？ | 抽 `main.cpp` 的 CLI 命令，但可搬量是 **846 行**不是 1180 行，其中 605 行是 `--selftest` |
| 最大的落地阻塞？ | **不是架构，是路径**。`tests/CMakeLists.txt` 有 **196 处** `${CMAKE_SOURCE_DIR}/src/XXX.cpp` 硬引用 |
| 现在能直接开工吗？ | **不能**。需先做 M0.5 路径解耦，否则一次搬运会同时打断 34 个测试二进制、2 张在办卡、全景面板链路 |
| 建议排在什么时候？ | **R14-01 / R14-03 之后**（这两张卡的 `scope` 指向将被搬运的文件，见 §6.1） |
| 卡片体系要改吗？ | 要。§7 提出：三方分工（人拍板 / 程序派发 / agent 执行）、**多套编号并存**、**派发态可观测（谁在跑/到哪了/断了谁接）**、**多项目隔离 + 全局视图**、**流程不能卡死** |
| 流程会不会卡死？ | 不该。§7.0 逐个死锁点给了出口：无卡→**待补卡**通道、程序挂→人手工兜底、上游弃→解除依赖、心跳误判→幂等重领 |
| 构建很慢怎么办？ | **与模块化解耦、且优先级更高**（§2.5 / §5 B0–B3）：sccache **从未安装**（缓存没生效）、测试侧 **160 次重复编译**、改 `AppPaths.h` 触发 **170 TU**。B0 是装个程序，零代码改动 |

**一句话**：架构论证已完成且可靠；剩下的问题全是工程性的——把散落在 5 类文件里的
`src/XXX.cpp` 硬编码路径先收拾干净，搬运本身反而简单。

---

## 1. 定位：它在路线图的哪里

### 1.1 现状：路线图里没有这一行

`docs/iteration-roadmap.md` §9 现有阶段 `R0`–`R14` + `DX`，模块化**不在任何一行里**。

按 DX-04 落地的机制，阶段条与阶段集合取自 §9 表格行序（`ProjectMap::stageOrder()`）。
所以模块化要能被看板看见，必须：

1. 在 §9 表格**新增一行**（建议 `MOD`，主题「架构分层」）；
2. 卡片短号建议**带类型前缀**（见下），`MOD-01`~`MOD-09` 仅作批次对照，
   实际编号由维护者立卡时指定。

> **卡号规则见 §7.1**（多套前缀并存 + 正则约束），此处不重复。
> 要点：现有阶段套内部已有 3 种形态（`R4-10` / `DX-01` / `M19`），
> 本批建议另加类型套 `REF-01`~`REF-09`，由维护者立卡时定号（待拍板 P6）。
>
> 不复用 `R14` 是因为它已有明确主题（Agent 插件「装了没生效」），混进去会让 §9 那一行失真；
> 不用 `DX` 是因为 DX 定位是「开发体验」，而模块化的收益是依赖约束，不是体验。

### 1.2 与在办卡片的关系（8 张 todo + 1 张已领用）

| 卡 | 状态 | 与模块化的关系 |
| --- | --- | --- |
| `DX-13` | **已领用**（agent-dx13） | 无交集（看板快照同步） |
| `DX-12` | todo | 无交集（`scripts/**` + 文档） |
| `DX-14` | todo | 弱交集（文档与闸门同步，M5 会动 `.github/**`） |
| `DX-15` | todo | **有交集**：「纯脚本/配置类卡片免全量编译」，与 M5 的 CI 接入是同一片区域 |
| `DX-16` | todo | **强交集**（= B0 + B3：sccache 安装与 `build/` 跨 worktree 复用，见 §2.5 / §6.2） |
| `R14-01` | todo | **直接冲突**：`scope` 含 `src/PluginRuntime.*` `src/PluginLoader.*` → 将搬入 `src/plugin/` |
| `R14-02` | todo | 弱交集（`CMakeLists.txt` / `installer` / `scripts`，M3 同区） |
| `R14-03` | todo | **直接冲突**：`scope` 含 `src/TaskRunner.*` → 将搬入 `src/agent/` |
| `R14-04` | todo | 无交集（纯文档核实） |

### 1.3 要不要做：三条诚实的判断

- **做的理由**：`main.cpp` 1910 行依赖 51/55 个类；`Main.qml` 4260 行（上一轮拆到 1180 又涨回来 3.6 倍）；
  加一个 QML 组件要同步改 2 处 CMake。这些是真实的日常成本。
- **不做的理由**：静态库不减 Qt 链接（§3.4）；`docs/pitfalls.md` 记过 QML 拆分引入运行时加载失败，
  重构本身有回归风险；当前还有 8 张 todo 卡，其中 4 张是**用户能感知的缺陷**（R14 Agent 插件装了不生效）。
  > 修正：「静态库不提速」只对主程序成立。测试侧 160 次重复编译正是靠静态库消掉的（§2.5）。
- **折中**：**不做 M3（静态库）也行**——`MOD-06`（物理分目录）+ `MOD-05`（守卫）就够拿到大部分收益，
  `MOD-09` 可以无限延期（待拍板 P2）。
- **但构建提速不该等**：§2.5 实测显示慢的主因（sccache 没装、测试重复编译）**与架构无关**，
  建议 `B0` / `B1` 排在模块化之前（§5）。

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

### 2.5 构建耗时：真正慢在哪（v4 新增，实测）

这是**独立于模块化**的议题，但与重构强相关：模块化期间要反复跑 `ctest` 验证，
编译慢会直接放大每一批的迭代成本。以下全部为实测，非估算。

| 项 | 实测值 |
| --- | --- |
| 编译单元总数 | 主程序 56 + 测试 **230** = **286**（8 核，Ninja；另 `NeonBoardHost` 有 3 个跨 target 重复，见 §2.3） |
| 测试侧重复编译 | 34 个 target / 230 TU，去重后只有 **36 个**源文件 → **160 次纯重复** |
| ↳ 口径 | 230 = 34 个 `tst_*.cpp` + 196 处源引用；160 = 196 − 36。**独立单元是 70 个，不是 36 个** |
| 重复次数最高 | `AppPaths.cpp` ×**25**、`Database.cpp` ×22、`TaskInputParser.cpp` ×20、`TaskModel.cpp` ×19 |
| **编译缓存** | **未安装** —— `build/CMakeCache.txt:309` 为 `NEONBOARD_SCCACHE-NOTFOUND` |
| PCH / Unity build | **均无**（`CMakeLists.txt` 只开了 `AUTOMOC` / `AUTORCC`） |
| moc 产物 | 48 个 `Q_OBJECT` 类 |
| 头文件爆炸半径 | 改 `AppPaths.h` → **170 TU**（占全量 59%）；`Database.h` → 139；中位数仅 2 |
| 官方耗时参考 | `.github/workflows/ci.yml:11` 注释「Qt 安装 + 编译 ≈ 8 分钟」，job `timeout-minutes: 45` |
| worktree 冷启动 | 新 worktree 的 `build/` 是空的 → 全量 286 TU（`DX-16` 未做） |

**四个根因，按贡献排序**：

1. **测试侧 160 次重复编译** —— 占全部编译量的 **56%**。34 个测试 target 各自把同一批
   `src/*.cpp` 重新编一遍，`AppPaths.cpp` 编 25 次。这是最大头，也是最容易消掉的。
2. **sccache 从未生效** —— `CMakeLists.txt:26-31` 写了完整支持、`build.ps1` 注释也写了
   「装有 sccache 时自动启用」，但本机 `find_program` 返回空。**R4-08 的编译缓存收益从未兑现**。
   后果：切分支、改 CMake、换 worktree 后都是全量重编。
3. **高扇出头文件** —— `AppPaths.h` 一处改动触发 170 TU。日常「改一行，等半天」的来源。
4. **worktree 冷启动** —— 每个新开区都是空 `build/`。并行会话越多，重复付全量成本越多。

> ⚠️ **一处必须修正的旧结论**：§1.3 / §3.4 / P2 原先写「静态库不提速」——
> **这个判断只对主程序成立，对测试侧不成立**。一旦公共源码编成静态库，
> 34 个测试就不必各自重复编译它们（230 TU → 34 TU）。
> 但**不必等 M3**：把 36 个公共源文件编成一个 `nb_test_support` 静态库给测试用，
> **今天就能做，不依赖 M2/M3 的任何改动**；M3 只是让它更自然。

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

排期原则：**先解耦路径，再搬运；先立守卫，再大改；构建提速独立于重构、且优先。**

| 批次 | 内容 | 触及文件 | 风险 | 前置 |
| --- | --- | --- | --- | --- |
| **B0**（`BLD-01`） | **装 sccache 并验证缓存命中**（零代码改动） | 环境 / `CMakeLists.txt`（无需改） | 无 | — |
| **B1**（`BLD-02`） | 测试公共源码收敛为静态库，消灭 160 次重复编译 | `tests/CMakeLists.txt`、`CMakeLists.txt` | 低 | — |
| **B2**（`BLD-03`） | 预编译头 + 拆高扇出头（`AppPaths.h` / `Database.h`） | `CMakeLists.txt`、`src/AppPaths.h`、`src/Database.h` | 中 | B1 |
| **B3** | `build/` 跨 worktree 复用（建议与 `DX-16` 合并） | `scripts/**` | 中 | B0 |
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

### B0 / B1 —— 构建提速（建议排在模块化之前）

**B0 · 让 R4-08 的编译缓存真正生效**

`CMakeLists.txt:26-31` 的 `find_program(NEONBOARD_SCCACHE NAMES sccache)` 已就位，
只是本机没装 → 缓存从未生效。装好并重新 configure 即可，
**不改一行代码**（`build/CMakeCache.txt:309` 是证据）。

```powershell
winget install -e --id Mozilla.sccache     # 或 cargo install sccache
sccache --version
cmake --fresh -B build -G Ninja            # 重新 configure 才会重新 find_program
python -c "print(open(r'build/CMakeCache.txt',encoding='utf-8').read().count('NOTFOUND'))"
```

**验收**：`CMakeCache.txt` 中 `NEONBOARD_SCCACHE` 为真实路径（非 `NOTFOUND`）；
连续两次 clean build 的第二次命中率 > 50%；`sccache --show-stats` 可见命中数。

**B1 · 测试侧公共源码收敛为静态库**

现状 34 个测试 target 各自重复编译同一批 `src/*.cpp`（230 TU，去重仅 36）。
做法：把这 36 个源文件编成一个 `STATIC` 库 `nb_test_support`，34 个测试改为链接它。

```cmake
add_library(nb_test_support STATIC ${NEONBOARD_TEST_SOURCES})
target_link_libraries(nb_test_support PUBLIC Qt6::Core Qt6::Sql Qt6::Test)
# 每个测试只剩自己的 tst_x.cpp 一个 TU
```

**预期**：测试侧 230 TU → **34 TU**（降 85%），且改 `AppPaths.h` 的爆炸半径
从 170 TU 降到约 34 + 少量。

**与 M0.5 的关系**：B1 天然完成 M0.5 的一半——
把 `tests/CMakeLists.txt` 里 196 处 `${CMAKE_SOURCE_DIR}/src/XXX.cpp` 收敛成一份集中定义。
**建议 B1 与 M0.5 合并为一张卡**，避免两次改同一个文件。

**与 M3 的关系**：M3 落地后 `nb_test_support` 可直接换成 `nb_core` + `nb_task` 等真实模块库，
B1 是它的低成本预演——**B1 不为 M3 铺路，但 M3 会让 B1 的成果更自然**。

> **为什么建议排在最前**：B0/B1 零架构风险、不动 `src/` 结构，
> 而模块化每一批都要靠 `ctest` 反复验证——**编译快 = 迭代快**，
> 先提速等于给后续所有批次降本。

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
- `DX-16`（worktree 冷启动与编译缓存）与 **B0 / B3 是同一件事**：
  B0 装 sccache 是 DX-16 的前置（没缓存就谈不上缓存复用），
  B3 的 `build/` 跨 worktree 复用就是 DX-16 的本体。
  **建议 B0 + B3 与 DX-16 合并为一张卡**，不要三处各改一遍。
  另：B1 / M3 改 CMake 会让 sccache 失效一次，属可接受的一次性成本（风险 R13）。

### 6.3 建议的整体顺序

```
现在  → B0 装 sccache（零代码，当天可完成）
      → B1 测试公共源码收敛为静态库（≈ MOD-02 的一半，建议合并）
      → R14-01 / R14-03（修缺陷，用户可感知）
      → MOD-01 归属更正 → MOD-02 路径解耦
      → MOD-03 拆 main.cpp（+ MOD-04 selftest 对齐，可并行）
      → MOD-05 守卫
      → MOD-06 物理分层          ← 此时 tests / 台账 / 面板三处都已解耦
      → MOD-07 QML URI → MOD-08 拆 Main.qml
      → MOD-09 静态库（可无限延期，见 P2）
```

---

## 7. 卡片体系：责任边界、编号、派发、多项目

> 本章是**治理层**的要求，与前面「怎么拆代码」正交：即使最后决定不做模块化，
> 这里的四条（责任边界 / 编号 / 派发态 / 多项目）也照样适用。

### 7.0 责任边界：三方分工（人 / 程序 / agent）

> **关键区分**：开分支既不该由 agent 自己做，也不该由人手工敲命令——
> 它是**派发程序**的动作，与任务分配一起完成，做完后整套环境交给 agent。

| 动作 | 谁执行 | agent 能做什么 |
| --- | --- | --- |
| **批准立卡**、**分配卡号**、**拍板**（P1–P9） | **维护者（人）** | 可**递交建卡请求**（带触发证据），但**不建卡、不占号**——批准权在人 |
| **主动领任务**（就绪且无人占用的卡） | **agent** + 程序占位 | ✅ 见下方「自下而上」，须满足 6 条准入 |
| 路线图 §9 增阶段行 | **维护者（人）** | 只提建议 |
| **开分支 + 开 worktree**（`agent-init.ps1`） | **派发程序** | ❌ 不自己开区 |
| **注入环境变量**（`.agent-env.ps1`：`NB_CARD` / `NB_SCOPE` / `NB_ACCEPTANCE` / `NB_DEPENDS`） | **派发程序** | 只读，不改写 |
| **登记领用**（`refs/claims/<短号>`，含 `sessionId`） | **派发程序** | ❌ 不自己登记 |
| **合入**（`git-done.ps1`）、**弃分支 / 回收 worktree**、**释放 claim** | **派发程序** | ❌ 不自己合入、不回收 |
| 改 `scope` / `baseline`（任务定义变更） | 维护者**授权**后由程序代提交 | 代提交须带「授权：」「批次：」两行 |
| 写代码、跑验收、更新自己的 `stage` / `progress` / `nextStep` | **agent** | ✅ 见 7.2 |

**派发一次任务的完整链路**（程序侧，人只做第 1 步）：

```
1. [人]    立卡、定号、拍板授权
2. [程序]  agent-init.ps1 -Card <短号> -Agent <名> -Branch <前缀>/<简述>
             → 建分支 + 建 worktree + 推 refs/claims/<短号>（带 sessionId）
3. [程序]  生成 .agent-env.ps1（注入卡号 / scope / acceptance / depends）
4. [程序]  把 worktree 路径 + 环境变量交给 agent 会话   ← 「派发包」交付点
5. [agent] 在边界内实现，逐项勾选 acceptance，写 nextStep
6. [程序]  verify.ps1 / CI 全绿后 git-done.ps1 合入、释放 claim、回收 worktree
```

> 这样切分的好处：**agent 拿到的永远是已经配置好的环境**，不存在「它自己开错区、
> 领错卡、忘注入环境变量」这类问题；人也不用手工敲命令，只在决策点上表态。

#### 自下而上：agent 的主动权（可以主动领、可以主动提建卡请求）

派发不是单向的。**agent 可以主动领任务、也可以主动发起建卡请求**，
前提是**说清楚**（为什么是它、为什么现在要建这张卡），且**批准权仍在人**。

**（a）主动领任务**——允许，但必须满足全部准入条件：

| # | 条件 | 判据 |
| --- | --- | --- |
| 1 | 卡处于**就绪**态 | `baseline != done`，且 `depends` 全部 done |
| 2 | 无人占用 | 服务端 `refs/claims/<短号>` 不存在（`ls-remote` 校验） |
| 3 | 领用仍走**程序** | 原子占位由程序执行，**不靠 agent 自报**（防双领） |
| 4 | 领用时登记 `sessionId` + 预计范围 | 见 7.2 |
| 5 | **一次只领一张** | 防止贪心占坑；交付或释放后才能领下一张 |
| 6 | 放弃/中断要**主动释放** | 否则只能等心跳超时（7.2） |

> 主动领任务与程序派发**不是两条路，而是同一条的后半段**：
> 一旦准入通过，后续的开区/注 env/登记 claim 仍由程序完成，agent 拿到的仍是同一个「派发包」。

**（b）主动发起建卡请求**——允许，但**agent 不建卡**，只提请求：

建卡请求应包含：标题 + 五段式草案（目标/验收/边界/输入/产出）+ 建议编号
+ **触发证据**（是在哪次改动/哪条日志里发现的、不做的代价）+ 建议 `scope` / `acceptance` / `depends`。

```
docs/card-requests/<日期>-<简述>.md     ← 请求落盘（建议位置，待拍板 P9）
  维护者批准 → 由程序立卡、定号（编号权仍在人）→ 进入派发链路
  维护者驳回 → 请求文件标注驳回理由，留档不删
```

> 与宪章「agent 不得自行加卡」**不冲突**：区别在**批准权**——
> agent 写的是**请求**，人做的是**批准 + 立卡 + 定号**。请求可留档，批准才生效。
>
> 这条同时给 7.2 的 `nextStep` 提供了出口：收工时发现的新活，
> 与其只写进 `nextStep` 等着被遗忘，不如落成**建卡请求**（与 beads 的
> "file issues for remaining work" 同一个思路）。

#### 流程不能卡死：每条硬规则都要有出口

> **原则：流程服务于交付，不是交付服务于流程。**
> 「因为没有卡 / 因为派发程序不可用 / 因为上游没做完」就干不了活，
> 是最糟糕的结果——**没有出口的硬规则只会被 `--no-verify` 绕过，比没有规则更危险**。
> 下面逐个死锁点给出**降级动作**与**留痕要求**。

| # | 卡死点 | 降级动作（不阻塞交付） | 留痕 |
| --- | --- | --- | --- |
| 1 | **想做的活没有对应卡**（无头） | 允许**先做后补**：按最小范围实施，提交体写 `待补卡：<简述>` 一行；维护者事后补卡并回填短号 | 提交体必须有 `待补卡：` 行；派生器定期扫描未回填的 `待补卡` 并告警 |
| 2 | 有卡但 `scope` 不够（要改的文件不在范围内） | ① 先提建卡请求拆出子卡；② 紧急时由维护者授权扩 `scope`，走 `chore(board):` + 授权行 | 授权行是硬要求（DX-10） |
| 3 | **`depends` 上游卡被弃 / 无限延期** | 允许**解除依赖**：标注原因后把该 `depends` 摘掉，或直接改挂到替代卡 | 台账 `note` 记明「原依赖 X 已弃」，不留悬空 |
| 4 | **派发程序不可用**（脚本失败 / 离线 / 无权限） | 由**人**手工跑一次开区兜底；或复用**既有 worktree**（`agent-init.ps1 -InPlace`，脚本已支持），不新建 | 记录降级原因，事后由程序补登记 |
| 5 | 心跳超时被判中断，但人还在做 | 重领即可（幂等）；**不得**因一次超时把卡永久锁死 | 重领时更新 `sessionId` |
| 6 | 验收/闸门误判（如 `#RGB` 被当成卡号） | 修判定脚本，必要时单条提交用 `--no-verify` | 已知噪音要登记进 `docs/pitfalls.md`，**不允许长期靠 `--no-verify` 过日子** |
| 7 | 一张卡太大，一次做不完 | 拆子卡（`REF-03.1` / `.2`，见 7.1 层级 ID），而不是把大卡长期挂 doing | 父卡 `note` 记子卡映射 |

三条配套要求：

1. **`待补卡` 通道必须真的存在且可被事后审计**——它是对「无卡不许干」这条硬规则的
   唯一合法软化。没有它，agent 遇到没有卡的活只能偷跑，审计就断了。
2. **降级不是免罚**：任何降级动作都要留痕，且**允许事后补**不等于**允许事后赖**——
   派生器要能列出「已降级但未回填」的条目，作为维护者的待办队列。
3. **卡死的信号要能被看见**：若同一张卡连续 2 次因流程原因无法推进，
   应自动上浮为**待拍板项**（进入 P 列表），而不是让执行者自己想办法绕。

#### 现状盘点：本项目当前真实存在的 10 个卡死点

> 下面每条都带**脚本位置作为证据**，不是推测。其中 1–3 是**硬死锁**（真的干不了活），
> 4–7 是**半死锁**（能干但会卡在半路或返工），8–10 是**效率性卡点**。

**A. 硬死锁**

| # | 卡死点 | 证据 | 后果 |
| --- | --- | --- | --- |
| **1** | **弃分支不走 `git-done` → 服务端 `refs/claims/<短号>` 永不释放 → 该卡永久不可再领** | 删除服务端 ref 的地方只有 `git-done.ps1` 的 `Release-ClaimRef`；`agent-init.ps1` 不清理孤儿；`release-claims.py` **只清台账投影、不碰服务端**（其文档自述） | 卡被永久锁死。恢复只能人工 `git push origin --delete refs/claims/<短号>`，且需联网（DX-11 就是这么手工救回来的） |
| **2** | **领用 / 合入强制联网** | `agent-init.ps1` L136 `git fetch` 失败即抛；`claim-card.py` L112/118 `ls-remote` 失败即拒；`git-done.ps1` L494–498 明确「离线必须 fail-closed」 | 代理间歇 502 / 断网时**全流程停摆**：本地改得再好也领不了、推不上去 |
| **3** | **没有卡就一行代码都提交不了**，而立卡要维护者授权 | `task_gate.py` L155 / L220 直接写「没有任务就不改代码；先由维护者在台账里立卡」；立卡须 `chore(board):` + 授权行 | 维护者不在场 = 没有「头」= 无法开工。**这正是「不能因为没有头就卡死」的现实版本** |

**B. 半死锁（能干，但会卡在半路或返工）**

| # | 卡死点 | 证据 | 后果 |
| --- | --- | --- | --- |
| **4** | **无 python 时闸门行为不一致** | `commit-msg` L88、`pre-commit` L108 **跳过**（fail-open）；`pre-push` L137–146 **阻断**（`exit 1`，fail-closed） | 能提交、**不能推送**，卡在半路。且报错只说「请安装 python」，没说已提交的改动怎么办 |
| **5** | **进度快照单写者** | `derive_board_status.py` L7–9 明写 `progress.json`「CI 独占维护」 | agent 改了台账却不能刷新快照 → `--check` 红。已漂移两次（DX-11、R13-01） |
| **6** | **基线漂移的双向误判** | 领用门只看 `origin/main` 的 `baseline`（`claim-card.py` L60–75） | 分叉时**已交付的卡被当成可领、重做一遍**；反之 git 标记缺失则该卡永远 todo |
| **7** | **台账单文件 + 多会话并行** | `docs/board-ledger.json` 同时承载 cards / baseline / claims | 合并冲突（DX-11 实测），冲突后要手工补 5 步才能收尾 |

**C. 效率性卡点**

| # | 卡死点 | 证据 | 后果 |
| --- | --- | --- | --- |
| **8** | 测试二进制是 `WIN32_EXECUTABLE`，直接跑 exe **看不到任何输出** | `CMakeLists.txt` L221 / L249 | 诊断卡死：明明通过却以为失败。必须走 `ctest` |
| **9** | 每开一个 worktree 就要一轮全量构建 | `DX-16`（冷启动与编译缓存）仍在 todo | 大批量重构时，开区成本吃掉大部分时间 |
| **10** | `--no-verify` 成了事实上的**唯一兜底** | 宪章说「仅限紧急情况」，但上面 1–3 没有别的出口 | 与「仅限紧急」自相矛盾——**出口越少，绕过越多** |

#### 对应的出口（逐条）

| 卡死点 | 建议出口 | 归属 |
| --- | --- | --- |
| 1 弃分支锁死 | 提供**独立的释放入口**（让 `release-claims.py` 支持 `--server`，或加 `release-claim.ps1`），作为弃分支的**标配动作**；并允许心跳超时后强制重领 | 待拍板 **P11** |
| 2 强制联网 | 加**离线模式**：本地台账 + 本地 ref 占位先记，联网后补推；或至少给出「离线不可推进」的明确提示 + 待办队列，别让人反复试 | 待拍板 **P11** |
| 3 无卡不能开工 | **「待补卡」通道**（先做后补 + 提交体留 `待补卡：` 行 + 事后告警） | 待拍板 **P10** |
| 4 闸门不一致 | 统一 fail-open-with-warning 或统一 fail-closed；**报错必须给恢复步骤** | 修脚本即可 |
| 5 快照单写者 | 允许本地 `--write`（CI 只校验不独占），或漂移时自动修 | 改 CI 约定 |
| 6 基线漂移 | 结卡后强制跑 `--write`（现有约定靠人记，故漂了两次）→ 应由**程序**在 git-done 收尾时做 | 已有机制，改成自动化 |
| 7 台账冲突 | claims / 进度拆到独立文件（对应 7.2 的载体方案 B） | 待拍板 **P7** |
| 8/9 | 文档醒目提示 + `verify.ps1` 统一走 ctest；推进 `DX-16` | 文档 / DX-16 |
| 10 | 1–3 的出口到位后，`--no-verify` 才可能真正只用于紧急 | 依赖上面全部 |

按宪章 §2 条款 6，**立卡与编号只能由维护者执行**。本方案不创建任何卡片、不占用任何编号；
`MOD-01`~`MOD-09` 只是**排期用的临时对照号**，实际卡号由维护者立卡时指定
（见 7.1，待拍板 P6）。

### 7.1 编号：多套前缀并存

> 设计原则：**前缀不是只有一套**。可以多套并存，每套回答一个不同维度的问题，
> 不要试图用一套取代另一套——卡片可同时带多套编码。

| 套 | 形态 | 数量 | 回答什么 |
| --- | --- | --- | --- |
| **① 阶段/主题套**（现有） | `R4-10` / `R14-01`、`DX-01`、`M19` | 115（67 / 14 / 16 / 18） | 属于哪个阶段、哪个主题 |
| **② 类型套**（建议新增） | `REQ`（需求）/ `FIX`（缺陷）/ `DOC`（文档）/ `REF`（重构） | 0 | 这是什么性质的工作 |

现有那套**内部本来就有 3 种形态**，所以「多套并存」不是新发明，而是把已存在的事实显式化。

#### 与外部惯例对齐（GitHub 参照）

**① Conventional Commits 的类型集合**——类型套**不要另起炉灶**，直接对齐提交类型。
好处是卡片类型与提交前缀天然一致，且 `scripts/task_gate.py` 已有 `HARD_TYPES`
（`feat`/`fix`/`refactor`/`perf`/`test`）可复用同一套词表：

| 提交类型 | 建议卡号前缀 | 含义 |
| --- | --- | --- |
| `feat` | `FEAT` | 新能力 |
| `fix` | `FIX` | 缺陷修复 |
| `docs` | `DOC` | 纯文档 |
| `refactor` | `REF` | 不改行为的结构调整 |
| `perf` | `PERF` | 性能优化 |
| `test` | `TEST` | 测试 |
| `build` / `ci` | `BLD` / `CI` | 构建与 CI |
| `chore` | `CHOR` | 杂务 |

**② beads（`steveyegge/beads`）的哈希 ID**——它用 `bd-a1b2` 而非 `bd-1`，理由是
**顺序号在多分支 / 多 agent 并行时会撞号**。我们的 `REF-01` 是顺序号，两个分支同时建卡必然撞。
启发：**顺序号适合单线推进，哈希/随机后缀适合并行**。
现状靠服务端 `refs/claims/<短号>` 的原子占位兜住了撞号（同号第二次领用会被拒），
所以顺序号**现在能用**；但若以后允许**多会话并行建卡**，编号需要带随机后缀。

**③ beads 的层级 ID**（`bd-a3f8` → `bd-a3f8.1` → `.1.1`）——大卡拆子任务时用
`REF-03.1` / `REF-03.2`，而不必再申请新号。我们的 9 个批次若某批中途拆细，可用这招。

#### 硬约束（正则）

```python
# scripts/task_gate.py L54
CARD = re.compile(r"#((?:[A-Za-z]{1,4}\d*-\d+)|(?:M\d+))")
```

- 新前缀须 **≤4 个字母**（所以上表里 `CHOR` / `BLD` 是缩写；若介意就要放宽正则）；
- `M19` 能匹配是因为正则**显式挂了第二个分支 `M\d+`**——它并不满足 `[A-Za-z]{1,4}\d*-\d+`
  （没有短横）。**新增无短横形态必须同步改这个正则**，不能假设「短号都长一个样」。

建议做成**前缀族注册表**（每套声明：名称 / 含义 / 正则 / 是否必填 / 可否与他套组合），
以后加第三套（如模块套 `UI` / `CORE` / `TEST`）不必再动判定代码。

### 7.2 卡片执行态与接手机制（派发后要看得见、断得了）

> 需求：卡片分下去后要能回答三个问题——**谁在跑**（哪个 agent / 哪个 session）、
> **跑到哪了**（进度）、**断了谁接**（另一个 session 能带着上下文接手，而不是从头来）。

#### 现状：能答「谁」，答不了「到哪了」

`scripts/claim-card.py` L266 写入的领用记录有 4 个字段：

```json
"claims": { "<短号>": { "agent": "agent-dx13", "branch": "...", "head": "<sha>", "claimedAt": 1690000000 } }
```

| 需求 | 现状 | 缺口 |
| --- | --- | --- |
| 谁在跑 | ✅ `agent` + `branch` | 无 session 标识（同名 agent 复用时无法区分先后会话） |
| 跑到哪了 | 🟡 只有 `head`（最后提交 sha） | **无进度粒度**；未提交的在制品完全不可见 |
| 是否还在跑 | ❌ | 无心跳。僵尸 worktree 会长期存在（实测：任务合入后 worktree 仍在），误判为「有人在做」 |
| 断了怎么接 | ❌ | 无「下一步」说明，接手者只能读 diff 反推意图 |

`refs/claims/<短号>` 是服务端硬门（原子占位、防双领），但它是**二进制**的：
只有「被占 / 已释放」两态，不承载进度信息。

#### 建议：给领用记录补 5 个字段

```json
"claims": { "<短号>": {
    "agent": "agent-mod03", "sessionId": "1a745e8a-...", "branch": "refactor/m1-cli",
    "head": "<sha>", "claimedAt": 1690000000,
    "stage": "编码中",                      // 已开区 / 编码中 / 自测中 / 待合入
    "progress": { "done": 4, "total": 10 }, // 或 acceptance 项勾选状态
    "updatedAt": 1690003600,                // 心跳：每次提交或每 N 分钟刷新
    "nextStep": "已搬 4 条命令，剩 --update-apply；注意 arity=1 的取值命令尚未设计"
} }
```

- `progress` 直接复用卡片的 `acceptance` 列表作为勾选清单——**这也是 §7.4 要求 acceptance
  必须可拆分、可执行的原因**：不可拆的验收项当不了进度条。
- `nextStep` 是**给接手者的一句话**，价值最高、成本最低。中断时没有它，接手者读 diff
  只能看到「改了什么」，看不到「为什么停在这」。
- `updatedAt` + `stage` 共同判定中断：`updatedAt` 超过阈值（建议 30 分钟）即视为中断，
  **不能只用「worktree 是否存在」**来判定。

#### 接手协议（中断后的标准动作）

1. 查谁占着：`git worktree list` + `git ls-remote origin 'refs/claims/*'`
2. 读领用记录拿 `branch` / `head` / `stage` / `nextStep`
3. 判定中断：`updatedAt` 超阈值，且该 branch 最后提交时间 > 阈值
4. 派发程序开区（同上：**开区由程序做，不由人敲命令、也不由 agent 自己做**）
   → 接手者基于该 branch 继续
5. 先跑一遍卡片的 `acceptance`，确认当前真实状态（不要相信 `stage` 字段的字面值）
6. 接手后更新 `agent` / `sessionId`（重领），并在 `nextStep` 里写下自己的切入点

> 第 5 步不能省。`stage` 是执行者自报的，可能与实际不符；**acceptance 是唯一可信的状态源**。

#### 外部参照：beads 的「会话收尾清单」

beads（`steveyegge/beads`）为 agent 会话定义了 **"Land the Plane" 收尾动作**，
与上面的接手需求是同一个问题的两种解法，值得直接借鉴：

| beads 的做法 | 我们的对应物 |
| --- | --- |
| `bd update --claim` 原子领用（一次操作同时置 assignee + in_progress） | `refs/claims/<短号>` 原子占位 ✅ 已有，但**不置 in_progress** |
| `bd ready` 查询「无阻塞的就绪工作」 | ❌ 缺。新会话冷启动时只能读台账，没有「现在能干什么」的入口 |
| 会话结束前必须把剩余工作落成 issue（"file issues for remaining work"） | ❌ 缺。这正是 `nextStep` 要补的 |
| `bd sync` + `git push` 后才算收工 | ✅ 已有（`git-done.ps1` 推送 + claim 释放） |
| `--json` 全命令覆盖，便于程序化 | 🟡 部分（`derive_board_status.py` 有，台账工具没有统一 JSON 输出） |

**最值得抄的一条**：把「结束会话前必须写下剩余工作」变成**流程强制项**而不是自觉行为。
`nextStep` 若只在中断时才写，那就永远没人写——**应当要求每次收工都写**，
中断只是没来得及更新而已。

#### 载体选择（待拍板 P7）

| 方案 | 优点 | 缺点 |
| --- | --- | --- |
| **A（推荐）** 扩展现有 claim 记录 | 随分支提交、天然远程可见；`git-done` 已有释放逻辑，不新增生命周期 | 频繁改 `docs/board-ledger.json` 会脏化 diff，且与主线合并冲突概率上升 |
| B 独立 `docs/agent-assignments.json` | 台账不被进度刷脏 | 新增一个事实源；需自己实现释放与清理 |
| C 写进 `.workbuddy/memory/` | 零成本 | ❌ 不进库、不共享，无法满足「其他 session 能接手」——**排除** |

折中建议：**A + 频率控制**——仅在 `stage` 变化或每完成一项 acceptance 时更新，
不要每次小提交都改台账。

### 7.3 多项目隔离与全局视图（分项目不混，但要有总览）

> 需求：卡片与进度要**按项目隔离**（不同项目的数据不能混在一起），
> 同时要有一个**全局展示**能一眼看到所有项目的状态。

#### 先分清两个「项目」

| 概念 | 载体 | 含义 |
| --- | --- | --- |
| **应用内项目** | `src/ProjectStore.*`（任务归属 `projectId`） | NeonBoard 用户把「任务」分组到不同项目，是**产品功能** |
| **治理项目** | 每个仓库各自的 `docs/board-ledger.json` | 哪一份代码库在推进哪些卡，是**工程治理** |

两者同名但**不是一回事**，不要试图合并。本条讲的是后者。

#### 现状：治理工具链是单仓库视角

| 环节 | 现状 | 是否天然隔离 |
| --- | --- | --- |
| 台账 | `docs/board-ledger.json`（本仓库唯一一份） | ✅ 随仓库走，天然隔离 |
| 领用占位 | `refs/claims/*`（挂在本仓库 remote） | ✅ 每个 remote 独立，天然隔离 |
| 派生状态 | `derive_board_status.py --repo <单仓库>` | ✅ 一次只算一个仓库 |
| 阶段条 | 各仓库自己的 `iteration-roadmap.md` §9 | ✅ |
| **全局汇总** | **不存在** | ❌ 缺 |

结论：**隔离已经天然成立**（数据本来就在各自仓库里），真正缺的只有**全局视图**这一层。
所以这条需求的工作量比表面看起来小——不必搞中央数据库，只需一个只读聚合器。

#### 建议：只读聚合器 + 项目登记表

```
docs/projects-registry.json        ← 项目登记表（新增）
  [{ "id": "neonboard", "name": "NeonBoard", "path": "E:/projects/NeonBoard",
     "remote": "origin", "enabled": true }, ...]

scripts/aggregate_boards.py        ← 全局聚合（新增，只读）
  逐个项目调 derive_board_status.py --check / 读其 ledger
  → 输出 docs/global-board.json（全局视图数据源）
```

三条设计约束：

1. **只汇总，不回写。** 聚合器对每个项目只读，绝不改别人的台账/claim/分支。
   跨项目写入会带来权限与冲突问题，且会让「哪个项目说了算」变得模糊。
2. **卡号在全局必须唯一。** 两个项目各自都有 `REF-01` 时，全局视图会撞号。
   全局唯一键建议 `<项目 id>.<卡号>`（如 `neonboard.REF-03`），
   而**各项目内部仍用短号**，不必在日常开发里写全前缀。
   ⚠️ 这与待拍板 **P6**（类型前缀）耦合——**两个编号规则要一起定**，
   否则以后改一次编号要全量迁移历史卡。
3. **全局视图必须显示数据新鲜度。** 各项目快照的 `updatedAt` 要带进汇总结果；
   没有时间戳的全局视图会让「3 天前的绿」被当成「现在还绿」。

#### 全局视图展示什么

| 维度 | 内容 |
| --- | --- |
| 项目健康度 | 每项目 todo / doing / done 计数 + 最后更新时间 |
| 我的在办 | 按 agent / session 聚合「谁在跑哪张卡」（复用 §7 的 `sessionId`） |
| 阻塞与冲突 | 跨项目 `depends`、同文件被多个会话占用 |
| 风险 | 各项目风险登记中 `高` 级别的条目上浮到全局 |

> 跨项目 `depends` 必须显式标注项目域（`neonboard.REF-03`），
> 否则全局视图会把「同名卡」误判为依赖边，画出**假依赖**。

### 7.4 本批 12 张卡的草案

按 `docs/task-card.md` 五段式（目标 / 验收 / 边界 / 输入 / 产出）给出。
**建议在 `docs/iteration-roadmap.md` §9 新增 `MOD 架构分层` 一行**（由维护者执行），
否则这些卡在阶段条上不可见（阶段集合取自 §9 行序，见 §1.1）。

> 下表**同时出现 `BLD-xx` 与 `MOD-xx` 两套前缀**，这正好是 §7.1「多套前缀并存」的示例：
> `BLD` 是类型套（构建类工作，对齐 Conventional Commits 的 `build`），
> `MOD` 是主题套（模块化这批活）。**两套编号互不冲突，卡片可同时带**；
> 实际编号仍以维护者立卡时为准。

| 批次对照 | 标题 | `scope` | `depends` |
| --- | --- | --- | --- |
| `BLD-01` | 安装 sccache 并验证缓存命中（B0，零代码改动） | `docs/pitfalls.md`, `.github/**` | — |
| `BLD-02` | 测试公共源码收敛为静态库，消灭 160 次重复编译（B1） | `tests/CMakeLists.txt`, `CMakeLists.txt` | `BLD-01` |
| `BLD-03` | 预编译头 + 拆 `AppPaths.h` / `Database.h` 高扇出头（B2） | `CMakeLists.txt`, `src/AppPaths.*`, `src/Database.*` | `BLD-02` |
| `MOD-01` | 更正 3 个类的模块归属（仅声明） | `docs/module-split-plan.md`, `docs/board-ledger.json` | — |
| `MOD-02` | 路径解耦：tests 源引用集中化 + 在办卡 scope 改 glob | `tests/CMakeLists.txt`, `docs/board-ledger.json` | `MOD-01` |
| `MOD-03` | 抽 `main.cpp`：命令表 + AppContext + 装配器 + 构造工厂 | `src/main.cpp`, `src/app/**`, `CMakeLists.txt` | `MOD-02` |
| `MOD-04` | `--selftest` 与 ctest 对齐，605 行瘦身 | `src/main.cpp`, `tests/**`, `tests/CMakeLists.txt` | — |
| `MOD-05` | 依赖方向与行数守卫脚本 + CI 接入 | `scripts/**`, `.github/**`, `AGENTS.md` | `MOD-03` |
| `MOD-06` | `src/` 物理分层为 8 个子目录 | `src/**`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/project-map.json` | `MOD-05`, `MOD-02` |
| `MOD-07` | QML 多模块 URI 化，消除 31 条手工 alias | `qml/**`, `CMakeLists.txt` | `MOD-06` |
| `MOD-08` | `Main.qml` 编排逻辑再拆分（4260 → ≤1200） | `qml/Main.qml`, `qml/components/**` | `MOD-07` |
| `MOD-09` | CMake 拆分为 8 个静态库 target | `CMakeLists.txt` | `MOD-06` |

**关键 `acceptance` 示例**（须**可拆分、可执行**，禁止主观描述——
它们同时充当 7.2 的进度勾选清单）：

```text
BLD-01: build/CMakeCache.txt 中 NEONBOARD_SCCACHE 为真实路径（非 NOTFOUND）
        sccache --show-stats 在第二次 clean build 后命中率 > 50%
BLD-02: ctest --test-dir build -C Release 全绿（34/34）
        测试侧编译单元数 230 → <= 40（ninja -t targets 或构建日志统计）
        ninja 全量构建的编译单元总数 <= 100（原 286）
BLD-03: 改 src/AppPaths.h 后触发重编的 TU 数 <= 60（原 170）
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

### 7.5 待拍板事项

- **P1**：M1 是否引入 `AppContext` 抽象？推荐引入（否则命令表无处挂靠）。
- **P1.5**：M1d（构造工厂）做不做？做则 `main.cpp` ≤450 行，但要动次序敏感的 ~700 行；
  不做则验收线必须改成 ≤950。**不定则 MOD-03 无法立卡。**
- **P2**：`MOD-09`（静态库）是否值得做？§3.4 已说明它不减 Qt 链接，
  收益是「依赖方向被 linker 强制」。只想要可读性的话 `MOD-06` + `MOD-05` 就够了，可无限延期。
  （**旧措辞「不提速」已按 §2.5 修正**：提速的场合是测试侧重复编译，
  而那部分由 `B1` 独立解决，不必绑在 MOD-09 上。）
- **P3**：`plugin → shell` 的托盘回调耦合是否做接口反转？做则插件层不再依赖窗口/托盘类型，
  代价是 `PluginRuntime` 对外契约要改。
- **P4**：`src/app/` 是否为 `main.cpp` 的新家？若是，`AGENTS.md` L423 / L695 两处要同步改。
- **P5**（新增）：新阶段叫什么？建议 `MOD`，需在路线图 §9 增行才能被阶段条看见。
- **P6**（新增）：是否新增**类型前缀套**（建议对齐 Conventional Commits：
  `FEAT`/`FIX`/`DOC`/`REF`/`PERF`/`TEST`/`BLD`/`CI`/`CHOR`）与现有阶段套**并存**？
  若采纳，本批卡号为 `REF-01`~`REF-09`，需同步修订 `docs/task-card.md` 的编号约定、
  `task_gate.py` 的正则与回归用例（前缀 ≤4 字母；无短横形态须另加分支）。
  建议一并落地**前缀族注册表**，让第三套以后不用改判定代码。详见 §7.1。
- **P7**（新增）：领用记录是否扩展 `sessionId` / `stage` / `progress` / `updatedAt` /
  `nextStep` 五个字段？载体走 A（扩展现有 claim）还是 B（独立文件）？
  **是否把「收工前必须写 nextStep」定为流程强制项**（参照 beads 的 Land the Plane）？
  见 §7.2。**不改则中断后接手只能靠读 diff 反推意图。**
- **P8**（新增）：全局视图怎么做？项目登记表放哪（独立仓库 / 本仓库 `docs/`）、
  全局载体是脚本输出 JSON 喂给 QML 面板还是 markdown 报告、
  卡号是否加项目域（`neonboard.REF-03`）。
  **必须与 P6（类型前缀）一起定**——两次改编号等于两次全量迁移。
- **P9**（新增）：agent 的两条主动路径怎么落地？
  ① 建卡请求载体（`docs/card-requests/*.md` 还是台账里的 `requests` 区段）与审批留档方式；
  ② 主动领任务的准入由谁判（脚本自动判 `depends` + `refs/claims`，还是人工确认）。见 §7.0。
- **P10**（新增）：是否开放**「待补卡」通道**（无卡先做、事后补卡 + 提交体留 `待补卡：` 行）？
  以及未回填条目的超时告警阈值。见 §7.0「流程不能卡死」。
  **不开放则「无卡不许干」这条硬规则没有合法出口，只会逼人用 `--no-verify` 绕。**
- **P11**（新增）：两个**硬死锁**要不要修？
  ① **弃分支后服务端 `refs/claims/<短号>` 永不释放** → 该卡永久不可再领（恢复只能人工 + 联网）；
  ② **领用/合入强制联网** → 断网即全流程停摆。
  见 §7.0「现状盘点：本项目当前真实存在的 10 个卡死点」第 1、2 条。
- **P12**（新增）：构建提速的 **B0 装 sccache** 是否立即执行？
  它零代码改动、零架构风险，且是当前「构建慢」四条根因里唯一能当天解决的（§2.5）。
  另需定：**B3（`build/` 跨 worktree 复用）是否与既有的 `DX-16` 合并为一张卡**（§6.2）——
  两者是同一件事，分开做会三处各改一遍。

---

## 8. 风险登记

| # | 风险 | 等级 | 缓解 |
| --- | --- | --- | --- |
| R1 | M1b 搬迁 CLI 改变启动语义（提前 return 顺序变了） | 中 | 逐条搬迁 + 每条保存重构前 stdout 基线做逐字节比对 |
| R2 | M2 改路径后构建失败或 moc 找不到头文件 | 中 | 8 个子目录全量加 include 路径，不改动任何 `#include` 文本 |
| R3 | M3 的 AUTOMOC 边界变化导致 moc 产物丢失 | 中高 | 单独分支先行验证 `nb_core` 一个库，跑通再推广 |
| R4 | M4 的 QML 拆分引入运行时加载失败（历史踩过） | 中 | `--smoke` 作硬门禁；参考 `docs/pitfalls.md` 第 14 节 |
| R5 | 重构期间与并行会话抢同一个文件 | 高 | 分支命名 `refactor/<批次>`，领用隔离；动手前 `git worktree list`（**开区/合入由派发程序执行**，见 §7.0 / §9） |
| R6 | `src/DeltaUpdate.cpp` L404–478 内置 `#ifdef NEONBOARD_DELTA_TOOL_MAIN` 的 `main()`；做成静态库后宏泄漏会出现重复 `main` 符号 | 中高 | 该宏只在 `neonboard-delta-tool` 上 `target_compile_definitions`；三个 exe 均须能链接启动 |
| R7 | 命令表搬走后 `utilityMode` 谓词未同步，新增命令漏判无头模式而拉起 GUI（§2.2.3） | 中 | `CliCommand` 带 `CliMode`，由 `allCommands()` 反推谓词；验收含该回归 |
| R8 | 命令表只按布尔 flag 设计，搬到可重复取值/取单值命令时被迫改签名返工（§2.2.2） | 中 | 首个命令落地前就把 `arity` / `repeatable` 设计进 `CliCommand` |
| R9 | M1 按 `<400 行` 验收，做到 ~960 行就判失败而缺口无人负责 | 中 | 补 M1d，或把验收线改为 ≤950（P1.5 必须先拍） |
| **R10**（新增） | M2 一次搬运打断 `tests/CMakeLists.txt` 196 处引用 → 34 个测试二进制链接失败 | **高** | **M0.5 先做路径解耦**；`MOD-06` 的 `scope` 必须含 `tests/CMakeLists.txt` |
| **R11**（新增） | M2 后 `docs/project-map.json` 的 `src/BoardSync.*` 证据失效 → `tst_projectmap` 红 | **高** | M2 同批改这两条证据路径；`MOD-06` 的 `scope` 含 `docs/project-map.json` |
| **R12**（新增） | 在办卡 `R14-01` / `R14-03` 的 `scope` 指向被搬运文件，改动被 `check_scope.py` 判越界 | 中 | 走 §6.1 方案 A（R14 先做），或 M0.5 授权改契约 |
| **R13**（新增） | M3 改 CMake 让 sccache 失效一次，与 `DX-16` 的冷启动优化撞车 | 低 | 记入 `MOD-09` 说明，接受一次性成本 |
| **R14**（新增） | **装了 sccache 却命中率极低**——MSVC 默认 `/Zi` 把调试信息写进共享 `.pdb`，sccache 无法缓存 | 中 | 改用 `/Z7`（调试信息嵌进 `.obj`）或 `-Fd` 独立 pdb；装完必须实测命中率，**不能只看 CMakeCache 里有路径就当生效**（§2.5 B0） |
| **R15**（新增） | B1 把 36 个源文件编进静态库，测试 target 会链接到本不需要的符号，可能暴露静态初始化顺序问题 | 中 | 静态库只给测试用，主程序 `NeonBoard` 的链接方式不变；每个测试仍单独 `add_test`，失败照旧定位到具体用例 |
| **R16**（新增） | B2 加 PCH 后，PCH 自身成为新的高扇出点——改一次 PCH 等于全量重编 | 中 | PCH 只放**稳定**的 Qt / STL 头，**业务头一律不进 PCH**；否则等于把 170 TU 的爆炸半径固化成 286 TU |

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

**回滚**：每批一个独立分支，CI 全绿后合并，任一批失败直接弃分支。
**分支的开（`agent-init.ps1`）、合（`git-done.ps1`）、弃（删分支/回收 worktree）
都由派发程序执行**；agent 只在交给它的分支内提交改动，不自行开区、不合入、不回收。
（三方分工的完整定义见 §7.0。）

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
| 编译单元总数 | 286（主程序 56 + 测试 230） | 主程序按 `src/*.cpp`；测试按 `neonboard_add_test` 块展开 |
| 测试侧重复编译次数 | 160（230 TU 去重后仅 36 个源文件） | 统计每个 `src/X.cpp` 出现在多少个测试块里，减 1 求和 |
| sccache 状态 | `NEONBOARD_SCCACHE-NOTFOUND` | `grep sccache build/CMakeCache.txt` |
| 高扇出头爆炸半径 | `AppPaths.h` → 170 TU；`Database.h` → 139 | 头文件 `#include` 传递闭包 × 各 TU 归属 target 数 |
| `Q_OBJECT` 类数（moc 产物） | 48 | `grep -l Q_OBJECT src/*.h \| wc -l` |

> **v2 的教训**：本文初版有 6 处数字错误、3 处事实遗漏，根因是「算过一次就当成事实」
> 而没有留下可复跑的脚本。**建议把扫描脚本固化为 `MOD-05` 的 `scripts/check_module_deps.py`，
> 并让它第一个检查项就是本表数字的可复跑性**——让「现在可行」变成「以后仍然可行」。
>
> **v3 的教训**：v1/v2 只盯着 `src/` 内部，没看 `src/` 被谁引用。
> 任何「大规模移动文件」的重构，**第一件事是扫全仓库的硬编码路径引用**。
>
> **v4 的教训**：v1–v3 只回答「代码怎么拆」，没回答「活怎么派」。
> 维护者的四条反馈全都落在**治理层**——谁有权建卡/开分支、卡号怎么编、
> 派发后能不能看见进度、多项目怎么隔离又总览。
> **技术方案里必须留一节讲「这套活怎么被分下去、被接住」，否则再好的架构也落不了地。**
>
> **v4 的第二条教训**：**「配置写了」不等于「生效了」**。
> `CMakeLists.txt` 完整支持 sccache、`build.ps1` 注释也写着「装有 sccache 时自动启用」，
> 但本机没装 → `find_program` 返回空 → **R4-08 的收益从未兑现**。
> 这类「静默降级」没有任何告警：`CMakeCache.txt` 里那行 `NOTFOUND` 是唯一线索。
> **任何可选的加速/校验能力，都应当在验收项里显式断言它真的生效，而不是断言它被配置了。**

### 外部参照（v4 借鉴来源）

| 来源 | 借鉴了什么 | 落在本文 |
| --- | --- | --- |
| **Conventional Commits** | 类型集合（`feat`/`fix`/`docs`/`refactor`/`perf`/`test`/`chore`…）已是事实标准；卡片类型套直接对齐它，与 `task_gate.py` 的 `HARD_TYPES` 同源 | §7.1 |
| **beads**（`steveyegge/beads`，git-backed 的 agent 用 issue tracker） | ① 哈希 ID 避免多分支撞号 ② 层级 ID（`bd-a3f8.1`）拆子任务 ③ 原子 `--claim` ④ `bd ready` 就绪查询 ⑤ **"Land the Plane" 会话收尾清单**——结束前必须把剩余工作落成 issue | §7.1 / §7.2 |

> 注：beads 是**独立工具**（Go 写的 CLI，数据存 `.beads/` + SQLite/JSONL），
> 本文**不建议引入它**——本项目已有 `docs/board-ledger.json` + `refs/claims/*`
> 且跑得通。借鉴的是它的**设计取舍**，不是工具本身。
