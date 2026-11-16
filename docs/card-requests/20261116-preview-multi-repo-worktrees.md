---
state: approved
card: DX-173
stage: DX
tier: P4
title: 预览中心接入项目仓库绑定：按项目列出各仓库 worktree
author: maintainer-session-2026-11-16
evidence: 维护者 2026-11-16 会话以「预览中心」空白截图追问「现在不是多项目了嘛，还要用环境变量嘛」。只读核查：R18-02 的多项目只落在路线图同步（RoadmapSync.h:20-26，project.repo_path → <repo>/progress.json），ProjectStore::boundRepos() 在 src/board/ProjectStore.cpp:357-377 已可用，但唯一消费者是 RoadmapSync.cpp:276/699；src/app/AppMain.cpp:296-298 只把 ProjectStore 注入了 RoadmapSync，PreviewHub 未注入。PreviewHub.cpp:158-186 anchorRoot() 仍按 NEONBOARD_REPO_ROOT → git-common-dir → exe/cwd 向上探测单一根，:235-242 定位不到就清空列表；:216-227 agentIdFor() 按该单根算相对路径，:208-214 runtimeFilePath() 只用一个 common dir，:1075-1078 出包只写 m_anchorRoot/dist/packages/<agent>。qml/components/PreviewPanel.qml:47-49 的提示文案直接让用户设 NEONBOARD_REPO_ROOT 重启。结论：绑定项目仓库能自动进看板进度，但进不了预览中心；本卡把同一份 project.repo_path 接给 PreviewHub。
scope: src/board/PreviewHub.cpp;src/board/PreviewHub.h;src/app/AppMain.cpp;qml/components/PreviewPanel.qml;tests/tst_previewhub.cpp;README.md
acceptance: PreviewHub 经新增的 setProjectStore() 从 ProjectStore::boundRepos() 读取 (projectId, repo_path) 清单；扫描根 = 旧锚点根 ∪ 有效绑定仓库（路径存在且含 .git，按 canonical path 去重），无效路径跳过且 statusText 点名；未绑定任何项目时扫描结果、agent id 与 statusText 与改动前逐字段一致;逐根执行 git worktree list --porcelain 后，AgentInfo 增补只读的 project / repo 字段供面板分组显示；锚点根下的 agent id 保持旧规则（main / .worktrees/<名> / 目录名），非锚点根用 <projectId>::<旧 agent id>，projectId 重复时以 canonical repo path 消歧，全部条目的 id 唯一;运行登记按仓库分流：每个工作区只写其所属仓库 <git-common-dir>/preview-runtime.json 的本仓库条目，读时合并全部扫描根；启动 / 停止绑定仓库的 agent 后，面板与 --preview-list 的 JSON 均显示正确 running / pid，锚点运行文件不混入他仓库条目;构建与出包仍按 agent.path 定位，产物只落锚点 dist/packages/<agentId>/，跨仓库同名 worktree 不互相覆盖；无 NeonBoard 构建布局（build/Release/NeonBoard.exe）的绑定仓库仍列出 git 状态，但构建 / 出包按钮禁用并写明不支持，不猜其他项目的构建系统;面板在无锚点根但有有效绑定仓库时 available == true，提示文案改为「在设置中绑定仓库或设置 NEONBOARD_REPO_ROOT 后重启」，列表行显示项目 / 仓库标签；scripts/preview.ps1 零改动即可通过 --preview-list 列出绑定仓库的 worktree（验证：沙箱中清空 NEONBOARD_REPO_ROOT、绑定一个临时仓库后启动，JSON 含该仓库条目）;pwsh -File scripts/verify.ps1 全量档退出码 0（scope 含 .cpp/.h/.qml，按代码类走全量），覆盖编译、ctest、--selftest、--smoke、模块守卫、QML 致命告警与看板一致性；tests/tst_previewhub.cpp 内新增断言覆盖扫描根合并、无效路径跳过、id 唯一与去重;python scripts/check_scope.py --base origin/main 与 python scripts/check_path_refs.py --base origin/main 退出码 0，改动不越出 scope；README.md「预览中心」条目更新为「自动扫描已绑定项目仓库 + 兼容 NEONBOARD_REPO_ROOT」，其余文档零改动
depends:
createdAt: 2026-11-16T00:00:00+00:00
---

## 目标

把「预览中心」的仓库发现从单一 `NEONBOARD_REPO_ROOT` 扩展为「锚点根 + 设置里已绑定的项目仓库」：
同一个 `project.repo_path` 既喂路线图同步，也喂 worktree 预览，用户不必再为多项目开多个环境变量实例。
旧锚点行为（含 agent id、运行登记位置、出包目录）保持逐字节兼容。

## 验收

见 frontmatter `acceptance`（7 条）。判据落在「绑定仓库真的被扫描」「id 与运行登记不串仓」
「无锚点根时面板可用」「全量 verify 绿灯」「scope 零越界」上。scope 含 `.cpp` / `.h` / `.qml`，
按宪章 §7.7 走代码类全量档；`tests/tst_previewhub.cpp` 是既有测试文件，不新增 CMake 登记。

## 边界

- 只改 scope 列出的 6 个文件；**不改** `ProjectStore`、`RoadmapSync`、`project` 表 schema、
  `scripts/preview.ps1`、`CMakeLists.txt`、`qml/Main.qml` 或其它 QML 组件。
- `NEONBOARD_REPO_ROOT` 不废弃、不改语义：它仍指向锚点根，仍是老用户与 CLI 的兜底；
  绑定仓库是**追加**扫描源，不与锚点根互相替代。
- 本卡只扩展 Git worktree 的发现与隔离预览；构建 / 出包仍只支持 NeonBoard 自身的构建布局
  （`build/Release/NeonBoard.exe`），其它项目只展示 git 状态。通用跨项目构建另立卡（Z1）。
- 不改沙箱隔离契约：预览实例仍走 `.sandbox` / 独立单实例 key / 独立路线图快照，
  绝不写真实数据目录或任何仓库根的 `progress.json`（宪章 §7.9 红线）。
- 不改「回收由人拍板」：未推送提交 / 未提交改动 / 游离 HEAD 三项判据与「不自动删」语义原样保留。
- 不新造重复判定：绑定仓库的路径有效性、去重规则只实现一次（`PreviewHub` 内纯函数），
  测试断言直接指向该函数，不在 QML 里再写一份（Z5）。

## 输入

基线 `origin/main`（本次整理时点为 3f56e6a）。依赖：无；R18-02 / R18-03 已完成，
`ProjectStore::boundRepos()` 与设置面板的仓库绑定入口（`DataBackupPage.qml:585`）可直接复用。
同族先例：`RoadmapSync::setProjectStore()` 的注入时机与注册表刷新写法（`RoadmapSync.cpp:276`）。

## 产出

① 实现提交在 6 条 scope 内完成接入，subject/body 带 `#<短号>` 与 `#<短号>=done`；
② 交付说明写回卡片 `note`，逐条对照上面 7 项验收写「已完成 / 待做」；
③ 合并提交 body 写全四段（变更／验收／影响／卡片），由 `git-done.ps1` 自动结卡。
