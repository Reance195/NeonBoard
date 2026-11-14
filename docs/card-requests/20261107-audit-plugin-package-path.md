---
state: closed
card: DX-132
stage: DX
tier: P3
title: 插件包信任链三洞：盘符路径穿越、卸载目录逃逸、manifest.id 未比对
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计（六域并行 + 逐条亲验）。其一：src/plugin/PluginPackage.cpp:14-32 isSafeRelativePath 只挡反斜杠/前导斜杠/..，不挡 Windows 盘符绝对路径——files 键写 "C:/Users/.../run.js" 四道校验全过，QDir::filePath 对绝对路径原样返回，writeFile 的 mkpath 直接落到插件目录之外（任意文件写），"C:name" 盘符相对与 ADS 写法同样通过。其二：PluginPackage.cpp:152-158 dirNameForId 对 ".." 原样返回 ".."，PluginLibrary.cpp:649-654 uninstall 拼 pluginDir()/.. 后 removeRecursively——uninstall("..") 删掉整个 NeonBoard 数据目录（board.db、hooks.db、全部插件），且 PluginLibrary.h:48 uninstall 为 Q_INVOKABLE 可被面板 QML 直调。其三：PluginPackage.cpp:128-131 installNeonFile 只校验 manifest.id 非空，从不与请求 id 比对——AGENTS §9.2 四道校验的第 2 道全链路未实现，装 A 落 B，权限弹窗按攻击者自选 id 计。
scope: src/plugin/PluginPackage.cpp;src/plugin/PluginPackage.h;src/plugin/PluginLibrary.cpp;src/plugin/PluginLibrary.h;src/core/SafePaths.h;src/core/SafePaths.cpp;CMakeLists.txt;tests/**;docs/pitfalls.md
acceptance: tests/tst_pluginpackage.cpp 新增用例：files 键为盘符绝对路径（"C:/x/run.js"）与盘符相对（"C:name"）的 .neon 安装整单拒绝且 staging 与目标目录均无任何文件落盘;新增用例：dirNameForId 对 ".."、"."、含路径分隔符的 id 返回空串，uninstall 与 install 在派生目录名为空时整单拒绝且不删除、不创建任何目录;新增用例：manifest.id 与请求 id 不一致的包经市场/直链安装入口整单失败（报错文案含 manifest.id），一致时正常安装不回归;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）;既有官方插件部署与安装行为不回归（tst_pluginpackage、tst_pluginruntime 全绿）
depends:
---

## 目标

把插件包信任链的三个缺口按 Z14 一次收口：路径判定按「内容」而不是「清单上出现过的攻击面」——isSafeRelativePath 补盘符与冒号判定（收口为 src/core/SafePaths 单一实现，Z5）；dirNameForId 对派生目录名做单段校验（空/点/分隔符即拒绝）；installNeonFile 增加期望 id 参数，安装四道校验补齐第 2 道。

## 验收

见 frontmatter acceptance（5 条）。口径：以 ctest 用例断言三洞各自被拦 + 不落盘/不删目录的产物级证据；全量 verify 以退出码为准。

## 边界

- 不改 .neon 单文件 JSON 包格式与 manifest 字段集（§9.2 协议冻结），不引入 zip。
- 不动 PluginMarket 多源合并/缓存协议（本卡不碰网络层）。
- SafePaths 只放「安全相对路径判定」这一个函数族，更新链（另一张卡）复用同一实现，不允许第二份副本。
- 卸载边界不变：仍只允许删 pluginDir()/<id 末段>，本卡只加校验不扩权。

## 输入

基线 origin/main（a63ccbc）。审计现场：上述 file:line；tst_pluginpackage.cpp 既有用例骨架。依赖：无。

## 产出

① SafePaths 共享判定 + PluginPackage 三处校验收口；② PluginLibrary 调用点空目录名防御与期望 id 传递；③ tst_pluginpackage 新增三组用例；④ pitfalls 补一条「信任边界按内容判定」沉淀（若判定有通用教训）。
