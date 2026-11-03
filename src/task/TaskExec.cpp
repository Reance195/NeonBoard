#include "TaskExec.h"

#include "Database.h"
#include "TaskModel.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

// R7-03：步骤覆盖项存 meta（不往 task_exec 加列 —— 侧表加列要走 Database::ensureColumn，
// 而 R7-03 的 scope 不含 Database.cpp；meta 同样不升 user_version）。
QString stepsKey(const QString &taskId)
{
    return QStringLiteral("exec.workflowSteps.") + taskId;
}

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
    // cwd 允许为空（旧卡尚未指定工作目录）；非空时必须是存在的绝对目录，
    // 规范化后不得残留 ".." —— 执行器绝不能在未确认的目录里跑 agent。
    QString normalizedCwd = QStringLiteral("");
    if (!cwd.isEmpty()) {
        normalizedCwd = QDir::cleanPath(cwd);
        const QStringList parts = normalizedCwd.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (!QDir::isAbsolutePath(normalizedCwd)
            || parts.contains(QStringLiteral(".."))
            || !QFileInfo(normalizedCwd).isDir()) {
            return false;
        }
    }
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
    query.addBindValue(normalizedCwd);
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
    // R7-03：步骤覆盖项跟着一起清，别留下指向已删卡的 meta 垃圾
    m_database->removeMetaValue(stepsKey(taskId));
    bump();
}

// ---- 角色链步骤数组（R7-03）----

QVariantList TaskExec::presetSteps(const QString &workflow)
{
    // 与 docs/product-design.md §7 的预设一致；顺序就是接力顺序。
    const QString name = workflow.trimmed().toLower();
    auto step = [](const QString &role, const QString &prompt) {
        return QVariantMap{
            {QStringLiteral("role"), role},
            {QStringLiteral("agent"), QString()},
            {QStringLiteral("prompt"), prompt},
            {QStringLiteral("acceptance"), QVariantList()},
            {QStringLiteral("scope"), QVariantList()},
        };
    };

    if (name == QLatin1String("fix")) {
        return {step(QStringLiteral("engineer"), QStringLiteral("按卡片定义修复问题")),
                step(QStringLiteral("qa"), QStringLiteral("复核修复结果与回归风险"))};
    }
    if (name == QLatin1String("feature")) {
        return {step(QStringLiteral("pm"), QStringLiteral("把需求拆成可交付的验收项")),
                step(QStringLiteral("architect"), QStringLiteral("给出实现方案与改动边界")),
                step(QStringLiteral("engineer"), QStringLiteral("按方案实现")),
                step(QStringLiteral("qa"), QStringLiteral("验收实现与边界"))};
    }
    if (name == QLatin1String("hotfix")) {
        // 单步修复：跳过评审，但 qa 仍然要跑（验收不能被省掉）
        return {step(QStringLiteral("engineer"), QStringLiteral("紧急修复，最小改动面")),
                step(QStringLiteral("qa"), QStringLiteral("确认紧急修复没有副作用"))};
    }
    if (name == QLatin1String("docs")) {
        return {step(QStringLiteral("writer"), QStringLiteral("按卡片定义更新文档"))};
    }
    if (name == QLatin1String("review")) {
        return {step(QStringLiteral("orchestrator"), QStringLiteral("只读复核：给结论与建议"))};
    }
    if (name == QLatin1String("delivery")) {
        return {step(QStringLiteral("pm"), QStringLiteral("确认交付范围")),
                step(QStringLiteral("architect"), QStringLiteral("确认方案与风险")),
                step(QStringLiteral("engineer"), QStringLiteral("实现交付内容")),
                step(QStringLiteral("qa"), QStringLiteral("完整验收")),
                step(QStringLiteral("orchestrator"), QStringLiteral("收口：写 summary 与下一步建议"))};
    }
    return {};
}

bool TaskExec::isKnownRole(const QString &role)
{
    static const QStringList known = {
        QStringLiteral("pm"),     QStringLiteral("architect"), QStringLiteral("engineer"),
        QStringLiteral("qa"),     QStringLiteral("writer"),    QStringLiteral("orchestrator"),
    };
    return known.contains(role.trimmed().toLower());
}

QVariantMap TaskExec::normalizeSteps(const QVariantList &input)
{
    QVariantMap out;
    if (input.isEmpty()) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QStringLiteral("步骤数组不能为空"));
        return out;
    }

    // acceptance / scope 必须是字符串数组（与 exec.acceptance 同一口径）
    const auto cleanList = [](const QVariant &value, bool *ok) {
        QVariantList list;
        for (const QVariant &item : value.toList()) {
            const QString text = item.toString().trimmed();
            if (text.isEmpty()) {
                *ok = false;
                return list;
            }
            list.append(text);
        }
        *ok = true;
        return list;
    };

    QVariantList normalized;
    for (const QVariant &value : input) {
        const QVariantMap raw = value.toMap();
        const QString role = raw.value(QStringLiteral("role")).toString().trimmed().toLower();
        if (!isKnownRole(role)) {
            out.insert(QStringLiteral("ok"), false);
            out.insert(QStringLiteral("error"),
                       QStringLiteral("未知角色：%1").arg(role.isEmpty() ? QStringLiteral("<空>")
                                                                      : role));
            return out;
        }
        bool ok = true;
        const QVariantList acceptance = cleanList(raw.value(QStringLiteral("acceptance")), &ok);
        if (!ok) {
            out.insert(QStringLiteral("ok"), false);
            out.insert(QStringLiteral("error"),
                       QStringLiteral("%1 步的 acceptance 必须是字符串数组").arg(role));
            return out;
        }
        const QVariantList scope = cleanList(raw.value(QStringLiteral("scope")), &ok);
        if (!ok) {
            out.insert(QStringLiteral("ok"), false);
            out.insert(QStringLiteral("error"),
                       QStringLiteral("%1 步的 scope 必须是字符串数组").arg(role));
            return out;
        }

        normalized.append(QVariantMap{
            {QStringLiteral("role"), role},
            {QStringLiteral("agent"), raw.value(QStringLiteral("agent")).toString().trimmed()},
            {QStringLiteral("prompt"), raw.value(QStringLiteral("prompt")).toString().trimmed()},
            {QStringLiteral("acceptance"), acceptance},
            {QStringLiteral("scope"), scope},
        });
    }

    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("steps"), normalized);
    return out;
}

QVariantList TaskExec::stepsOf(const QString &taskId) const
{
    if (!m_database || taskId.isEmpty())
        return {};
    // 卡片覆盖优先；没有覆盖项就用预设。两者都没有（workflow 未定）返回空表。
    const QString raw = m_database->metaValue(stepsKey(taskId));
    if (!raw.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        if (doc.isArray()) {
            const QVariantMap normalized = normalizeSteps(doc.toVariant().toList());
            if (normalized.value(QStringLiteral("ok")).toBool())
                return normalized.value(QStringLiteral("steps")).toList();
        }
    }
    const QVariantMap exec = execOf(taskId);
    return presetSteps(exec.value(QStringLiteral("workflow")).toString());
}

bool TaskExec::setSteps(const QString &taskId, const QVariantList &steps)
{
    if (!m_database || taskId.isEmpty())
        return false;
    if (!m_tasks || m_tasks->taskById(taskId).isEmpty())
        return false;
    const QVariantMap normalized = normalizeSteps(steps);
    if (!normalized.value(QStringLiteral("ok")).toBool())
        return false;
    const QJsonArray array =
        QJsonArray::fromVariantList(normalized.value(QStringLiteral("steps")).toList());
    if (!m_database->setMetaValue(
            stepsKey(taskId),
            QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact))))
        return false;
    bump();
    return true;
}

bool TaskExec::clearSteps(const QString &taskId)
{
    if (!m_database || taskId.isEmpty())
        return false;
    if (!m_database->removeMetaValue(stepsKey(taskId)))
        return false;
    bump();
    return true;
}
