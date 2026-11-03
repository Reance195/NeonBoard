#include "TaskTree.h"

#include "Database.h"
#include "TaskModel.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

namespace {
constexpr int kMaxDepth = 8;
}

TaskTree::TaskTree(Database *database, TaskModel *tasks, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_tasks(tasks)
{
    reload();
    refreshSnapshot();

    if (m_tasks) {
        connect(m_tasks, &TaskModel::taskCreated, this, [this]() { refreshSnapshot(); });
        connect(m_tasks, &TaskModel::taskUpdated, this, [this]() { refreshSnapshot(); });
        connect(m_tasks, &TaskModel::taskMoved, this, [this]() { refreshSnapshot(); });
        connect(m_tasks, &TaskModel::taskCompleted, this, [this]() { refreshSnapshot(); });
        connect(m_tasks, &TaskModel::taskRemoved, this, [this](const QString &id) {
            removeLinkFor(id);
            refreshSnapshot();
        });
        connect(m_tasks, &QAbstractItemModel::modelReset, this, [this]() { refreshSnapshot(); });
        connect(m_tasks, &QAbstractItemModel::dataChanged, this, [this]() { refreshSnapshot(); });
    }
}

int TaskTree::linkCount() const
{
    return m_links.size();
}

int TaskTree::revision() const
{
    return m_revision;
}

void TaskTree::bump()
{
    ++m_revision;
    emit changed();
}

void TaskTree::reload()
{
    m_links.clear();
    if (!m_database)
        return;

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return;

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT child_id, parent_id FROM task_link"))) {
        qWarning() << "TaskTree: load links failed:" << query.lastError().text();
        return;
    }
    while (query.next()) {
        const QString child = query.value(0).toString();
        const QString parent = query.value(1).toString();
        if (child.isEmpty() || parent.isEmpty() || child == parent)
            continue;
        m_links.insert(child, parent);
    }
    rebuildIndex();
    bump();
}

void TaskTree::rebuildIndex()
{
    m_children.clear();
    for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
        m_children[it.value()].append(it.key());
}

void TaskTree::refreshSnapshot()
{
    m_snapshot.clear();
    if (!m_tasks)
        return;
    const QVariantList list = m_tasks->allTasks();
    m_snapshot.reserve(list.size());
    for (const QVariant &item : list) {
        const QVariantMap map = item.toMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        m_snapshot.insert(id, map);
    }
    bump();
}

QString TaskTree::parentId(const QString &taskId) const
{
    return m_links.value(taskId);
}

QString TaskTree::parentTitle(const QString &taskId) const
{
    const QString parent = m_links.value(taskId);
    if (parent.isEmpty())
        return {};
    return m_snapshot.value(parent).value(QStringLiteral("title")).toString();
}

QVariantMap TaskTree::childStats(const QString &taskId) const
{
    int total = 0;
    int done = 0;
    int doing = 0;
    int todo = 0;
    const QStringList children = m_children.value(taskId);
    for (const QString &child : children) {
        const auto it = m_snapshot.constFind(child);
        if (it == m_snapshot.constEnd())
            continue;
        ++total;
        const QString status = it.value().value(QStringLiteral("status")).toString();
        if (status == QLatin1String("done"))
            ++done;
        else if (status == QLatin1String("doing"))
            ++doing;
        else
            ++todo;
    }
    return {
        {QStringLiteral("total"), total},
        {QStringLiteral("done"), done},
        {QStringLiteral("doing"), doing},
        {QStringLiteral("todo"), todo},
        {QStringLiteral("progress"), total > 0 ? double(done) / double(total) : 0.0}
    };
}

int TaskTree::childCount(const QString &taskId) const
{
    return m_children.value(taskId).size();
}

QStringList TaskTree::childIds(const QString &taskId) const
{
    return m_children.value(taskId);
}

bool TaskTree::wouldCycle(const QString &childId, const QString &parentId) const
{
    if (childId == parentId)
        return true;
    QString cursor = parentId;
    for (int depth = 0; depth < kMaxDepth; ++depth) {
        if (cursor == childId)
            return true;
        const QString next = m_links.value(cursor);
        if (next.isEmpty())
            return false;
        cursor = next;
    }
    return true;
}

bool TaskTree::setParent(const QString &childId, const QString &parentId)
{
    if (childId.isEmpty())
        return false;
    if (parentId.isEmpty())
        return clearParent(childId);
    if (childId == parentId)
        return false;
    if (!m_snapshot.contains(childId) || !m_snapshot.contains(parentId))
        return false;
    if (wouldCycle(childId, parentId))
        return false;

    if (m_database) {
        QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
        if (!db.isOpen())
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO task_link (child_id, parent_id, created_at) VALUES (?, ?, ?)"));
        query.addBindValue(childId);
        query.addBindValue(parentId);
        query.addBindValue(QDateTime::currentSecsSinceEpoch());
        if (!query.exec()) {
            qWarning() << "TaskTree: setParent failed:" << query.lastError().text();
            return false;
        }
    }

    m_links.insert(childId, parentId);
    rebuildIndex();
    bump();
    return true;
}

bool TaskTree::clearParent(const QString &childId)
{
    if (childId.isEmpty() || !m_links.contains(childId))
        return false;

    if (m_database) {
        QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
        if (db.isOpen()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("DELETE FROM task_link WHERE child_id = ?"));
            query.addBindValue(childId);
            if (!query.exec())
                qWarning() << "TaskTree: clearParent failed:" << query.lastError().text();
        }
    }

    m_links.remove(childId);
    rebuildIndex();
    bump();
    return true;
}

void TaskTree::removeLinkFor(const QString &taskId)
{
    if (!m_links.contains(taskId))
        return;
    if (m_database) {
        QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
        if (db.isOpen()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("DELETE FROM task_link WHERE child_id = ? OR parent_id = ?"));
            query.addBindValue(taskId);
            query.addBindValue(taskId);
            if (!query.exec())
                qWarning() << "TaskTree: removeLinkFor failed:" << query.lastError().text();
        }
    }
    m_links.remove(taskId);
    rebuildIndex();
    bump();
}

qint64 TaskTree::groupOrderKey(const QString &taskId) const
{
    const QString parent = m_links.value(taskId);
    if (parent.isEmpty())
        return m_snapshot.value(taskId).value(QStringLiteral("orderKey")).toLongLong();
    const auto it = m_snapshot.constFind(parent);
    if (it == m_snapshot.constEnd())
        return m_snapshot.value(taskId).value(QStringLiteral("orderKey")).toLongLong();
    return it.value().value(QStringLiteral("orderKey")).toLongLong();
}

bool TaskTree::isChild(const QString &taskId) const
{
    return m_links.contains(taskId);
}

int TaskTree::depth(const QString &taskId) const
{
    int level = 0;
    QString cursor = m_links.value(taskId);
    while (!cursor.isEmpty() && level < kMaxDepth) {
        ++level;
        cursor = m_links.value(cursor);
    }
    return level;
}
