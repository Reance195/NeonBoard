# 任务卡规范（五段式）

> 一张卡必须能被另一个 agent **独立执行、独立验收**，不需要口头补充。
> 字段落在 `docs/board-ledger.json` 的 `cards[<短号>]` 里；缺失的字段不阻断，但 `scope` 缺失时 `check_scope` 不生效。

## 五段式

| 段 | 字段 | 必填 | 说明 |
| --- | --- | --- | --- |
| 目标 | `title` + `note` 首段 | ✅ | 一句话说清「把什么改成什么」，note 首段写起因/背景 |
| 验收 | `acceptance[]` | ✅ | **可执行**的验收项，每项一条命令或一个断言，禁止写「体验更好」这类主观描述 |
| 边界 | `scope[]` | ✅ | 允许改动的路径 glob；越界 CI 直接红 |
| 输入 | `base` / `depends[]` | ⚠️ | `depends` 列出必须先完成的卡号；基线默认 `origin/main` |
| 排位 | `tier` | ✅ | `P0..P4` 五档（开工正确性 / 交付死结 / 共享脚本收口 / 独立脚本卡 / 全量编译卡），请求单 frontmatter 声明；派生器按档位给新卡算 `orderKey` 落点，不声明立卡直接被拒（DX-123） |
| 产出 | 合并提交的 body 四段 | ✅ | 变更 / 验收 / 影响 / 卡片（见下） |

## 字段示例

```json
"R5-03": {
  "id": "aee6be17-c2c3-4157-88e2-b495478fd32d",
  "stage": "R5",
  "title": "R5 发布 v0.1.1：版本号单源化 + 端到端升级验证",
  "note": "起因：v0.1.0 发布后发现安装包版本号与源码版本不一致……",
  "acceptance": [
    "pwsh -File scripts/verify.ps1 全过",
    "v0.1.1 安装包 --check-update 能检出 v0.1.2"
  ],
  "scope": ["src/**", "installer/**", "scripts/release.ps1", ".github/workflows/release.yml"],
  "depends": ["R5-02"]
}
```

`scope` 支持 glob：
- `src/**` 覆盖 `src` 下任意层级
- `docs/plugin-api.md` 精确路径
- `plugins/*/plugin.json` 单层通配
- **`plugins/agent-cursor/` 等价于 `plugins/agent-cursor/**`**（目录式写法，尾斜杠表示整个目录）。
  两种写法都对，机器会先把它规范成 `**` 再匹配；DX-17 之前只按字面 glob 编译，
  目录式一条文件都盖不住，写了等于没写。

体检现有 scope 有没有「写了但没生效」的：

```powershell
python scripts/check_scope.py --audit                   # 默认只审在办卡
python scripts/check_scope.py --audit --include-closed  # 连已结卡一起审
python scripts/check_scope.py --selftest                # 规范化、匹配与折叠语义的离线断言
```

`--audit` 的判据分两档：**目录式命中 0 直接退出 1**（写了等于没写，是缺陷）；其它命中 0
只提示（多半是卡要建的文件还没建）。

**默认只审在办卡**（DX-18）：已结卡写死的精确路径会在搬家/重构后集体失效（`MOD-06` 把
`src/` 拆成 8 个模块子目录后一次造出 191 条），那是历史记录、不是缺陷 —— 改它们属任务定义
变更（DX-10），且会把在办卡的真缺陷淹掉。这些条目折叠成一行计数：

```
# 2026-11-04 的实测输出，条数随卡增减，别当固定值
已结卡另 774 条 scope（其中命中 0 的 190 条），已折叠（--include-closed 查看）
```

要做历史考古、或想确认某张已结卡的 scope 是否真的覆盖过文件时，加 `--include-closed`。
折叠只影响「看什么」，不影响退出码：被审集合里只要有目录式命中 0，仍退 1。

CI 里的这一步仍是**信号**（`continue-on-error`）：存量卡还有未补 scope 的，硬门禁会一片红；
补齐之后再摘掉。

## scope / acceptance 是契约，不是备注

两者与 `title` / `stage` 一样属于**任务定义**（DX-10）：`scope` 决定 agent 被允许改哪些路径，
`acceptance` 决定怎样算做完。改它们跟加卡同权，同样受 `scripts/task_gate.py` 的代登记审计：

- 必须单独一条 `chore(board):` 提交，body 写「授权：」「批次：」两行；
- 不得与代码路径同提交（先调整契约再动手，或反过来，但别混在一条提交里）；
- 自动结卡（`derive_board_status.py --close-cards`）只改 `baseline`，**这条通道不扩权**。

比对时忽略数组顺序与首尾空白，纯排版整理不算定义变更，不会要求凭证。但放宽 `scope`
或改软 `acceptance` 一定会在提交信息里留下授权来源 —— 这是留给事后复核的痕迹；
闸门无法验证「谁真的批准过」，真正的门仍是维护者复核。

**验收项提到的文件若在 scope 外，须写明是只读验证工具还是本卡产物**（DX-100）：
验收项声明「回归用例 / 新增用例 / 补用例 / 新增脚本」却引用 scope 外的
`scripts/*.py|ps1`，意味着本卡要改 scope 外文件 —— `register_board_cards.py`
会在立卡阶段直接拒绝并点名该路径；只读验证命令（`selftest` / `-ScriptOnly` /
`--smoke` / `--check`，如 `pwsh -File scripts/verify.ps1 -ScriptOnly 全绿`）不受限。
存量卡由 `python scripts/register_board_cards.py --audit-acceptance-scope` 回放，命中只告警（棘轮）。

## 产出：合并提交 body 四段

`docs/git-workflow.md` 要求 `merge: 合入 <分支名>` 作为标题，正文补四段：

```text
merge: 合入 fix/stagebar-chip-overflow

变更：修复统计芯片在删除任务后不刷新（TaskModel::removeTask 未发 statsChanged）
验收：pwsh scripts/verify.ps1 全过；--smoke 无 QML 错误
影响：仅 TaskModel，无 schema 变更，无新增依赖
卡片：R3-07
```

理由：本项目不用 PR，合并提交就是唯一的「变更说明」存档。

## 两类验收口径

同一句「`verify.ps1` 全过」，在脚本卡和代码卡上的成本差一个数量级：冷启动 worktree 的全量
编译要 20~30 分钟（DX-13 实测 512 个目标、卡在最后两个链接目标上），而脚本层的检查 2 秒就够，
且与编译毫无关系。所以验收按卡片的改动面分两档：

| 档 | 判定规则 | 验收命令 | 覆盖什么 |
| --- | --- | --- | --- |
| 代码类（默认） | `scope` 内有 `.cpp` / `.h` / `.hpp` / `.qml` / `CMakeLists.txt` | `pwsh -File scripts/verify.ps1` | 编译 → ctest → `--selftest` → `--smoke` → 模块守卫 → 看板一致性 |
| 脚本 / 配置类 | `scope` 内**没有**上面任何一类 | `pwsh -File scripts/verify.ps1 -ScriptOnly` | 改动清单自查 → PS 语法解析 → `check_script_hygiene` → 工作流 YAML → `check_scope` → `check_path_refs` → 台账分档 |

判定**看 scope，不看心情**：卡片声明里没有代码路径就走脚本层。实现里一旦冒出代码文件，
`-ScriptOnly` 的第一项（口径自检）会直接拒绝并指回全量 —— 所以「按脚本卡申报、顺手改点代码」
这条路是堵死的，不会变成绕过编译的后门。

**改台账另按「改了什么」分流**（DX-25）：`docs/board-ledger.json` 看着像文档，实际是
`tst_projectmap` 的测试输入（它读 `stage` 排序、把 `baseline` 当 done 判据、校验 `cards`
与 roadmap 行对应），所以不能一律当脚本文档放过：

| 本次改动 | `-ScriptOnly` 的处置 |
| --- | --- |
| 只动 `baseline` / `claims`（结卡、领用登记，最高频） | 自动跑 `derive_board_status.py --check`，且要求通过 |
| 立卡（新增卡，`stage` 是既有取值） | 同样走派生一致性 —— 不拦，但新卡必须已在快照里 |
| **只改既有卡的 `note`（交付说明，DX-113）** | 留在轻档走派生一致性 —— 交付说明是每张卡收尾的必经动作，不升级验收档 |
| 改了既有卡的契约字段（`scope` / `stage` / `title` / `depends` / `acceptance` 任一，或 note 与契约同改）、删卡、或新卡带陌生 `stage` | 直接拒绝并指回全量 |

立卡不拦是有意的：立卡是每张卡的必经步骤、必然改 `cards`，若一律要求全量，脚本卡的轻档就
永远用不上。真正会让 `tst_projectmap` 与 roadmap 对不上的是**改既有卡的契约**与**陌生 stage**，
判据就盯这两样。立卡后若快照落后，`derive --check` 会指名要跑 `--write`（收尾时 `git-done`
也会自动做）。

判定由 `scripts/check_board_diff.py --base <ref>` 机械完成，不需要人记规则。

## 交付说明（note）的提交与验收口径（DX-113）

交付说明写进卡片 `note`，**单独一条 `docs(board): 补 <短号> 交付说明` 提交**，与实现提交
分开；`note` 不属任务定义，无需授权凭证（同 `claims`）。它不改 `tst_projectmap` 读的任何
派生输入，`check_board_diff` 判 derive 档 —— 所以**轻档验收在含交付说明提交的最终分支态
也成立**，卡片验收项里的「`-ScriptOnly` 全绿」按最终分支态理解，不再存在「实现态绿、
最终态红」靠挑提交时态自证的空间（DX-94 / DX-81 / R11-11~12 曾三次同型）。

全量编译仍由 CI 兜底，本地这一档不降低任何门槛：脚本层不跑 ctest、不碰 exe、不看 `--smoke`。
两种口径的退出码语义一致（0 通过 / 1 失败），`scripts/verify.ps1` 仍是唯一入口。

## 立卡流程

立卡是**一次提交**，所以它自己占一条分支，不和实现混在一起：

```text
立 card 分支（只改台账） → 合并回主干 → 从主干开实现区领卡 → 干活 → git-done 结单
```

第二条之前的每一步都有工具，别手贴 JSON：

```powershell
# 1) 请求单五段式表单（frontmatter + 目标/验收/边界/输入/产出）落 docs/card-requests/
# 2) 立卡唯一合法入口：校验表单 → 发号（服务端占位）→ 写台账与回执 → derive 刷快照
python scripts/register_board_cards.py --form docs/card-requests/<YYYYMMDD>-<slug>.md `
  --authorized-by "维护者 <会话> <日期>"

# 3) 程序打印两段提交指引（docs(board) 收录请求单 + chore(board) 立卡），照做后合并立卡分支
# 4) 再从主干开实现区领卡
pwsh -File scripts/agent-init.ps1 -Agent <名> -Branch <分支> -Card <短号>
```

> 预演可用 `python scripts/board-apply.py <请求单> --diff`（只读，纯增量文本判定，
> 不落任何文件）；它的 `--apply` / `--commit` 落盘通道已停用（DX-100）——
> 发号、服务端占位与回执只有 `register_board_cards.py --form` 一条路。

几个边界：

- 立卡的落盘、发号、服务端占位与回执**只有 `register_board_cards.py --form` 一条路**；
  `board-apply.py` 的 `--apply` / `--commit` 已停用（请求即拒并指向新入口，DX-100）。
- 授权来源必须经 `--authorized-by` 显式传入并写进回执；程序不替你写判断，只替你把凭证落盘。
- 落盘前程序自校验纯增量（既有 `cards` / `baseline` / `claims` 逐条相等），预演 diff 里
  出现任何「-」行都会被 `plan.text_not_additive` 阻断（DX-100）。
- 校验不是格式检查：卡号正则、tier 档位（DX-123）、验收项引用 scope 外脚本（DX-100）、
  与在办卡争同一路径、依赖成环、`acceptance` 里没有任何可执行记号……逐条见脚本头部说明。

**立卡 → 实现分开两条分支**不是形式主义。合并成一条会丢掉 `chore(claim): 领取 #<短号>`
那条领用提交：开区时卡还不存在，`-Card` 只能填 `none`，`refs/claims/<短号>` 也就没挂，
收尾时 `check_claim_refs.py` 会拦（dxp2 的现场）。

## 配套工具

| 工具 | 作用 |
| --- | --- |
| `scripts/verify.ps1` | 本地与 CI 共用的唯一验收入口（全量：编译 → ctest → selftest → smoke；`-ScriptOnly`：语法 / 卫生 / YAML / 范围 / 路径引用 / 台账分档） |
| `scripts/check_board_diff.py` | 改台账时判档：`cards` 结构变了要求全量，只动 `baseline` / `claims` 则跑派生一致性 |
| `scripts/check_path_refs.py` | 扫全仓对 `src/` 文件的硬编码引用，指向不存在的路径即失败（棘轮：只报本次新增） |
| `scripts/check_scope.py` | CI 里比对本次 diff 与卡的 `scope`，越界即失败 |
| `scripts/agent-init.ps1 -Card <短号>` | 开工作区时把 `NB_CARD` / `NB_SCOPE` / `NB_ACCEPTANCE` / `NB_DEPENDS` 注入 `.agent-env.ps1` |
| `scripts/register_board_cards.py --form <请求单>` | 立卡唯一合法入口（DX-98）：五段式表单校验、跨 worktree 发号 + 服务端占位、生成回执、刷快照并打印两段提交指引 |
| `scripts/board-apply.py --diff` | 立卡请求**只读预演**：20 项校验 + 纯增量文本判定（DX-100）；`--apply` / `--commit` 落盘通道已停用 |

## 排位如何生效（DX-123）

档位判断留在人这一侧，落位与校验交给机器：

```text
请求单 frontmatter 声明 tier → register_board_cards.py 校验枚举并写进 cards[<短号>].tier
                             → derive --write 给新卡算落点：本档末张与下一档首张的中点
                               （与看板拖拽同一套分数索引口径，不重排任何既有卡）
                             → derive --check 校验「档位随 orderKey 单调不减」
```

- 五档含义与实现只有一份：`scripts/board_priority.py`（Z5）；漂移靠退出码拦，不靠人记（Z6）。
- 未声明 tier 的存量卡按棘轮豁免，`--check` 只校验声明了档位的 todo 卡。
- `tier` 属任务定义：改既有卡的档位必须单独 `chore(board):` 提交，body 带「授权：」「批次：」两行；
  回填既有卡会被 `check_board_diff.py` 判为「改了既有卡」→ 走全量 `verify.ps1`。
- 新卡落点冲突（档间间隙耗尽）时派生器直接报错拒绝写盘，不静默挪别人的卡。

## agent 侧约定

dot-source 工作区环境后即可拿到边界与验收：

```powershell
. .\.agent-env.ps1
$env:NB_CARD        # R5-03
$env:NB_SCOPE       # src/**;installer/**;scripts/release.ps1
$env:NB_ACCEPTANCE  # pwsh -File scripts/verify.ps1 全过;...
$env:NB_DEPENDS     # R5-02
```

**动手前先看 `NB_SCOPE`，收尾前先跑 `scripts/verify.ps1`。**
