---
state: closed
card: MOD-27
stage: MOD
tier: P3
title: MOD QML 治理系列复盘：MOD-17~26 十卡三段式复盘与对策入册
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 维护者会话。MOD-17~26 十卡（Main.qml 主内容整迁 / SettingsPanel 六分页+三件套+两弹窗拆分 / alias 收敛 / 棘轮登记）已全部 baseline=done，过程实录踩坑 9 类，按 R19-1 建立的「问题清单 → 根因 → 程序化对策」三段式要求做系列复盘，沉淀进 docs/retro/ 复盘文档并把可机器判据的对策入册 docs/pitfalls.md（Z6：踩坑两次沉淀成机器判据）。素材全部来自本会话台账卡 note 与交付记录（MOD-17~26 note 已逐卡留痕）。维护者 2026-11-10 会话指令「选 1，给 QML 治理系列立复盘卡」，据此授权立卡、领用、实施、verify、rebase、推 origin/main 与结卡。
scope: docs/retro/**;docs/pitfalls.md;docs/card-requests/**
acceptance: 新增 docs/retro/20261110-qml-governance-retro.md，三段式结构（问题清单 / 根因 / 程序化对策）覆盖 MOD-17~26 十卡过程实录且每条对策标注「已程序化 / 待程序化 / 仅纪律」类别;docs/pitfalls.md 新增条目覆盖至少三条已程序化对策（迁移脚本根开根闭成对纪律 / 立卡与实施同分支须先领用 / 后台长任务超时截断续跑），每条含判据与实录引用;复盘文档与 pitfalls 条目无互相矛盾表述，卡 note 结卡时引用复盘文档路径;pwsh -File scripts/verify.ps1 -ScriptOnly 通过（纯 docs 改动）;维护者人工复核：复盘对策条目认可，待程序化项有去向
depends: []
createdAt: 2026-11-10T19:40:00+00:00
---

## 目标

对 QML 治理系列（MOD-17~26）做一次三段式复盘：问题清单（每条带实录卡号）→ 根因归类 → 程序化对策（已程序化的标注判据位置，可程序化的给去向）。可入册的对策同步写进 `docs/pitfalls.md`。

## 验收

见 frontmatter `acceptance`（5 条）。

## 边界

见 frontmatter `scope`：新增 `docs/retro/20261110-qml-governance-retro.md`、追加 `docs/pitfalls.md` 条目、请求单收录。**不动**：任何 QML/C++/脚本实现（「待程序化」对策只给去向不实现，实现另立卡）。

## 输入

基线 `origin/main`。素材：MOD-17~26 台账卡 note、docs/charter-appendix.md 相关条款、R19-1 建立的三段式复盘格式。

## 产出

复盘文档 + pitfalls 对策条目；合并提交 body 四段；交付说明单独 `docs(board):` 提交并引用复盘文档路径。
