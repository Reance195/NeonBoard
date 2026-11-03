#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Database;
class TaskModel;
class TaskExec;

// 台账卡单向导入（R6-09，docs/adr/0001-task-definition-source.md）：
// 读 docs/board-ledger.json，按短号找到 board.db 里对应任务，
// 只补 task_exec 里为空的 goal / acceptance / scope —— 开发卡的定义只在台账写一次。
//
// 硬性边界（ADR-0001）：
//   1. 只允许台账 → board.db 单向导入；反向写入台账没有代码路径（UI 也不提供按钮）；
//   2. 只补空值，不覆盖非空值；两边都有且不同 → 报冲突，交人决定；
//   3. 台账里有、board.db 没建卡 → 报「未建卡」，不自动建卡；
//   4. 幂等：连续导入两次，第二次零变更；
//   5. goal 取台账 note 的首段（「起因：…目标：…」按句号切第一段，避免长篇背景灌进 goal）。
class LedgerImport : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    LedgerImport(Database *database, TaskModel *tasks, TaskExec *exec, QObject *parent = nullptr);

    int revision() const;

    // 台账路径：显式指定优先；否则从 exe 目录向上探测 docs/board-ledger.json。
    static QString resolveLedgerPath();

    // 解析台账文件 → {ok, cards:[{code,id,title,note,acceptance[],scope[]}], error?}
    // 静态纯函数，供测试直接调。
    static QVariantMap parseLedgerFile(const QString &path);

    // note 首段：按中文句号/换行切第一段并去掉「起因：」前缀，最长 300 字符。
    static QString goalFromNote(const QString &note);

    // 导入全部台账卡：{ok, imported:[code…], conflicts:[{code,field,ledger,board}…],
    //                   missing:[code…], unchanged:[code…]}
    // path 为空时用 resolveLedgerPath()。
    Q_INVOKABLE QVariantMap importAll(const QString &path = QString());

    // 只导一张卡（按短号）。结果结构同 importAll 的单卡子集。
    Q_INVOKABLE QVariantMap importCard(const QString &code, const QString &path = QString());

signals:
    void changed();

private:
    QVariantMap importOne(const QString &code, const QVariantMap &card);
    QString findTaskId(const QString &code, const QVariantMap &card) const;

    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    TaskExec *m_exec = nullptr;
    int m_revision = 0;
};
