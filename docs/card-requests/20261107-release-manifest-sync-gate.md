---
state: approved
stage: DX
title: release.yml 清单回写步撞自家任务闸门：build.ps1 激活 hooksPath 后 bot 提交被拒，步骤不查 commit 退出码连累 rebase 二次失败
author: agent-codebuddy-release-session-2026-11-07
evidence: v0.1.7 发布 run 42338278076 实录。① 打包/发布全绿（setup/portable/delta/update.json 全部挂上 Release），唯独最后一步「回写更新清单到仓库」失败：bot 的 `chore(release): 同步软件更新清单至 0.1.7` 被 commit-msg 任务闸门拒 —— update.json 不在豁免清单（EXEMPT_FILES 只认 progress.json/.gitignore/.gitattributes/LICENSE），判代码路径、无短号即拒。② 根因：DX-37（75fd108，v0.1.6 打 tag 之后才合入）把 release 构建换成 build.ps1，而 build.ps1 自 ad5134d 起构建前跑 setup-git-hooks.ps1 固化 core.hooksPath（clone 级配置、fresh runner 本无钩子）—— v0.1.6 的 release 还不走 build.ps1，钩子从未在 release CI 激活过；v0.1.7 是第一次，bot 提交从此撞自家闸门。③ 连锁：步骤不查 git commit 退出码，commit 被拒后带着未提交暂存执行 git rebase origin/main → 「cannot rebase: Your index contains uncommitted changes」→ 二次失败。④ 后果：Release 已出但 raw update.json 停在 0.1.6，客户端判定「已是最新」收不到 0.1.7（v0.1.1「Release 出好了但清单没回写」同款实录的第二次）。
scope: .github/workflows/release.yml;update.json
tier: P1
acceptance: 回写步在 bot 提交前恢复检出默认钩子态（unset core.hooksPath），并在 commit 之后显式检查 $LASTEXITCODE、失败立即 throw（不再带病 rebase）;run 42338278076 产出的 update.json（latest=0.1.7，三个资产的 sha256/size 与已挂 Release 资产一致）同步落地 origin/main，客户端 raw 读取可见 0.1.7;pwsh -File scripts/verify.ps1 -ScriptOnly 全过（.github/workflows 与 update.json 均非编译路径，走脚本档）;下次真实发版（v0.1.8+）清单回写步不再红 —— 本卡只修脚本与补同步，真发验证留给下一版
depends: DX-37
suggestedId: DX-130
createdAt: 2026-11-07T04:45:00+00:00
---
## 目标
让 release.yml 的「回写更新清单到仓库」步在 DX-37 之后的构建链路下重新可用：
bot 提交不再被 build.ps1 激活的任务闸门拦住，commit 失败要当场炸而不是带病 rebase。
同时把 v0.1.7 的清单补回仓库（Release 资产已在，只差 raw 清单），客户端恢复收到 0.1.7。## 验收
见 frontmatter `acceptance`（4 条）。## 边界
.github/workflows/release.yml;update.json- 只动回写步：bot 提交前 unset core.hooksPath（恢复 fresh checkout 默认态），不动打包/发布逻辑；
- 不给 bot 提交加短号/claim —— 程序化派生物同步走 DX-48 文档口径，不应要求任务卡；
- update.json 内容取 run 42338278076 的产物原样（单一写者仍是 CI，本卡只是搬运其产物）；
- 不重打 tag、不重发 Release（资产已正确挂上，重复发布只会引入第二份 sha256）。## 输入
基线 origin/main（d3d3818）。证据链：run 42338278076 失败步日志（闸门拒 commit → rebase 带暂存失败）；
scripts/build.ps1 L66-81（构建前固化 hooksPath）；scripts/setup-git-hooks.ps1 L54-70（无条件写入）；
75fd108（release 构建换 build.ps1）；v0.1.6 run 对照（当时无 build.ps1，钩子未激活，回写成功）。## 产出
release.yml 回写步修复提交；update.json 同步提交（均带 #DX-130 标记与领用）；
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）。
