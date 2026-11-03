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
| 脚本 / 配置类 | `scope` 内**没有**上面任何一类 | `pwsh -File scripts/verify.ps1 -ScriptOnly` | 改动清单自查 → PS 语法解析 → `check_script_hygiene` → 工作流 YAML → `check_scope` |

判定**看 scope，不看心情**：卡片声明里没有代码路径就走脚本层。实现里一旦冒出代码文件，
`-ScriptOnly` 的第一项（口径自检）会直接拒绝并指回全量 —— 所以「按脚本卡申报、顺手改点代码」
这条路是堵死的，不会变成绕过编译的后门。

全量编译仍由 CI 兜底，本地这一档不降低任何门槛：脚本层不跑 ctest、不碰 exe、不看 `--smoke`。
两种口径的退出码语义一致（0 通过 / 1 失败），`scripts/verify.ps1` 仍是唯一入口。

## 配套工具

| 工具 | 作用 |
| --- | --- |
| `scripts/verify.ps1` | 本地与 CI 共用的唯一验收入口（全量：编译 → ctest → selftest → smoke；`-ScriptOnly`：语法 / 卫生 / YAML / 范围） |
| `scripts/check_scope.py` | CI 里比对本次 diff 与卡的 `scope`，越界即失败 |
| `scripts/agent-init.ps1 -Card <短号>` | 开工作区时把 `NB_CARD` / `NB_SCOPE` / `NB_ACCEPTANCE` / `NB_DEPENDS` 注入 `.agent-env.ps1` |

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
