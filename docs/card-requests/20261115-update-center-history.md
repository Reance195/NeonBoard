---
state: approved
card: DX-170
stage: DX
tier: P4
title: 更新中心：标题栏更新按钮 + 历史版本变更内容
author: maintainer-session-2026-11-15
evidence: 维护者 2026-11-15 会话以截图圈出标题栏「＋ 新建」左侧的空白槽位，要求「加一个更新的按钮，另外，更新的界面，要显示历史更新内容」，并明确「是否理解，我没描述足够的地方，你自己补一下」，即授权自行补全未描述的设计细节。只读核查结论：标题栏按钮集中在 qml/components/BoardChrome.qml:106-169，从右往左为窗口控制、ThemePicker、专注、全景、＋ 新建，红框槽位在 newTaskButton 左侧，目前无任何控件；现有软件更新界面是设置面板「关于」分页里的 qml/components/AboutPage.qml:44-213，只显示当前版本、最新版本、发布日、包体积与截断 6 行的 releaseNotes，完全没有历史版本列表。数据来源侧，update.json 只有 latest 一条，GitHub Releases 正文（gh api 实测 v0.1.5~v0.1.9）只有标题、一条 commits 链接与 3 行 *-sha256: 校验值，没有可读的变更要点；因此「显示历史更新内容」若只拉 Releases 列表，用户看到的会是一串哈希。仓库已有 10 个 v0.1.0~v0.1.9 标签且 git log 可按区间归纳要点，具备补一份中文版本摘要的条件。
scope: CMakeLists.txt;qml/components/BoardChrome.qml;qml/components/UpdateCenterDialog.qml;qml/components/AboutPage.qml;qml/Main.qml;src/update/UpdateManager.h;src/update/UpdateManager.cpp;resources/release-history.json;tests/tst_updatemanager.cpp;docs/auto-update.md;README.md
acceptance: 标题栏在「＋ 新建」左侧新增「更新」按钮，走既有 TopToolButton，updateAvailable 为真时呈强调态并显示提示点，点击打开更新中心；python scripts/check_qml_warnings.py --selftest 与 --smoke 启动日志无 ReferenceError、无「is not a type」且 QML 致命告警 0 行;更新中心以倒序列表展示不少于 2 个历史版本条目，每条含版本号、发布日期、可读变更要点、当前版本徽章与 Release 页跳转，且加载中、空态、离线、错误四态都有对应呈现；离线下仍能渲染内置历史并标注来源;UpdateManager::parseReleaseHistory 为静态纯函数，兼容 GitHub Releases 数组与内置摘要格式，过滤 draft／prerelease 与 *-sha256: 行，按语义版本倒序去重并限制条数，非法或空输入返回空列表并给出 error；tests/tst_updatemanager.cpp 新增解析／排序／过滤／缓存用例全 PASS，且既有更新检查／下载／校验／安装用例不回归;CMakeLists.txt 同时登记新 QML 组件的 QT_RESOURCE_ALIAS 与 qt_add_qml_module 的 QML_FILES，并把 resources/release-history.json 挂进 QRC；模块守卫检查通过;pwsh -File scripts/verify.ps1 全量档退出码 0，覆盖编译、ctest、--selftest、--smoke、模块守卫、QML 致命告警与看板一致性；python scripts/check_scope.py --base origin/main、check_path_refs.py --base origin/main、check_qml_pitfalls.py、task_gate.py selftest、derive_board_status.py --check 退出码均为 0;远端正文一律按纯文本渲染（Text.PlainText），外链只接受 https，不把 Release body 当富文本或 HTML 执行；不改动 checkForUpdates／downloadAndInstall／增量更新／静默安装的既有语义
depends: 
createdAt: 2026-11-15T00:00:00+00:00
---

## 目标

把「软件更新」从设置面板里一段只讲最新版本的小区块，提升成用户能一眼找到、且能回看历史的入口：

1. 标题栏红框槽位放一个「更新」按钮，与「全景」「＋ 新建」同款；有新版本时主动亮起，点击打开更新中心。
2. 更新中心是一个新的 DialogShell 弹窗：上区是当前状态与全部更新操作，下区是**倒序的版本历史**，
   每条带版本、日期、变更要点、当前版本徽章、展开全文与 Release 页跳转。
3. 历史内容不能是哈希串：以 `resources/release-history.json` 内置中文摘要按版本号与
   GitHub Releases 列表元数据合并，本地缓存 + QRC 兜底，离线也看得到完整历史。

## 验收

见 frontmatter `acceptance`（7 条）。判据落在「按钮存在且能打开弹窗」「历史条目数量与字段」
「解析是纯函数且有测试」「离线降级可达」「CMake 三处登记」「全量 verify 绿灯」「远端内容不被当富文本」上，
不落在「肉眼好看」这类主观判断上。本卡改 CMake、C++、QML 并新增资源文件，走 P4 全量档。

## 边界

- 只加「入口 + 历史视图 + 历史数据链路」。既有 `checkForUpdates` / `downloadAndInstall` /
  增量更新 / 静默安装 / 安装回执 / 系统代理的语义一行不改，更新中心的按钮只调用这些既有 Q_INVOKABLE。
- 历史拉取走独立的 QNetworkReply 与独立缓存文件，不复用、不干扰清单检查的并行竞速链路。
- 不动 `Theme.qml` / `Motion.qml` / `DialogShell.qml`，新界面全部用既有令牌与 `NeonButton` / `DataButton`。
- 不改发布流水线（`release.yml` / `build-update-manifest.ps1` / `release.ps1`）。内置摘要是一份随源码发布的
  JSON，发版时追加一条即可；是否自动化由后续卡决定，文档里写明这条维护约定。
- 不动数据库、任务卡治理脚本、插件平台、CI workflow；不新增静态检查脚本。
- 设置面板「关于」页的现有更新区保留，只补一个「查看更新历史」入口，避免同一功能两套实现漂移。

## 输入

基线 origin/main（62cc2d7），本地 main 与 origin/main 一致、工作区干净。
现场：`qml/components/BoardChrome.qml:106-169`（标题栏按钮链，newTaskButton 在 :160）、
`qml/Main.qml:233-253`（openSettings / openProjectMap 的开窗范式）、`:1285-1348`（Loader 范式）、
`qml/components/AboutPage.qml:44-213`（现有更新区）、`qml/components/DialogShell.qml:27-70`（壳接口）、
`src/update/UpdateManager.h:40-258`（属性与方法清单）、`src/update/UpdateManager.cpp:35-190`（纯逻辑 namespace 与构造器）、
`docs/auto-update.md:1-30`（更新入口表）、`CMakeLists.txt:221-332`（QML 登记两段）、`:334-343`（qt_add_resources）。
数据源：`gh api repos/Reance195/NeonBoard/releases?per_page=5` 实测返回 tag_name / published_at / body，
但 body 仅 319 字符且全是校验值；`git tag --list 'v*'` 共 v0.1.0~v0.1.9 十个，区间提交可归纳要点。
依赖：无。

## 产出

① `resources/release-history.json`：v0.1.0~v0.1.9 十条中文摘要（version / date / title / highlights / url）；
② `UpdateManager` 新增 `releaseHistory` 属性、`refreshReleaseHistory()` 与静态 `parseReleaseHistory()`，
   兼容 Releases 数组与内置格式，缓存 + QRC 兜底；
③ `qml/components/UpdateCenterDialog.qml`：状态区 + 历史 ListView + 四态 + 全量操作区；
④ `BoardChrome.qml` 更新按钮、`Main.qml` 开窗函数与 Loader、`AboutPage.qml` 历史入口；
⑤ `CMakeLists.txt` 三处登记（QT_RESOURCE_ALIAS / QML_FILES / QRC 资源）；
⑥ `tests/tst_updatemanager.cpp` 新增纯逻辑用例，`docs/auto-update.md` 与 `README.md` 补说明；
⑦ 实现提交带 `#<短号>`，合并提交 body 写全四段，交付说明写回卡片 `note`，
   收尾提交带 `#<短号>=done`，由 `git-done.ps1` 自动结卡。
