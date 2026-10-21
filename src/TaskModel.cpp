#include "TaskModel.h"

#include "Database.h"

#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

static QString nonNull(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

TaskModel::TaskModel(Database *database, QObject *parent)
    : QAbstractListModel(parent)
    , m_database(database)
{
}

int TaskModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_tasks.size();
}

QVariant TaskModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size())
        return {};

    const Task &task = m_tasks.at(index.row());
    switch (role) {
    case IdRole:
        return task.id;
    case TitleRole:
        return task.title;
    case NoteRole:
        return task.note;
    case StatusRole:
        return task.status;
    case PriorityRole:
        return task.priority;
    case TagsRole:
        return task.tags;
    case OrderKeyRole:
        return task.orderKey;
    case DueAtRole:
        return task.dueAt;
    case CreatedAtRole:
        return task.createdAt;
    case UpdatedAtRole:
        return task.updatedAt;
    case CompletedAtRole:
        return task.completedAt;
    case TagRole:
        return firstTag(task.tags);
    case DueRole:
        return dueText(task.dueAt);
    default:
        return {};
    }
}

QHash<int, QByteArray> TaskModel::roleNames() const
{
    return {
        { IdRole, "id" },
        { TitleRole, "title" },
        { NoteRole, "note" },
        { StatusRole, "status" },
        { PriorityRole, "priority" },
        { TagsRole, "tags" },
        { OrderKeyRole, "orderKey" },
        { DueAtRole, "dueAt" },
        { CreatedAtRole, "createdAt" },
        { UpdatedAtRole, "updatedAt" },
        { CompletedAtRole, "completedAt" },
        { TagRole, "tag" },
        { DueRole, "due" }
    };
}

int TaskModel::count() const
{
    return m_tasks.size();
}

int TaskModel::todoCount() const
{
    int value = 0;
    for (const Task &task : m_tasks) {
        if (task.status == QLatin1String("todo"))
            ++value;
    }
    return value;
}

int TaskModel::doingCount() const
{
    int value = 0;
    for (const Task &task : m_tasks) {
        if (task.status == QLatin1String("doing"))
            ++value;
    }
    return value;
}

int TaskModel::doneCount() const
{
    int value = 0;
    for (const Task &task : m_tasks) {
        if (task.status == QLatin1String("done"))
            ++value;
    }
    return value;
}

int TaskModel::doneTodayCount() const
{
    const qint64 start = QDate::currentDate().startOfDay().toSecsSinceEpoch();
    int value = 0;
    for (const Task &task : m_tasks) {
        if (task.status == QLatin1String("done") && task.completedAt >= start)
            ++value;
    }
    return value;
}

void TaskModel::load()
{
    beginResetModel();
    m_tasks.clear();

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
            "SELECT id, title, note, status, priority, tags, order_key, due_at, "
            "created_at, updated_at, completed_at FROM task "
            "ORDER BY status, order_key, created_at"))) {
        qWarning() << "TaskModel: load failed:" << query.lastError().text();
    } else {
        while (query.next()) {
            Task task;
            task.id = query.value(0).toString();
            task.title = query.value(1).toString();
            task.note = query.value(2).toString();
            task.status = query.value(3).toString();
            task.priority = query.value(4).toInt();
            task.tags = query.value(5).toString();
            task.orderKey = query.value(6).toLongLong();
            task.dueAt = query.value(7).toLongLong();
            task.createdAt = query.value(8).toLongLong();
            task.updatedAt = query.value(9).toLongLong();
            task.completedAt = query.value(10).toLongLong();
            m_tasks.append(task);
        }
    }

    endResetModel();
    qInfo() << "TaskModel: loaded" << m_tasks.size() << "tasks";
    emit countsChanged();
}

QVariantMap TaskModel::taskById(const QString &id) const
{
    const int row = findRow(id);
    if (row < 0)
        return {};
    const Task &task = m_tasks.at(row);
    return {
        {QStringLiteral("id"), task.id},
        {QStringLiteral("title"), task.title},
        {QStringLiteral("note"), task.note},
        {QStringLiteral("status"), task.status},
        {QStringLiteral("priority"), task.priority},
        {QStringLiteral("tags"), firstTag(task.tags)},
        {QStringLiteral("tagsFull"), task.tags},
        {QStringLiteral("stage"), stageOf(task.tags)},
        {QStringLiteral("orderKey"), task.orderKey},
        {QStringLiteral("dueAt"), task.dueAt},
        {QStringLiteral("createdAt"), task.createdAt},
        {QStringLiteral("updatedAt"), task.updatedAt},
        {QStringLiteral("completedAt"), task.completedAt},
        {QStringLiteral("due"), dueText(task.dueAt)}
    };
}

QVariantList TaskModel::allTasks() const
{
    QVariantList result;
    result.reserve(m_tasks.size());
    for (const Task &task : m_tasks) {
        result.append(QVariantMap{
            {QStringLiteral("id"), task.id},
            {QStringLiteral("title"), task.title},
            {QStringLiteral("note"), task.note},
            {QStringLiteral("status"), task.status},
            {QStringLiteral("priority"), task.priority},
            {QStringLiteral("tags"), firstTag(task.tags)},
            {QStringLiteral("tagsFull"), task.tags},
            {QStringLiteral("stage"), stageOf(task.tags)},
            {QStringLiteral("orderKey"), task.orderKey},
            {QStringLiteral("dueAt"), task.dueAt},
            {QStringLiteral("createdAt"), task.createdAt},
            {QStringLiteral("updatedAt"), task.updatedAt},
            {QStringLiteral("completedAt"), task.completedAt}
        });
    }
    return result;
}

QString TaskModel::upsertTask(const QString &id, const QVariantMap &fields)
{
    QString taskId = id.trimmed();
    if (taskId.isEmpty())
        taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const int row = findRow(taskId);
    if (row < 0) {
        Task task;
        task.id = taskId;
        task.title = fields.value(QStringLiteral("title")).toString().trimmed();
        if (task.title.isEmpty())
            return {};
        task.note = nonNull(fields.value(QStringLiteral("note")).toString());
        task.status = fields.value(QStringLiteral("status")).toString();
        if (task.status != QLatin1String("todo")
            && task.status != QLatin1String("doing")
            && task.status != QLatin1String("done")) {
            task.status = QStringLiteral("todo");
        }
        task.priority = qBound(0, fields.value(QStringLiteral("priority")).toInt(), 3);
        task.tags = normalizeTags(fields.value(QStringLiteral("tags")).toString());
        task.orderKey = fields.value(QStringLiteral("orderKey")).toLongLong();
        if (task.orderKey <= 0)
            task.orderKey = nextOrderKey(task.status);
        task.dueAt = fields.value(QStringLiteral("dueAt")).toLongLong();
        task.createdAt = fields.value(QStringLiteral("createdAt")).toLongLong() > 0
            ? fields.value(QStringLiteral("createdAt")).toLongLong() : now;
        task.updatedAt = fields.value(QStringLiteral("updatedAt")).toLongLong() > 0
            ? fields.value(QStringLiteral("updatedAt")).toLongLong() : now;
        task.completedAt = task.status == QLatin1String("done")
            ? (fields.value(QStringLiteral("completedAt")).toLongLong() > 0
                   ? fields.value(QStringLiteral("completedAt")).toLongLong() : now)
            : 0;

        const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO task (id, title, note, status, priority, tags, order_key, due_at, "
            "created_at, updated_at, completed_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        query.addBindValue(task.id);
        query.addBindValue(task.title);
        query.addBindValue(nonNull(task.note));
        query.addBindValue(task.status);
        query.addBindValue(task.priority);
        query.addBindValue(nonNull(task.tags));
        query.addBindValue(task.orderKey);
        query.addBindValue(task.dueAt);
        query.addBindValue(task.createdAt);
        query.addBindValue(task.updatedAt);
        query.addBindValue(task.completedAt);
        if (!query.exec()) {
            qWarning() << "TaskModel: upsert insert failed:" << query.lastError().text();
            return {};
        }

        beginInsertRows(QModelIndex(), m_tasks.size(), m_tasks.size());
        m_tasks.append(task);
        endInsertRows();
        emit countsChanged();
        m_database->refreshStats();
        return task.id;
    }

    // 已存在：按字段更新，但保留原有 order_key（不把卡片挪到列尾）
    Task updated = m_tasks.at(row);
    if (fields.contains(QStringLiteral("title"))) {
        updated.title = fields.value(QStringLiteral("title")).toString().trimmed();
        if (updated.title.isEmpty())
            return {};
    }
    if (fields.contains(QStringLiteral("note")))
        updated.note = nonNull(fields.value(QStringLiteral("note")).toString());
    if (fields.contains(QStringLiteral("priority")))
        updated.priority = qBound(0, fields.value(QStringLiteral("priority")).toInt(), 3);
    if (fields.contains(QStringLiteral("tags")))
        updated.tags = normalizeTags(fields.value(QStringLiteral("tags")).toString());
    if (fields.contains(QStringLiteral("dueAt")))
        updated.dueAt = fields.value(QStringLiteral("dueAt")).toLongLong();
    if (fields.contains(QStringLiteral("status"))) {
        const QString status = fields.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("todo")
            || status == QLatin1String("doing")
            || status == QLatin1String("done")) {
            updated.status = status;
            if (status == QLatin1String("done")
                && fields.value(QStringLiteral("completedAt")).toLongLong() > 0) {
                updated.completedAt = fields.value(QStringLiteral("completedAt")).toLongLong();
            } else if (status == QLatin1String("done") && updated.completedAt == 0) {
                updated.completedAt = now;
            } else if (status != QLatin1String("done")) {
                updated.completedAt = 0;
            }
        }
    }
    updated.updatedAt = fields.value(QStringLiteral("updatedAt")).toLongLong() > 0
        ? fields.value(QStringLiteral("updatedAt")).toLongLong() : now;

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE task SET title = ?, note = ?, status = ?, priority = ?, tags = ?, "
        "order_key = ?, due_at = ?, updated_at = ?, completed_at = ? WHERE id = ?"));
    query.addBindValue(updated.title);
    query.addBindValue(nonNull(updated.note));
    query.addBindValue(updated.status);
    query.addBindValue(updated.priority);
    query.addBindValue(nonNull(updated.tags));
    query.addBindValue(updated.orderKey);
    query.addBindValue(updated.dueAt);
    query.addBindValue(updated.updatedAt);
    query.addBindValue(updated.completedAt);
    query.addBindValue(updated.id);
    if (!query.exec()) {
        qWarning() << "TaskModel: upsert update failed:" << query.lastError().text();
        return {};
    }

    m_tasks[row] = updated;
    QVector<int> roles;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        roles.append(it.key());
    emit dataChanged(index(row), index(row), roles);
    emit countsChanged();
    m_database->refreshStats();
    return updated.id;
}

QVariantMap TaskModel::stageStats() const
{
    QVariantMap result;
    for (const Task &task : m_tasks) {
        const QString stage = stageOf(task.tags);
        QVariantMap entry = result.value(stage).toMap();
        entry[QStringLiteral("total")] = entry.value(QStringLiteral("total")).toInt() + 1;
        if (task.status == QLatin1String("done"))
            entry[QStringLiteral("done")] = entry.value(QStringLiteral("done")).toInt() + 1;
        else if (task.status == QLatin1String("doing"))
            entry[QStringLiteral("doing")] = entry.value(QStringLiteral("doing")).toInt() + 1;
        else
            entry[QStringLiteral("todo")] = entry.value(QStringLiteral("todo")).toInt() + 1;
        result[stage] = entry;
    }
    return result;
}

QString TaskModel::addTask(const QString &title, const QString &status, int priority,
                           const QString &tags, qint64 dueAt)
{
    const QString cleanTitle = title.trimmed();
    if (cleanTitle.isEmpty())
        return {};

    QString cleanStatus = status;
    if (cleanStatus != QLatin1String("todo")
        && cleanStatus != QLatin1String("doing")
        && cleanStatus != QLatin1String("done")) {
        cleanStatus = QStringLiteral("todo");
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    Task task;
    task.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.title = cleanTitle;
    task.note = QStringLiteral("");
    task.status = cleanStatus;
    task.priority = qBound(0, priority, 3);
    task.tags = normalizeTags(tags);
    task.orderKey = nextOrderKey(cleanStatus);
    task.dueAt = dueAt;
    task.createdAt = now;
    task.updatedAt = now;
    task.completedAt = cleanStatus == QLatin1String("done") ? now : 0;

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO task (id, title, note, status, priority, tags, order_key, due_at, "
        "created_at, updated_at, completed_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(task.id);
    query.addBindValue(task.title);
    query.addBindValue(nonNull(task.note));
    query.addBindValue(task.status);
    query.addBindValue(task.priority);
    query.addBindValue(nonNull(task.tags));
    query.addBindValue(task.orderKey);
    query.addBindValue(task.dueAt);
    query.addBindValue(task.createdAt);
    query.addBindValue(task.updatedAt);
    query.addBindValue(task.completedAt);

    if (!query.exec()) {
        qWarning() << "TaskModel: add failed:" << query.lastError().text();
        return {};
    }

    beginInsertRows(QModelIndex(), m_tasks.size(), m_tasks.size());
    m_tasks.append(task);
    endInsertRows();
    emit countsChanged();
    m_database->refreshStats();
    return task.id;
}

bool TaskModel::updateTask(const QString &id, const QVariantMap &fields)
{
    const int row = findRow(id);
    if (row < 0)
        return false;

    Task updated = m_tasks.at(row);
    if (fields.contains(QStringLiteral("title"))) {
        updated.title = fields.value(QStringLiteral("title")).toString().trimmed();
        if (updated.title.isEmpty())
            return false;
    }
    if (fields.contains(QStringLiteral("note")))
        updated.note = nonNull(fields.value(QStringLiteral("note")).toString());
    if (fields.contains(QStringLiteral("priority")))
        updated.priority = qBound(0, fields.value(QStringLiteral("priority")).toInt(), 3);
    if (fields.contains(QStringLiteral("tags")))
        updated.tags = normalizeTags(fields.value(QStringLiteral("tags")).toString());
    if (fields.contains(QStringLiteral("dueAt")))
        updated.dueAt = fields.value(QStringLiteral("dueAt")).toLongLong();

    if (fields.contains(QStringLiteral("status"))) {
        const QString status = fields.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("todo")
            || status == QLatin1String("doing")
            || status == QLatin1String("done")) {
            if (status != updated.status) {
                updated.status = status;
                updated.orderKey = nextOrderKey(status);
                updated.completedAt = status == QLatin1String("done")
                    ? QDateTime::currentSecsSinceEpoch()
                    : 0;
            }
        }
    }

    updated.updatedAt = QDateTime::currentSecsSinceEpoch();

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE task SET title = ?, note = ?, status = ?, priority = ?, tags = ?, "
        "order_key = ?, due_at = ?, updated_at = ?, completed_at = ? WHERE id = ?"));
    query.addBindValue(updated.title);
    query.addBindValue(nonNull(updated.note));
    query.addBindValue(updated.status);
    query.addBindValue(updated.priority);
    query.addBindValue(nonNull(updated.tags));
    query.addBindValue(updated.orderKey);
    query.addBindValue(updated.dueAt);
    query.addBindValue(updated.updatedAt);
    query.addBindValue(updated.completedAt);
    query.addBindValue(updated.id);

    if (!query.exec()) {
        qWarning() << "TaskModel: update failed:" << query.lastError().text();
        return false;
    }

    m_tasks[row] = updated;
    QVector<int> roles;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        roles.append(it.key());
    emit dataChanged(index(row), index(row), roles);
    emit countsChanged();
    m_database->refreshStats();
    return true;
}

bool TaskModel::deleteTask(const QString &id)
{
    const int row = findRow(id);
    if (row < 0)
        return false;

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM task WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        qWarning() << "TaskModel: delete failed:" << query.lastError().text();
        return false;
    }

    beginRemoveRows(QModelIndex(), row, row);
    m_tasks.removeAt(row);
    endRemoveRows();
    emit countsChanged();
    m_database->refreshStats();
    return true;
}

QVariantMap TaskModel::removeTask(const QString &id)
{
    const int row = findRow(id);
    if (row < 0)
        return {};
    const Task &task = m_tasks.at(row);
    const QVariantMap snapshot = {
        {QStringLiteral("id"), task.id},
        {QStringLiteral("title"), task.title},
        {QStringLiteral("note"), task.note},
        {QStringLiteral("status"), task.status},
        {QStringLiteral("priority"), task.priority},
        {QStringLiteral("tags"), task.tags},
        {QStringLiteral("orderKey"), task.orderKey},
        {QStringLiteral("dueAt"), task.dueAt},
        {QStringLiteral("createdAt"), task.createdAt},
        {QStringLiteral("updatedAt"), task.updatedAt},
        {QStringLiteral("completedAt"), task.completedAt}
    };
    if (!deleteTask(id))
        return {};
    return snapshot;
}

QString TaskModel::restoreTask(const QVariantMap &task)
{
    Task restored;
    restored.id = task.value(QStringLiteral("id")).toString();
    if (restored.id.isEmpty())
        restored.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    restored.title = task.value(QStringLiteral("title")).toString().trimmed();
    if (restored.title.isEmpty())
        return {};
    restored.note = nonNull(task.value(QStringLiteral("note")).toString());
    restored.status = task.value(QStringLiteral("status")).toString();
    if (restored.status != QLatin1String("todo")
        && restored.status != QLatin1String("doing")
        && restored.status != QLatin1String("done")) {
        restored.status = QStringLiteral("todo");
    }
    restored.priority = qBound(0, task.value(QStringLiteral("priority")).toInt(), 3);
    restored.tags = normalizeTags(task.value(QStringLiteral("tags")).toString());
    restored.orderKey = task.value(QStringLiteral("orderKey")).toLongLong();
    if (restored.orderKey == 0)
        restored.orderKey = nextOrderKey(restored.status);
    restored.dueAt = task.value(QStringLiteral("dueAt")).toLongLong();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    restored.createdAt = task.value(QStringLiteral("createdAt")).toLongLong();
    if (restored.createdAt == 0)
        restored.createdAt = now;
    restored.updatedAt = now;
    restored.completedAt = restored.status == QLatin1String("done")
        ? task.value(QStringLiteral("completedAt")).toLongLong() : 0;
    if (restored.status == QLatin1String("done") && restored.completedAt == 0)
        restored.completedAt = now;

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO task (id, title, note, status, priority, tags, order_key, "
        "due_at, created_at, updated_at, completed_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(restored.id);
    query.addBindValue(restored.title);
    query.addBindValue(nonNull(restored.note));
    query.addBindValue(restored.status);
    query.addBindValue(restored.priority);
    query.addBindValue(nonNull(restored.tags));
    query.addBindValue(restored.orderKey);
    query.addBindValue(restored.dueAt);
    query.addBindValue(restored.createdAt);
    query.addBindValue(restored.updatedAt);
    query.addBindValue(restored.completedAt);
    if (!query.exec()) {
        qWarning() << "TaskModel: restore failed:" << query.lastError().text();
        return {};
    }
    load();
    return restored.id;
}

bool TaskModel::moveTask(const QString &id, const QString &status, int newIndex)
{
    const int row = findRow(id);
    if (row < 0)
        return false;

    QString targetStatus = status;
    if (targetStatus != QLatin1String("todo")
        && targetStatus != QLatin1String("doing")
        && targetStatus != QLatin1String("done")) {
        return false;
    }

    QVector<qint64> keys = orderKeys(targetStatus, id);
    const int targetCount = keys.size();
    const int insertAt = qBound(0, newIndex, targetCount);

    qint64 orderKey = 0;
    if (targetCount == 0) {
        orderKey = 1024;
    } else if (insertAt == 0) {
        orderKey = keys.first() - 1024;
    } else if (insertAt == targetCount) {
        orderKey = keys.last() + 1024;
    } else {
        qint64 previous = keys.at(insertAt - 1);
        qint64 next = keys.at(insertAt);
        if (next - previous <= 1) {
            renumberStatus(targetStatus);
            keys = orderKeys(targetStatus, id);
            previous = keys.at(insertAt - 1);
            next = keys.at(insertAt);
        }
        orderKey = previous + (next - previous) / 2;
    }

    Task updated = m_tasks.at(row);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    updated.status = targetStatus;
    updated.orderKey = orderKey;
    updated.updatedAt = now;
    if (targetStatus == QLatin1String("done") && m_tasks.at(row).status != QLatin1String("done"))
        updated.completedAt = now;
    else if (targetStatus != QLatin1String("done"))
        updated.completedAt = 0;

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE task SET status = ?, order_key = ?, updated_at = ?, completed_at = ? "
        "WHERE id = ?"));
    query.addBindValue(updated.status);
    query.addBindValue(updated.orderKey);
    query.addBindValue(updated.updatedAt);
    query.addBindValue(updated.completedAt);
    query.addBindValue(updated.id);

    if (!query.exec()) {
        qWarning() << "TaskModel: move failed:" << query.lastError().text();
        return false;
    }

    m_tasks[row] = updated;
    emit dataChanged(index(row), index(row),
                     { StatusRole, OrderKeyRole, UpdatedAtRole, CompletedAtRole });
    emit countsChanged();
    m_database->refreshStats();
    return true;
}

bool TaskModel::clearDone()
{
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("DELETE FROM task WHERE status = 'done'"))) {
        qWarning() << "TaskModel: clearDone failed:" << query.lastError().text();
        return false;
    }

    beginResetModel();
    for (int i = m_tasks.size() - 1; i >= 0; --i) {
        if (m_tasks.at(i).status == QLatin1String("done"))
            m_tasks.removeAt(i);
    }
    endResetModel();
    emit countsChanged();
    m_database->refreshStats();
    return true;
}

int TaskModel::findRow(const QString &id) const
{
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i).id == id)
            return i;
    }
    return -1;
}

qint64 TaskModel::nextOrderKey(const QString &status) const
{
    qint64 value = 0;
    for (const Task &task : m_tasks) {
        if (task.status == status && task.orderKey > value)
            value = task.orderKey;
    }
    return value > 0 ? value + 1024 : 1024;
}

QVector<qint64> TaskModel::orderKeys(const QString &status, const QString &excludeId) const
{
    QVector<qint64> keys;
    for (const Task &task : m_tasks) {
        if (task.status == status && task.id != excludeId)
            keys.append(task.orderKey);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool TaskModel::renumberStatus(const QString &status)
{
    QVector<int> rows;
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i).status == status)
            rows.append(i);
    }
    std::sort(rows.begin(), rows.end(), [this](int left, int right) {
        return m_tasks.at(left).orderKey < m_tasks.at(right).orderKey;
    });

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.transaction())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE task SET order_key = ? WHERE id = ?"));
    for (int i = 0; i < rows.size(); ++i) {
        const qint64 key = static_cast<qint64>(i + 1) * 1024;
        const int row = rows.at(i);
        query.addBindValue(key);
        query.addBindValue(m_tasks.at(row).id);
        if (!query.exec()) {
            qWarning() << "TaskModel: renumber failed:" << query.lastError().text();
            db.rollback();
            return false;
        }
        m_tasks[row].orderKey = key;
    }
    return db.commit();
}

QString TaskModel::normalizeTags(const QString &tags)
{
    const QString trimmed = tags.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("[]");

    QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8());
    if (document.isArray())
        return QString::fromUtf8(document.toJson(QJsonDocument::Compact));

    QJsonArray array;
    const QStringList parts = trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString value = part.trimmed();
        if (!value.isEmpty())
            array.append(value);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString TaskModel::firstTag(const QString &tags)
{
    const QJsonDocument document = QJsonDocument::fromJson(tags.toUtf8());
    if (document.isArray()) {
        const QJsonArray array = document.array();
        if (!array.isEmpty())
            return array.first().toString();
    }
    return tags;
}

QString TaskModel::dueText(qint64 dueAt)
{
    if (dueAt <= 0)
        return {};
    return QDateTime::fromSecsSinceEpoch(dueAt).toString(QStringLiteral("MM-dd"));
}

QString TaskModel::stageOf(const QString &tags)
{
    const QString tag = firstTag(tags).trimmed();
    static const QRegularExpression pattern(QStringLiteral("^R\\d+$"),
                                            QRegularExpression::CaseInsensitiveOption);
    if (pattern.match(tag).hasMatch())
        return tag.toUpper();
    return QStringLiteral("OTHER");
}
