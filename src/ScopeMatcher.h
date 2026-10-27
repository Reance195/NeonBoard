#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// 越界检查（R6-05）：run 前记 git status 基线，run 后比对差异路径与卡片 scope。
//
// 设计要点（docs/r6-dispatcher-plan.md §R6-05）：
//   1. 基线 = `git status --porcelain` 的行集合（QProcess 直起 git，不经过 shell）；
//      跑完再取一次，「当前有而基线没有」的行就是 run 期间产生的差异路径。
//   2. 差异路径用卡片 scope 的 glob 匹配（与 scripts/check_scope.py 同一语义：
//      `**` 跨层级、`*` 不跨层级）；未命中的路径就是越界，写进 violations。
//   3. scope 为空不检查（存量卡不阻断），git 不可用（非仓库等）同样跳过并报 reason ——
//      越界检查是增强判定，不该把非 git 目录里的执行全判死。
//   4. 本类只产出事实（changed / violations），不改 run 状态、不改看板列；
//      passed / blocked 的聚合判定在 AcceptanceRunner::verdict。
class ScopeMatcher : public QObject
{
    Q_OBJECT

public:
    explicit ScopeMatcher(QObject *parent = nullptr);

    // 跑前基线：{ok:true, lines:[porcelain 行…]}；git 起不来/非仓库 {ok:false, error}
    Q_INVOKABLE QVariantMap captureBaseline(const QString &cwd) const;

    // 当前 porcelain 与基线的差异路径（去重、正斜杠归一）。git 失败返回空表。
    Q_INVOKABLE QVariantList changedPaths(const QString &cwd, const QVariantList &baseline) const;

    // 越界判定：{ok, skipped?, reason?, changed[], violations[]}
    //   scope 为空 或 git 不可用 → ok:true + skipped:true（不阻断）
    //   violations 非空 → ok:false
    Q_INVOKABLE QVariantMap check(const QString &cwd, const QVariantList &baseline,
                                  const QVariantList &scope) const;

    // path 命中 globs 中任意一条即算在范围内。纯函数，供 QML / 测试直接用。
    Q_INVOKABLE static bool matches(const QString &path, const QStringList &globs);

    // porcelain 行 → 路径：剥状态前缀、重命名取新路径、去引号反转义。
    static QString pathFromPorcelain(const QString &line);

    // 直接跑一次 `git status --porcelain`（无 shell）。ok/error 可选带出。
    static QStringList porcelainStatus(const QString &cwd, bool *ok = nullptr,
                                       QString *error = nullptr);
};
