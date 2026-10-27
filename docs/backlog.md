# 待办与优先级

**唯一权威来源是 GitHub Issues**，本文件只做索引，避免 agent 在离线环境看不到。

- 里程碑：[`v0.2 质量与分发`](https://github.com/Reance195/NeonBoard/milestone/1)
- 全部待办：<https://github.com/Reance195/NeonBoard/issues>
- 按标签筛选：`enhancement` / `documentation` / `good first issue`

## 当前排期（v0.2）

| # | 事项 | 类型 | 依赖 |
| --- | --- | --- | --- |
| [1](https://github.com/Reance195/NeonBoard/issues/1) | CI 格式检查与范围检查切成硬门禁 | CI | 依赖 #2 的 scope 覆盖 |
| [2](https://github.com/Reance195/NeonBoard/issues/2) | 补齐 49 张卡片的 acceptance 与 scope | 流程 | — |
| [3](https://github.com/Reance195/NeonBoard/issues/3) | 接入测试覆盖率（OpenCppCoverage） | CI | — |
| [4](https://github.com/Reance195/NeonBoard/issues/4) | 首次启动 3.7s → 2s 以内 | 性能 | — |
| [5](https://github.com/Reance195/NeonBoard/issues/5) | 上架 winget | 分发 | — |
| [6](https://github.com/Reance195/NeonBoard/issues/6) | 建立 `docs/adr/` 与首批决策记录 | 文档 | — |
| [7](https://github.com/Reance195/NeonBoard/issues/7) | 对外发布（按 announcement 草稿） | 文档 | — |

> #6 的首批决策记录已落地：[`docs/adr/0001-task-definition-source.md`](adr/0001-task-definition-source.md)
> 与 [`docs/adr/0002-run-state-and-executor-safety.md`](adr/0002-run-state-and-executor-safety.md)，
> 目录规范见 [`docs/adr/README.md`](adr/README.md)。

## 建议顺序

```
#2（补 scope/acceptance） → #1（切硬门禁）
#4（首启优化，配合 perf-baseline 观察）
#3（覆盖率） → #5（winget） → #6 / #7（文档与发布）
```

## 相关文档

| 文档 | 说明 |
| --- | --- |
| `docs/task-card.md` | 任务卡五段式规范 |
| `docs/r6-dispatcher-plan.md` | R6 任务派发器：卡片拆分、验收、波次与登记片段 |
| `docs/adr/` | 架构决策记录（编号、格式与索引见 `README.md`） |
| `docs/perf-baseline.json` | 性能基线（CI 每次比对） |
| `docs/announcement.md` | 发布草稿与 checklist |
| `SECURITY.md` | 安全策略与漏洞报告 |

## 认领方式

按 `AGENTS.md` 条款 6：先确认 issue 对应到看板台账里的卡片短号，
再用 `scripts/agent-init.ps1 -Card <短号>` 开工作区，收尾跑 `scripts/verify.ps1`。
