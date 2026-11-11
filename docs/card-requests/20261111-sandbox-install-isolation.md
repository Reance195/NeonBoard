---
state: proposed
stage: DX
tier: P2
title: 沙箱安装注册隔离：/DSandboxAppId 编译期开关 + 沙箱劫持宿主注册沉淀
author: maintainer-session-2026-11-11
evidence: 2026-11-11 实录：某 agent 会话验证安装包时手工执行 Setup.exe /DIR=<worktree>\.sandbox\install-clean\app，因与正式版共用 Inno AppId {8E3B6A2C-9F4D-4C1A-9B7E-2D5F6A8C1E30}，沙箱安装直接接管宿主 HKCU 卸载项与开始菜单组（InstallLocation 指向沙箱、版本顶成 0.1.8）；worktree 按 §7.2 回收后全部指向失效，表现为「NeonBoard 总是被卸载」。机器已手工修复（卸载项/快捷方式指回 %LOCALAPPDATA%\Programs\NeonBoard，DisplayVersion 按 install-receipt.json 校正为 0.1.7——exe 的 VersionInfo 为空，不可作判据）。仓库脚本无该入口，缺陷是缺结构性保证：任何一次沙箱 Setup 安装都能污染宿主，且无闸门可拦。
scope: installer/neonboard.iss;scripts/verify.ps1;docs/pitfalls.md
acceptance: installer/neonboard.iss 支持 /DSandboxAppId 编译期开关：定义后 AppId、AppName、DefaultGroupName 全部与正式版不同（独立固定 GUID 与组名），未定义时产物语义与现状完全一致（Z3 不动既有语义）;scripts/verify.ps1 的 ISCC 编译闸门对正式版与沙箱版各真编译一次，两者退出码均为 0（[Code] 段编译期错误当场拦下）;docs/pitfalls.md 新增条目：现象（快捷方式指向失效 worktree 沙箱）、根因（同 AppId 劫持 HKCU 注册）、修复步骤（注册表指回真实安装目录 + 重建快捷方式）、判据（DisplayVersion 以 install-receipt.json 为准）;pwsh -File scripts/verify.ps1 -ScriptOnly 退出码 0（含 ISCC 双闸门与 git-helpers 自测）
depends:
---

## 目标

把「沙箱验证安装不能影响宿主机」从约定变成结构性保证（Z6）：沙箱构建用独立 AppId 与程序组，装进沙箱目录时写的是自己的注册表键，宿主注册与快捷方式永不被顶掉；同卡把本次实录沉淀进 pitfalls。

## 验收

见 frontmatter acceptance（4 条）。口径：ISCC 双变体真编译以退出码为准；-ScriptOnly 全绿；pitfalls 条目含可复现的修复命令。

## 边界

- 不改 UpdateManager 的 AppId 判定（src/update 仍认正式 GUID）——沙箱 GUID 必须与之不同，本卡不改 C++（scope 不含 src）。
- 不改正式版发布产物语义：release.ps1 / CI 不传 /DSandboxAppId，正式包与现状逐字节等价。
- 不引入「沙箱安装自动回滚宿主注册」之类的第二套机制（Z1）：隔离靠 AppId 本身，不靠事后清理。
- 沙箱 GUID 固定写死在 iss 内（一个 #ifdef 两个常量），不做配置化。

## 输入

基线 origin/main。实录现场：HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\{8E3B6A2C-...}_is1 曾被指向 .worktrees\agent-duty-r014\.sandbox\install-clean\app\（2026-11-11 修复前取证）；开始菜单组 %APPDATA%\Microsoft\Windows\Start Menu\Programs\NeonBoard 同步被劫持。依赖：无。

## 产出

① installer/neonboard.iss 的 #ifdef SandboxAppId 隔离开关；② verify.ps1 ISCC 闸门双变体编译；③ docs/pitfalls.md 沉淀条目。
