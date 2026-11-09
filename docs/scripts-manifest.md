# scripts 清单（DX-151）

> `check_script_hygiene.py` 规则 H 的对账对象：`scripts/` 顶层每个脚本必须在此登记一行；
> 删除或改名脚本时同步删行。用途一律从脚本自身头部说明如实摘录，不在清单里重写。

| 文件 | 用途 |
| --- | --- |
| `acceptance_split.py` | 验收项子项拆分的单一实现（DX-84）。 |
| `agent-init.ps1` | 为某个 agent 创建独立工作区 |
| `agent-status.ps1` | Agent 状态汇总脚本：整合 worktree 活跃度、当前分支、待办事项。 |
| `assignment-store.py` | docs/agent-assignments.json 的唯一写者（DX-149）。 |
| `agent-workspace.ps1` | NeonBoard 多代理工作区占用登记 |
| `assignment-store.py` | docs/agent-assignments.json 的唯一写者：接手判活事实存储（DX-149）。 |
| `board-apply.py` | 建卡请求的预演器与落盘器（默认 dry-run）。 |
| `board-view.py` | 把台账 + 建卡计划渲染成一张只读 HTML 面板。 |
| `board_priority.py` | 立卡排位规则的唯一实现（DX-123）。 |
| `build-delta.ps1` | 生成 NeonBoard 增量升级包（R5-06：文件级 diff + bsdiff 二进制增量）。 |
| `build-update-manifest.ps1` | 生成 NeonBoard 的软件更新清单 update.json。 |
| `build.ps1` | NeonBoard 一键构建脚本（探测 CMake / Qt，配置 + 构建，可选自检）。 |
| `check-format.ps1` | check-format.ps1 —— 只检查本次改动涉及的 C++ 文件（棘轮策略） |
| `check-perf.ps1` | check-perf.ps1 —— 从 --smoke 聚合日志里提取 cpu 中位数并按 R0-11 绝对预算判定 |
| `check-worktree-activity.ps1` | 检查所有 worktree 的最后活跃时间，报告超过阈值未活动的。 |
| `check_board_diff.py` | 改台账时判断「这一档验收够不够」。 |
| `check_claim_refs.py` | 领用闸门：分支必须先走 agent-init 领用（服务端 claim ref 占位）。 |
| `check_dist.py` | 便携包「该随包走」的清单校验（唯一实现，deploy / CI / release 共用）。 |
| `check_module_deps.py` | 模块依赖方向与文件行数守卫（棘轮式）。 |
| `check_ninja_deps.py` | DX-54 的守卫：Ninja 的依赖条目必须真的在跟踪头文件。 |
| `check_path_refs.py` | 校验仓库里对 src/ 文件的硬编码引用是否仍然存在。 |
| `check_qml_pitfalls.py` | QML 两处已踩坑陷阱的静态判据（DX-102）。 |
| `check_qml_warnings.py` | 启动日志致命告警检查（DX-122）：按形态识别，规则单源。 |
| `check_readme_sync.py` | README 同步提示器（DX-150）：宪章 §五 硬性约定 6 的机器执行者。 |
| `check_scope.py` | 校验本次改动是否越出任务卡允许的路径范围。 |
| `check_script_hygiene.py` | 脚本卫生检查：宪章里「唯一入口 / 唯一豁免」两条款的机器校验。 |
| `check_secrets.py` | NeonBoard 密钥防入库扫描器（DX-148）。 |
| `check_toolchain_versions.py` | 工具链版本单点守门（DX-145）。 |
| `check_utf8_flag.py` | DX-53 的守卫：/utf-8 只许声明一次，且每一个编译单元都得带上。 |
| `claim-card.py` | 任务卡领用校验与登记（唯一实现）。 |
| `classify-cards.py` | 把存量任务卡归入项目（R10-04）。 |
| `deploy.ps1` | 多代理并行时把产物写进自己的 worktree，避免抢占共享的 dist/ |
| `derive_board_status.py` | 看板状态派生器：从 git 提交历史 + 卡片台账派生看板卡片状态。 |
| `dev-env.ps1` | NeonBoard 构建环境一键就绪（MSVC / Windows SDK / CMake / Ninja / Qt）。 |
| `dev-run.ps1` | 以沙箱方式启动 NeonBoard（多代理安全） |
| `emit-agent-task.ps1` | emit-agent-task.ps1 —— agent-task-event v1 的统一投递器 |
| `format.ps1` | format.ps1 —— 用 clang-format 就地格式化源码 |
| `git-done.ps1` | NeonBoard 多代理合并助手 —— 合并分支回主分支并清理 |
| `handoff.ps1` | 在办卡原地接手（DX-149）：删旧 claim 占位 → 原地换身份重领 → 生成残局摘要 handoff.json。 |
| `git-helpers.ps1` | git-helpers.ps1 —— git 符号引用取值的空值安全封装（单一实现，DX-63） |
| `handoff.ps1` | 在办卡原址接手：判活事实源与接手三态巡检（DX-149）。 |
| `gui-uia-verify.ps1` | gui-uia-verify.ps1 —— GUI 实机验收通道（DX-102） |
| `install-agent-adapters.ps1` | install-agent-adapters.ps1 —— 把 NeonBoard 适配器装到本机各 agent 的配置目录 |
| `make_icon.py` | 生成应用图标（PIL 绘制霓虹圆环，输出 .ico/.png 预览） |
| `merge-resolve.ps1` | merge-resolve.ps1 — 合并撞冲突后的两阶段善后（DX-27） |
| `merge_board.py` | 台账 JSON 冲突的三路语义合并（供 merge-resolve.ps1 调用）。 |
| `nb-console.ps1` | nb-console.ps1 —— 控制台 UTF-8 编码的单一实现（DX-77 / DX-83） |
| `nb-paths.ps1` | nb-paths.ps1 —— NeonBoard 落盘位置在 PowerShell 侧的唯一入口 |
| `nb_paths.py` | Python 侧的 NeonBoard 数据目录单一口径（PowerShell 侧镜像见 scripts/nb-paths.ps1）。 |
| `nbflow.py` | NeonBoard 开发会话派发程序（单一入口）。 |
| `preview.ps1` | NeonBoard 预览中心（命令行版） |
| `probe-splash-frames.ps1` | DX-39：启动闪屏「有没有被冻住」的截帧探针。 |
| `publish-market.ps1` | 把一个插件目录打成市场用的 .neon 单文件包，并写入市场索引 market.json。 |
| `register_board_cards.py` | 立卡唯一合法入口（DX-98）。 |
| `release-claims.py` | 清掉台账 claims 里的过期领用登记（唯一实现，供 git-done.ps1 收尾调用）。 |
| `release.ps1` | NeonBoard 发布闸门：一处出包，产物可溯源 |
| `setup-git-hooks.ps1` | setup-git-hooks.ps1 —— 让仓库钩子真正生效（幂等） |
| `task_gate.py` | 任务闸门：没有任务不许改代码。 |
| `test-delta.ps1` | 本地串一遍增量发布链路（发布前自测，不进 CI） |
| `verify.ps1` | verify.ps1 —— 唯一验收入口：本地和 CI 用同一把尺子 |
