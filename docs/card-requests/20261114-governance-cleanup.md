---
state: proposed
stage: DX
tier: P2
title: 流程类提交夹带实现路径无闸门，及本轮仓库卫生遗留清理
author: maintainer-session-2026-11-14
evidence: 2026-11-14 同步 origin/main 6f55b24 后体检实录。① f68f4f2「chore(board): 登记 agent-dx164 占用投影 #DX-164」夹带 installer/neonboard.iss、scripts/verify.ps1、docs/pitfalls.md 共 4 文件 72 行实现，而 task_gate 的 audit_problems 只在提交改了台账任务定义时才跑，流程类提交只要 subject 带 #短号 就能洗白；缺 #DX-164=done 标记使 baseline 至今 todo（327 张卡唯一未完成），derive_board_status --baseline-check 因流程类引用不算证据反而报「滞留 0」。② 4 份立卡请求单 form-*.md 落在仓库根目录，check_scope --base db453aa 把它们判为唯一越出 219 张卡 scope 并集的文件，registered/ 下 4 份回执的 form 字段仍指向根目录。③ 114 份请求单中 27 份 state=proposed 却已有 registered 回执且对应卡 baseline=done。④ check_path_refs --audit 报 77 处存量死链，其中 2 处在真实 QML 注释（BoardColumn.qml:291、TaskEditorDialog.qml:396）仍写拆目录前的 DragFeel.cpp / TaskModel.cpp 旧路径。⑤ task_gate registrations 常驻告警孤儿占位 refs/registrations/DX-118（无卡无回执）。⑥ docs/agent-plugin-open-issues.md 仍是 10-30 的 P0 清单，R14 四卡早已 done 未标收口。
scope: scripts/task_gate.py;qml/components/BoardColumn.qml;qml/components/TaskEditorDialog.qml;form-*.md;docs/card-requests/*;docs/card-requests/registered/*.json;docs/agent-plugin-open-issues.md
acceptance: python scripts/task_gate.py selftest 全过，且新增「流程类提交夹带实现路径被拒 / 纯流程路径放行 / 非流程类型不受限」三类用例;python scripts/task_gate.py registrations 退出码 0 且无 DX-118 孤儿占位告警;python scripts/check_scope.py --base origin/main 退出码 0;python scripts/check_path_refs.py --base origin/main 退出码 0（本次新增失效 0）;pwsh -File scripts/verify.ps1 -ScriptOnly 退出码 0;state=proposed 且有 registered 回执且对应卡 baseline=done 的请求单回写 closed，无客观依据的 proposed 保持原样;4 份 form-*.md 移入 docs/card-requests/ 并同步 4 份回执的 form 字段
depends:
---

## 目标

补上「流程类提交不得夹带实现路径」的硬闸门，堵住 DX-164 那种「实现塞进 chore(board) 提交 → 没有 done 标记 → 卡永远 todo 且可被重领」的链路；同时把体检暴露的仓库卫生遗留一次性收口，让 CI 常驻告警归零、死链注释不再误导后续 agent。

## 验收

见 frontmatter acceptance（7 条）。口径：task_gate 自测与 registrations 以退出码为准；范围与路径检查以 `--base origin/main` 增量口径为准；脚本层验收跑 `verify.ps1 -ScriptOnly`（本卡不改 C++，全量编译由 CI 对 qml/ 改动兜底）。

## 边界

- 只新增一条判据：kind=chore 且 scope 属 claim/roadmap/board 时，diff 若触及非豁免路径（code_files 非空）直接拒；不改现有 EXEMPT_PREFIXES / PROCESS_PATHS / FLOW_SCOPES 口径，不动 audit_problems 的既有语义。
- docs/pitfalls.md、scripts/verify.ps1 属既有豁免路径，不在本次新判据拦截面内，避免误伤合法流程提交（如领用登记、结卡、快照同步）。
- 判定实现只放 task_gate.py 一处（cmd_message 与 cmd_push 共用同一纯函数），不在别处重复实现。
- 请求单状态只回写有客观依据的 27 份（有回执 + 卡已 done）；43 份无 card 的 proposed 属真未受理，保持原样，不猜。
- 回执 sha256 是卡片规范 JSON 的哈希，改 form 路径字段不影响校验；不新建卡、不改既有卡契约、不手改 baseline。
- DX-164 的 baseline 只由 git-done 收尾的 derive --close-cards 自动结；DX-118 只删除远程占位 ref（维护者明确选「弃」），不补立卡。

## 输入

基线 origin/main 6f55b24。现场：scripts/task_gate.py:135-160（process_commit_exempt / flow_scope_beyond_paths）、:240-350（cmd_message）、:770-830（cmd_push）；qml/components/BoardColumn.qml:291、TaskEditorDialog.qml:396；docs/card-requests/registered/{DX-153,DX-154,DX-156,R11-14}.json 的 form 字段。依赖：无。

## 产出

① task_gate.py 新增 flow_impl_paths 纯函数 + 两处调用 + selftest 用例；② 2 处 QML 注释路径回改；③ 4 份请求单归位、4 份回执 form 同步、27 份 state 回写；④ agent-plugin-open-issues.md 补收口说明；⑤ DX-118 远程占位删除；⑥ DX-164 补交付标记并由 git-done 自动结卡。
