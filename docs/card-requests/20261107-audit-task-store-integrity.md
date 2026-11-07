---
state: proposed
stage: DX
tier: P3
title: task 存储完整性四处：reorder 无事务、撤销删除丢列、renumber 失败内存漂移、removeRowFor 吞错
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计（亲验）。其一：src/task/TaskRunStore.cpp:698-709 reorder 两条 UPDATE 无事务、失败无告警（同文件 moveColumn/deleteColumn 都包了事务）——第一条成功第二条失败时两条 waiting 行持有相同 position，队列顺序退化。其二：TaskModel.cpp:586-598 removeTask 快照不含 columnId，:620 restoreTask 固定落「该状态默认列」——自定义列里删卡再撤销，卡复活到错误列（移动类撤销支持按列回滚，删除类不齐）。其三：TaskModel.cpp:1033-1040 renumberColumn（renumberStatus :1112-1140 同构）先改内存后 commit，commit 失败不回滚内存——模型与 DB 从此漂移；且 renumberColumn 排序带 id tiebreak（:1016-1020）而 renumberStatus 没有，重复 order_key 时两处对同一状态的重排结果可与展示顺序错位。其四：TaskExec.cpp:252-257 removeRowFor 的 DELETE 返回值未检查无告警（同族 TaskExtra::removeRowFor :316-320 有告警）——残留行会让撤销复活旧执行定义。
scope: src/task/TaskRunStore.cpp;src/task/TaskModel.cpp;src/task/TaskExec.cpp;tests/**;docs/pitfalls.md
acceptance: tests/tst_taskruns.cpp 新增用例：reorder 的两条 UPDATE 在第二条失败（模拟 busy/错误）时整体回滚，队列不出现同 position 条目且失败有告警日志;tests/tst_taskmodel.cpp 或 tst_taskundo 扩展：自定义列中删卡 → 撤销 → 卡回到原列（快照含 columnId）；默认列卡撤销不回归;新增用例：renumber 在 commit 失败时内存 orderKey 回滚（以可注入的失败路径或事务包裹后的一致性断言）;新增用例：同状态重复 order_key 时 renumber 与展示顺序一致（tiebreak 口径统一）;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）
depends:
---

## 目标

按 Z14 把「多条写无事务 / 失败不回滚 / 失败无告警」一族在 task 域一次修齐：reorder 包事务；撤销快照带 columnId 并在 restore 时按列还原；renumber 全程事务内先算后写、commit 失败回滚内存，两处 tiebreak 口径统一；TaskExec 清行失败告警（与 TaskExtra 对齐）。

## 验收

见 frontmatter acceptance（5 条）。口径：事务与回滚以注入失败/一致性断言证明，不依赖注入框架；全量 verify 以退出码为准。

## 边界

- 不改 TaskModel::moveTask 的既有语义（宪章 Z3/§10.2），不动 task_link 与 user_version。
- renumber tiebreak 统一到「与 taskIdsInStatus 展示一致」的现口径，不引入第三种排序。
- 撤销快照新增字段只加不删，旧 payload（无 columnId）回退默认列（兼容存量撤销栈）。

## 输入

基线 origin/main（d93cf6a）。审计现场：上述 file:line；tst_taskruns/tst_taskmodel/tst_taskundo 既有骨架。依赖：无。

## 产出

① reorder 事务化；② 撤销删除保列归属；③ renumber 事务与内存一致 + tiebreak 统一；④ TaskExec 告警对齐；⑤ 对应用例。
