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

## 配套工具

| 工具 | 作用 |
| --- | --- |
| `scripts/verify.ps1` | 本地与 CI 共用的唯一验收入口（编译 → ctest → selftest → smoke） |
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
