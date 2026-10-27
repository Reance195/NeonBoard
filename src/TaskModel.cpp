#include "TaskModel.h"

#include "Database.h"
#include "TaskInputParser.h"

#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
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
    case ColumnIdRole:
        return task.columnId;
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
        { ColumnIdRole, "columnId" },
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
            "SELECT id, title, note, status, column_id, priority, tags, order_key, due_at, "
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
            task.columnId = query.value(4).toString();
            task.priority = query.value(5).toInt();
            task.tags = query.value(6).toString();
            task.orderKey = query.value(7).toLongLong();
            task.dueAt = query.value(8).toLongLong();
            task.createdAt = query.value(9).toLongLong();
            task.updatedAt = query.value(10).toLongLong();
            task.completedAt = query.value(11).toLongLong();
            // 极端情况下（老库未回填 / 列被删）按状态回落默认列，保证模型侧永远有列
            if (task.columnId.isEmpty())
                task.columnId = m_database->defaultColumnForStatus(task.status);
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
        {QStringLiteral("columnId"), task.columnId},
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
            {QStringLiteral("columnId"), task.columnId},
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
        task.columnId = m_database->defaultColumnForStatus(task.status);
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
            "INSERT OR REPLACE INTO task (id, title, note, status, column_id, priority, tags,"
            " order_key, due_at, created_at, updated_at, completed_at)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        query.addBindValue(task.id);
        query.addBindValue(task.title);
        query.addBindValue(nonNull(task.note));
        query.addBindValue(task.status);
        query.addBindValue(task.columnId);
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
                           const QString &tags, qint64 dueAt, const QString &columnId)
{
    // 快速录入解析：标题里的日期/时间/标签/优先级标记会被拆出来
    const ParsedTaskInput parsed = TaskInputParser::parse(title, QDateTime::currentDateTime());
    QString cleanTitle = parsed.title.isEmpty() ? title.trimmed() : parsed.title;
    if (cleanTitle.isEmpty())
        return {};

    // 列优先：给了有效列就以列为准（状态由列推导），否则按状态落默认列。
    // 注意 columnStatus() 返回的是列的「状态」（todo/doing/done），不是列 id ——
    // 拿它当列 id 写库会让 column_id 变成 'doing'，卡片从所有列里消失。
    const QString requestedStatus = m_database->columnStatus(columnId);
    QString cleanColumn = requestedStatus.isEmpty() ? QString() : columnId;
    QString cleanStatus = requestedStatus.isEmpty() ? status : requestedStatus;
    if (cleanStatus != QLatin1String("todo")
        && cleanStatus != QLatin1String("doing")
        && cleanStatus != QLatin1String("done")) {
        cleanStatus = QStringLiteral("todo");
        cleanColumn.clear();
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    Task task;
    task.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.title = cleanTitle;
    task.note = QStringLiteral("");
    task.status = cleanStatus;
    task.columnId = cleanColumn.isEmpty() ? m_database->defaultColumnForStatus(cleanStatus)
                                          : cleanColumn;
    task.priority = parsed.priority >= 0 ? parsed.priority : qBound(0, priority, 3);
    task.tags = mergeTags(tags, parsed.tags);
    task.orderKey = nextOrderKey(cleanStatus);
    task.dueAt = parsed.dueAt > 0 ? parsed.dueAt : dueAt;
    task.createdAt = now;
    task.updatedAt = now;
    task.completedAt = cleanStatus == QLatin1String("done") ? now : 0;

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO task (id, title, note, status, column_id, priority, tags, order_key, due_at, "
        "created_at, updated_at, completed_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(task.id);
    query.addBindValue(task.title);
    query.addBindValue(nonNull(task.note));
    query.addBindValue(task.status);
    query.addBindValue(task.columnId);
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
    emit taskCreated(task.id, task.title);
    if (task.status == QLatin1String("done"))
        emit taskCompleted(task.id);
    return task.id;
}

bool TaskModel::updateTask(const QString &id, const QVariantMap &fields)
{
    const int row = findRow(id);
    if (row < 0)
        return false;

    Task updated = m_tasks.at(row);
    const QString previousStatus = updated.status;
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

    // 列归属：显式给列时以列为准（状态跟着列走），否则保证状态与列自洽 ——
    // 只改 status 不搬列会留下「doing 的卡挂在待办列」的撕裂数据。
    if (fields.contains(QStringLiteral("columnId"))) {
        const QString requested = fields.value(QStringLiteral("columnId")).toString();
        const QString requestedStatus = m_database->columnStatus(requested);
        if (!requestedStatus.isEmpty()) {
            if (requestedStatus != updated.status) {
                updated.status = requestedStatus;
                updated.orderKey = nextOrderKey(requestedStatus);
                updated.completedAt = requestedStatus == QLatin1String("done")
                    ? QDateTime::currentSecsSinceEpoch()
                    : 0;
            }
            updated.columnId = requested;
        }
    }
    updated.columnId = columnForStatus(updated.columnId, updated.status);

    updated.updatedAt = QDateTime::currentSecsSinceEpoch();

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE task SET title = ?, note = ?, status = ?, column_id = ?, priority = ?, "
        "tags = ?, order_key = ?, due_at = ?, updated_at = ?, completed_at = ? WHERE id = ?"));
    query.addBindValue(updated.title);
    query.addBindValue(nonNull(updated.note));
    query.addBindValue(updated.status);
    query.addBindValue(updated.columnId);
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
    emit taskUpdated(updated.id);
    if (previousStatus != updated.status) {
        emit taskMoved(updated.id, previousStatus, updated.status);
        if (updated.status == QLatin1String("done"))
            emit taskCompleted(updated.id);
    }
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
    emit taskRemoved(id);
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
    restored.columnId = m_database->defaultColumnForStatus(restored.status);
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
        "INSERT OR REPLACE INTO task (id, title, note, status, column_id, priority, tags,"
        " order_key, due_at, created_at, updated_at, completed_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(restored.id);
    query.addBindValue(restored.title);
    query.addBindValue(nonNull(restored.note));
    query.addBindValue(restored.status);
    query.addBindValue(restored.columnId);
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
    emit taskCreated(restored.id, restored.title);
    return restored.id;
}

bool TaskModel::restorePlacement(const QString &id, const QString &status,
                                 qint64 orderKey, qint64 completedAt, const QString &columnId)
{
    const int row = findRow(id);
    if (row < 0)
        return false;

    QString target = status;
    if (target != QLatin1String("todo")
        && target != QLatin1String("doing")
        && target != QLatin1String("done")) {
        target = QStringLiteral("todo");
    }

    // 撤销记录里带列就搬回原列；没带（老记录）则保持当前列，仅当列与状态不匹配才落默认列
    QString targetColumn;
    if (!columnId.isEmpty() && m_database->columnStatus(columnId) == target)
        targetColumn = columnId;
    else
        targetColumn = columnForStatus(m_tasks.at(row).columnId, target);

    return commitMove(row, targetColumn, target,
                      orderKey > 0 ? orderKey : nextOrderKey(target), completedAt);
}

bool TaskModel::restoreSnapshot(const QVariantMap &snapshot)
{
    const QString id = snapshot.value(QStringLiteral("id")).toString();
    const int row = findRow(id);
    if (row < 0)
        return false;

    Task updated = m_tasks.at(row);
    updated.title = snapshot.value(QStringLiteral("title")).toString().trimmed();
    if (updated.title.isEmpty())
        return false;
    updated.note = nonNull(snapshot.value(QStringLiteral("note")).toString());
    updated.priority = qBound(0, snapshot.value(QStringLiteral("priority")).toInt(), 3);
    updated.tags = normalizeTags(snapshot.value(QStringLiteral("tags")).toString());
    updated.dueAt = snapshot.value(QStringLiteral("dueAt")).toLongLong();
    updated.orderKey = snapshot.value(QStringLiteral("orderKey")).toLongLong();
    if (updated.orderKey <= 0)
        updated.orderKey = nextOrderKey(updated.status);
    updated.completedAt = updated.status == QLatin1String("done")
        ? snapshot.value(QStringLiteral("completedAt")).toLongLong() : 0;
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
        qWarning() << "TaskModel: restoreSnapshot failed:" << query.lastError().text();
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
    emit taskUpdated(updated.id);
    return true;
}

bool TaskModel::moveTaskToColumn(const QString &id, const QString &columnId)
{
    const int row = findRow(id);
    if (row < 0)
        return false;

    const QString targetStatus = m_database->columnStatus(columnId);
    if (targetStatus.isEmpty())
        return false;

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());

    // 列内尾部排序键：按 column_id 取，同状态的多个列各排各的
    qint64 lastKey = 0;
    {
        QSqlQuery keys(db);
        keys.prepare(QStringLiteral(
            "SELECT COALESCE(MAX(order_key), 0) FROM task WHERE column_id = ? AND id <> ?"));
        keys.addBindValue(columnId);
        keys.addBindValue(id);
        if (keys.exec() && keys.next())
            lastKey = keys.value(0).toLongLong();
    }

    // 已经是 done 卡再搬列：保留原完成时间，不该被刷成现在
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 completedAt = (targetStatus == QLatin1String("done") && m_tasks.at(row).completedAt > 0)
            ? m_tasks.at(row).completedAt
            : now;
    return commitMove(row, columnId, targetStatus, lastKey + 1024, completedAt);
}

bool TaskModel::moveTaskBeforeColumn(const QString &id, const QString &columnId,
                                     const QString &beforeId)
{
    const int row = findRow(id);
    if (row < 0)
        return false;

    const QString targetStatus = m_database->columnStatus(columnId);
    if (targetStatus.isEmpty())
        return false;

    // 列内排序键（排除自身）—— 同状态的多个列各排各的
    QVector<qint64> keys = orderKeysInColumn(columnId, id);
    int insertAt = keys.size();
    if (!beforeId.isEmpty() && beforeId != id) {
        const int beforeRow = findRow(beforeId);
        // 落点卡不在本列：退化为落到列尾，绝不跨列插位
        if (beforeRow >= 0 && m_tasks.at(beforeRow).columnId == columnId) {
            const qint64 beforeKey = m_tasks.at(beforeRow).orderKey;
            insertAt = 0;
            while (insertAt < keys.size() && keys.at(insertAt) < beforeKey)
                ++insertAt;
        }
    }

    qint64 orderKey = 0;
    if (keys.isEmpty()) {
        orderKey = 1024;
    } else if (insertAt <= 0) {
        orderKey = keys.first() - 1024;
    } else if (insertAt >= keys.size()) {
        orderKey = keys.last() + 1024;
    } else {
        qint64 previous = keys.at(insertAt - 1);
        qint64 next = keys.at(insertAt);
        if (next - previous <= 1) {
            renumberColumn(columnId);
            keys = orderKeysInColumn(columnId, id);
            previous = keys.at(insertAt - 1);
            next = keys.at(insertAt);
        }
        orderKey = previous + (next - previous) / 2;
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 completedAt = (targetStatus == QLatin1String("done") && m_tasks.at(row).completedAt > 0)
            ? m_tasks.at(row).completedAt
            : now;
    return commitMove(row, columnId, targetStatus, orderKey, completedAt);
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

    // 状态变了但卡片还挂在别的列上时，把列一起搬到该状态的默认列。
    // 否则会出现「status=doing、column_id=col-todo」的撕裂状态：
    // 卡片继续留在待办列显示，而按状态统计已经算成进行中。
    const QString targetColumn = columnForStatus(m_tasks.at(row).columnId, targetStatus);

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 completedAt = m_tasks.at(row).completedAt;
    if (targetStatus == QLatin1String("done"))
        completedAt = completedAt > 0 ? completedAt : now;
    else
        completedAt = 0;

    return commitMove(row, targetColumn, targetStatus, orderKey, completedAt);
}

bool TaskModel::commitMove(int row, const QString &columnId, const QString &status,
                           qint64 orderKey, qint64 completedAt)
{
    if (row < 0 || row >= m_tasks.size())
        return false;

    Task updated = m_tasks.at(row);
    const QString previousStatus = updated.status;
    updated.status = status;
    updated.columnId = columnId;
    updated.orderKey = orderKey;
    updated.updatedAt = QDateTime::currentSecsSinceEpoch();
    updated.completedAt = status == QLatin1String("done") ? completedAt : 0;

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE task SET status = ?, column_id = ?, order_key = ?, updated_at = ?, "
        "completed_at = ? WHERE id = ?"));
    query.addBindValue(updated.status);
    query.addBindValue(updated.columnId);
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
                     { StatusRole, ColumnIdRole, OrderKeyRole, UpdatedAtRole, CompletedAtRole });
    emit countsChanged();
    m_database->refreshStats();
    emit taskUpdated(updated.id);
    if (previousStatus != updated.status)
        emit taskMoved(updated.id, previousStatus, updated.status);
    // 进 done 列统一在这里发完成信号：按状态移动与按列移动走同一条路径，
    // 否则「拖进已完成列」不会触发完成提醒 / 重复任务派生。
    if (updated.status == QLatin1String("done") && previousStatus != QLatin1String("done"))
        emit taskCompleted(updated.id);
    return true;
}

QString TaskModel::columnForStatus(const QString &columnId, const QString &status) const
{
    // 列仍匹配目标状态就原地不动（自定义列不该被搬回默认列），否则落默认列
    if (!columnId.isEmpty() && m_database->columnStatus(columnId) == status)
        return columnId;
    return m_database->defaultColumnForStatus(status);
}

bool TaskModel::moveTaskBefore(const QString &id, const QString &status, const QString &beforeId)
{
    // beforeId 为空表示落到列尾（注意 moveTask 的 -1 语义是列首，列尾要用 moveTaskToEnd）
    if (beforeId.isEmpty() || beforeId == id)
        return beforeId.isEmpty() ? moveTaskToEnd(id, status) : false;

    const int beforeRow = findRow(beforeId);
    if (beforeRow < 0)
        return moveTaskToEnd(id, status);
    if (m_tasks.at(beforeRow).status != status)
        return moveTaskToEnd(id, status);

    // 在「排除自身」的有序 order_key 序列里定位 before 的位置，
    // 交给 moveTask 取前后中点作为新的 order_key
    const qint64 beforeKey = m_tasks.at(beforeRow).orderKey;
    const QVector<qint64> keys = orderKeys(status, id);
    int index = 0;
    while (index < keys.size() && keys.at(index) < beforeKey)
        ++index;
    return moveTask(id, status, index);
}

QStringList TaskModel::taskIdsInStatus(const QString &status) const
{
    // 必须与 TaskFilterModel 的排序规则一致（order_key 升序，id 兜底），
    // 否则界面里的落点计算会与实际显示顺序错位
    QVector<Task> items;
    for (const Task &task : m_tasks) {
        if (task.status == status)
            items.append(task);
    }
    std::sort(items.begin(), items.end(), [](const Task &left, const Task &right) {
        if (left.orderKey != right.orderKey)
            return left.orderKey < right.orderKey;
        return left.id < right.id;
    });

    QStringList ids;
    ids.reserve(items.size());
    for (const Task &task : items)
        ids.append(task.id);
    return ids;
}

QStringList TaskModel::taskIdsInColumn(const QString &columnId) const
{
    // 与 taskIdsInStatus 同样的排序规则，只是按 column_id 而不是 status 取 ——
    // 同状态的多个列必须各排各的，否则拖拽落点会算到隔壁列去。
    QVector<Task> items;
    for (const Task &task : m_tasks) {
        if (task.columnId == columnId)
            items.append(task);
    }
    std::sort(items.begin(), items.end(), [](const Task &left, const Task &right) {
        if (left.orderKey != right.orderKey)
            return left.orderKey < right.orderKey;
        return left.id < right.id;
    });

    QStringList ids;
    ids.reserve(items.size());
    for (const Task &task : items)
        ids.append(task.id);
    return ids;
}

QVector<qint64> TaskModel::orderKeysInColumn(const QString &columnId,
                                            const QString &excludeId) const
{
    QVector<qint64> keys;
    for (const Task &task : m_tasks) {
        if (task.columnId == columnId && task.id != excludeId)
            keys.append(task.orderKey);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool TaskModel::renumberColumn(const QString &columnId)
{
    QVector<int> rows;
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i).columnId == columnId)
            rows.append(i);
    }
    std::sort(rows.begin(), rows.end(), [this](int left, int right) {
        if (m_tasks.at(left).orderKey != m_tasks.at(right).orderKey)
            return m_tasks.at(left).orderKey < m_tasks.at(right).orderKey;
        return m_tasks.at(left).id < m_tasks.at(right).id;
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
            qWarning() << "TaskModel: renumberColumn failed:" << query.lastError().text();
            db.rollback();
            return false;
        }
        m_tasks[row].orderKey = key;
    }
    return db.commit();
}

bool TaskModel::moveTaskToEnd(const QString &id, const QString &status)
{
    if (findRow(id) < 0)
        return false;

    const int targetCount = orderKeys(status, id).size();
    return moveTask(id, status, targetCount);
}

bool TaskModel::clearDone()
{
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("DELETE FROM task WHERE status = 'done'"))) {
        qWarning() << "TaskModel: clearDone failed:" << query.lastError().text();
        return false;
    }

    QStringList removedIds;
    for (const Task &task : m_tasks) {
        if (task.status == QLatin1String("done"))
            removedIds.append(task.id);
    }

    beginResetModel();
    for (int i = m_tasks.size() - 1; i >= 0; --i) {
        if (m_tasks.at(i).status == QLatin1String("done"))
            m_tasks.removeAt(i);
    }
    endResetModel();
    emit countsChanged();
    m_database->refreshStats();
    for (const QString &removedId : removedIds)
        emit taskRemoved(removedId);
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

QString TaskModel::mergeTags(const QString &baseTags, const QStringList &extraTags)
{
    QJsonArray array;
    QSet<QString> seen;

    const QJsonDocument document = QJsonDocument::fromJson(normalizeTags(baseTags).toUtf8());
    if (document.isArray()) {
        const QJsonArray base = document.array();
        for (const QJsonValue &value : base) {
            const QString tag = value.toString().trimmed();
            if (tag.isEmpty() || seen.contains(tag))
                continue;
            seen.insert(tag);
            array.append(tag);
        }
    }
    for (const QString &raw : extraTags) {
        const QString tag = raw.trimmed();
        if (tag.isEmpty() || seen.contains(tag))
            continue;
        seen.insert(tag);
        array.append(tag);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVariantMap TaskModel::previewInput(const QString &text) const
{
    const ParsedTaskInput parsed = TaskInputParser::parse(text, QDateTime::currentDateTime());
    QVariantMap result;
    result.insert(QStringLiteral("title"),
                  parsed.title.isEmpty() ? text.trimmed() : parsed.title);
    result.insert(QStringLiteral("tags"), parsed.tags);
    result.insert(QStringLiteral("priority"), parsed.priority);
    result.insert(QStringLiteral("dueAt"), parsed.dueAt);
    result.insert(QStringLiteral("dueText"),
                  parsed.dueAt > 0 ? QDateTime::fromSecsSinceEpoch(parsed.dueAt)
                                        .toString(QStringLiteral("MM-dd HH:mm"))
                                  : QString());
    result.insert(QStringLiteral("hasDue"), parsed.dueAt > 0);
    return result;
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
    // 阶段名是「短号前缀」而不是卡号：R4 / DX / OPS 都算阶段，但 R4-10 这种具体卡号不算
    // （卡归它所属的阶段，不该每张卡各成一个阶段）。认不出的一律落 OTHER，
    // 由 stageList() 统一折进「其他」并给出可对账的计数，不再静默吞掉。
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z]{1,4}\\d*$"),
                                            QRegularExpression::CaseInsensitiveOption);
    if (pattern.match(tag).hasMatch())
        return tag.toUpper();
    return QStringLiteral("OTHER");
}

QVariantList TaskModel::stageList(const QStringList &stageOrder) const
{
    const QVariantMap stats = stageStats();
    const int totalAll = m_tasks.size();

    QVariantList list;
    QStringList shown;
    int shownTotal = 0;

    // 1) 按调用方给的顺序（界面侧取自路线图 §9 的行序）输出：
    //    §9 表里加一行，阶段条就自动多一个 chip，不用改 QML。
    for (const QString &key : stageOrder) {
        if (key.isEmpty() || shown.contains(key))
            continue;
        shown.append(key);

        const QVariantMap stat = stats.value(key).toMap();
        const int total = stat.value(QStringLiteral("total")).toInt();
        shownTotal += total;

        QVariantMap item;
        item.insert(QStringLiteral("key"), key);
        // 标签就用阶段短号本身：§9 的「主题」列是长句，塞不进 86px 的 chip
        item.insert(QStringLiteral("label"), key);
        item.insert(QStringLiteral("total"), total);
        item.insert(QStringLiteral("done"), stat.value(QStringLiteral("done")).toInt());
        item.insert(QStringLiteral("doing"), stat.value(QStringLiteral("doing")).toInt());
        item.insert(QStringLiteral("todo"), stat.value(QStringLiteral("todo")).toInt());
        list.append(item);
    }

    // 2) 兜底桶：不在 §9 里的阶段标签（临时 tag、认不出的写法）全部折进「其他」。
    //    计数 = 全部任务数 − 已显示各阶段之和 —— 这就是验收第 3 条要的「可对账」：
    //    只要还有卡没上阶段条，其他桶就会把它们兜住，不会凭空消失。
    const int otherTotal = totalAll - shownTotal;
    if (otherTotal > 0) {
        int done = 0, doing = 0, todo = 0;
        for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) {
            if (shown.contains(it.key()))
                continue;
            const QVariantMap stat = it.value().toMap();
            done += stat.value(QStringLiteral("done")).toInt();
            doing += stat.value(QStringLiteral("doing")).toInt();
            todo += stat.value(QStringLiteral("todo")).toInt();
        }
        QVariantMap item;
        item.insert(QStringLiteral("key"), QStringLiteral("OTHER"));
        item.insert(QStringLiteral("label"), QStringLiteral("其他"));
        item.insert(QStringLiteral("total"), otherTotal);
        item.insert(QStringLiteral("done"), done);
        item.insert(QStringLiteral("doing"), doing);
        item.insert(QStringLiteral("todo"), todo);
        list.append(item);
    }
    return list;
}
