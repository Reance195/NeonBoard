## 变更

<!-- 做了什么：一句话说清「把什么改成什么」 -->

## 验收

<!-- 跑了什么、结果如何。默认写：pwsh -File scripts/verify.ps1 全过 -->

## 影响

<!-- 影响面：动了哪些模块 / 有没有 schema 变更 / 有没有新增依赖 / 有没有破坏兼容 -->

## 任务卡

<!-- 关联短号，如 R5-03；对应 docs/board-ledger.json -->

## 自检

- [ ] `pwsh -File scripts/verify.ps1` 全过（编译 / ctest / selftest / smoke）
- [ ] 改动未超出任务卡 `scope`（`python scripts/check_scope.py --card <短号>`）
- [ ] 新增的 C++ 代码经过 `clang-format`（`pwsh -File scripts/format.ps1`）
- [ ] 没有手工改 `progress.json`（它由 CI 派生写回）
