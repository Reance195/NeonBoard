# 计划钩子：提交即流转

提交信息里写一个短号，标题里带同一个短号的任务就会自动换列。

```text
提交信息：fix(model): 修复 #T12 统计刷新
任务标题：修复统计芯片刷新 #T12
结果：    该任务自动从「待办」移到「进行中」
```

## 组成

```text
git commit
   │
   ├─ .githooks/post-commit        只做「投递」：把提交信息写进事件队列
   │      └─ NeonBoard.exe --emit-hook-file <临时文件>
   │
   ├─ hooks.db（独立库）            hook_event 表：待投递事件
   │
   ├─ 主进程轮询（1 s）             取事件 → 转成 hook.commit 插件事件
   │
   └─ plan-hook 插件（后台常驻）    读规则 → 匹配任务 → moveTaskToEnd
```

钩子只负责**投递信号**，真正的识别与流转全部在插件里。宿主不解析短号，也不认识任何具体规则。

## 安装

仓库里的 `plugins/plan-hook/` 是官方插件，**不随安装包分发**。把它复制到用户插件目录即可生效：

```powershell
Copy-Item -Recurse -Force plugins\plan-hook `
  "$env:APPDATA\com.lin\NeonBoard\plugins\plan-hook"
```

复制后点侧栏「设置」→「重新扫描插件」，插件列表里会出现「计划钩子」。

未安装时提交里的 `#T12` 不会报错，也不会有任何效果——钩子找不到可执行文件时只打印一行提示并静默跳过。

## 标记语法

| 写法 | 效果 |
| --- | --- |
| `#T12` | 移到「进行中」（默认状态） |
| `#T12=done` | 移到「已办结」 |
| `#T12=todo` | 移到「待办」 |

规则：

- 短号 = `#` 加一个字母开头的串（`#T12`、`#t12`、`#Tx` 等，不区分大小写，统一按大写比较）。
- **整词匹配**：`#T1` 不会命中 `#T12`。
- 任务侧靠**标题**里的短号定位，所以任务标题末尾要带短号，例如「修复统计芯片刷新 #T12」。
- 同一个短号出现在多个任务标题里 → **全部**流转。
- 目标状态与当前状态相同 → 跳过，不写库。
- 不记录「谁做的」：不加备注、不写作者。

## 规则

规则存在 `board.db` 的 `meta` 表，键名 `plan.rules`，内容是 JSON：

```json
{
  "version": 1,
  "enabled": true,
  "defaultStatus": "doing",
  "match": {
    "field": "title",
    "pattern": "#([A-Za-z][A-Za-z0-9]*)(?:=(todo|doing|done))?"
  },
  "statusMap": { "todo": "todo", "doing": "doing", "done": "done" },
  "insert": "end",
  "onAlreadyInTarget": "skip"
}
```

| 字段 | 说明 |
| --- | --- |
| `enabled` | 关掉后所有标记都不生效 |
| `defaultStatus` | 不带 `=状态` 时使用的目标状态 |
| `match.field` | 在任务的 `title` / `note` / `tags` 里找短号 |
| `match.pattern` | 提取短号的正则，捕获组 1 是短号 |
| `statusMap` | 短号后缀 → 目标状态 |
| `insert` | `end` = 插入目标列末尾 |
| `onAlreadyInTarget` | `skip` = 已在目标列则跳过；`move` = 仍要重排 |

在插件面板里可以直接改这几个字段并保存（保存即写 `meta` 表），**改规则不需要重新编译、不需要重启**。

## 手动执行

插件面板里有「手动执行」输入框：粘贴一段提交信息点执行，走的是同一套规则，**不经过 Git**。用于：

- 补投一次历史提交；
- 验证规则写得对不对；
- 调试匹配。

## 应用没开的时候

`post-commit` 钩子投递的事件会先落进 `hooks.db`。应用下次启动、后台插件加载完成后，主进程会把积压的事件补投递一遍。所以「先提交、后开应用」也能正常流转。

## 命令行投递

不想用 Git 钩子时，可以直接调用：

```powershell
NeonBoard.exe --emit-hook "fix(model): 修复 #T12 统计刷新"
NeonBoard.exe --emit-hook-file C:\path\to\commit-message.txt
```

两个参数都走 `utilityMode`，**应用正在运行时也能用**（不受单实例限制）。`--emit-hook-file` 更稳：提交信息里的引号和换行不会被命令行拆散。

退出码：`0` 投递成功，`2` 参数为空或投递失败。

## 排错

| 现象 | 排查方向 |
| --- | --- |
| 提交后任务没动 | ① 插件装了吗（插件列表里有「计划钩子」吗）② 规则 `enabled` 是 true 吗 ③ 任务标题里带短号了吗 ④ 应用当时开着吗 |
| 日志里没有 hook 记录 | 钩子没跑到：检查 `git config core.hooksPath` 是否指向 `.githooks` |
| 钩子报「未找到 NeonBoard.exe」 | 设 `NEONBOARD_EXE` 指向 exe 绝对路径 |
| 钩子报「投递失败（退出码 127）」 | exe 找不到 Qt 运行库：装版/dist 版自带 DLL；开发版设 `NEONBOARD_QT_BIN=C:\Qt\6.8.3\msvc2022_64\bin` |
| 任务动了但列不对 | 看 `plan.rules` 的 `defaultStatus` / `statusMap` |

日志位置：`%APPDATA%\com.lin\NeonBoard\neonboard.log`。

## 边界

- 钩子**永不阻塞提交**：任何一步失败都只是打印一行提示，提交照常完成。
- 只在**本机**生效，不做协同、不记录操作人。
- 事件队列保留最近 500 条（`hooks.db` 的 `hook_event` 表），超出自动清理最旧的。
- 任务流转是幂等的：同一条事件重复投递不会把任务反复搬家（已在目标列会跳过）。
