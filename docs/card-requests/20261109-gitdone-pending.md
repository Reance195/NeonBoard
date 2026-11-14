---
state: approved
card: DX-156
title: git-done 挂起结卡的补结出路按确认方式分说
stage: DX
scope: scripts/git-done.ps1
acceptance: pwsh -File scripts/git-done.ps1 -SelfTest 退出码 0，含新增场景 C（-Close + 推送全拒）断言：退出码非 0、explicit 挂起提示不含「自动补结」承诺、出路提示可执行、回执 closeMode=explicit;pwsh -File scripts/verify.ps1 -ScriptOnly 通过;源码断言：推送未落地与结单对账挂起两处提示按 -Close 显式确认分说（grep 可判定）
evidence: 推送失败回滚收尾提交后 ccPending 挂起，1597 行统一承诺「挂起的结卡由下次合并收尾自动补结」——对 =done 标记路径成立（标记在合并里，下次 --close-cards 扫全历史可寻回），对 -Close 显式确认路径不成立：下次收尾的 claimedCodes 不含该卡、无人再传 -Close，确认不跨收尾继承，只能看板点结或重跑带 -Close（DX-58/DX-116 回执口径已记 closeMode 但提示未分说）
author: 维护者
tier: P2
---

## 目标

git-done 推送失败（fail-soft 回滚）后，挂起结卡（closePending）的补结出路提示按确认方式分说：=done 标记路径维持「由下次合并收尾自动补结」；-Close 显式确认路径如实告知「确认不跨收尾继承」，给出真实出路（补推成功后重跑收尾再带 -Close，或看板点结 ADR 0002）。涉及两处提示：推送未落地段与结单对账挂起段。SelfTest 新增场景 C（-Close + 推送全拒）钉住 explicit 路径。

## 验收

- pwsh -File scripts/git-done.ps1 -SelfTest 退出码 0，新增场景 C 断言通过（退出码非 0 / 提示不含「自动补结」/ 出路提示 / 回执 closeMode=explicit）
- pwsh -File scripts/verify.ps1 -ScriptOnly 通过
- 源码断言（grep）：两处提示按 closeList 含否分说

## 边界

- 只改 scripts/git-done.ps1；不改推送重试 / 回滚 / 回执行为（DX-58/DX-116 语义不动）
- 对账挂起段的 explicit 分支（需「结卡提交成功但推送失败」组合）不进 SelfTest，仅推送未落地段由场景 C 覆盖
- 结单对账 missed 分支（DX-153）措辞不动

## 输入

- DX-153 深挖报告缺陷 2：R11-14 同款误导，1597 行承诺对 explicit 路径是空头支票
- 回执已记 closeMode（DX-116），提示端未消费

## 产出

- scripts/git-done.ps1：两处提示分说 + SelfTest 场景 C（临时仓带 chore(claim) 领用提交 + -Close + -StubPushFailures 99）
