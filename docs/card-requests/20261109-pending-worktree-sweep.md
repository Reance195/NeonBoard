---
state: proposed
stage: DX
tier: P2
title: git-done 回收「删自己脚下」必败且不复发：待删清单无人 drain，空壳残留两周攒出 28 个
author: qodercn-session-2026-11-09
evidence: 维护者 2026-11-09 指令「登记新卡」。实测：`git-done.ps1:1644` DX-120 延后回收段让 pwsh 自己 Set-Location 让位后 `git worktree remove` 本 worktree，但句柄实际在启动它的父 shell 进程里（Windows 不允许删除任何进程 CWD 所在目录），非 force 口径（DX-93 教训，正确）下每次都停在最后一刀；失败后只打印手工命令，无 pending 登记、无后续收敛入口——2026-11-09 清点 `.worktrees/` 得 34 目录，其中 28 个是「0 文件、无 .git 注册」的空壳（会话实录：gapsb/gapsc 收尾同样报「未能自动回收」，事后由父 shell 让位才 rm 掉），残留两周不可见、无人追责。
scope: scripts/git-done.ps1;scripts/agent-init.ps1;scripts/agent-workspace.ps1
acceptance: git-done 自身 worktree 回收失败时把路径追加登记进待清理清单（数据目录内单 JSON，键=绝对路径，幂等，重复登记不倍增；成功回收则不登记）；提供统一 drain 例程（放 agent-workspace.ps1 或 git-helpers.ps1 单处实现，Z5）：对清单每项与 `.worktrees/` 顶层目录执行「目录为空 且 `git worktree list --porcelain` 无对应注册 且 无 `.git` 指针 → 直删空目录」，三判据缺一律保留并记一行原因（回收由人拍板语义不变，非空目录永不删）；agent-init 开区与 git-done 收尾两个入口先 drain 再干活，`-Action Check` 汇总行报「待清理清单 N 项 / 空壳 M 个」且行为不新增退出码；实测三条：手工 mkdir 一个空壳目录后跑 agent-init（任意新 agent）该壳被 drain；对非空目录 drain 不动并留原因；git-done 自回收失败路径能把待删路径落进清单（可用假 worktree 路径注入自测或脚本级测试）；`.ps1` UTF-8 BOM、check_script_hygiene 规则 H 清单对账（若新增脚本则同批登记清单行）、verify.ps1 -ScriptOnly 全过；不改 remote 模式合并/单次推送/结卡链路，不引入 --force
depends: DX-154
rejectReason: 
createdAt: 2026-11-09T03:10:00+00:00
---

## 目标
把「回收失败留空壳」从每次靠人手工补刀升级成机器闭环：失败即登记待清理清单，任何后续入口自动收敛——只删「空 + 未注册 + 无 .git 指针」三判据齐备的零数据风险目录，非空一律不碰。

## 验收
见 frontmatter acceptance。核心是第 1、2、4 条：失败必登记、drain 单处实现、三判据缺一不删。第 5 条实测空壳被收、非空保留留原因。运行态设计遵循 Z13（幂等、任意一步死掉重跑无副作用）。

## 边界
见 frontmatter scope。

- 回收成功路径不动：仍是当场 `git worktree remove`（非 force）；本卡只补「失败后的登记与收敛」，不改 DX-120 段既有语义。
- drain 永不删非空目录、永不碰注册在册的 worktree、不动 `dist/` 与真实数据目录——「回收由人拍板」（§7.10）对一切有内容的对象继续成立。
- 与 DX-154（cb3 会话在改 git-done.ps1 预警前置段）同文件不同职责段：串行合并，后合者 rebase；depends 已声明。
- 不做历史 husk 批量清理脚本，不回收其它会话的非空工作区（2026-11-09 已手工清过一轮空壳）。

## 输入
基线 origin/main。`git-done.ps1` DX-120 段（1644-1662）与 `Invoke-GitQuiet`；`agent-init.ps1` 开区入口与占用登记时序；`agent-workspace.ps1` Check/Status 既有分区与退出码约定（硬冲突 3 不新增）；`git-helpers.ps1` 共享例程先例（DX-63）；数据目录唯一入口 `nb-paths.ps1`（宪章 §7.6，清单文件位置必须走它不得拼字面量）。

## 产出
待清理清单的读写与 drain 例程（单处实现）、git-done 失败登记接入、agent-init/Check 两入口收敛、Check 汇总行扩字段；卡片 note 记录三场景实测命令与退出码。
