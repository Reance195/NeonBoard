---
state: proposed
stage: R21
tier: P4
title: 多板切换：板选择器与建删改 + 切板上下文按板隔离
author: codebuddy-session-2026-11-10
evidence: 路线图 §4「多板切换」是 P0.5 表格唯一未开工项（「R4-03 的后续」，规模 M）。数据层 R4-03 已就位：board / board_column 表与默认三列已建（Database.cpp:491-554，schema v2）、Database::boards() 与 boardColumns(boardId) 读取 API 已有（Database.h:47-48）、task.column_id 隐式归属板。但界面与写路径全部硬编码默认板：BoardColumns::reload() 写死 m_database->defaultBoardId()（BoardColumns.cpp:173），Database::addColumn() 写死 defaultBoardId()（Database.cpp:669）；板 CRUD（建 / 改名 / 删）零实现；当前板无持久化。立卡时台账快照实况：356 done + 3 张弃单墓碑，0 张可领卡，看板清空。维护者 2026-11-10 会话指令「领下一张卡片或立新卡」，选项答复「多板切换（推荐）」，据此授权立卡、领用、实施、verify、rebase、推 origin/main 与结卡。方案取舍：撤销栈按板隔离采用「撤销操作跨板时直接跳过该步并给出提示」的简单口径，不做分段栈；删板采用「板内有卡拒删并提示先迁卡」的 fail-closed 口径，不做自动迁移。
scope: src/core/Database.h;src/core/Database.cpp;src/core/UiSettings.h;src/core/UiSettings.cpp;src/board/BoardColumns.h;src/board/BoardColumns.cpp;src/task/TaskUndo.h;src/task/TaskUndo.cpp;src/task/TaskModel.h;src/task/TaskModel.cpp;src/task/TaskFilterModel.h;src/task/TaskFilterModel.cpp;qml/Main.qml;qml/components/BoardChrome.qml;qml/components/ColumnManager.qml;qml/components/BoardColumn.qml;CMakeLists.txt;tests/**
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke QML 致命告警 0 行;Database 新增板 CRUD：createBoard（自带默认三列）/ renameBoard / deleteBoard（板内仍有卡片时拒删并返回原因，fail-closed），BoardColumns 列集合与列管理全部按「当前板」读写，reload / addColumn / renameColumn / moveColumn / removeColumn 不再硬编码 defaultBoardId;板切换：切板后列集合、顶栏统计片、快速录入落列（columnIdForStatus）、拖拽落点全部跟随当前板；当前板 id 持久化（重启恢复上次板），当前板被删回退默认板;兼容边界：老库单板（仅 board-default）时界面行为与现状一致，不新增表列、不升 user_version（tst_databasemigration 通过）;撤销隔离：切板后撤销不作用到另一板的卡片（跨板步跳过并提示）;测试：tst_boardcolumns 增双板用例（切板 / 建删改 / 隔离 / 当前板删除回退），selftest 相应断言;维护者人工复核：沙箱实例双板切换与建删改肉眼验收（DX-163 同款收尾）
depends:
createdAt: 2026-11-10T19:30:00+00:00
---

## 目标

把 R4-03 铺好的多板数据层接通到界面：BoardColumns 与列管理从硬编码 defaultBoardId 改为跟随「当前板」，补齐板 CRUD（建 / 改名 / 删，删板 fail-closed），提供板切换入口并持久化当前板；切板后列集合、统计、录入落列、拖拽落点全部按板隔离，撤销不跨板。老库单板行为与现状一致。

## 验收

见 frontmatter `acceptance`（7 条）。第 7 条诚实边界：双板切换的观感请维护者人工复核。

## 边界

见 frontmatter `scope`：

- `src/core/Database.*`：板 CRUD 三 API + 相关查询，不动表结构、不升 user_version；
- `src/board/BoardColumns.*`：当前板状态 + 按板重读；
- `src/core/UiSettings.*`：当前板 id 持久化；
- `src/task/TaskUndo.*`、`TaskModel.*`、`TaskFilterModel.*`：撤销跨板跳过与过滤跟随（仅按需最小改动）；
- `qml/Main.qml`、`BoardChrome.qml`：板切换器入口（新增 QML 组件须 CMake 双登记，DX-163 判据会拦漏登记）；
- `ColumnManager.qml`：列管理跟随当前板；
- `tests/**`：双板用例。

**不动**：task 表结构、plan-hook / 插件链路、DataPort 导入导出语义（导出含全部板的数据即可，无需按板导出）。

## 输入

基线 `origin/main`（13e8aeb）。R4-03 / R10 系列已结卡。缺陷面：BoardColumns.cpp:173 与 Database.cpp:669 的硬编码。

## 产出

多板可用：切板即换列集合与任务视野；板 CRUD 齐备；双板用例入 ctest；路线图 §4 多板切换从「未开工」转「已交付」。
