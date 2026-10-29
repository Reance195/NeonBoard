# NeonBoard 模块化方案

> 事实基线：2026-10-30，工作区 `src/` 与 `qml/` 全量扫描（55 个类 + `main.cpp` / 28850 行 C++，31 个 QML 文件）。
> 本文所有数字由脚本从 `#include` 关系与文件行数算出，可被复跑核对，不是估算。
> 本方案**只做说明与排期**，不含任何代码改动 —— 落地前需维护者按下方 §7 的卡片清单授权立卡。
>
> **修订记录（2026-10-30 复核）**：全文数字被独立重跑核对了一遍。架构论断（3 组环、修正后无环、
> 层序、CMake 重复登记、`core` 不纯）**全部复现成立**；但 §2.2 的 CLI 行数、§3.2 的边长、
> `setContextProperty` 处数等 6 项数字有误，M1 的验收线在自身范围内算不出来，
> 并遗漏了 `utilityMode` 谓词耦合、`--selftest` 与 `tests/` 重叠、`DeltaUpdate.cpp` 内置 `main()`
> 三处会导致返工的事实。修订处已在对应小节标注 **【复核修订】**。

> **统计口径**（复核时确立，后续复跑请沿用）：
> - 行数 28850 = `src/**.h` + `src/**.cpp`，**不含** `src/host/`（283 行）；含 `host/` 为 29131。
> - 「110 个 `.h/.cpp`」= 55 个类的 55 个 `.h` + 55 个 `.cpp`，**不含** `main.cpp`；含 `main.cpp` 为 111。
> - 类数 55（按 `.h` 计数）；`main.cpp` 不是类，单列。
> - 类入度按「去重后的 `#include` 关系」计，包含 `.h→.h` 与 `.cpp→.h`，包含 `main.cpp` 的引用。

---

## 1. 结论摘要

现状是「八个模块混在一个平面目录里，由单个 `main.cpp` 手动装配」。
可行的目标形态是**八个单向依赖的静态库 + 一个薄的装配层**，且拓扑排序已经跑通——**修正 3 个类的归属后即可彻底无环**。

三句话版本：

1. 分层**在技术上没有障碍**，障碍是 6 条反向边，其中 3 条是归类错误（挪一下就消），剩下都是单向合法的。
2. 第一批动作仍应是抽 `main.cpp` 的 CLI 命令，但**收益比初版估计的小得多**——实测可整块搬走的只有
   **846 行**（其中 `--selftest` 一块占 605 行），不是 1180 行；且这 605 行本该属于测试目录而非命令表。
   见 §2.2 与 §4 的 **M0.5**。
3. **必须配守卫**。上一轮「组件拆分 Main.qml 1605→1180 行」早已 `done`，现在 Main.qml 是 4260 行——
   没有 CI 约束的重构会被日常开发吃掉。守卫应**提前到 M2 之前**，理由见 §4。

---

## 2. 现状证据

### 2.1 `src/` 零分层

110 个 `.h/.cpp` 直接铺在 `src/` 顶层，`src/` 下唯一的子目录是 `host/`（只装 `HostMain.cpp`）。
没有子目录意味着：**边界只存在于读者脑中**，任何 `#include` 都能成立，编译器不会反对。

### 2.2 `main.cpp` 超载

| 指标 | 值 |
| --- | --- |
| 行数 | 1910 |
| 直接依赖的类 | 51 / 55 |
| 命令行 flag 总数 | 37 |
| CLI 派发块（花括号配对实测） | **846 行**，10 个块 —— 见下表 |
| 其中 `--selftest` 单块 | **605 行**（531–1135），占全部 CLI 的 72% |
| 其余 9 个块 | **241 行** |
| `setContextProperty` 注入 | **45 处**（1673–1730） |
| `return 0;` / `return 2;` | 22 / **23** |

> **【复核修订】** 初版写的「CLI 约 1180 行（62%）、分布在 20+ 个 `if (arguments.contains(...))` 分支、
> 每个以 `return 0;` 结束、`setContextProperty` 约 20 处」四项**全部不成立**，实测见上表。
> 1180 是把 479→1668 当成连续 CLI 区算出来的，而该区间内还夹着约 700 行对象构造
> （初版 §2.2 的分段表自己就写着「领域对象构造 ~416–1212」，与「CLI 488–528」重叠）。

它是事实上的上帝文件，但内部**已有清晰分段**：

| 区段 | 行范围（基线） | 职责 |
| --- | --- | --- |
| 日志/CLI 工具 | 88–114 | `neonboardLogHandler`、`emitCliLine` |
| 应用初始化 | 126–~416 | `QApplication`、图标、字体、`SingleInstance`、`SplashWindow` |
| 领域对象构造 | ~416–1212 | 约 30 个对象陆续构造 + 定时器/轮询连线 |
| CLI 命令分支 | 479–504、531–1135、1227–1630 | 10 个派发块，846 行 |
| QML 引擎与上下文注入 | 1671–1730 | `QQmlApplicationEngine` + **45** 处 `setContextProperty` |
| 信号连线 | 1795–1866 | 托盘 / 热键 / 单实例 / 更新 / `aboutToQuit` |
| 启动收尾 | 1868–1898 | 跳转列表、`silentStart`、`splash->finish()`、8 秒后更新检查 |
| `--smoke` 与 `exec` | 1900–1910 | |

CLI 分支**穿插在对象构造之间**（479–504 在构造之前，1227–1630 在构造之后），
每个命令依赖的对象集合不同，不能简单地整体剪切。

### 2.2.1 CLI 派发块实测清单

按花括号配对逐块量出，这是 M1b 搬迁的**真实工作量**：

| 行范围 | 行数 | 命令 | 派发写法 |
| --- | --- | --- | --- |
| 479–489 | 11 | `--assoc-status` | ① `contains` |
| 490–504 | 15 | `--register-file-assoc` / `--unregister-file-assoc` | ① |
| 531–1135 | **605** | `--selftest` | ① |
| 1227–1317 | 91 | `--plugin-report` | ① |
| 1360–1372 | 13 | `--market-sources` | ① |
| 1374–1391 | 18 | `--market-refresh` | ① |
| 1431–1436 | 6 | `--preview-list` | ① |
| 1524–1529 | 6 | `--update-sources` | ① |
| 1545–1568 | 24 | `--check-update` | ① |
| 1574–1630 | 57 | `--update-apply` | ① |
| **合计** | **846** | 10 个块 | |

### 2.2.2 三种派发写法（M1b 必须一次设计到位）

> **【复核修订】** 初版假定命令都是「一个 `if (arguments.contains("--xxx"))` 一个 `return 0`」。
> 实测 37 个 flag 用了**三套**写法，只按第一种设计命令表，搬到一半会被迫改签名。

| 写法 | 形态 | 实例 |
| --- | --- | --- |
| ① 布尔 flag | `if (arguments.contains("--x")) { ... }` | 仅 **10** 个块（见上表） |
| ② 可重复 flag + 取值 | `for (i...) if (arguments.at(i) == "--market-source") { ...at(i+1) }` | 1332–1344 |
| ③ flag + 单值 | `const int idx = arguments.indexOf("--market-install"); ...at(idx+1)` | 1393–1396，`--install-plugin-url` 1410 同款 |

因此 `CliCommand` 需要携带 **arity / repeatable / 取值语义**，不能只靠
`std::function<bool(const QStringList &, AppContext &)>`。

同时失败路径**并非都返回 0**：实测 23 处 `return 2;`（`--market-install` 缺 id、
`--market-refresh` 超时、`--check-update` 无更新等）。命令表返回值应为 `int`（0 / 2），
否则退出码变更会让 CI 里靠退出码判定的步骤误判。

### 2.2.3 `utilityMode` 谓词：M1b 的隐藏耦合点

> **【复核修订】** 初版完全没有提到这一条，风险 R1 也只写了「提前 return 顺序」。

`main.cpp` 152–184 有一个**28 个 flag 的大 OR 谓词**，决定本次启动是否走无头模式：

```cpp
const bool utilityMode = arguments.contains("--selftest") || smokeMode || syncMode || dataMode
    || arguments.contains("--emit-hook") || ... || arguments.contains("--import-ledger");
```

另有两个更小的同类谓词：`syncMode`（153–154）、`dataMode`（155–157）。

命令表搬走后这个谓词会**留在 `main.cpp`**，于是「新增一个命令」必须同时改两处 ——
漏改则新命令不会被判为 utility 模式，**会直接把 GUI 拉起来**。

修法：`CliCommand` 增加 `Mode mode{Sync | Data | Utility}` 字段，由 `allCommands()` 反推谓词，
使命令表成为唯一真相源。这条应写进 M1b 的 `acceptance`。

### 2.3 CMake 是平铺清单且跨 target 重复

`CMakeLists.txt` 285 行，源文件逐个手写。存在两组重复登记：

| 重复源文件 | 同时出现在 |
| --- | --- |
| `AppPaths` / `SingleInstance` / `AppActions` | `NeonBoard`、`NeonBoardHost` |
| `DeltaUpdate` / `BsdiffWrapper` / `thirdparty/bsdiff` / `thirdparty/bzip2` | `NeonBoard`、`neonboard-delta-tool` |

后果：加工具类要么漏登记、要么两处都改；`NeonBoardHost` 和主程序的 AppPaths 行为一旦要求一致，
没有任何机制阻止它们漂移。

### 2.4 QML 侧：拆过，又涨回来了

`qml/Main.qml` 现在 **4260 行**，而台账里那张「组件拆分：Main.qml 1605 → 1180 行」
（`acceptance` 含 `(Get-Content qml/Main.qml).Count -le 1200`）已结项为 `done`。
`components/` 目录确实做起来了（26 个组件），但 Main.qml 自己承担了全部装配与编排逻辑。

另外 `CMakeLists.txt` 里有 **31 条手工 `QT_RESOURCE_ALIAS`**（137–167 行）：每新增一个组件要同步改
`QML_FILES` 列表 + 一条 alias 语句，漏改则运行时报「组件不可见」（`docs/pitfalls.md` 记过这个坑）。

---

## 3. 目标分层

### 3.1 模块划分与成员

按 `#include` 关系聚类 + 职能语义归纳，覆盖范围**完整无遗漏**（8 组 55 个类 + `main`，行数合计与总量 28850 精确吻合）：

| 模块 | 类数 | 行数 | 成员 |
| --- | --- | --- | --- |
| `core` 基础设施 | 7 | 2163 | `Database` `AppPaths` `SingleInstance` `AppActions` `Metrics` `UiSettings` `ScopeMatcher` |
| `task` 任务域 | 10 | 5013 | `TaskModel` `TaskRunStore` `TaskExec` `TaskExtra` `TaskTree` `TaskInputParser` `TaskUndo` `TaskFilterModel` `TaskAudit` `TaskStats` |
| `plugin` 插件宿主 | 9 | 4946 | `PluginRuntime` `PluginMarket` `PluginLibrary` `PluginLoader` `PluginContext` `PluginPackage` `PluginHost` `PluginEventBus` `HookEventQueue` |
| `agent` 自动化链 | 8 | 4741 | `AgentProfile` `WorkflowRunner` `WorkflowPresets` `TriageRunner` `AcceptanceRunner` `Scheduler` `LedgerImport` `TaskRunner` |
| `board` 看板与呈现 | 8 | 5129 | `PreviewHub` `ProjectMap` `RoadmapSync` `BoardSync` `BoardColumns` `ProjectStore` `CardDetail` `DataPort` |
| `shell` 窗口与外壳 | 8 | 1940 | `WindowsIntegration` `SplashWindow` `WindowController` `TrayController` `DragFeel` `Hotkey` `KeyMove` `ReminderScheduler` |
| `update` 更新与差分 | 3 | 2377 | `UpdateManager` `DeltaUpdate` `BsdiffWrapper` |
| `theme` 主题 | 2 | 631 | `ThemeMarket` `ThemePack` |

> 注：`thirdparty/`、`resources/`、`src/host/` 不在本次拆分范围内。

### 3.2 依赖层序（已验证无环）

对上述划分做拓扑排序，**一次跑通，无环**。合法层序（低 → 高，箭头只许向下）：

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

> **【复核修订】** 初版写「模块间依赖边合计 47 条」——**该数字在任何口径下都复跑不出来**。
> 实测：**模块级（去重后）11 条**；**类级（含 `main.cpp`）110 条**。
> （可疑来源：`arguments.contains` 在 `main.cpp` 中恰好出现 47 次，初版疑串了统计口径。）
> 层序与「无环」结论不受影响 —— 拓扑排序用的是 11 条模块级边，复核后仍一次跑通。

实测模块级边（11 条）：

```
agent  → core, task
board  → core, task
plugin → core, shell, task
shell  → task
task   → core
theme  → core
update → core
```

核心事实：

- `core` 被 6 个模块依赖，`Database` 单点入度 **23**（55 个类里最高）
- `TaskModel` 入度 **18**（第二高）
- `theme`、`board`、`update` **无人依赖**，是纯叶子/顶层

> 入度口径：去重后的 `#include` 关系，含 `.h→.h` 与 `.cpp→.h`，含 `main.cpp` 的引用。
> 初版写 24 / 19，复核为 23 / 18，差 1 属口径细节，不影响任何结论。

### 3.3 必须先行修正的三个归属（M0）

原始聚类存在 3 组双向环，全部由**单个类放错组**造成。定位到的精确边：

| 反向边 | 位置 | 处置 |
| --- | --- | --- |
| `DataPort.cpp → TaskModel.h` | 造成 `core ↔ task` 环 | `DataPort` 划归 **`board`**。它是给 QML 用的数据端口，本质是呈现层桥，不是基础设施 |
| `HookEventQueue.cpp → PluginEventBus.h` | 造成 `core ↔ plugin` 环 | `HookEventQueue` 划归 **`plugin`**。它是 git 钩子事件转插件事件的桥，属于插件侧的输入源 |
| `TaskRunner.cpp → AgentProfile.h` | 造成 `task ↔ agent` 环 | `TaskRunner` 划归 **`agent`**。它是任务执行器，属于自动化执行链而非任务数据模型 |

修正后三组环全部消失（§3.2 的层序即修正后的结果）。
这三个处置**只是归属声明的更正，不移动文件、不改一行代码**——文件搬迁在 M2 统一做。

余下的 2 条 `plugin → shell` 边（`PluginRuntime.cpp` / `PluginContext.cpp` → `TrayController.h`）
是**单向合法**的（shell 在 L6，plugin 在 L7），本次保留。它是「插件往宿主托盘挂东西」的回调耦合，
更彻底的做法是接口反转（插件发信号、由装配层连到托盘），列为 §7 待拍板项 P3。

### 3.4 一个必须提前说清的预期

**静态库粒度 ≠ Qt 解耦。** 各模块实际用到的 Qt 组件：

| 模块 | 需要的 Qt 组件 |
| --- | --- |
| `core` | Sql（`Database`）、Network（`SingleInstance` 的 `QLocalServer`）、Quick（`Metrics` 的 `QQuickWindow`）、Core（`QProcess`/`QSettings`） |
| `task` | Sql、Core |
| `agent` | Sql、Core（`QProcess`） |
| `board` | Sql、Core（`QProcess`、`QFileSystemWatcher`、`QThreadPool`） |
| `plugin` | **Qml（`QQmlApplicationEngine`/`QQmlComponent`/`QQmlContext`/`QQmlEngine`）**、Sql、Network、Core |
| `theme` / `update` | Network、Core |
| `shell` | Widgets、Quick、Core |

> **【复核修订】** 初版把 `plugin` 只标为「Sql + Network」，**漏了 Qt::Qml** ——
> 而 `plugin` 正是 QML 插件宿主，用了 `QQmlApplicationEngine` / `QQmlComponent` /
> `QQmlContext` / `QQmlEngine` 四个 Qml 头文件。M3 若照初版配 `nb_plugin` 的链接项，
> 会在**链接期直接失败**。这是修订里最需要落地时盯着的一条。

注意 `core` 里的 `Metrics` 依赖 `QQuickWindow`、`SingleInstance` 依赖 `QLocalServer`——
**所谓「基础设施」并不纯净**，它必须 link Network + Quick + Sql，几乎等于全套 Qt 组件。

因此 M3 的收益要如实定义为：**约束本项目 8 个模块之间的依赖方向**（compiler/linker 级强制），
而不是减少 Qt 链接或缩短编译时间。这仍然值得做——它能防止 `plugin` 被 `task` 反向依赖这类退化，
但这不是性能优化，别按性能立项。

---

## 4. 分批计划

> **【复核修订】** 初版批次的三个问题，已在本表修正：
> 1. **缺「对象构造」这一批** —— `main.cpp` 里约 700 行的对象构造（416–1212，含次序敏感约束）
>    在初版里没有任何批次负责：M1a 明确不搬、M2 只搬路径、M3 只改 CMake。新增 **M1d**。
> 2. **`--selftest` 的 605 行不该进命令表** —— 它是内联手写测试套件，与 `tests/` 下 34 个
>    `tst_*.cpp` 职责重叠。新增 **M0.5** 先做对齐，能把 M1b 工作量降约 70%。
> 3. **M5 守卫排在最后** —— 等于让 M2/M3 两次大改在无约束下发生。提前到 **M2 之前**。

按「依赖顺序 + 风险由低到高」排期。**M4 可以在 M2 之后任意时刻并行**，两者无代码交集。

| 批次 | 内容 | 触及文件 | 风险 | 建议前置 |
| --- | --- | --- | --- | --- |
| **M0** | 更正 3 个类的模块归属（仅文档+后续归类，零代码改动） | 本文 / 台账 | 无 | — |
| **M0.5**（新增） | `--selftest` 605 行与 `tests/` 34 个用例对齐：能迁 ctest 的迁走，剩余抽 `SelfTestRunner` | `src/main.cpp`、`tests/**` | 低 | M0 |
| **M1** | 抽 `main.cpp`：命令表（含 mode/arity/退出码）+ AppContext + 连线装配器 + **构造工厂** | `src/main.cpp` + 新增 4 个文件 | 中（M1d 涉构造次序） | M0.5 |
| **M5**（提前） | 守卫：依赖方向 + 行数上限检查脚本接入 CI | 新增脚本 + workflow | 低 | M1 |
| **M2** | `src/` 物理分层为 8 个子目录，CMake 改 `target_sources` | 110 个文件的路径 + CMakeLists | 中（纯搬运，但要一次成型） | M5 |
| **M3** | 8 个 CMake `STATIC` target + 显式依赖声明，消除跨 target 重复 | CMakeLists | 中高（AUTOMOC/moc 边界要重排） | M2 |
| **M4b** | QML 多模块 URI 化，消除 31 条手工 alias | `qml/**` + CMakeLists | 中 | M2 |
| **M4a** | Main.qml 编排逻辑再拆分（4260 → ≤1200） | `qml/Main.qml`、`qml/components/**` | 中 | **M4b** |

### M0.5 —— `--selftest` 与 ctest 对齐（新增）

`--selftest` 单块 605 行（531–1135），占全部 CLI 的 **72%**。它不是「命令行功能」，
而是一段内联的手写测试脚本（`addTask` → `taskById` → `updateTask` → `moveTask` …），
与 `tests/` 下已有的 34 个 `tst_*.cpp`（由 ctest 驱动）**职责重叠**。

先做对齐再谈搬迁，收益是双向的：

- 能迁进 `tests/` 的迁走 —— M1b 的搬迁量从 846 行降到 **241 行**，工作量减约 70%；
- 迁不走的（需要真实数据目录 / 端到端的）才抽成 `SelfTestRunner`，作为 M1b 的一条命令；
- 顺带消除「自检逻辑有两份」这个长期隐患。

**验收**：`ctest --test-dir build -C Release` 全绿且用例数不减少；
`build/Release/NeonBoard.exe --selftest` 输出与对齐前**逐字节一致**。

### M1 —— 拆 `main.cpp`

> **【复核修订】** 初版目标「1910 → **< 400 行**」在它自己划的范围内**算不出来**：
> 1910 − 846（M1b）− 104（M1c）≈ **960 行**，距 400 差 **560 行**，而 M1a 又明确「不搬对象构造」。
> 那 560 行里的大头正是对象构造。因此：**要么补 M1d，要么把验收线改到 950**。
> 本修订选择补 M1d，并把验收线定为 **≤ 450 行**（留 50 行余量给日志与 `exec` 收尾）。

目标：1910 行 → **≤ 450 行**。分四步，每步都可独立验证。

**M1a · `AppContext` 指针聚合**
```cpp
// src/app/AppContext.h
struct AppContext {           // 全部为指针，不拥有对象、不改变构造顺序
    Database *database{};
    TaskModel *taskStore{};
    TrayController *tray{};
    UpdateManager *updater{};
    // ... 实测需覆盖 45 个 setContextProperty 注入点（1673–1730），不是初版估的 20 个
};
```
关键约束：**不在这一步把对象构造搬进来**。现有注释明确标注了构造次序敏感
（416 行「必须声明在 `QQmlApplicationEngine` 之前」），搬构造会连带风险，**由 M1d 单独处理**。

**M1b · CLI 命令表**（M0.5 之后，搬迁量 **241 行**而非 846）
```cpp
// src/app/CliCommand.h
enum class CliMode { Gui, Sync, Data, Utility };   // 反推 utilityMode 谓词，见 §2.2.3

struct CliCommand {
    QString flag;                     // "--plugin-report"
    QString summary;                  // 用于 --help
    CliMode mode{CliMode::Utility};   // 决定本次启动是否无头
    int arity{0};                     // 0=布尔 flag；1=取一个值；n=取 n 个值
    bool repeatable{false};           // 对应 §2.2.2 写法 ②（--market-source 可重复）
    // 返回退出码：0=成功并消费本次启动，2=失败（现状有 23 处 return 2）
    // 返回 -1 表示未命中该命令，继续匹配下一条
    std::function<int(const QStringList &args, AppContext &ctx)> run;
};
std::vector<CliCommand> allCommands();               // src/app/CliCommands.cpp
```

> 三种派发写法必须一次设计到位（§2.2.2），否则搬到 `--market-install` / `--market-source`
> 时会被迫改签名返工。

搬迁顺序：先 `--update-apply`（57 行）与 `--check-update`（24 行）练手，
再搬 `--plugin-report`（91 行），最后收尾 6 行的那些。
**每搬一条跑一次 `--selftest`**，并与重构前保存的 stdout 基线**逐字节比对**。

**M1c · 连线与启动装配器**（约 104 行）
- `WireUp`：`dispatch` lambda（1795–1812）+ 托盘/热键/更新/`aboutToQuit` 连线（1814–1866）
- `StartupSequencer`：跳转列表、`silentStart`、`splash->finish()`、8 秒后自动更新检查（1868–1898）

**M1d · 对象构造工厂**（新增，约 700 行 —— 达成 ≤450 的关键）

这是初版**完全缺失**的一批。`main.cpp` 416–1212 之间的对象构造没有任何批次负责过。

- 抽 `src/app/AppObjects.cpp` 的 `buildAppObjects(AppContext &)`，按现状顺序构造并返回聚合体；
- **必须保留构造次序**：416 行注释「必须声明在 `QQmlApplicationEngine` 之前」是硬约束，
  搬迁时不得重排；
- M1a 的 `AppContext` 在这一步才从「外部填指针」升级为「由工厂填充」，M1a 阶段保持纯指针聚合。

**验收**
```powershell
pwsh -File scripts/build.ps1 -SelfTest
build/Release/NeonBoard.exe --smoke          # 输出 METRICS 且无 QML 报错
ctest --test-dir build -C Release            # 全绿
# 逐个 CLI 命令输出与重构前逐字节比对（先保存基线输出），退出码 0/2 必须逐一复核
(Get-Content src/main.cpp).Count             # <= 450
# utilityMode 回归：新加一条命令后必须仍走无头模式，不得拉起 GUI
```

### M2 —— `src/` 物理分层

```
src/
  core/    task/    plugin/   agent/
  board/   shell/   update/   theme/
  app/     host/    main.cpp
```
CMake 改用 `target_sources` 分块登记。**所有 8 个子目录都加入 include 路径，因此 `#include "Database.h"`
不需要改**——这是把 M2 的风险压到最低的关键设计（搬运与改 include 分离）。

**验收**：`--selftest` / `--smoke` / `ctest` 全绿；`git diff --stat` 只显示 rename（无内容改动）。

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
# NbCore.cmake 里定义一个 nb_common_qt 接口目标，统一挂载 Qt 组件，避免 8 处重复
```

落地要点与风险：
- `CMAKE_AUTOMOC` 对每个 target 独立生效，moc 产物归属会变化 —— 需要重排 `resources/` 与编译选项
- `NeonBoardHost` 改为 `target_link_libraries(NeonBoardHost PRIVATE nb_core)`，
  天然消除 `AppPaths`/`SingleInstance`/`AppActions` 的重复登记
- `neonboard-delta-tool` 改为链接 `nb_update`，同理消除 thirdparty 重复
- 三个 target 都保留 `WIN32_EXECUTABLE TRUE`
- **`src/DeltaUpdate.cpp` 404–478 有 `#ifdef NEONBOARD_DELTA_TOOL_MAIN` 包着的 `main()`**
  （初版未记录，见风险 R6）：该宏只能在 `neonboard-delta-tool` target 上定义，
  否则静态库里会多出一个 `main` 符号与主程序冲突
- **`nb_plugin` 必须 link `Qt::Qml`**（§3.4 复核修订），否则链接期直接失败

**验收**：三个 target 均构建成功；`ctest` 全绿；`build-delta.ps1` 产出的 delta 包可被 `--update` 正常消费。

### M4 —— QML 模块化（**顺序已反转：先 M4b 后 M4a**）

> **【复核修订】** 初版写「两件事，建议分开」但顺序是 a 然后 b。应**反过来**：
> 改 URI 会动所有 `import` 语句，若先拆 `Main.qml` 再改 URI，两批改动会叠加在同一批文件上，
> 冲突概率高。先立好模块边界，再在稳定边界内做拆分。

**M4b · 多 QML 模块 URI（先做）**
`NeonBoard.Core` / `NeonBoard.Components` / `NeonBoard.Theme`，各模块自带 `qmldir`，
**从根上消除 31 条手工 `QT_RESOURCE_ALIAS`**。新增组件从此只需改一处文件列表。

**M4a · Main.qml 4260 → ≤ 1200 行（后做）**
把其中的编排函数（模板进位 detail 处理、`Loader` 生命周期管理、面板开关、`Shortcut` 绑定）
抽成 `qml/components/WorkspaceChrome.qml` 等组合组件，或直接抽出 JS 控制器。
现有 **10 个** `*Loader`（初版写「8 个」但列了 10 项：taskEditor / cardDetail / taskComposer /
settings / preview / pluginPanel / projectMap / themeMarket / permissionDialog / pluginLibrary）
是天然的分组依据。

**验收**：`--smoke` 首屏无 QML 报错（回归 Main.qml 拆分历史上踩过的坑）；
`(Get-Content qml/Main.qml).Count` ≤ 1200；CMakeLists 中 `QT_RESOURCE_ALIAS` 语句数为 0。

### M5 —— 守卫（防止再次回潮，**已提前到 M2 之前**）

> **【复核修订】** 初版把守卫排在 M3 之后，等于让 M2（搬 110 个文件）和 M3（改 CMake）
> 两次最大的改动**在没有约束的情况下发生**。M2 是纯搬运，正是守卫最该在场、
> 也最能防止搬运过程引入反向依赖的时点。

新增 `scripts/check_module_deps.py`：扫描模块间 `#include`，发现**反向依赖**（低层 include 高层）即退出非 0；
`Main.qml` / `main.cpp` 的行数上限作为 `-max-lines` 参数检查。接入 `.github/workflows/ci.yml` 与 `verify.ps1`。

实现上**照 `scripts/check_scope.py` 的棘轮模式**写（只检查本次改动涉及的文件，历史存量不阻塞），
不要做成全量扫描——否则首次接入就会红，又会被 `--no-verify` 绕过。
仓库已有 `check_scope.py` / `check_script_hygiene.py` / `check-perf.ps1` / `check-format.ps1`
四个同类脚本与对应 CI step，直接复用其形态与输出约定。

> 这一步不能省。`Main.qml` 从 1180 涨回 4260 就是因为当年只有一次性拆分、没有持续约束。
> 没有守卫的重构，会在半年内被日常开发吃掉。

---

## 5. 验收统一口径

每一批都必须通过全量：

```powershell
pwsh -File scripts/build.ps1 -SelfTest
build/Release/NeonBoard.exe --selftest
build/Release/NeonBoard.exe --smoke
ctest --test-dir build -C Release
python scripts/task_gate.py selftest
```

**回滚**：每批一个独立分支，CI 全绿后走 `scripts/git-done.ps1` 合并。任一批失败直接弃分支，不影响已合批次。

---

## 6. 风险登记

| # | 风险 | 等级 | 缓解 |
| --- | --- | --- | --- |
| R1 | M1b 搬迁 CLI 时改变启动语义（某命令的提前 return 顺序变了） | 中 | 逐条搬迁 + 每条保存重构前 stdout 基线做逐字节比对 |
| R2 | M2 改路径后构建失败或 moc 找不到头文件 | 中 | 8 个子目录全量加 include 路径，不改动任何 `#include` 文本 |
| R3 | M3 的 AUTOMOC 边界变化导致 moc 产物丢失 | 中高 | 单独分支先行验证 `nb_core` 一个库，跑通再推广 |
| R4 | M4 的 QML 拆分引入运行时加载失败（历史踩过） | 中 | `--smoke` 作为硬门禁，要求启动时无 QML 报错；参考 `docs/pitfalls.md` 第 14 节 |
| R5 | 重构期间与并行会话抢同一个 `main.cpp` | 高 | 拆分前确认无其他会话在改 `src/`，批次分支命名 `refactor/<批次>`，可用 `-Card` 领用隔离 |
| **R6**（新增） | `src/DeltaUpdate.cpp` 404–478 内置 `#ifdef NEONBOARD_DELTA_TOOL_MAIN` 的 `main()`；M3 做成静态库后若宏泄漏到主程序 target，会出现**重复的 `main` 符号** | 中高 | 该宏只在 `neonboard-delta-tool` 上 `target_compile_definitions`；验收时三个 exe 均须能链接并启动 |
| **R7**（新增） | M1b 命令表搬走后，`main.cpp` 152–184 的 `utilityMode` 谓词未同步，新增命令**不被判为无头模式而直接拉起 GUI**（§2.2.3） | 中 | `CliCommand` 带 `CliMode`，由 `allCommands()` 反推谓词；验收含「新增一条命令后仍走无头模式」的回归 |
| **R8**（新增） | M1b 命令表只按「布尔 flag」设计，搬到 `--market-source`（可重复取值）/`--market-install`（取单值）时**被迫改签名返工**（§2.2.2） | 中 | 首个命令落地前就把 `arity` / `repeatable` 设计进 `CliCommand` |
| **R9**（新增） | M1 按初版「< 400 行」验收，做到 ~960 行就判失败，而缺口没有任何批次负责（§4） | 中 | 补 M1d 构造工厂，或把验收线改为 ≤ 950；二选一必须**在立卡前**拍板 |

---

## 7. 待维护者拍板 / 卡片登记建议

按宪章 §2 条款 6，以下立卡只能由维护者执行（或在会话中对具体批次显式授权后由 agent 代登记，
提交前缀 `chore(board):`、body 含「授权：」「批次：」两行）。

建议卡片拆分（8 张，与批次 1:1）：

| 建议短号槽位 | 标题 | 建议 `scope` glob |
| --- | --- | --- |
| M0.5（新增） | `--selftest` 605 行与 `tests/` 用例对齐 | `src/main.cpp`, `tests/**` |
| M1 | 抽取 AppContext 与 CLI 命令注册表（含 mode/arity/退出码） | `src/app/**`, `src/main.cpp`, `CMakeLists.txt` |
| M1d（新增） | 对象构造工厂，main.cpp 降至 ≤450 行 | `src/app/**`, `src/main.cpp` |
| M5 | 依赖方向守卫脚本与 CI 接入（提前到 M2 之前） | `scripts/**`, `.github/**` |
| M2 | src/ 按模块建物理子目录 | `src/**`, `CMakeLists.txt` |
| M3 | CMake 拆分为 8 个静态库 target | `CMakeLists.txt` |
| M4b | QML 多模块 URI 化，消除手工 resource alias | `qml/**`, `CMakeLists.txt` |
| M4a | Main.qml 编排逻辑再拆分（4260 → ≤1200） | `qml/Main.qml`, `qml/components/**` |

> M3 的 `scope` 去掉了初版里的 `cmake/**` —— 仓库当前**没有 `cmake/` 目录**，
> 若 M3 决定新建该目录再补回。

**待拍板事项**：

- **P1**：M1 是否允许引入 `AppContext` 这一新抽象？还是坚持「只做机械搬迁、不引入结构」？
  前者能让 M1c 的连线也搬出去。推荐前者。
  （初版写的「后者则 M1 止步于 ~600 行」经复核不成立：不引入抽象时 M1 只能砍掉 846+104 行，
  止步于 ~960 行。）
- **P1.5**（新增，建议最先拍）：M1d 是否做？做则 `main.cpp` 可达 ≤450 行，但要动构造次序敏感的
  ~700 行；不做则验收线必须改成 ≤950。**这条不定，M1 的卡就立不了。**
- **P2**：M3 是否值得做？§3.4 已说明它对编译速度没有帮助，收益是「依赖方向被 linker 强制」。
  若只想要可读性，M2 就够了，M3 可以无限延期。
- **P3**：`plugin → shell` 的托盘回调耦合是否做接口反转？做则插件层不再依赖任何窗口/托盘类型，
  架构更干净，代价是 `PluginRuntime` 的对外契约要改。
- **P4**：`src/app/` 是否为 `main.cpp` 的新家？还是保留 `main.cpp` 在 `src/` 根。

---

## 8. 附：数据可被复跑核对

本文的模块矩阵、层序、循环依赖检测均来自对 `src/**.h/.cpp` 的 `#include` 扫描。
关键推导有四条可供复核（**均已由 2026-10-30 的独立复跑验证**）：

1. 原始聚类（未修正归属）存在 3 组双向环：`core↔task`、`core↔plugin`、`agent↔task` ✅ 复现
2. 造成它们的 6 条边已逐条定位（见 §3.3 表格） ✅ 复现
3. 修正归属后拓扑排序一次跑通，层序见 §3.2 ✅ 复现
4. 模块级边 11 条 / 类级边 110 条（见 §3.2 复核修订）

**复跑清单**（下次改本文数字时，请照此逐项核对，避免再出现 47 这种串口径的数）：

| 项 | 期望值 | 取数方式 |
| --- | --- | --- |
| 类数 | 55 | `ls src/*.h \| wc -l` |
| 类文件数（不含 main.cpp） | 110 | 55 `.h` + 55 `.cpp` |
| C++ 总行数（不含 `host/`） | 28850 | 含 `host/` 为 29131 |
| `main.cpp` 行数 | 1910 | |
| CLI 派发块 | 846 行 / 10 块 | 按花括号配对量，勿按行区间估算 |
| `--selftest` 单块 | 605（531–1135） | |
| `setContextProperty` | 45 | 勿按「约 20」沿用 |
| `QT_RESOURCE_ALIAS` | 31（CMakeLists 137–167） | |
| `return 0;` / `return 2;` | 22 / 23 | |
| `Database` / `TaskModel` 入度 | 23 / 18 | 去重 `#include`，含 `main.cpp` |
| 模块级边 / 类级边 | 11 / 110 | |

> 实施时建议把该扫描脚本固化为 M5 的 `scripts/check_module_deps.py`，
> 让「现在可行」变成「以后仍然可行」。
> 该脚本的**第一个检查项应当是本文自身数字的可复跑性**——本文初版 6 处数字有误、
> 3 处事实遗漏，正是因为「算过一次就当成事实」而没有留下一份可复跑的脚本。
