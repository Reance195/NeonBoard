---
state: closed
card: DX-55
stage: DX
title: nbflow 编号自动分配：request/approve 按类型命名空间取号，人不再报号
author: agent
evidence: 2026-11-05 一天三次撞号实证：①agent 提 DX-41 与在办卡撞号（弃单顺延 DX-43）②DX-42 请求与另一会话立的 DX-42 撞号（弃单重提 DX-44）③本卡首提 DX-48 与并行批次撞号（该批 DX-48/49/50/51 已 approved）。根因：cmd_request 强制人工报号（nbflow.py:369）且不查台账占用；冲突工单路径已有取号纯函数（nbflow.py:936，含自测）未接线到普通立卡。号码只是元数据：approve 落盘前取号 + 快进推送兜底，撞号可安全自动改配。
scope: scripts/nbflow.py
acceptance: nbflow request 的 --card 改可选：省略时按 --stage 对应命名空间（DX-nn / R#-nn / Mnn / MOD-nn）从 origin/main 台账已占用号取 max+1，请求单不再需要人报号; nbflow approve 落盘前重查占用：suggestedId 已被占时自动改配下一个空号，回写请求单 card: 字段并在 chore(board) 提交体注明「原建议号 X 已占用，改配 Y」; 复用并推广现有冲突工单取号纯函数（nbflow.py:936），四种号格式共用一套逻辑，自测覆盖四种格式与撞号改配; 自测入口全过，pwsh -File scripts/verify.ps1 -ScriptOnly 全过
depends: 
suggestedId: DX-52
rejectReason: 
createdAt: 2026-11-04T20:45:37+00:00
---

## 目标
nbflow 编号自动分配：request/approve 按类型命名空间取号，人不再报号

## 验收
nbflow request 的 --card 改可选：省略时按 --stage 对应命名空间（DX-nn / R#-nn / Mnn / MOD-nn）从 origin/main 台账已占用号取 max+1，请求单不再需要人报号; nbflow approve 落盘前重查占用：suggestedId 已被占时自动改配下一个空号，回写请求单 card: 字段并在 chore(board) 提交体注明「原建议号 X 已占用，改配 Y」; 复用并推广现有冲突工单取号纯函数（nbflow.py:936），四种号格式共用一套逻辑，自测覆盖四种格式与撞号改配; 自测入口全过，pwsh -File scripts/verify.ps1 -ScriptOnly 全过

## 边界
scripts/nbflow.py

## 输入
基线 origin/main；依赖：

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）

