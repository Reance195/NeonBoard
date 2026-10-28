# 交接提示词：继续 NeonBoard R6 任务派发器

> 用法：新会话开头把「## 提示词正文」整段粘进去即可。下面 §仓库现状 是核对过的事实，§已完成的坑 是踩过的雷，别重复踩。

---

## 提示词正文

```
接手 NeonBoard（E:\projects\NeonBoard）R6 任务派发器的后续开发。

【项目定位】
NeonBoard 是个人任务派发器：每天把「需求」收成任务卡，每张卡五段式
（目标 / 验收 / 边界 / 输入 / 产出），填好执行定义后一键拉起 agent 干活。
8 层模型：需求 / 算力 / Agent / 工作流 / 代码 / 上下文 / 接入 / 观测。

【当前进度】
R6 共 9 张卡，已完成 4 张：
  R6-01 执行定义收尾            =done (79028ca)
  R6-02 运行记录侧表与同卡锁    =done (617627d)
  R6-03 Agent Profile 与执行前确认=done (9ff8a19)
  R6-04 单次执行器 TaskRunner   =已合并但缺 done 标记（见下）
下一张：R6-05 验收器与越界检查（depends=R6-04，已满足）

【你的第一个任务：把 R6-04 正式结项】
R6-04 代码（TaskRunner + 安全强化）已合入 main 且 CI 全绿，但提交信息没带
`#R6-04=done`，导致台账 baseline 仍是 todo、看板不显示完成。
做法：一次 chore(board) 提交，补 `#R6-04=done` 标记 + 台账 baseline 改 done，
提交信息按仓库规范写「授权：」「批次：」两行（这是 DX-06 立的规矩）。
注意：不要重跑代码，只要结项元数据。

【第二个任务：R6-05 验收器与越界检查】
卡片 scope（只能改这些）：
  src/AcceptanceRunner.*  src/ScopeMatcher.*
  tests/tst_acceptance.cpp  tests/tst_scopematch.cpp（按实际命名，见卡片）
  tests/CMakeLists.txt  CMakeLists.txt
验收：跑 ctest -R tst_acceptance / tst_scopematcher 全绿 + --selftest + --smoke

【必须遵守的仓库规矩（踩过坑，别绕开）】
1. 任务先行闸门：改代码的提交，subject 里必须带 `#卡号`；推 main 前还必须有
   服务端占位 `refs/claims/<卡号>`，否则 pre-push 直接拦。
2. 领用流程（唯一正确路径）：
     git commit --allow-empty -m "chore(claim): 领取 #R6-05 实施任务"
     python scripts/claim-card.py <repo> R6-05 <agent> <branch> <board.db>
     git add docs/board-ledger.json && git commit -m "chore(claim): 登记 #R6-05 领用到台账"
   board.db 默认在 %APPDATA%\com.lin\NeonBoard\board.db
3. 收尾只用 scripts/git-done.ps1，不要手工 merge 或直推 main：
     pwsh -NoProfile -File scripts/git-done.ps1 -Branch <你的分支>
   它会做：领用闸门校验 → 基于 origin/main 建临时分支 → --no-ff 合并 →
   快进推 origin/main → 释放服务端 claim ref → 删本地分支。
4. 侧表模式：执行/运行/附加状态一律走 task_extra / task_exec / task_run 侧表，
   禁止给 task 表加列，禁止动 user_version（固定 2），老程序要能开同一个库。
5. 格式棘轮：改过的 C++ 文件必须整体符合 .clang-format。
     pwsh -NoProfile -File scripts/format.ps1
6. 构建/验收只用仓库脚本，别手工 cmake（手搓会因缺 vcvars64 报 type_traits 找不到）：
     pwsh -NoProfile -File scripts/build.ps1 -QtRoot 'C:/Qt/6.8.3/msvc2022_64' -BuildDir build-local
     pwsh -NoProfile -File scripts/verify.ps1 -QtRoot 'C:/Qt/6.8.3/msvc2022_64' -BuildDir build-local -Config Release
7. 测试二进制落在 build-local 根目录（不是 build-local/tests），跑单个用例直接：
     $env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
     ./build-local/tst_xxx.exe
8. 新增测试在 tests/CMakeLists.txt 用 neonboard_add_test(target) 注册，
   target 需 contain Qt6::Sql，并给 PATH 加 Qt bin（照抄现有条目）。

【如果 github.com 连不上（这台机器常发）】
症状：git fetch/push 报 "Failed to connect to github.com port 443"，
但 api.github.com / codeload.github.com 正常 —— 是路由问题不是断网。
临时解法（用完记得删）：
  git config http.curloptResolve "github.com:443:140.82.113.3"
  ... 操作 ...
  git config --unset http.curloptResolve
可用的 IP 会变，先用 Test-NetConnection <ip> -Port 443 探一个通的。
注意 git-done.ps1 的领用闸门也要联网，所以覆盖要设在整个流程之前。

【沟通风格】
简洁中文汇报，重点说「做了什么 / 结果 / 卡在哪」，不要长篇过程流水账。
每张卡完成给一小段总结 + 下一张是什么。
```

---

## §仓库现状（2026-10-28 02:48 核对）

| 项 | 值 |
|---|---|
| 本机 HEAD | `915bcf3` |
| `origin/main` | `915bcf3`（完全同步） |
| 工作区 | 干净 |
| 本地分支 | 仅 `main` |
| 最近推送的 R6-04 | `1f0c9f2`（TaskRunner）、`707c04c`（安全强化） |
| CI | 两次均全绿 |

### R6 九张卡状态

| 卡 | 标题 | depends | 状态 |
|---|---|---|---|
| R6-01 | 执行页签与 cwd 校验 | — | ✅ done |
| R6-02 | 运行记录侧表与同卡锁 | — | ✅ done |
| R6-03 | Agent Profile 与执行前确认 | R6-01 | ✅ done |
| R6-04 | 单次执行器 TaskRunner | R6-02,R6-03 | ⚠️ 代码已合并，**缺 done 标记** |
| R6-05 | 验收器与越界检查 | R6-04 | ⬜ 下一张 |
| R6-06 | 执行结果回写与执行面板 | R6-04,R6-05 | ⬜ |
| R6-07 | Triage 只读分析 | R6-01,R6-03 | ⬜ **已解锁** |
| R6-08 | Loop 失败摘要回灌与有限重试 | R6-04,R6-05 | ⬜ |
| R6-09 | 台账卡单向导入（可选） | R6-01 | ⬜ **已解锁** |

`R6-07` 和 `R6-09` 的依赖也已满足，可以与 R6-05 并行或另起会话。

---

## §已完成的 R6-04 实现（供 R6-05 参考）

R6-05 要接着 `TaskRunner` 的输出做事，先了解它给了什么：

**`src/TaskRunner.h` 对外接口**
```cpp
Q_INVOKABLE QVariantMap start(const QString &taskId);       // 按执行定义拉起
Q_INVOKABLE QVariantMap startRaw(taskId, exe, args, cwd, timeoutSec, promptToStdin, prompt);
Q_INVOKABLE bool stop(const QString &runId);
Q_INVOKABLE QStringList activeRuns() const;
signals: logAppended(runId, text) / runFinished(runId, detail) / changed()
```

**关键行为**
- 总开关 `AgentProfile::executorEnabled()` 默认关闭，关闭时 `start()` 直接拒绝
- 忙则拒绝（M1 不排队）：同卡已有活跃 run 返回 `{ok:false, code:"busy"}`
- 心跳 10 秒（`kHeartbeatMs`），孤儿判定 90 秒
- 超时：`terminate` → 5 秒宽限 → `killTree`，标 `blocked` + `lastError="执行超时"`
- 取消：`killTree`，标 `cancelled`
- **日志脱敏**：`api_key/token/secret/password/authorization/bearer` 的值、
  `sk-/ghp_/xho*` 前缀令牌、长十六进制串，全部替换成 `[已脱敏]`
- **进程树回收**：Windows Job Object + `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`，
  取消/超时能连带杀掉 agent 拉起的子孙进程（编译器、git、包管理器等）
- 日志落 `AppPaths::logFilePath()` 同级的 `runs/<runId>.log`
- 无 shell 执行（`AgentProfile::argv` 拼 argv，不经过 shell 解析）

**run 终态**：`passed` / `blocked` / `cancelled`，`release` 写回
`exit_code, last_error, log_path, scope_violation` 四列。

---

## §R6-05 建议的切入点

卡片要求「验收器 + 越界检查」，即把卡片里声明的 `acceptance[]` 和 `scope[]`
在 run 结束后自动判定：

- **ScopeMatcher**：把 run 期间实际被改动的文件（可从 git status / diff 取）
  与卡片 `scope[]` 的 glob 比对，越界则记 `scope_violation`
- **AcceptanceRunner**：逐条跑 `acceptance[]` 里的命令（卡片里就是可执行命令字符串），
  汇总通过/失败，供 R6-06 回写成执行面板

两处都要注意：执行器**不自动 push、不自动改看板列**（这是 R6 的硬约束）。

---

## §踩过的坑（别再踩）

1. **手工跑 cmake 会失败** —— 报 `type_traits` 找不到，因为没 import vcvars64。
   永远走 `scripts/build.ps1`。
2. **`Remove-Object` 不是删文件** —— 想清旧 obj 用 `Remove-Item`，
   否则链接器会拿旧 obj 报「无法解析的外部符号」，看起来像签名不匹配。
3. **测试 exe 在 build-local 根目录**，不在 `tests/` 子目录。
4. **QProcess 裸命令名可能找不到** —— 测试里用 `qEnvironmentVariable("ComSpec")`
   拿全路径更稳。
5. **pre-push 会拦非快进和缺 claim 的推送** —— 必须走 git-done.ps1。
6. **`docs/board-ledger.json` 只有锚点区能改** —— 在 `.worktrees/` 里立卡/改台账
   会被 `task_gate.py` 拦下。
7. **提交信息规范**（DX-06 新立）：`chore(board):` 前缀 + body 必须写
   「授权：」「批次：」两行，且不得与代码改动混在同一提交。
