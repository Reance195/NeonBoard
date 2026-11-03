#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class Database;
class TaskModel;

// 主任务 / 子任务关联门面。
//
// 设计要点（见 AGENTS.md「十、主任务与子任务约定」）：
//   1. 关联关系是「边」，存在独立边表 task_link(child_id, parent_id)，
//      不往 task 表加列、不升 user_version，老版本程序照常能开库。
//   2. 只用公开接口取任务数据，不绕过 TaskModel 直接改 task 表。
//   3. 防自环与成环：setParent 会沿父链上溯，发现成环直接拒绝。
class TaskTree : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int linkCount READ linkCount NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    TaskTree(Database *database, TaskModel *tasks, QObject *parent = nullptr);

    int linkCount() const;
    int revision() const;

    // 从边表重新载入全部关联
    void reload();

    // 父卡 id（无父返回空串）
    Q_INVOKABLE QString parentId(const QString &taskId) const;
    // 父卡标题（无父返回空串）
    Q_INVOKABLE QString parentTitle(const QString &taskId) const;
    // 子任务统计：{total, done, doing, todo, progress}
    Q_INVOKABLE QVariantMap childStats(const QString &taskId) const;
    Q_INVOKABLE int childCount(const QString &taskId) const;
    Q_INVOKABLE QStringList childIds(const QString &taskId) const;

    // 建立关联（父卡为空串等价于解除）
    Q_INVOKABLE bool setParent(const QString &childId, const QString &parentId);
    // 解除关联
    Q_INVOKABLE bool clearParent(const QString &childId);

    // 排序分组：所属父卡的 order_key（无父则用自身）
    qint64 groupOrderKey(const QString &taskId) const;
    bool isChild(const QString &taskId) const;
    // 相对根的层级（无父为 0），带环保护
    int depth(const QString &taskId) const;

signals:
    // 关联或任务状态变化都会触发，QML 侧靠 revision 建立依赖
    void changed();

private:
    void refreshSnapshot();
    void rebuildIndex();
    bool wouldCycle(const QString &childId, const QString &parentId) const;
    void removeLinkFor(const QString &taskId);
    void bump();

    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    QHash<QString, QString> m_links;           // childId -> parentId
    QHash<QString, QStringList> m_children;    // parentId -> childId 列表
    QHash<QString, QVariantMap> m_snapshot;    // id -> {title, status, orderKey}
    int m_revision = 0;
};
