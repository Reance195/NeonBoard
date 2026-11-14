---
state: closed
card: DX-151
stage: DX
tier: P2
title: scripts/ 目录无清单事实源，脚本膨胀与临时脚本残留不可见
author: qodercn-session-2026-11-09
evidence: 维护者 2026-11-09 指令「1。授权」，采纳审计轮遗留缺口 #4。实测：`scripts/` 现有 40+ 个脚本，宪章 §四 只有一句「临时脚本用完即删，不要留在 scripts/ 里」散文约定，无任何机器对账——脚本有没有清单、清单准不准、目录里多出来的脚本是谁的、干什么的，全靠读源码；多代理并行下（§七）脚本数量只增不减，「残留」与「孤儿」两类污染对锚点同步与占用判定之外完全隐形（Z5：脚本层缺一份「存在即注册」的单一清单；Z6：新会话反复要先 ls scripts/ 猜用途）。
scope: scripts/check_script_hygiene.py;docs/scripts-manifest.md
acceptance: 新增 `docs/scripts-manifest.md`：`scripts/` 下每个受控文件一行「文件名 —— 用途一句话」，存量脚本在本卡一次登记到位；`check_script_hygiene.py` 增加规则 H 双向对账：`scripts/` 里存在而清单无行 → 违规；清单有行而文件不存在 → 违规（违规输出点名文件与方向，并入既有 problems 汇总，不另起退出码通道）；清单豁免面固定且点名（如 `__pycache__`、非受控临时物仅按显式条目豁免，不开放通配）；`python scripts/check_script_hygiene.py selftest` 覆盖两类违规正反例并全过；`pwsh -File scripts/verify.ps1 -ScriptOnly` 全过（其已调用 hygiene，即规则 H 自动进本地与 CI 双闸）；规则 H 判定只实现于 hygiene 脚本一处（Z5）
depends: 
rejectReason: 
createdAt: 2026-11-09T02:00:00+00:00
---

## 目标
让 `scripts/` 的每个文件「存在即注册、注册须有用」：一份机器对账的清单堵住临时脚本残留与孤儿脚本，新会话读清单即可选型，不再猜。

## 验收
见 frontmatter acceptance。核心是第 2 条双向对账与第 4 条 selftest 正反例；第 1 条的存量登记必须本卡完成，否则规则 H 上线即红。

## 边界
见 frontmatter scope。

- 只登记 `scripts/` 一层（不递归子目录如有）；`.githooks/`、`cmake/`、`installer/` 不在本卡射程。
- 不改写任何脚本的行为与内容（除 hygiene 自身加规则 H）；清单用途行按脚本头注释/docstring 如实摘录，不重写文档。
- 不删除任何现有脚本——「残留」的回收仍是维护者拍板（§7.10 同族口径），本卡只做可见性。
- 豁免面不开放目录级/通配级，只能逐文件显式行。

## 输入
基线 origin/main。`check_script_hygiene.py` 规则 A–G 的实现与 selftest 结构；`scripts/` 现有文件清单与各脚本头部说明；verify.ps1 调用 hygiene 的既有链路。

## 产出
`docs/scripts-manifest.md`（存量全量登记 + 格式说明）、`check_script_hygiene.py` 规则 H 与 selftest 用例；卡片 note 记录 selftest 输出与对账覆盖数。
