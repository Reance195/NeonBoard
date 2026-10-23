#include "TaskUndo.h"

#include "TaskExtra.h"
#include "TaskModel.h"

#include <QDebug>

TaskUndo::TaskUndo(TaskModel *tasks, QObject *parent)
    : QObject(parent)
    , m_tasks(tasks)
{
}

void TaskUndo::setTaskExtra(TaskExtra *extra)
{
    m_extra = extra;
}

bool TaskUndo::deleteTask(const QString &id)
{
    if (!m_tasks)
        return false;
    const QVariantMap snapshot = m_tasks->removeTask(id);
    if (snapshot.isEmpty() || snapshot.value(QStringLiteral("id")).toString().isEmpty())
        return false;
    push(Kind::Deleted, snapshot.value(QStringLiteral("id")).toString(), snapshot,
         QStringLiteral("已删除任务"));
    return true;
}

bool TaskUndo::moveTask(const QString &id, const QString &status, int newIndex, const QString &description)
{
    if (!m_tasks)
        return false;
    const QVariantMap before = m_tasks->taskById(id);
    if (before.isEmpty())
        return false;
    if (!m_tasks->moveTask(id, status, newIndex))
        return false;

    QVariantMap payload;
    payload.insert(QStringLiteral("id"), id);
    payload.insert(QStringLiteral("status"), before.value(QStringLiteral("status")));
    payload.insert(QStringLiteral("orderKey"), before.value(QStringLiteral("orderKey")));
    payload.insert(QStringLiteral("completedAt"), before.value(QStringLiteral("completedAt")));

    QString text = description;
    if (text.isEmpty()) {
        const QString target = status;
        const QString source = before.value(QStringLiteral("status")).toString();
        if (target == QLatin1String("done"))
            text = QStringLiteral("已完成任务");
        else if (target == QLatin1String("doing"))
            text = QStringLiteral("已开始任务");
        else if (target == QLatin1String("todo"))
            text = source == QLatin1String("done") ? QStringLiteral("已恢复任务")
                                                   : QStringLiteral("已移回待办");
        else
            text = QStringLiteral("已移动任务");
    }
    push(Kind::Moved, id, payload, text);
    return true;
}

bool TaskUndo::moveTaskBefore(const QString &id, const QString &status, const QString &beforeId,
                              const QString &description)
{
    if (!m_tasks)
        return false;
    const QVariantMap before = m_tasks->taskById(id);
    if (before.isEmpty())
        return false;
    if (!m_tasks->moveTaskBefore(id, status, beforeId))
        return false;

    QVariantMap payload;
    payload.insert(QStringLiteral("id"), id);
    payload.insert(QStringLiteral("status"), before.value(QStringLiteral("status")));
    payload.insert(QStringLiteral("orderKey"), before.value(QStringLiteral("orderKey")));
    payload.insert(QStringLiteral("completedAt"), before.value(QStringLiteral("completedAt")));

    const QString text = description.isEmpty() ? QStringLiteral("已移动任务") : description;
    push(Kind::Moved, id, payload, text);
    return true;
}

bool TaskUndo::updateTask(const QString &id, const QVariantMap &fields, const QString &description)
{
    if (!m_tasks)
        return false;
    const QVariantMap before = m_tasks->taskById(id);
    if (before.isEmpty())
        return false;
    if (!m_tasks->updateTask(id, fields))
        return false;

    QVariantMap payload;
    payload.insert(QStringLiteral("id"), id);
    payload.insert(QStringLiteral("title"), before.value(QStringLiteral("title")));
    payload.insert(QStringLiteral("note"), before.value(QStringLiteral("note")));
    payload.insert(QStringLiteral("status"), before.value(QStringLiteral("status")));
    payload.insert(QStringLiteral("priority"), before.value(QStringLiteral("priority")));
    payload.insert(QStringLiteral("tags"), before.value(QStringLiteral("tagsFull")));
    payload.insert(QStringLiteral("orderKey"), before.value(QStringLiteral("orderKey")));
    payload.insert(QStringLiteral("dueAt"), before.value(QStringLiteral("dueAt")));
    payload.insert(QStringLiteral("createdAt"), before.value(QStringLiteral("createdAt")));
    payload.insert(QStringLiteral("completedAt"), before.value(QStringLiteral("completedAt")));

    const QString text = description.isEmpty() ? QStringLiteral("已修改任务") : description;
    push(Kind::Edited, id, payload, text);
    return true;
}

bool TaskUndo::undo()
{
    if (m_stack.isEmpty() || !m_tasks)
        return false;
    const Entry entry = m_stack.takeLast();
    const QString description = entry.payload.value(QStringLiteral("description")).toString();
    if (!revert(entry)) {
        m_stack.append(entry);
        return false;
    }
    m_lastAction = m_stack.isEmpty() ? QString()
                                      : m_stack.last().payload.value(QStringLiteral("description")).toString();
    emit changed();
    emit undone(description);
    return true;
}

void TaskUndo::clear()
{
    if (m_stack.isEmpty())
        return;
    m_stack.clear();
    m_lastAction.clear();
    emit changed();
}

bool TaskUndo::canUndo() const
{
    return !m_stack.isEmpty();
}

QString TaskUndo::lastAction() const
{
    return m_lastAction;
}

void TaskUndo::push(Kind kind, const QString &id, const QVariantMap &payload, const QString &description)
{
    Entry entry;
    entry.kind = kind;
    entry.id = id;
    entry.payload = payload;
    entry.payload.insert(QStringLiteral("description"), description);
    m_stack.append(entry);
    while (m_stack.size() > 50)
        m_stack.removeFirst();
    m_lastAction = description;
    emit changed();
    emit actionPerformed(description);
}

bool TaskUndo::revert(const Entry &entry)
{
    if (!m_tasks)
        return false;
    switch (entry.kind) {
    case Kind::Deleted:
        // 删除撤销：整行按原样插回（沿用 removeTask 提供的完整快照）
        return !m_tasks->restoreTask(entry.payload).isEmpty();
    case Kind::Moved: {
        // 移动撤销：状态、列内位置、完成时间一并回到原值
        const bool ok = m_tasks->restorePlacement(entry.id,
                                                  entry.payload.value(QStringLiteral("status")).toString(),
                                                  entry.payload.value(QStringLiteral("orderKey")).toLongLong(),
                                                  entry.payload.value(QStringLiteral("completedAt")).toLongLong());
        // 撤销「完成」时连带删除重复任务派生的下一期卡
        if (ok && m_extra) {
            const QString spawned = m_extra->takeSpawned(entry.id);
            if (!spawned.isEmpty())
                m_tasks->deleteTask(spawned);
        }
        return ok;
    }
    case Kind::Edited:
        return m_tasks->restoreSnapshot(entry.payload);
    }
    return false;
}
