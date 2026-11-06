---
state: approved
stage: DX
title: DX 启动告警零容忍：把枚举式 pattern 换成按形态识别，规则收成一份
author: maintainer-session-2026-11-07
evidence: R11-13 复盘的直接产物。① 漏检事实：R11-07（9d92d45）引入的 EmptyState 非法锚定告警 `QML EmptyState: Cannot anchor to an item that isn't a parent or sibling` 连过 R11-08 ~ R11-12 五张卡的验收，直到 R11-13 复盘才被发现 —— 期间编译、ctest 39/39、--selftest、--smoke 全部全绿。② 漏检根因：`scripts/verify.ps1` L457-460 与 `.github/workflows/ci.yml` L244-247 各写了一份**完全相同**的 12 条枚举 pattern（`is not a type|is not a member|TypeError|ReferenceError|Unable to assign|Cannot assign|QQmlApplicationEngine failed|不是一种类型|不是一个类型|不存在的附加对象|没有安装|ASSERT|FATAL|Segmentation`），`Cannot anchor` 不在其中 —— 名单里甚至有 `Cannot assign`，只差一个词就漏了。枚举式判据的本质是「只拦已知坏法」，任何新形态的告警都会静默通过。③ 规则两份实现：同一份 pattern 在 verify.ps1 与 ci.yml 各活一份，改一处忘另一处就是判据漂移（Z5「同一份判定不允许在钩子、脚本、文档里各活一份」）。④ 当前主线已干净：R11-13 修完后跑真实 --smoke 日志（32 行），按 QML 告警形态筛命中 0，说明这道闸门建起来即可全量通过，不需要先清存量。⑤ 已知覆盖边界（写进卡里，不夸大）：--smoke 只加载启动期 QML，点开才加载的浮层告警（如 R11-12 核验抓到的 PluginPanel 加载失败）本卡抓不到，仍需逐界面核验兜底。
scope: scripts/check_qml_warnings.py;scripts/verify.ps1;.github/workflows/ci.yml;docs/pitfalls.md;docs/iteration-roadmap.md
acceptance: python scripts/check_qml_warnings.py --selftest 退出码 0，自测含正反例（造一条含 Cannot anchor to an item that isn't a parent or sibling 的日志行 → 必须命中且判失败；造一条正常启动日志 → 必须不命中）;反向证伪：把一条含 Cannot anchor 的日志喂给检查，退出码非 0 且点名该行（证明它真能抓到 R11-13 那条连过五张卡的告警）;当前主线跑该检查（喂真实 --smoke 日志）命中 0;规则只有一份：QML/C++ 致命 pattern 字符串在 scripts/ 与 .github/ 下合计只出现一处（grep 断言），verify.ps1 与 ci.yml 均改为调用 scripts/check_qml_warnings.py;现有拦截能力不退化：自测覆盖 ASSERT / FATAL / Segmentation / TypeError / ReferenceError / Unable to assign 六类仍全部命中;pwsh -File scripts/verify.ps1 全过（编译 + ctest + --selftest + --smoke 退出码 0 + 模块守卫 + 看板一致性），CI 的「冒烟启动并检查日志」步骤改为调用同一脚本且仍能拦截;docs/pitfalls.md 记一条「枚举式 pattern 会漏检」的坑，docs/iteration-roadmap.md 补本卡行
depends: R11-13
suggestedId: DX-122
rejectReason: 
createdAt: 2026-11-07T00:25:00+00:00
---

## 目标
把「启动期的 QML 运行期告警」变成**机器拦得住**的事，不再靠人翻日志。

现在的判据是「枚举已知坏法」：一个固定的 12 条 pattern 字符串，命中才算失败。
后果已经付出代价了 —— R11-07 引入的空态锚定告警（`Cannot anchor to an item that isn't a parent
or sibling`）不在名单里，于是它安安静静地过了 R11-08 ~ R11-12 五张卡的验收，
每张卡的 verify 全绿、ctest 全绿、smoke 退出码 0，直到复盘时人工翻日志才看见。

本卡把判据从「枚举」换成「按形态识别」：QML 引擎的告警行有稳定形态
（`qrc:/qt/qml/<URI>/<文件>.qml:<行>:<列>: QML <类型>: <消息>` /
`file:///<路径>.qml:<行>:<列>: <错误类型>: <消息>`），
按形态识别就不必预知每一种坏法；C++ 侧的致命信号（ASSERT / FATAL / Segmentation）另作一类保留。
同时把两份 pattern 收成一份实现，verify.ps1 与 ci.yml 都调它。

## 验收
见 frontmatter `acceptance`（7 条，分号分隔）。第 2 条是这张卡的核心 —— **反向证伪**：
造一条那条漏了五张卡的告警喂进去，检查必须失败；只证明「当前主线命中 0」是不够的，
那既可能因为检查写对了，也可能因为检查根本没在工作。

## 边界
scripts/check_qml_warnings.py;scripts/verify.ps1;.github/workflows/ci.yml;docs/pitfalls.md;docs/iteration-roadmap.md

- 新脚本只读运行日志，不碰应用代码、不碰数据模型。
- 判据只有一份实现（脚本），verify.ps1 与 ci.yml 只调用 —— 不再各自维护 pattern 列表。
- 存量告警当前为 0，不需要「先清存量再上闸门」；脚本同时支持全量（--strict）与
  按本次改动文件过滤两种口径（后者用于分支上存量不阻塞，棘轮口径与 check_module_deps 一致）。
- **不夸大覆盖面**：--smoke 只加载启动期 QML；点开才加载的浮层告警本卡抓不到，
  那类仍靠逐界面核验（见 docs/design/ui-relayout-verify.md 的做法）。

## 输入
基线 origin/main；依赖 R11-13（该卡修掉了暴露这个洞的那个具体缺陷）。
告警行样例见 docs/design/ui-relayout-verify.md 第五节的证据链；
现有枚举 pattern 在 scripts/verify.ps1 与 .github/workflows/ci.yml 各一份。

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）。
