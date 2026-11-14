---
state: closed
card: DX-148
stage: DX
tier: P1
title: 宪章 §四「密钥严禁入库」零机器执行，入库闸门全靠自觉
author: qodercn-session-2026-11-08
evidence: 维护者 2026-11-08 会话指令「全做」→「授权」，采纳审计轮档位二缺口 #2，并定下口径：误报逃生走可评审名单，不新开 --no-verify 豁免。实测：全仓执行点清单（钩子 5 文件、校验脚本 14 个、CI 三 workflow）中没有任何一条内容级密钥扫描；`.gitignore` 只按文件名拦（`.env`、`*.pem`、`*.key`），把私钥贴进 `README.md`、`board-ledger.json` 或任何被跟踪文件的路径完全无闸——这是宪章全部安全类规范里唯一纯靠散文的一条（Z4 失败模式未定、Z6 坑未沉淀成判据）。
scope: scripts/check_secrets.py;.githooks/pre-commit;docs/secrets-allowlist.md
acceptance: 新建 `scripts/check_secrets.py`：只扫描本次暂存新增行（棘轮，不回扫历史），命中私钥头（`-----BEGIN ... PRIVATE KEY`）、云厂商 AK 前缀定长串（AKIA/ASIA/LTAI 后跟 16+ 位大写字母数字）、`token|secret|password|api[_-]?key` 赋值长串、40+ 位连续 hex/base64 高熵串这四类模式即 exit 1 并点名文件、行号、模式名；`python scripts/check_secrets.py --selftest` 全过（正反例构造一律用字符串拼接，扫描器与自测不得互相命中）；`.githooks/pre-commit` 在既有检查后接入该闸，python 不可用时按 DX-147 fail-closed 口径处置；误报唯一逃生是 `docs/secrets-allowlist.md` 登记（每行：相对路径 + 行内容 sha256 前 12 位 + 理由，闸门只认名单，名单本身进 git 可评审），`--no-verify` 豁免面保持为空（`check_script_hygiene.py` 规则 B/D 不因本卡放松）；实测：暂存一个含假私钥头的文件提交被拒，登记名单后同内容可提交
depends: 
rejectReason: 
createdAt: 2026-11-08T13:40:00+00:00
---

## 目标
给宪章 §四「密钥、令牌、本地私有配置严禁入库」装上第一道机器闸：内容级模式扫描接入 pre-commit，命中即拒，误报走可评审名单——把「安全类规范里唯一没执行者的一条」补进 fail-closed 族。

## 验收
见 frontmatter acceptance（5 条，分号分隔）。核心是第 1、4、5 条：扫描器自测全绿、豁免面仍为零、假私钥实测被拒且名单登记后可放行。第 2 条「只扫新增行」决定闸门成本与存量兼容——不回扫历史，否则首跑即红。

## 边界
见 frontmatter scope。

- 只拦「新提交写入的内容」；已入库历史中的既有匹配由名单一次性登记豁免，不在本卡做历史清洗。
- 不改 `.gitignore` 既有文件名拦截、不引入 git-secrets/gitleaks 等第三方工具（宪章 §九零第三方依赖同族口径）。
- 不改 `--no-verify` 零豁免策略本身（`docs/adr/0003`）；名单是扫描器的判据组成部分，不是钩子绕行开关。
- 不动 pre-commit 其它判定；python 缺失场景以 DX-147 的统一口径为准（本卡不得各自发明）。

## 输入
基线 origin/main。`.gitignore` 现有文件名拦（§四）；hygiene 规则 B/D 的零豁免实现（`check_script_hygiene.py`）；pre-commit 现有钩子结构与 python 探测写法（对照 `pre-push`）；钩子文件 LF + 无 BOM 约束（宪章 §三）。

## 产出
`scripts/check_secrets.py`（扫描 + --selftest）、`.githooks/pre-commit` 接入、`docs/secrets-allowlist.md` 空名单模板（含格式说明）；合并提交 body 写明四类模式与名单格式；卡片 note 记录假私钥实测输出（拒 / 放行两条命令与退出码）。
