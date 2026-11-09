---
state: proposed
stage: DX
tier: P2
title: 宪章「认领分支必须推 origin」与 pre-push 禁推未合并分支自相矛盾：实战三撞
author: maintainer-session-2026-11-09
evidence: 维护者 2026-11-09 会话指令「把三个尾巴立成小卡」（尾巴 2）。实录（DX-138~143 接手批次）：三个接手 agent 按派工指令「git push origin <认领分支>」全部被 pre-push 规则 1 拒（推送未合并进 main 的分支），三次撞同一处后各自改走 git-done 合并推送完成交付——语义实际是「claim 占位 ref（refs/claims/<短号>）由 agent-init 推送保证远端可达；认领分支本身的可达性由 git-done 合并推送兑现」。AGENTS.md §7.2.1 现行措辞「认领分支必须推 origin（否则 claim ref 远端不可达，闸门无法校验）」与钩子行为矛盾：claim ref 的可达性从来由占位 ref 自己保证，与分支是否单独推送无关。Z6：同一处实战三撞，必须变成文档判据。
scope: AGENTS.md;docs/charter-appendix.md;docs/git-workflow.md;docs/module-split-plan.md
acceptance: AGENTS.md §7.2.1 的「认领分支必须推 origin」表述改为与 pre-push 规则 1 一致的口径（认领分支不单独推送——pre-push 规则 1 会拒；claim 占位 ref 由 agent-init 的原子推送保证远端可达；认领分支成果的远端可达性由 git-done 合并推 main 兑现），红线实质不放松：refs/claims/<短号> 远端可达仍是硬要求;docs/charter-appendix.md「7.2/7.2.1」节同步澄清，并收录 DX-138~143 批次三 agent 撞同一拦截的实录为依据;docs/git-workflow.md 与 docs/module-split-plan.md 中同语义表述逐处扫描并同步（grep「推 origin」「推送.*认领分支」等，逐处清单写进卡片 note，漏一处算未过）;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
rejectReason: 
createdAt: 2026-11-09T21:30:00+00:00
---

## 目标
消除宪章文档与 pre-push 钩子的语义矛盾：把「认领分支必须推 origin」澄清为「claim 占位 ref 可达（agent-init 已保证）+ 分支成果由合并推 main 兑现」，让 agent 按文档行事不再撞钩子。

## 验收
见 frontmatter acceptance（5 条，分号分隔）。核心：**红线实质不放松**——refs/claims/<短号> 远端可达仍是硬要求，改的是表述不是约束；全仓同语义表述一次扫清（Z14）。

## 边界
见 frontmatter scope。硬约束：

- 只改文档表述，不改任何钩子/脚本行为（pre-push 现行判据是对的，保持不动）。
- AGENTS.md 为最高宪章：改动仅限 §7.2.1 该句及紧邻句子，不动其它条款；本卡由维护者会话授权（「把三个尾巴立成小卡」），授权来源进合并提交 body。
- 不改 docs/board-ledger.json、不改 scripts/。

## 输入
基线 origin/main。实录：DX-138~143 接手批次（agent-h1 报告「直接 push 分支被 pre-push 规则 1 拦（设计如此，未绕行）」、agent-h2/agent-h3 同款偏离事项）；pre-push 规则 1 判据见 .githooks/pre-push；claim ref 可达性机制见 charter-appendix「7.2/7.2.1」。

## 产出
四处文档的语义统一表述 + 实战依据收录；合并提交 body 四段（变更 / 验收 / 影响 / 卡片），逐处清单进卡片 note。
