#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class ScopeMatcher;

// 验收器（R6-05）：顺序执行卡片 acceptance[]，并与退出码、越界检查聚合成终局判定。
//
// 设计要点（docs/r6-dispatcher-plan.md §R6-05、docs/product-design.md §验收的两类形态）：
//   1. acceptance 条目两种形态：
//      - command：`pwsh -File scripts/verify.ps1` 这类命令串。首 token 必须命中程序
//        白名单（或是指向 cwd 内真实脚本的路径），QProcess 直接以 argv 起进程，
//        **不经过 shell**；条目里出现 `| > < & ;` 等 shell 语法直接拒绝（不执行）。
//      - assert：内置检查器执行的小语法。v1 只实现 `file.exists("<path>")`；
//        http.* / json.* 留接口不实现（判「暂不支持」，不算通过）。
//   2. 顺序执行：第一条失败即停（fail-fast），结果里带每条的 ok / output / 耗时。
//   3. verdict()：agent 退出码 0 + acceptance 全过 + 无越界 = passed，否则 blocked；
//      lastError 汇总失败原因（退出码 / acceptance 第几条 / 越界路径）。
//   4. 本类不写库、不改 run 状态、不改看板列 —— 只产出判定结果，回写是 R6-06 的事。
class AcceptanceRunner : public QObject
{
    Q_OBJECT

public:
    explicit AcceptanceRunner(QObject *parent = nullptr);

    // 命令白名单（程序基名，小写比较）。默认：pwsh / powershell / python / python3 /
    // ctest / cmake / git / node。测试可整体替换。
    QStringList allowedPrograms() const;
    void setAllowedPrograms(const QStringList &programs);

    // 单条解析：{kind:"command"|"assert"|"empty", ...}；kind=command 带 program/args，
    // 命中 shell 语法或白名单外程序时带 rejected+reason（不执行）。
    Q_INVOKABLE QVariantMap parseItem(const QString &item, const QString &cwd = QString()) const;

    // 顺序执行 acceptance；fail-fast。
    // {ok, total, passed, results:[{item, kind, ok, output?, exitCode?, reason?, ms}], stoppedAt?}
    Q_INVOKABLE QVariantMap run(const QVariantList &acceptance, const QString &cwd = QString(),
                                int timeoutSec = 300) const;

    // 聚合判定：{status:"passed"|"blocked", lastError?}
    // exitCode<0 表示「进程未能启动/无退出码」；acceptanceResult / scopeResult
    // 分别来自 run() 与 ScopeMatcher::check()（可为空 map，空 = 该项跳过）。
    Q_INVOKABLE QVariantMap verdict(int exitCode, const QVariantMap &acceptanceResult,
                                    const QVariantMap &scopeResult) const;

    // assert 小语法执行：目前只有 file.exists；path 相对 cwd 解析。
    // {ok, reason} —— 不支持的断言 ok=false + reason「暂不支持」。
    Q_INVOKABLE QVariantMap evalAssert(const QString &expr, const QString &cwd = QString()) const;

    // 命令串 → argv：按空格分词，双引号包裹的段保持整体（无 shell 参与）。
    static QStringList tokenize(const QString &command);

private:
    QStringList m_allowedPrograms;
};
