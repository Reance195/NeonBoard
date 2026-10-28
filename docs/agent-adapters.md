# Agent 适配器：把本机各 agent 的待办同步到看板

> 本文是 [agent-task-event.md](agent-task-event.md) 的下游。那份文档定义了协议与消费端，
> 本文定义**投递侧**：每个 agent 一个适配器，把自身的待办清单变成一次合法投递。
>
> 一句话边界：**适配器只采集，不解析；协议只封装，不执行；插件只消费，不采集。**

## 一、三层结构

| 层 | 位置 | 职责 | 不允许做 |
| --- | --- | --- | --- |
| 适配器 | `scripts/agent-adapters/<agent>/`（装到 agent 自己的配置目录） | 从 agent 的 hook / 插件机制里取出待办清单 | 组装协议、解析规则、直接写库 |
| 投递器 | `scripts/emit-agent-task.ps1` | 校验并组装 `agent-task-event v1`，调 `NeonBoard.exe --emit-hook-file --hook-event agent.task` | 执行业务规则 |
| 消费 | `plugins/agent-bridge` | 按 meta 映射建卡 / 流转 | 直接采集 |

适配器一律**只调投递器**，不自己拼 JSON、不自己找 exe。投递器是全仓库唯一的封装实现。

应用没运行时投递也会落 `hooks.db`，下次启动补消费 —— 所以适配器不需要等待或重试。

## 二、本机实测（2026-10-29）

| Agent | CLI | 入站扩展点 | 出站 profile | 状态 |
| --- | --- | --- | --- | --- |
| **claude** | `Volta\bin\claude`（在 PATH） | hooks（PostToolUse / PostToolUseFailure） | 内置 | ✅ 已装并端到端验证 |
| **opencode** | `Volta\bin\opencode`（在 PATH） | 插件（`~/.config/opencode/plugins/`） | 内置 | ✅ 已装（插件已复制） |
| **cursor-agent** | `AppData\Local\cursor-agent`（**不在 PATH**） | `~/.cursor/hooks.json` | 内置但需写绝对路径 | ✅ 已装（追加，原条目保留） |
| **codex** | Store 版，藏在 WindowsApps | **无 hooks 机制** | 未内置（未实测整轮） | ⚠️ 仅提供 `-Run` 包装 |
| **Qoder** | 未安装 | — | — | ⬜ 本机没有，暂不纳入 |
| **MiniMax Code** | 只有 GUI（`MiniMax Code.exe`），无 CLI | — | — | ⬜ 无 CLI，入站出站都不可行 |

> MiniMax Code 的 `~/.minimax/bin` 下只有 `mcode-tools*.cmd`（工具服务端），不是 agent CLI；
> `~/.minimax/agents`、`plugins` 均为空目录。要纳入得等它提供 CLI 或 hooks。

## 三、投递器

```powershell
# 文件通道（最可靠，适配器统一走这条）
emit-agent-task.ps1 -Agent claude -SessionId <sid> -ItemsFile items.json

# 标准输入
'<json>' | emit-agent-task.ps1 -Agent claude -SessionId <sid> -Stdin

# 已组装好的整包
emit-agent-task.ps1 -Agent claude -PayloadFile envelope.json

# 干跑 / 自检
emit-agent-task.ps1 -Agent claude -ItemsFile items.json -DryRun
emit-agent-task.ps1 -SelfTest
```

退出码：`0` 成功 / `2` 参数或载荷非法 / `3` 找不到 NeonBoard.exe / `4` 投递失败。

`items` 每条支持 `id`（必填）、`title` 或 `content`（Claude 的形状）、`status`、`note`、`tags`、`taskId`。
缺 `id` 的条目**整条丢弃**并计数提示 —— 一条非法 id 会让 `agent-bridge` 的 meta 映射键错位，污染整批对账。

## 四、各 agent 的适配器

### claude

- 位置：`scripts/agent-adapters/claude/neonboard-hook.ps1`
- 装到：`~/.claude/settings.json` 的 `PostToolUse` / `PostToolUseFailure`
- 触发：只认 `TodoWrite` / `TaskCreate` / `TaskUpdate`
- 稳定 id：Claude 的 todo 没有 id，取 `content` 的 MD5 前 8 位（内容不变则 id 不变）

> **本机 claude 的 hooks 已被 `orca-agent-hook` 占满全部事件**。安装脚本只做幂等追加，
> 每次改前先写 `.neonboard-<时间戳>.bak`，绝不覆盖 —— 装完实测：orca 仍在 `PostToolUse[0]`，
> 我们追加在 `[1]`。

### opencode

- 位置：`scripts/agent-adapters/opencode/neonboard-todo-bridge.js`
- 装到：`~/.config/opencode/plugins/`（连带写一个同名 `.json` 记录投递器路径）
- 触发：`tool.execute.before` 抓清单、`tool.execute.after` 确认成功再投递
  —— 工具失败了还投递，看板上会出现一张并不存在的「已完成」卡片
- 这是本机**唯一已有适配器**的 agent，其余三个都照它的契约实现

### cursor

- 位置：`scripts/agent-adapters/cursor/neonboard-hook.ps1`
- 装到：`~/.cursor/hooks.json` 的 `postToolUse` / `afterAgentResponse`
- 与 claude 适配器刻意**不合并**：合并就要靠「按 agent 分支」猜字段，
  而两个 agent 的字段会各自演进，分开各自的失败只影响自己

### codex

Codex **没有 hooks 机制**（实测 codex-cli 0.164.0 无 `hook` 子命令，config.toml 也无 hooks 段），
拿不到内部待办清单。因此只提供：

```powershell
neonboard-codex.ps1 -Probe                                  # 体检 CLI 与版本
neonboard-codex.ps1 -Run -Prompt "<提示>" [-Cwd <目录>]      # 只读跑一次并投递结果
```

`-Run` 走 `codex exec -s read-only -C <cwd> --json -o <file>`：
只读沙箱是原生能力（实测确实拦住写文件），`-o` 单独收末条回复（`--json` 是事件流）。

**出站 profile 默认不启用**：此前两次 `exec` 都没在 200 s / 420 s 内跑完一个回合
（config.toml 把 provider 指向 `127.0.0.1:15721` 的本地网关）。
按 [agent-profile.md](agent-profile.md) §12.1「禁止内置未实测 profile」，
需先跑通一次以 `turn.completed` 结束的 exec 再打开。

## 五、安装与体检

```powershell
install-agent-adapters.ps1 -Status                       # 体检
install-agent-adapters.ps1 -Install                      # 装全部（幂等）
install-agent-adapters.ps1 -Install -Agents claude,cursor
install-agent-adapters.ps1 -Uninstall                    # 只摘本脚本加的条目
```

## 六、实测踩到的坑（都是会静默失败的那种）

适配器是 **fail-open** 的：任何异常都吞掉，绝不拖慢 agent 主流程。代价是出问题时
完全没信号 —— 看板上什么都不出现，也不知道是没触发还是投递失败。
设 `NEONBOARD_ADAPTER_DEBUG=<日志路径>` 可让适配器把每一步与异常写进文件。

1. **`ArgumentList` 在 .NET Framework 上不存在** —— `ProcessStartInfo.ArgumentList` 是
   .NET Core 属性，Windows PowerShell 5.1 访问它直接 `PropertyNotFound`。改用 `Arguments` 字符串。
2. **`ProcessStartInfo.EnvironmentVariables` 同理取不到** —— 要给子进程补 PATH，改当前进程的
   `$env:PATH` 让它继承，别去索引赋值。
3. **`$MyInvocation.MyCommand.Path` 在函数内部取不到** —— 函数里的 `$MyInvocation` 指这次函数调用，
   取 `.Path` 抛「找不到属性 Path」。用 `$PSScriptRoot`。
4. **带引号的 JSON 不能走 argv** —— 经 `powershell.exe -File` 传 `[{"id":"t1"}]` 会报
   「无效的 JSON 基元: t1」（引号被转义撕碎）。一律走文件或 stdin。
5. **`ConvertFrom-Json` 把整个数组当作一个对象写出** —— `@($json | ConvertFrom-Json)` 得到的是
   **1 个元素**（元素本身才是那 3 条 todo）。再走一次管道才会展开，否则表现为
   「丢弃 N 条缺 id 的条目」却看不出为什么。
6. **控制台代码页会把中文塌缩成 `?`** —— 不同内容塌缩成同样的 `?????`，进而算出相同的稳定 id，
   表现为看板上少卡、卡片互相覆盖。一律用显式 UTF-8 的 `StreamReader` 读标准输入。
7. **`-like` 不能用来匹配含方括号的文本** —— `'"items":[]'` 里的 `[]` 是通配符字符类，
   直接抛 `WildcardPatternException`。用 `.Contains()`。
8. **两个 agent 的 hooks 条目形状不同** —— claude 是 `[{"hooks":[{"command":...}]}]`，
   cursor 是 `[{"command":...}]`。按一边写的 helper 碰到另一边就炸。
9. **Ninja 单配置构建不产生 `build/Release/`** —— exe 直接落在 `build/`。exe 探测两种布局都要列。
10. **脱离 Qt 环境跑 exe 是 `0xC0000135`**（缺 DLL，退出码 `-1073741515`）—— 投递器会自动补 Qt bin 到 PATH。
11. **`C:\Program Files\WindowsApps` 列不出来**（访问被拒绝），但 `Test-Path` 直取可以 ——
    找 Store 版 codex 必须先用 `Get-AppxPackage` 拿 InstallLocation。

## 七、未做的事（站内插件化）

以上全是**站外**适配器。真正的「一 agent 一个 NeonBoard 插件」还需要宿主侧新增
`contributes.agentProfiles` 扩展点，让插件声明出站 profile（`exe` / `args` / `readonlyArgs` /
`output` / `promptMode`），`AgentProfile` 按「插件声明 > 用户 meta > 内置」三层合并，
替换现在硬编码在 `src/AgentProfile.cpp:194` 的三条。

各 agent 的 profile 数据已按现有格式备好，在 `scripts/agent-adapters/<agent>/agent.json`。
这部分要动 `src/` 与 `plugins/`，按仓库宪章需先立卡。
