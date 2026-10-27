# Triage：新建任务时先让 agent 分析

> 目的：把「一句话需求」变成「可执行卡片」。分析阶段**只读**，产出结构化草案，人确认后才可执行。
> 对应产品设计稿 §6；卡片字段见 `docs/task-card.md`。

## 输入

| 输入 | 来源 |
| --- | --- |
| 卡片标题 + 人写的描述 | 卡片 |
| 项目上下文 | 目标仓库的 `AGENTS.md` + 目录结构（agent 自己读，不塞摘要） |
| 相关代码 | agent 用只读工具检索（`Read` / `Grep` / `Glob`） |

## 输出（固定 schema，缺字段视为分析失败）

```jsonc
{
  "understanding": "一句话复述需求（用于人确认理解是否一致）",
  "affected": {
    "files": ["src/TaskModel.cpp"],
    "modules": ["模型层", "统计芯片"],
    "risk": "低 | 中 | 高 —— 附一句理由"
  },
  "scope_draft": ["src/TaskModel.cpp", "tests/tst_taskcolumns.cpp"],
  "acceptance_draft": ["pwsh -File scripts/verify.ps1"],
  "questions": ["统计芯片只算当前列还是全部列？"],
  "split": [{ "title": "补回归测试", "independent": true }],
  "estimate": { "workflow": "fix", "rounds": 1, "files": 3, "size": "S | M | L" },
  "confidence": 0.75
}
```

约束：

- `questions` **最多 3 条**，且每条必须是「不看代码也能回答的业务问题」，不是「我需要知道 X 文件在哪」
- `confidence < 0.5` 时**禁止**推荐 `size: L` 以上的直接执行，必须先拆或先澄清
- `scope_draft` 必须是路径 glob，能直接喂给 `check_scope.py`

## Prompt 模板

### 通用骨架

```text
你是只读分析器，不能修改任何文件、不能执行命令。
先读仓库根目录的 AGENTS.md，再按需读取相关代码。
任务：<卡片标题 + 描述>
请输出严格 JSON（字段见下），不要输出解释性文字。

字段：understanding / affected{files,modules,risk} / scope_draft / acceptance_draft /
      questions（≤3） / split / estimate{workflow,rounds,files,size} / confidence(0-1)
```

### fix（修 bug）

```text
重点回答：
1. 复现路径是什么？现有测试能否覆盖？
2. 根因最可能在哪几个函数？
3. 最小改动集是什么（scope 尽量窄）？
4. 需要补哪条回归测试？
```

### feature（新功能）

```text
重点回答：
1. 现有架构里应该挂在哪里？有没有类似实现可复用？
2. 需要动数据库 schema 吗？（能不动就不动，动就要单独说明迁移）
3. 拆成几步？每步的验收是什么？
4. 有没有破坏兼容的风险？
```

### review（只读审查）

```text
重点回答：
1. 这个 diff 改了什么？有没有超出预期的改动？
2. 有没有明显的正确性/性能/安全问题？
3. 测试是否覆盖了新行为？
输出 issues 数组（severity: high|medium|low），不要直接改代码。
```

## 采纳流程

| 采纳什么 | 写哪 | 谁做 |
| --- | --- | --- |
| `scope_draft` / `acceptance_draft` / `questions` 的答案 | `board.db` 卡片字段（立即可用） | 应用，一键 |
| 卡片定义（进 git、可评审） | `docs/board-ledger.json` | 一次提交，人确认 |

## 质量校验（应用侧自动做）

| 检查 | 不通过时 |
| --- | --- |
| 输出是合法 JSON 且含全部必填字段 | 重跑一次，仍失败则标记 `draft` + 附原始输出 |
| `questions` ≤ 3 且不含「文件在哪」类问题 | 截断并提示 |
| `scope_draft` 均为合法 glob | 丢弃非法项 |
| `acceptance_draft` 至少 1 条且可执行 | 补默认 `verify.ps1` |
| 分析期间**没有产生文件改动** | 直接失败（证明只读失效） |

## 实现现状（R6-07 落地，2026-10-28）

`src/TriageRunner.*` 已实现上表全部校验，落点如下：

| 约束 | 实现 |
| --- | --- |
| 强制只读 | 三道硬门：① profile 必须配 `readonlyArgs`（没有即拒绝分析，不接受「prompt 里写别改文件」）；② argv 用 `AgentProfile::argv(id, readonly=true)` 拼装；③ 分析前后各取一次 `git status --porcelain -uall`（复用 `ScopeMatcher`），有差异直接判 `readonly-violation` 失败 |
| 严格 JSON | `extractJson` 从带解释文字/代码围栏的输出里抠第一个平衡 `{...}` 块；解析失败或缺必填字段（`understanding`/`affected`/`scope_draft`/`acceptance_draft`/`questions`/`estimate`/`confidence`）→ 重跑一次；仍失败标记 `draft` 并把原始输出（末尾 4000 字符）存进记录 |
| questions ≤ 3 | `validateDraft` 截断并记 problem |
| 非法 glob 丢弃 | `isValidGlob`：拒绝空串、绝对路径、盘符、反斜杠、含 `..`；与 `scripts/check_scope.py` / `ScopeMatcher` 的正斜杠语义一致 |
| acceptance 补默认 | 为空时补 `pwsh -File scripts/verify.ps1` 并记 problem |
| confidence < 0.5 | `draft.executable=false`；编辑面板据此禁用「采纳草案」一键回填与「执行」按钮（提示先拆分或澄清） |
| 草案只写 board.db | meta 表 `exec.triage.draft.<taskId>`（不加表、不升 `user_version`、不进台账）；「采纳」由 QML 把 `scope_draft`/`acceptance_draft`/`understanding` 回填执行页签表单，保存时才落 `task_exec` |

界面入口：编辑面板 → 执行页签 → 「先分析」。分析用当前表单的 agent / cwd / workflow
（先落 `task_exec` 再拉起），cwd 必须在信任白名单内，执行器总开关必须开启。
