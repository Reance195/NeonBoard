# 待办与优先级

**唯一权威来源是 GitHub Issues**，本文件只做索引，避免 agent 在离线环境看不到。

> **本索引的「本地状态」与 GitHub 可能不一致**：卡片在仓库里已 `#短号=done` 结项，
> 但对应 issue 常忘关。下表只登记**可核验的仓库事实**（git 标记 + 台账），
> 关 issue 由人决定，agent 不代关。

- 里程碑：[`v0.2 质量与分发`](https://github.com/Reance195/NeonBoard/milestone/1)
- 全部待办：<https://github.com/Reance195/NeonBoard/issues>
- 按标签筛选：`enhancement` / `documentation` / `good first issue`

## 当前排期（v0.2）

| # | 事项 | 类型 | 依赖 | 本地状态（2026-10-29 核对） |
| --- | --- | --- | --- | --- |
| [1](https://github.com/Reance195/NeonBoard/issues/1) | CI 格式检查与范围检查切成硬门禁 | CI | 依赖 #2 的 scope 覆盖 | **前置已解除**（#2 已由 `DX-09` 结卡），本项仍待做 |
| [2](https://github.com/Reance195/NeonBoard/issues/2) | 补齐 49 张卡片的 acceptance 与 scope | 流程 | — | **已交付**：`#DX-09=done`（`d9e3c42`），台账 93 卡 `acceptance` / `scope` 覆盖率 100% |
| [3](https://github.com/Reance195/NeonBoard/issues/3) | 接入测试覆盖率（OpenCppCoverage） | CI | — | 待做 |
| [4](https://github.com/Reance195/NeonBoard/issues/4) | 首次启动 3.7s → 2s 以内 | 性能 | — | **已立卡 `R0-11`**（2026-10-29 代登记）：台账已登记，四项瓶颈（数据库 / Shader 预热 / 字体 / 插件扫描）收口 + `perf-baseline.json` 收紧，可用 `agent-init.ps1 -Card R0-11` 领用 |
| [5](https://github.com/Reance195/NeonBoard/issues/5) | 上架 winget | 分发 | — | 待做 |
| [6](https://github.com/Reance195/NeonBoard/issues/6) | 建立 `docs/adr/` 与首批决策记录 | 文档 | — | **主体已交付**：目录规范 + 首批两篇 ADR 已落地（见下方说明），issue 可确认后关闭 |
| [7](https://github.com/Reance195/NeonBoard/issues/7) | 对外发布（按 announcement 草稿） | 文档 | — | 待做 |

## R7 派发器进阶（已收口，原「规划中」）

`docs/iteration-roadmap.md` §9 的 R7 行此前长期停在「规划中」，与仓库事实不符：
R7-01 ~ R7-06 六张卡**全部已交付并结项**（`#R7-0x=done` + `chore(board): 结卡` 提交），
看板快照与台账 `baseline` 一致为 `done`。执行队列 / 队列 UI / 角色链 workflow /
预设 workflow + orchestrator / 夜间批量 / 成本账 均已落地，
卡片拆分见 [`r7-dispatcher-plan.md`](r7-dispatcher-plan.md)。

## 台账已交付、GitHub 仍 OPEN 的 issue（需人确认后关闭）

| # | 事项 | 对应卡片 | 结项提交 |
| --- | --- | --- | --- |
| [8](https://github.com/Reance195/NeonBoard/issues/8) | 一分支多卡：领用占位列表化 | `DX-03` | `6a538e5` |
| [9](https://github.com/Reance195/NeonBoard/issues/9) | 会话隔离硬门：锚点区禁止 `-InPlace` | `DX-05` | `6bcdbd7` |
| [10](https://github.com/Reance195/NeonBoard/issues/10) | 代登记可审计（`chore(board)` + 授权来源） | `DX-06` | `06d6450` |
| [11](https://github.com/Reance195/NeonBoard/issues/11) | 阶段条未监听路线图变更，需重启才显示 | `DX-08` | `bde5c46` |

> #11 由 `DX-04`（`adf131c`，阶段条按 §9 渲染）与 `DX-08`（监听 §9 改动免重启）两步共同解决。

## 建议顺序

```
#1（切硬门禁，前置已解除，可立即开工）
#4（首启优化 —— 已立卡 R0-11，按 perf-baseline 定位瓶颈后可直接领用）
#3（覆盖率） → #5（winget） → #7（发布）
```

> #2 / #6 已交付，从顺序里移出；#8 ~ #11 只需确认并关 issue，无开发工作量。

## 相关文档

| 文档 | 说明 |
| --- | --- |
| `docs/task-card.md` | 任务卡五段式规范 |
| `docs/pitfalls.md` | 踩坑经验台账；第七节是「尚无解决方案、需人拍板」清单 |
| `docs/r6-dispatcher-plan.md` | R6 任务派发器：卡片拆分、验收、波次与登记片段 |
| `docs/r7-dispatcher-plan.md` | R7 派发器进阶：队列编排与角色链的卡片拆分 |
| `docs/adr/` | 架构决策记录（编号、格式与索引见 `README.md`） |
| `docs/perf-baseline.json` | 性能基线（CI 每次比对） |
| `docs/announcement.md` | 发布草稿与 checklist |
| `SECURITY.md` | 安全策略与漏洞报告 |

## 认领方式

按 `AGENTS.md` 条款 6：先确认 issue 对应到看板台账里的卡片短号，
再用 `scripts/agent-init.ps1 -Card <短号>` 开工作区，收尾跑 `scripts/verify.ps1`。

> **台账里没有短号的 issue 不能直接开卡**：立卡权归维护者
> （条款 6 第 1 点，agent 不得自行加卡），需先由维护者登记进 `docs/board-ledger.json`。
