# 测试体系（R4-05）

本文件说明 `tests/` 的定位、边界与扩展方式。卡片：R4 自动化测试体系 + CI 质量门禁。

## 为什么是「纯逻辑优先」

NeonBoard 的回归风险集中在两处：**输入解析**（用户随手一句话要变成结构化任务）
与**数据/包处理**（插件包解包、原子替换、越界路径）。这两类问题靠「起 GUI 点一遍」
很难稳定覆盖，而它们的逻辑本身不依赖窗口、数据库连接或 QML 引擎。

因此测试目标只收录**无外部状态**的模块：

| 测试目标 | 被测源码 | 覆盖内容 |
| --- | --- | --- |
| `tst_taskinputparser` | `src/TaskInputParser.cpp` | 优先级（数字/中文）、标签去重、绝对/相对日期、星期（裸周几顺延、下周）、时刻（24 小时、24:00 边界、上午/下午/中午换算）、「只给时刻且已过 → 顺延」、标题清理 |
| `tst_pluginpackage` | `src/PluginPackage.cpp` | 包合法性校验（非 JSON / 缺 manifest / 缺 files / 空 id）、`id → 目录名`、manifest 与 files 落盘、**目录穿越拒绝**、覆盖安装整体替换 |
| `tst_appactions` | `src/AppActions.cpp` | 命令行 → 动作的优先级（palette > newtask > …）、动作白名单（拒掉任意 QML 方法名注入）、动作 → QML 调用（无参/带参不混用）、二次启动 IPC 帧的往返/超长/换行折叠/UTF-8 |
| `tst_windowsintegration` | `src/WindowsIntegration.cpp` | AUMID 契约稳定性、`shell\open\command` 引号与 `%1` 占位保留、`--install-plugin-file` 参数提取、关联状态结构、**跳转列表任务项可点击**（参数能被动作层认出且不是"只亮窗口"）。**只读**，不做任何注册表/跳转列表写入 |
| `tst_databasemigration` | `src/Database.cpp` | schema 迁移：新库直接建到 v2（1 板 + 3 列）、v1 老库（带 `CHECK(status)`）搬迁后标题/备注/优先级/标签/时间戳/`column_id` 回填逐项一致、改过列后重开不被回填覆盖、`user_version` 高于程序支持版本时拒绝打开 |
| `tst_taskcolumns` | `src/TaskModel.cpp` / `src/TaskFilterModel.cpp` | 列感知模型层：新增任务落默认列、`moveTaskToColumn` 同步 status/columnId/completedAt 并落盘、按列过滤不把同状态的另一列串进来、列过滤为空时回退 status 过滤、未知列被拒绝 |
| `tst_boardcolumns` | `src/BoardColumns.cpp` / `src/TaskModel.cpp` | 列清单由库驱动：列顺序与角色、搜索/阶段过滤落到每一列、按列录入（列优先于状态）、列内精确插位、按状态移动/改状态时同步列（防「doing 挂在待办列」）、`reload` 列集合未变时不重建模型、撤销连列一起还原 |

## 结构

```
tests/
├── CMakeLists.txt          # 用 neonboard_add_test(<模块>) 登记
├── tst_taskinputparser.cpp
├── tst_pluginpackage.cpp
├── tst_appactions.cpp
├── tst_windowsintegration.cpp
├── tst_databasemigration.cpp
├── tst_taskcolumns.cpp
└── tst_boardcolumns.cpp
```

`tests/CMakeLists.txt` 的关键取舍：**把被测 `.cpp` 直接编进测试目标**，不链接
`NeonBoard` 主程序。主程序是 `WIN32_EXECUTABLE` 且打包了 QML 模块，作为测试依赖既过重
也拿不到测试入口。代价是被测源码会编译两次，收益是测试可离线、可并行、无 QML 引擎依赖。

根 `CMakeLists.txt` 用 `include(CTest)` 提供 `BUILD_TESTING` 开关（默认 `ON`）。
默认开启是有意的：`cmake --build` 会一并编译测试目标，**编译不过即等于门禁失败**，
不会出现「测试代码烂掉但没人发现」。

## 运行

```powershell
ctest --test-dir build -C Release --output-on-failure      # 全部
ctest --test-dir build -C Release -R taskinputparser -V    # 单个，带详细输出
ctest --test-dir build -C Release -N                        # 只列用例
```

跳过测试出包：`-DBUILD_TESTING=OFF`。

## 写新测试的约定

1. **固定时间基准**：涉及相对日期的断言一律传确定的 `QDateTime`（如 `2026-10-25 10:00`
   周日），不要用 `QDateTime::currentDateTime()`，否则用例会随执行时间漂移。
2. **断言口径与实现同源**：期望的 `dueAt` 用 `QDateTime(date, time).toSecsSinceEpoch()`
   构造，而不是硬编码数字 —— 硬编码会在非 UTC 机器上误报。
3. **一行为一用例**：用例名说清规则（`timeOnlyInPastRollsToTomorrow`），失败即可定位规则。
4. **安全断言要正向验证**：例如路径穿越不仅要 `install` 返回 false，还要断言目标目录外
   **确实没有**文件生成。
5. **临时目录**：用 `QTemporaryDir`，不要污染仓库或用户数据目录。
6. **中文用例数据**：MSVC 必须带 `/utf-8`（`neonboard_add_test` 已统一加），否则会按本地
   代码页解析成乱码，表现为「断言莫名失败」。

## CI 门禁顺序

`Windows 编译与自检` 作业内按序执行，任一失败即阻断合入：

1. 配置 CMake（`-DBUILD_TESTING=ON`）
2. 编译 Release（**含测试目标**）
3. **单元测试**（`ctest --output-on-failure`）
4. 数据库自检 `--selftest`
5. 冒烟启动 `--smoke` 并扫描日志关键字
6. 看板状态一致性（仅 PR：`derive_board_status.py --check`）

## 下一步可扩展方向

- **数据层**：`TaskModel` / `DataPort` / `TaskStats` 需要真实 sqlite。做法是建临时库
  （`QTemporaryDir` + `QSQLITE`），按 `Database` 的 schema 初始化后跑增删改与统计，
  重点覆盖「到期时间排序」「完成时间统计口径」「损坏库降级」。
- **关联防环**：`TaskTree::setParent` 的成环拒绝是纯内存逻辑，喂一张构造好的任务表即可测。
- **插件包安全**：继续补「同名 key 覆盖」「超深子目录」「非字符串 value」等畸形包。
