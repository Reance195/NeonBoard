#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

class Database;
class TaskModel;
class QProcess;

// 看板 ↔ 多工作区领用同步门面。
//
// 职责边界（AGENTS.md 第七节）：agent 只负责领任务、干活、提交；
// 看板负责把「谁领了哪张卡」反映到界面与任务状态上。
//
// 数据来源：
//   1. 锚点区 docs/board-ledger.json 与各 .worktrees/<agent>/docs/board-ledger.json 的 claims；
//   2. git 本地分支是否存在、是否已合并进 origin/main（未合并的领用才算活跃）。
//
// 行为：
//   - 活跃领用且任务仍在「待办」→ 自动转「进行中」（同一领用只转一次，手动拖回不回弹）；
//   - 领用信息通过 claimForTask / claimAgentForTask 暴露给卡片，已失效领用置灰展示；
//   - 不自动结卡：完成以提交信息 #短号=done 为准，由派生器与 CI 回写快照。
class BoardSync : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int activeClaims READ activeClaims NOTIFY changed)

public:
    BoardSync(Database *database, TaskModel *tasks, QObject *parent = nullptr);

    int revision() const;
    int activeClaims() const;

    // {code, agent, branch, active, claimedAt}；无领用返回空 map
    Q_INVOKABLE QVariantMap claimForTask(const QString &taskId) const;
    // 卡片直接用的短标签（agent 名）；无领用返回空串
    Q_INVOKABLE QString claimAgentForTask(const QString &taskId) const;

public slots:
    void refresh();

signals:
    void changed();

private:
    struct Claim {
        QString code;
        QString agent;
        QString branch;
        QString head;
        qint64 claimedAt = 0;
        bool active = false;
    };

    QString findRepoRoot() const;
    static QString anchorRootFor(const QString &root);
    QHash<QString, Claim> collectClaims(const QString &anchorRoot) const;
    QString taskIdForCode(const QString &code) const;
    void reloadCodeMap();
    void startMergedQuery();
    void finishRefresh();
    void applyClaims();

    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    QProcess *m_branchProcess = nullptr;
    QProcess *m_mergedProcess = nullptr;
    QString m_anchorRoot;
    QHash<QString, Claim> m_claims;
    QHash<QString, Claim> m_pending;
    QHash<QString, QString> m_taskIdByCode;
    QHash<QString, QString> m_codeByTaskId;
    QStringList m_branchTips;
    QSet<QString> m_mergedBranches;
    QSet<QString> m_applied;
    int m_revision = 0;
    int m_activeClaims = 0;
    bool m_refreshing = false;
    bool m_retriedMain = false;
};
