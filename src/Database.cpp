#include "Database.h"

#include "AppPaths.h"

#include <QDebug>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>

Database::Database(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("neonboard"))
{
}

Database::~Database()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool Database::open(QString *error)
{
    const QString dir = AppPaths::dataDir();
    if (dir.isEmpty()) {
        if (error)
            *error = QStringLiteral("无法获取应用数据目录");
        return false;
    }
    if (!QDir().mkpath(dir)) {
        if (error)
            *error = QStringLiteral("无法创建目录：%1").arg(dir);
        return false;
    }

    m_path = dir + QStringLiteral("/board.db");
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_path);
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }

    if (!execQuery(QStringLiteral("PRAGMA journal_mode=WAL"), error))
        return false;
    if (!execQuery(QStringLiteral("PRAGMA synchronous=NORMAL"), error))
        return false;
    if (!execQuery(QStringLiteral("PRAGMA busy_timeout=5000"), error))
        return false;
    if (!execQuery(QStringLiteral("PRAGMA foreign_keys=ON"), error))
        return false;

    QSqlQuery versionQuery(m_db);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next()) {
        if (error)
            *error = versionQuery.lastError().text();
        return false;
    }
    const int version = versionQuery.value(0).toInt();

    if (version == 0) {
        if (!exec(QStringLiteral("BEGIN"), error))
            return false;

        const QStringList schema = {
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS task ("
                "  id TEXT PRIMARY KEY,"
                "  title TEXT NOT NULL,"
                "  note TEXT NOT NULL DEFAULT '',"
                "  status TEXT NOT NULL CHECK(status IN ('todo','doing','done')),"
                "  priority INTEGER NOT NULL DEFAULT 0,"
                "  tags TEXT NOT NULL DEFAULT '[]',"
                "  order_key INTEGER NOT NULL,"
                "  due_at INTEGER,"
                "  created_at INTEGER NOT NULL,"
                "  updated_at INTEGER NOT NULL,"
                "  completed_at INTEGER"
                ")"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_task_status_order "
                "ON task(status, order_key)"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS meta ("
                "  key TEXT PRIMARY KEY,"
                "  value TEXT NOT NULL"
                ")"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS plugin_kv ("
                "  plugin_id TEXT NOT NULL,"
                "  key TEXT NOT NULL,"
                "  value TEXT,"
                "  PRIMARY KEY (plugin_id, key)"
                ")"),
            QStringLiteral("PRAGMA user_version=1")
        };

        for (const QString &sql : schema) {
            if (!exec(sql, error)) {
                exec(QStringLiteral("ROLLBACK"));
                return false;
            }
        }
        if (!exec(QStringLiteral("COMMIT"), error))
            return false;
    } else if (version > 1) {
        if (error)
            *error = QStringLiteral("数据库版本 %1 高于当前程序支持的版本").arg(version);
        return false;
    }

    // 主任务 / 子任务关联边表：独立表，不动 task 表结构、不升 user_version，
    // 老版本程序开同一个库也不受影响（认不出这张表而已）。
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS task_link ("
            "  child_id TEXT PRIMARY KEY,"
            "  parent_id TEXT NOT NULL,"
            "  created_at INTEGER NOT NULL DEFAULT 0"
            ")"), error)) {
        return false;
    }
    if (!exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_task_link_parent ON task_link(parent_id)"), error)) {
        return false;
    }

    refreshStats();
    return true;
}

bool Database::exec(const QString &sql, QString *error)
{
    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    return true;
}

bool Database::execQuery(const QString &sql, QString *error)
{
    return exec(sql, error);
}

void Database::refreshStats()
{
    if (!m_db.isOpen())
        return;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM task")) || !query.next())
        return;

    const int count = query.value(0).toInt();
    if (count == m_taskCount)
        return;

    m_taskCount = count;
    emit taskCountChanged();
}

QString Database::pluginValue(const QString &pluginId, const QString &key) const
{
    if (!m_db.isOpen())
        return {};

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT value FROM plugin_kv WHERE plugin_id = ? AND key = ?"));
    query.addBindValue(pluginId);
    query.addBindValue(key);
    if (!query.exec() || !query.next())
        return {};
    return query.value(0).toString();
}

bool Database::setPluginValue(const QString &pluginId, const QString &key, const QString &value)
{
    if (!m_db.isOpen())
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO plugin_kv (plugin_id, key, value) VALUES (?, ?, ?)"));
    query.addBindValue(pluginId);
    query.addBindValue(key);
    query.addBindValue(value);
    if (!query.exec()) {
        qWarning() << "Database: plugin storage write failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Database::removePluginValue(const QString &pluginId, const QString &key)
{
    if (!m_db.isOpen())
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM plugin_kv WHERE plugin_id = ? AND key = ?"));
    query.addBindValue(pluginId);
    query.addBindValue(key);
    if (!query.exec()) {
        qWarning() << "Database: plugin storage delete failed:" << query.lastError().text();
        return false;
    }
    return true;
}

QString Database::metaValue(const QString &key, const QString &defaultValue) const
{
    if (!m_db.isOpen())
        return defaultValue;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec() || !query.next())
        return defaultValue;
    return query.value(0).toString();
}

bool Database::setMetaValue(const QString &key, const QString &value)
{
    if (!m_db.isOpen())
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)"));
    query.addBindValue(key);
    query.addBindValue(value);
    if (!query.exec()) {
        qWarning() << "Database: meta write failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Database::removeMetaValue(const QString &key)
{
    if (!m_db.isOpen())
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM meta WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec()) {
        qWarning() << "Database: meta delete failed:" << query.lastError().text();
        return false;
    }
    return true;
}
