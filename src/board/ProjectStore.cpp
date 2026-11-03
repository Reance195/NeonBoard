#include "ProjectStore.h"

#include "Database.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

ProjectStore::ProjectStore(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
{
}

int ProjectStore::revision() const
{
    return m_revision;
}

void ProjectStore::bump()
{
    // 任何归属/项目变更都从这里过，缓存失效只需挂一处 ——
    // 散在各个写方法里迟早漏一个，然后卡片角标就长期显示旧项目。
    m_taskProjectCache.clear();
    ++m_revision;
    emit changed();
}

int ProjectStore::nextOrderKey() const
{
    if (!m_database)
        return 1024;
    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    if (query.exec(QStringLiteral("SELECT COALESCE(MAX(order_key), 0) + 1024 FROM project"))
        && query.next()) {
        return query.value(0).toInt();
    }
    return 1024;
}

QVariantList ProjectStore::projects() const
{
    QVariantList list;
    if (!m_database)
        return list;

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    // 计数走 JOIN task：task_project 里可能有已删任务留下的孤儿行，不该算进项目卡片数。
    if (!query.exec(QStringLiteral(
            "SELECT p.id, p.name, p.color, p.order_key,"
            " (SELECT COUNT(*) FROM task_project tp JOIN task t ON t.id = tp.task_id"
            "   WHERE tp.project_id = p.id) "
            "FROM project p ORDER BY p.order_key, p.name"))) {
        qWarning() << "ProjectStore: 读取项目清单失败" << query.lastError().text();
        return list;
    }
    while (query.next()) {
        list.append(QVariantMap{
            {QStringLiteral("id"), query.value(0).toString()},
            {QStringLiteral("name"), query.value(1).toString()},
            {QStringLiteral("color"), query.value(2).toString()},
            {QStringLiteral("orderKey"), query.value(3).toLongLong()},
            {QStringLiteral("taskCount"), query.value(4).toInt()},
        });
    }
    return list;
}

QString ProjectStore::createProject(const QString &name, const QString &color)
{
    const QString trimmed = name.trimmed();
    if (!m_database || trimmed.isEmpty())
        return QString();

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO project (id, name, color, order_key, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(id);
    query.addBindValue(trimmed);
    // QString() 是 null，绑进 SQL 就是 NULL —— project.color 是 NOT NULL DEFAULT ''，
    // 不转空串会直接撞约束。调用方不传颜色是最常见的用法，不能让它失败。
    query.addBindValue(color.isNull() ? QStringLiteral("") : color);
    query.addBindValue(nextOrderKey());
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        qWarning() << "ProjectStore: 新建项目失败" << query.lastError().text();
        return QString();
    }

    bump();
    return id;
}

QString ProjectStore::ensureProject(const QString &name, const QString &color)
{
    const QString trimmed = name.trimmed();
    if (!m_database || trimmed.isEmpty())
        return QString();

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM project WHERE name = ? LIMIT 1"));
    query.addBindValue(trimmed);
    if (query.exec() && query.next())
        return query.value(0).toString();

    return createProject(trimmed, color);
}

bool ProjectStore::renameProject(const QString &id, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!m_database || id.isEmpty() || trimmed.isEmpty())
        return false;

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE project SET name = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(trimmed);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() <= 0)
        return false;

    bump();
    return true;
}

bool ProjectStore::setProjectColor(const QString &id, const QString &color)
{
    if (!m_database || id.isEmpty())
        return false;

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE project SET color = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(color.isNull() ? QStringLiteral("") : color);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() <= 0)
        return false;

    bump();
    return true;
}

bool ProjectStore::removeProject(const QString &id)
{
    if (!m_database || id.isEmpty())
        return false;

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    // 只清归属、不删卡：卡片是人的数据，项目只是它的一层分类。
    // 两步放同一事务，避免「归属清了、项目还在」或反过来的半截状态。
    if (!db.transaction()) {
        qWarning() << "ProjectStore: 开启事务失败" << db.lastError().text();
        return false;
    }

    QSqlQuery dropAssignments(db);
    dropAssignments.prepare(QStringLiteral("DELETE FROM task_project WHERE project_id = ?"));
    dropAssignments.addBindValue(id);
    if (!dropAssignments.exec()) {
        qWarning() << "ProjectStore: 清理归属失败" << dropAssignments.lastError().text();
        db.rollback();
        return false;
    }

    QSqlQuery dropProject(db);
    dropProject.prepare(QStringLiteral("DELETE FROM project WHERE id = ?"));
    dropProject.addBindValue(id);
    if (!dropProject.exec()) {
        qWarning() << "ProjectStore: 删除项目失败" << dropProject.lastError().text();
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qWarning() << "ProjectStore: 提交失败" << db.lastError().text();
        db.rollback();
        return false;
    }

    bump();
    return true;
}

QString ProjectStore::projectOfTask(const QString &taskId) const
{
    if (!m_database || taskId.isEmpty())
        return QString();

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT project_id FROM task_project WHERE task_id = ? LIMIT 1"));
    query.addBindValue(taskId);
    if (query.exec() && query.next())
        return query.value(0).toString();
    return QString();
}

bool ProjectStore::assignTask(const QString &taskId, const QString &projectId)
{
    const QString tid = taskId.trimmed();
    if (!m_database || tid.isEmpty())
        return false;

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    const QString pid = projectId.trimmed();

    // 空项目 = 解除归属：删行而不是写空串，保持「查不到即未归属」这一条约定。
    if (pid.isEmpty()) {
        QSqlQuery drop(db);
        drop.prepare(QStringLiteral("DELETE FROM task_project WHERE task_id = ?"));
        drop.addBindValue(tid);
        if (!drop.exec()) {
            qWarning() << "ProjectStore: 解除归属失败" << drop.lastError().text();
            return false;
        }
        bump();
        return true;
    }

    // 项目必须存在：否则归属指向幽灵行，看板上只显示一个查不到名字的 id。
    QSqlQuery exists(db);
    exists.prepare(QStringLiteral("SELECT 1 FROM project WHERE id = ? LIMIT 1"));
    exists.addBindValue(pid);
    if (!exists.exec() || !exists.next()) {
        qWarning() << "ProjectStore: 项目不存在，拒绝归属" << pid;
        return false;
    }

    // 一卡一项目：task_id 是主键，REPLACE 直接覆盖，不会留旧行。
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO task_project (task_id, project_id, created_at) "
        "VALUES (?, ?, ?)"));
    query.addBindValue(tid);
    query.addBindValue(pid);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!query.exec()) {
        qWarning() << "ProjectStore: 写入归属失败" << query.lastError().text();
        return false;
    }

    bump();
    return true;
}

int ProjectStore::taskCount(const QString &projectId) const
{
    if (!m_database || projectId.isEmpty())
        return 0;

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM task_project tp JOIN task t ON t.id = tp.task_id "
        "WHERE tp.project_id = ?"));
    query.addBindValue(projectId);
    if (query.exec() && query.next())
        return query.value(0).toInt();
    return 0;
}

int ProjectStore::unassignedCount() const
{
    if (!m_database)
        return 0;

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    if (query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM task t "
            "LEFT JOIN task_project tp ON tp.task_id = t.id "
            "WHERE tp.task_id IS NULL"))
        && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

QStringList ProjectStore::taskIdsOf(const QString &projectId) const
{
    QStringList ids;
    if (!m_database || projectId.isEmpty())
        return ids;

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    // JOIN task：task_project 里可能有已删任务留下的孤儿行，让它们不出现在任何视图里
    query.prepare(QStringLiteral(
        "SELECT tp.task_id FROM task_project tp JOIN task t ON t.id = tp.task_id "
        "WHERE tp.project_id = ?"));
    query.addBindValue(projectId);
    if (!query.exec()) {
        qWarning() << "ProjectStore: 读取项目卡片失败" << query.lastError().text();
        return ids;
    }
    while (query.next())
        ids.append(query.value(0).toString());
    return ids;
}

QVariantMap ProjectStore::taskProject(const QString &taskId) const
{
    if (!m_database || taskId.isEmpty())
        return QVariantMap();

    const auto cached = m_taskProjectCache.constFind(taskId);
    if (cached != m_taskProjectCache.constEnd())
        return cached.value();

    QVariantMap info;
    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT p.id, p.name, p.color FROM task_project tp "
        "JOIN project p ON p.id = tp.project_id WHERE tp.task_id = ? LIMIT 1"));
    query.addBindValue(taskId);
    if (query.exec() && query.next()) {
        info = QVariantMap{
            {QStringLiteral("id"), query.value(0).toString()},
            {QStringLiteral("name"), query.value(1).toString()},
            {QStringLiteral("color"), query.value(2).toString()},
        };
    }
    // 未归属也缓存（空 map）：绝大多数卡在项目视图外，每次都查库没必要。
    m_taskProjectCache.insert(taskId, info);
    return info;
}

void ProjectStore::forgetTask(const QString &taskId)
{
    m_taskProjectCache.remove(taskId);
    if (!m_database || taskId.isEmpty())
        return;

    QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM task_project WHERE task_id = ?"));
    query.addBindValue(taskId);
    if (!query.exec()) {
        qWarning() << "ProjectStore: 清理归属失败" << query.lastError().text();
        return;
    }
    // 只有真删掉行才递增：没有归属的卡被删时不该惊动整个视图。
    if (query.numRowsAffected() > 0)
        bump();
}
