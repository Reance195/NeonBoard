# Agent 插件化遗留问题清单

> 2026-10-30 核对 R9 交付时整理。R9-01~05 本身已全部结卡（ctest 30/30），
> 但**交付 ≠ 生效**：核对真实数据目录后发现两批空白，已立 R14 四张卡。
>
> 这份清单的价值不在罗列条目，而在记录**为什么当时没发现**：
> R9 的验收全跑在全新沙箱上，而全新库会触发祖父条款把插件权限全授予 ——
> 绿灯掩盖了「既有库上新增插件一律未授权」这个真实场景。
> 详见最后一节的「核对方法」。

---

## P0 —— 交付了但没生效

### 1. 既有库上新增官方插件默认未授权，声明被静默丢弃 → R14-01

**现象**：本机真实数据目录上，四个 agent 插件一个都没生效。真实库副本跑 `--plugin-report`：

```text
com.lin.agent-claude    authorized= False
com.lin.agent-opencode  authorized= False
com.lin.agent-cursor    authorized= False
com.lin.agent-codex     authorized= False
effective: opencode / claude / cursor-agent 全部 source= builtin
codex 连列表都没进
```

**根因**：`PluginRuntime::ensureGrantsBootstrapped` 只在 meta 里尚不存在
`plugins.grants` 时，对「当时已发现」的插件做一次性全授予（祖父条款）。
本机这个库早就 bootstrap 过（只有 agent-bridge / plan-hook / today 三条），
之后新增的插件一律 unauthorized。而 `declaredAgentProfiles()` 对未授权声明
是**静默丢弃** —— 插件列表里看得见、也没弹授权窗，声明却根本没进三层合并。

**为什么难发现**：失败形态是「退回内置表」，而内置表里恰好有同 id 的
opencode / claude / cursor-agent 三条，所以列表看起来一切正常。
只有 codex（内置表没有）彻底消失，才留下一点线索。

**处理**：官方插件自动授予 / 显式提示 + 一键授权，二选一。
硬约束：第三方插件不得被顺带放行，否则等于废掉权限模型。

### 2. 官方插件不进打包产物 → R14-02

**现象**：`dist/NeonBoard/plugins` 下只有 `surfaces`、`today` 两个示例插件；
`CMakeLists.txt` 没有 install 插件的规则，installer 只拷 `dist\NeonBoard\*`。

**后果**：安装版用户看不到任何官方插件（agent-bridge / plan-hook / 四个 agent-*）。
开发机上之所以看得见，全靠 `PluginLoader::sourceDirs()` 里的
`NEONBOARD_SOURCE_DIR + /plugins` —— 那条只在有源码的机器上成立。

与第 1 条是同一件事的两半：一半装了不授权，一半压根没装。

---

## P1 —— 声明了但没实测过，不敢保证能跑

### 3. cursor-agent 是 .cmd 包装器，执行器能否直接拉起未实测 → R14-03

本机 PATH 上的 `cursor-agent` 是 `.cmd`，真身是
`%LOCALAPPDATA%/cursor-agent/versions/2026.10.19-6bbf2c4/node.exe + index.js`，
外层只负责转调 powershell 脚本。执行器是 `setProgram(exe)` 直接拉起、不经 shell。

当时面板写的是「若报启动失败按真实路径排查」—— 那是回避，不是结论。
`isAvailable=true` 只代表 exe 解析得到，与「拉得起来」是两件事。

### 4. opencode 只读参数 `--agent plan` 是否真实存在未核实 → R14-04

opencode 没有原生只读开关，`readonlyArgs` 用 `--agent plan` 指定一个只读 agent 兜底。
但本机 opencode 配置目录下并没有 agent 目录，这个名为 `plan` 的 agent 是否存在没查过。

风险是静默的：agent 不存在时只读派活会退化成普通执行 ——
用户以为勾了只读，agent 照样能改文件。

---

## P2 —— 已知遗留与事实过期

### 5. codex 版本号文档过期

`docs/agent-profile.md` 记的是 **v0.163.0**（2026-10-28 实测），
本机现在是 **0.164.0-alpha.11.1**（Microsoft Store 版）。参数面未重测。

### 6. codex 整轮 exec 仍未跑通

按 `docs/agent-profile.md` §12.1「禁止内置未实测 profile」，用 `enabled=false` 挡着。
这是设计意图而非缺陷，但它现在就是不可用的 —— 需要有人真跑通一次
以 `turn.completed` 结束的 exec，才能把它改成启用。

### 7. codex 的末条回复执行器未接管

`--json` 给的是事件流，最终回复要用 `-o <file>` 另收。插件面板里写了这句，
但执行器没有实现——真启用时拿不到回复正文。

### 8. 流程机制：scope 与验收条款系统性脱节

R9-04/05 补了三次 scope（qml、TaskRunner.cpp、TaskRunner.h），R10-02 也踩过同样的坑。
立卡时 scope 只写「插件目录」，验收却写「设置页显示」「执行器接 NUL」。

scope 对不上时只有三种结果：卡完不成 / 实现偷偷越界 / 补 scope。
**这是立卡环节的结构性缺陷，不是偶发**，值得单独立一张治理卡。

### 9. 流程机制：git-done 的退出码不可信

R9-05 首次跑返回 1 但**实际已推送成功**（含自动结卡）；照报错重跑，
反而在旧 HEAD 上多合并一次。判定成功只看
`git merge-base --is-ancestor <sha> origin/main`，不看退出码。

另：`git-done` 结束常留 detached HEAD，`agent-init -InPlace` 不切分支且常在
claim 成功后以 -1 退出 —— 领用成没成要看 `git log` 与台账，不能看退出码。

---

## P3 —— 小事

### 10. 面板文案不一致

`agent-claude` 面板写 `-Agent claude`，脚本参数其实是 `-Agents`
（PS 前缀唯一匹配能用，实测确认过），但与 `agent-opencode / agent-cursor`
的 `-Agents` 写法不一致。

### 11. Qoder / MiniMax Code 未纳入

Qoder 本机未安装；MiniMax Code 只有 GUI、没有 CLI 入口。

---

## 核对方法（别再靠沙箱）

**沙箱是全新库 → 触发祖父条款 → 插件权限全授予 → 一切看起来都好。**
要验「新增插件在既有库上生不生效」，必须拿**真实库的副本**：

```text
copy %APPDATA%\com.lin\NeonBoard\*.db  →  <临时目录>
NEONBOARD_DATA_DIR=<临时目录>  NeonBoard.exe --plugin-report
```

看 `agentProfiles.declared[].authorized`。有 false 就是第 1 条没修。

同理，验打包要拿 **dist 产物**启动（不能让 `NEONBOARD_SOURCE_DIR` 指向仓库根），
否则扫的是源码目录，掩盖第 2 条。
