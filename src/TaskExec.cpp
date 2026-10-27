#include "TaskExec.h"

#include "Database.h"
#include "TaskModel.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

QString encodeList(const QVariant &value)
{
    QJsonArray array;
    const QVariantList list = value.toList();
    for (const QVariant &item : list) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty())
            array.append(text);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVariantList decodeList(const QString &text)
{
    QVariantList out;
    if (text.isEmpty())
        return out;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isArray())
        return out;
    for (const QJsonValue &item : doc.array()) {
        if (item.isString())
            out.append(item.toString());
    }
    return out;
}

// 允许的 workflow 名（与 docs/product-design.md §7 的预设一致）
bool isKnownWorkflow(const QString &name)
{
    static const QStringList known = {
        QStringLiteral("fix"), QStringLiteral("feature"), QStringLiteral("hotfix"),
        QStringLiteral("docs"), QStringLiteral("review"), QStringLiteral("delivery"),
    };
    return known.contains(name);
}

} // namespace

TaskExec::TaskExec(Database *database, TaskModel *tasks, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_tasks(tasks)
{
    if (m_tasks) {
        connect(m_tasks, &TaskModel::taskRemoved, this, [this](const QString &id) {
            removeRowFor(id);
        });
    }
}

int TaskExec::revision() const
{
    return m_revision;
}

void TaskExec::bump()
{
    ++m_revision;
    emit changed();
}

QVariantMap TaskExec::emptyMap() const
{
    return {
        {QStringLiteral("goal"), QString()},
        {QStringLiteral("acceptance"), QVariantList()},
        {QStringLiteral("scope"), QVariantList()},
        {QStringLiteral("agent"), QString()},
        {QStringLiteral("cwd"), QString()},
        {QStringLiteral("workflow"), QString()},
        {QStringLiteral("timeoutMin"), 30},
        {QStringLiteral("maxAttempts"), 1},
    };
}

QVariantMap TaskExec::execOf(const QString &taskId) const
{
    QVariantMap out = emptyMap();
    if (!m_database || taskId.isEmpty())
        return out;

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return out;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT goal, acceptance, scope, agent, cwd, workflow, timeout_min, max_attempts "
        "FROM task_exec WHERE task_id = ?"));
    query.addBindValue(taskId);
    if (!query.exec() || !query.next())
        return out;

    out.insert(QStringLiteral("goal"), query.value(0).toString());
    out.insert(QStringLiteral("acceptance"), decodeList(query.value(1).toString()));
    out.insert(QStringLiteral("scope"), decodeList(query.value(2).toString()));
    out.insert(QStringLiteral("agent"), query.value(3).toString());
    out.insert(QStringLiteral("cwd"), query.value(4).toString());
    out.insert(QStringLiteral("workflow"), query.value(5).toString());
    out.insert(QStringLiteral("timeoutMin"), query.value(6).toInt());
    out.insert(QStringLiteral("maxAttempts"), query.value(7).toInt());
    return out;
}

bool TaskExec::setExec(const QString &taskId, const QVariantMap &fields)
{
    if (!m_database || taskId.isEmpty())
        return false;
    if (!m_tasks || m_tasks->taskById(taskId).isEmpty())
        return false;

    const QVariantMap current = execOf(taskId);
    const QString goal = fields.contains(QStringLiteral("goal"))
        ? fields.value(QStringLiteral("goal")).toString().trimmed()
        : current.value(QStringLiteral("goal")).toString();
    const QVariantList acceptance = fields.contains(QStringLiteral("acceptance"))
        ? fields.value(QStringLiteral("acceptance")).toList()
        : current.value(QStringLiteral("acceptance")).toList();
    const QVariantList scope = fields.contains(QStringLiteral("scope"))
        ? fields.value(QStringLiteral("scope")).toList()
        : current.value(QStringLiteral("scope")).toList();
    const QString agent = fields.contains(QStringLiteral("agent"))
        ? fields.value(QStringLiteral("agent")).toString().trimmed()
        : current.value(QStringLiteral("agent")).toString();
    const QString cwd = fields.contains(QStringLiteral("cwd"))
        ? fields.value(QStringLiteral("cwd")).toString().trimmed()
        : current.value(QStringLiteral("cwd")).toString();
    const QString workflow = fields.contains(QStringLiteral("workflow"))
        ? fields.value(QStringLiteral("workflow")).toString().trimmed()
        : current.value(QStringLiteral("workflow")).toString();
    int timeoutMin = fields.contains(QStringLiteral("timeoutMin"))
        ? fields.value(QStringLiteral("timeoutMin")).toInt()
        : current.value(QStringLiteral("timeoutMin")).toInt();
    int maxAttempts = fields.contains(QStringLiteral("maxAttempts"))
        ? fields.value(QStringLiteral("maxAttempts")).toInt()
        : current.value(QStringLiteral("maxAttempts")).toInt();

    if (goal.size() > 4000)
        return false;
    if (!workflow.isEmpty() && !isKnownWorkflow(workflow))
        return false;
    if (timeoutMin < 1 || timeoutMin > 480)
        return false;
    if (maxAttempts < 1 || maxAttempts > 5)
        return false;
    // acceptance / scope 必须是字符串列表
    for (const QVariant &item : acceptance) {
        if (!item.canConvert<QString>() || item.toString().trimmed().isEmpty())
            return false;
    }
    for (const QVariant &item : scope) {
        if (!item.canConvert<QString>() || item.toString().trimmed().isEmpty())
            return false;
    }

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO task_exec (task_id, goal, acceptance, scope, agent, cwd, workflow, "
        "                      timeout_min, max_attempts, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(task_id) DO UPDATE SET "
        "  goal = excluded.goal, acceptance = excluded.acceptance, scope = excluded.scope, "
        "  agent = excluded.agent, cwd = excluded.cwd, workflow = excluded.workflow, "
        "  timeout_min = excluded.timeout_min, max_attempts = excluded.max_attempts, "
        "  updated_at = excluded.updated_at"));
    query.addBindValue(taskId);
    // Qt 把 null QString 绑成 SQL NULL，而侧表列是 NOT NULL；统一收敛成空串
    query.addBindValue(goal.isNull() ? QStringLiteral("") : goal);
    query.addBindValue(encodeList(acceptance));
    query.addBindValue(encodeList(scope));
    query.addBindValue(agent.isNull() ? QStringLiteral("") : agent);
    query.addBindValue(cwd.isNull() ? QStringLiteral("") : cwd);
    query.addBindValue(workflow.isNull() ? QStringLiteral("") : workflow);
    query.addBindValue(timeoutMin);
    query.addBindValue(maxAttempts);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!query.exec()) {
        qWarning() << "TaskExec: setExec failed:" << query.lastError().text();
        return false;
    }
    bump();
    return true;
}

bool TaskExec::clearExec(const QString &taskId)
{
    if (!m_database || taskId.isEmpty())
        return false;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM task_exec WHERE task_id = ?"));
    query.addBindValue(taskId);
    if (!query.exec())
        return false;
    bump();
    return true;
}

void TaskExec::removeRowFor(const QString &taskId)
{
    if (taskId.isEmpty())
        return;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return;
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM task_exec WHERE task_id = ?"));
    query.addBindValue(taskId);
    query.exec();
    bump();
}
