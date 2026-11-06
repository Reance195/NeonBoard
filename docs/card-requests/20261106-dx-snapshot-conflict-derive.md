---
state: approved
card: 
stage: DX
title: progress.json 合并冲突派生善后：冲突文件为纯派生物时自动重算，五步人工善后收口进 git-done
author: agent-c
evidence: DX-106 收尾实测两次撞 progress.json 合并冲突，每次都要手工五步（merge --abort → 切回分支 → 合主线 → --ours 取版 → derive --write 重算 → 提交）才能继续收尾，步骤全靠人记且无任何脚本兜底。快照是派生产物（AGENTS §11.2：内容恒由 derive --write 按派生结果重算），内容冲突不存在需要人脑的语义合并——正确解恒为「任取一侧后重算」。DX-27 对台账同类问题已给出「本可语义合并」的剖析，快照侧因派生器幂等而更简单，可先行收口进 git-done 合并冲突路径。
scope: scripts/git-done.ps1;docs/pitfalls.md
acceptance: git-done 合并撞冲突且冲突文件仅为纯派生物（progress.json，可含 docs/board-ledger.json）时，自动完成「取主线版→完成合并→derive --write 重算→随收尾提交」，不要求人工介入;冲突含非派生文件（代码/其它文档）时行为不变：仍中止并给人工解决指引;dry-run 演练与真实收尾各验证一次冲突自动善后路径;pitfalls 补现象/根因/解法条目;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: DX-107
suggestedId: 
---

## 目标
progress.json 合并冲突派生善后：冲突文件为纯派生物时自动重算，五步人工善后收口进 git-done

## 验收
git-done 合并撞冲突且冲突文件仅为纯派生物（progress.json，可含 docs/board-ledger.json）时，自动完成「取主线版→完成合并→derive --write 重算→随收尾提交」，不要求人工介入;冲突含非派生文件（代码/其它文档）时行为不变：仍中止并给人工解决指引;dry-run 演练与真实收尾各验证一次冲突自动善后路径;pitfalls 补现象/根因/解法条目;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿

## 边界
scripts/git-done.ps1;docs/pitfalls.md

## 输入
基线 origin/main；依赖：DX-107

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
