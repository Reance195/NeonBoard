#include "Database.h"

#include "AppPaths.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

// SQLite 文件头固定为 "SQLite format 3\0"；随机字节 / 空文件不满足，用于区分
//「文件坏了」和「打不开但文件还在」（后者可能是权限问题，不能擅自动备份）。
static bool hasSqliteHeader(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(16);
    return header == QByteArrayLiteral("SQLite format 3\0");
}

Database::Database(QObject *parent)
    : QObject(parent)
{
}

Database::~Database()
{
    closeConnection();
}

void Database::closeConnection()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
    }
}

bool Database::openConnection(QString *error)
{
    // 连接名唯一：自检 / 测试可能同时开多个库，固定名会互相顶掉。
    if (m_connectionName.isEmpty()) {
        m_connectionName = QStringLiteral("neonboard_")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_path);
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    return true;
}

bool Database::integrityOk(QString *reason) const
{
    if (!m_db.isOpen())
        return false;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("PRAGMA quick_check")) || !query.next()) {
        if (reason)
            *reason = query.lastError().text();
        return false;
    }
    const QString result = query.value(0).toString();
    if (result.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
        if (reason)
            *reason = result;
        return false;
    }
    return true;
}

bool Database::recoverCorrupt(const QString &reason, QString *error)
{
    closeConnection();

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString backup = m_path + QStringLiteral(".corrupt-") + stamp;
    int attempt = 1;
    while (QFile::exists(backup))
        backup = m_path + QStringLiteral(".corrupt-") + stamp + QLatin1Char('-')
            + QString::number(attempt++);

    if (!QFile::rename(m_path, backup)) {
        if (error)
            *error = QStringLiteral("数据库已损坏，且备份到 %1 失败").arg(backup);
        return false;
    }
    // WAL 边车文件跟随主库一起挪走，否则新库会读进旧的 WAL 内容。
    for (const char *suffix : {"-wal", "-shm"}) {
        const QString sidecar = m_path + QLatin1String(suffix);
        if (QFile::exists(sidecar))
            QFile::rename(sidecar, backup + QLatin1String(suffix));
    }

    if (!openConnection(error))
        return false;

    m_recovered = true;
    m_recoveryBackup = backup;
    m_recoveryReason = reason;
    qWarning().noquote() << "Database: corrupted file backed up to" << backup
                         << "reason:" << reason;
    return true;
}

bool Database::open(QString *error, const QString &pathOverride)
{
    closeConnection();
    m_recovered = false;
    m_recoveryBackup.clear();
    m_recoveryReason.clear();

    QString path = pathOverride;
    if (path.isEmpty()) {
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
        path = dir + QStringLiteral("/board.db");
    } else {
        path = QFileInfo(path).absoluteFilePath();
        const QString dir = QFileInfo(path).absolutePath();
        if (!QDir().mkpath(dir)) {
            if (error)
                *error = QStringLiteral("无法创建目录：%1").arg(dir);
            return false;
        }
    }

    m_path = path;
    const bool existed = QFileInfo::exists(m_path);

    QString openError;
    bool opened = openConnection(&openError);
    QString integrityReason;
    if (opened && !integrityOk(&integrityReason)) {
        closeConnection();
        opened = false;
        openError = integrityReason;
    }

    if (!opened) {
        if (!existed || !hasSqliteHeader(m_path)) {
            // 新库打不开 = 环境问题；文件还在但不认识 = 损坏，两种情况分开处理。
            if (existed && !hasSqliteHeader(m_path)) {
                if (!recoverCorrupt(openError, error))
                    return false;
            } else {
                if (error)
                    *error = openError;
                return false;
            }
        } else if (!recoverCorrupt(openError, error)) {
            return false;
        }
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

    // 任务附加属性侧表（提醒 / 重复）：独立表，不往 task 表加列、不升 user_version，
    // 老版本程序开同一个库也不受影响（认不出这张表而已）。
    // rrule 列为重复任务（RFC 5545 RRULE）预留，提醒功能只读写 remind_at / notified_at。
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS task_extra ("
            "  task_id TEXT PRIMARY KEY,"
            "  remind_at INTEGER NOT NULL DEFAULT 0,"
            "  notified_at INTEGER NOT NULL DEFAULT 0,"
            "  rrule TEXT NOT NULL DEFAULT '',"
            "  created_at INTEGER NOT NULL DEFAULT 0"
            ")"), error)) {
        return false;
    }

    if (m_recovered) {
        setMetaValue(QStringLiteral("db.recovered_at"),
                     QString::number(QDateTime::currentSecsSinceEpoch()));
        setMetaValue(QStringLiteral("db.recovered_backup"), m_recoveryBackup);
        setMetaValue(QStringLiteral("db.recovered_reason"), m_recoveryReason);
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
