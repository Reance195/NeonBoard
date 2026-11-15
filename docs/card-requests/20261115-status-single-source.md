---
state: approved
card: DX-168
stage: DX
tier: P4
title: 卡片状态同源：column_id 为唯一可写源，status 降为数据库派生的只读投影
author: maintainer-session-2026-11-15
evidence: 维护者 2026-11-15 会话指令「授权」，承接同一轮「同一个单子，为啥两个状态」的诊断与「数据要同源」的纠偏。只读查本机 board.db 实证：17 张卡 status 与 column_id 撕裂（15 张 done 挂 col-todo、2 张 done 挂 col-doing），全是带 #短号 的路线图导入卡；R6-04 卡 status=done、completed_at=2026-10-28 02:53:58，卡片却仍在待办列，详情徽章显示「已办结」而「所在列」显示「待办」。表层触发点是 TaskModel::upsertTask 已存在分支的 UPDATE 不含 column_id，但根因是 task.status 与 task.column_id 各存一份可写状态、写者分散在拖拽／撤销／RoadmapSync 导入／DataPort 导入等入口；docs/board-sync.md 第 10 行已写明「卡片状态是算出来的，不是手写的」，实现违背了它。补 columnForStatus 只是再打一次双写补丁，本卡按收敛处理：库内只留一个可写源。
scope: src/core/Database.cpp;src/task/TaskModel.cpp;src/board/RoadmapSync.cpp;docs/board-sync.md;tests/tst_taskmodel.cpp;tests/tst_roadmapsync.cpp;tests/tst_databasemigration.cpp;tests/CMakeLists.txt
acceptance: ctest -R "tst_taskmodel|tst_roadmapsync|tst_databasemigration" --output-on-failure 全过，新增用例覆盖直写 task.status 被触发器按列状态纠正、INSERT 只给 column_id 时状态自动派生、board_column.status 变更时列内卡同步重投影;tst_roadmapsync 新增用例断言导入把 todo 改成 done 后 column_id 落到 col-done 且 status 为 done 且 completedAt 保留文件值，并断言导出再导入的状态往返一致;tst_databasemigration 新增用例断言启动时一次性把存量撕裂行的 column_id 补到 status 对应列，且 PRAGMA user_version 仍为 2（旧版程序仍可读库）;全库一致性断言 SELECT COUNT(*) FROM task t JOIN board_column c ON c.id=t.column_id WHERE t.status<>c.status 为 0，且 src/task/TaskModel.cpp 的 UPDATE 语句不再给 status 列赋值（状态只由 column_id 换算）;pwsh -File scripts/verify.ps1 全量档退出码 0;python scripts/check_scope.py --base origin/main 与 python scripts/check_path_refs.py --base origin/main 退出码 0
depends: 
suggestedId: DX-168
createdAt: 2026-11-15T00:00:00+00:00
---

## 目标

把 `board.db` 里的卡片状态从「两份可写事实」收敛成「一份可写事实 + 一份派生投影」：
`task.column_id` → `board_column.status` 是唯一状态持有者，`task.status` 降为由数据库触发器
维护的只读投影。应用层所有写路径只表达「换列」，外部协议仍以 todo/doing/done 三态呈现，
但在写库边界先换算成目标状态的列。存量 17 行撕裂数据随一次性迁移纠正。

## 验收

见 frontmatter `acceptance`（6 条）。判据落在数据库约束与回归用例上，不落在「每个写入口都记得
同步」的自觉上：直写 `status` 必须被触发器纠正，换个入口漏写也造不出第二份事实。
代码类卡片（scope 含 .cpp）走全量档 `verify.ps1`，编译与 ctest 由本地全量验证，CI 对 main 常规兜底。

## 边界

- 不新增列、不重建 task 表、不升 `user_version`（保持 2）：触发器与存量纠正都以幂等 DDL/DML
  挂在 `finalizeSchema()` 上，旧版程序仍能打开同一个库，且其直写 status 会被触发器兜住。
- 不改 `board_column.status` 的既有写入语义（当前无改列状态的界面入口），但补一个防御性触发器，
  列状态一旦变更，列内卡片状态同步重投影。
- 不改 `RoadmapSync` 的 `updatedAt` 冲突仲裁，不改 `progress.json` 的写回方向：跨端单向
  （git 提交标记 → 派生快照 → 看板）属 `docs/adr/0001` 与 `AGENTS.md` §7 的治理范畴，需另卡处理，
  本卡只负责库内这一层同源，不在本卡偷换同步语义。
- 协议层不动：`progress.json` / 导出 / 插件 API 仍用 todo/doing/done 三态字段名，那是对外投影，
  不是可写源；不改 QML、不新增脚本文件、不改 CI workflow。
- 不修改真实看板数据；所有用例只用 QTemporaryDir 里的临时 sqlite 库。

## 输入

基线 origin/main（e2e98c8），本地 main 与 origin/main 一致、工作区干净。
现场：`src/core/Database.cpp` 402-575（迁移）、625-646（列状态查询）、799-806（三态校验）；
`src/task/TaskModel.cpp` 237-368（upsertTask 两条分支）、460-554（updateTask 的 columnForStatus）、
752-930（按列移动与 commitMove）；`src/board/RoadmapSync.cpp` 379-394（导入字段）；
`docs/board-sync.md` 第 10 行与 §六。诊断细节记在 `.workbuddy/memory/2026-11-15.md`（不入库）。
依赖：无。

## 产出

① `Database.cpp` 增加状态同源的幂等落盘（一次性存量纠正 + 三个触发器 + 启动一致性校验）；
② `TaskModel.cpp` 所有写入口只写 `column_id`，状态从列反推，`upsertTask` 已存在分支补上列搬迁；
③ `tst_taskmodel` / `tst_roadmapsync` / `tst_databasemigration` 补回归用例；
④ `docs/board-sync.md` 补「库内状态只有一份事实」章节；
⑤ 实现提交带 `#DX-168`，合并提交 body 写全四段（变更／验收／影响／卡片）。
