---
state: proposed
card: DX-145
stage: DX
tier: P1
title: 工具链版本散落 20+ 文件无单点，改一次 Qt 版本要同步多处
author: codebuddy-session-2026-11-08
evidence: 工程化分析实测：全仓 grep 6.8.3 命中 20 个文件，覆盖 README.en.md / README.md / AGENTS.md / CONTRIBUTING.md / SECURITY.md / docs/announcement.md / docs/git-workflow.md / docs/plan-hook.md / docs/pitfalls.md / docs/design/ui-relayout-verify.md / docs/board-ledger.json / docs/iteration-roadmap.md / progress.json / scripts/check_ninja_deps.py / scripts/agent-init.ps1 / scripts/dev-run.ps1 / scripts/dev-env.ps1 / scripts/deploy.ps1 等，ci.yml 与 release.yml 也各自硬编码。产品版本号已收敛到 CMakeLists.txt 的 project(VERSION)（CMakeLists.txt:42-48 是单点范例），工具链版本却没有单点，违反 AGENTS.md Z5「规则只实现一次」，改一次 Qt 版本要同步 6+ 处且靠人记。维护者 2026-11-08 会话指令「按你的建议来」采纳建议 C，授权立卡并排位 P1。
scope: CMakeLists.txt;scripts/dev-env.ps1;scripts/build.ps1;scripts/deploy.ps1;.github/workflows/ci.yml;.github/workflows/release.yml;scripts/check_toolchain_versions.py;cmake/toolchain.cmake;README.md;README.en.md;AGENTS.md;CONTRIBUTING.md;docs/git-workflow.md
acceptance: 新建单点 cmake/toolchain.cmake（Qt 版本、MSVC 工具集、生成器、sccache 版本只在此声明），ci.yml / release.yml / dev-env.ps1 均从单点取值，全仓不再各自硬编码 6.8.3;新增 scripts/check_toolchain_versions.py --check 守门：全仓 6.8.3 字面量只许出现在单点与明确标注的归档/历史文档，出现新的重复硬编码即退 1；python scripts/check_toolchain_versions.py --selftest 全过;pwsh -File scripts/verify.ps1 -ScriptOnly 全过（scope 无代码路径，轻档）;不改 CMakeLists.txt 里 project(VERSION) 的产品版本号单点
depends: 
suggestedId: DX-145
rejectReason: 
createdAt: 2026-11-08T10:40:00+00:00
---

## 目标
把工具链版本收敛到单一事实源，消除「改一次 Qt 版本要同步 6+ 处、靠人记」的漂移（AGENTS.md Z5），并加机器守门防止新增散落硬编码。

## 验收
见 frontmatter acceptance（4 条，分号分隔）。核心是第 1、2 条：单点声明 + 扫描闸门，两者缺一都只是「文档约定」，会像 Main.qml 行数上限那样重新漂移。

## 边界
见 frontmatter scope。

- 只收敛工具链版本（Qt / MSVC / 生成器 / sccache），不动产品版本号 project(VERSION) 的既有单点。
- 归档/历史文档里明确标注的 6.8.3 不作为违规，扫描闸门要有白名单并逐条说明理由。
- 不改 CI 的构建逻辑与产物形态，只换版本取值来源。

## 输入
基线 origin/main。正面范例见 CMakeLists.txt:42-48（产品版本号收敛到 project(VERSION) 一处）；Z5 见 AGENTS.md:335；工程化分析实测的 6.8.3 命中清单在 evidence。

## 产出
cmake/toolchain.cmake（单点）、CI 与 dev-env 改为读单点、scripts/check_toolchain_versions.py（守门 + 自测）、相关文档口径统一；合并提交 body 说明单点位置与白名单理由。
