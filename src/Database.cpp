#include "Database.h"

#include "AppPaths.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariantMap>

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

    if (!ensureSchema(error))
        return false;

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

    // 任务执行定义侧表（M0：目标 / 验收 / 边界 / 执行参数）：独立表，
    // 不往 task 表加列、不升 user_version，老版本程序开同一个库不受影响。
    // 只放「人定的定义」；run_id / 心跳 / lastError 等运行态属于 executor，不进本表。
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS task_exec ("
            "  task_id TEXT PRIMARY KEY,"
            "  goal TEXT NOT NULL DEFAULT '',"
            "  acceptance TEXT NOT NULL DEFAULT '[]',"
            "  scope TEXT NOT NULL DEFAULT '[]',"
            "  agent TEXT NOT NULL DEFAULT '',"
            "  cwd TEXT NOT NULL DEFAULT '',"
            "  workflow TEXT NOT NULL DEFAULT '',"
            "  timeout_min INTEGER NOT NULL DEFAULT 30,"
            "  max_attempts INTEGER NOT NULL DEFAULT 1,"
            "  updated_at INTEGER NOT NULL DEFAULT 0"
            ")"), error)) {
        return false;
    }

    // 任务执行运行记录侧表（R6-02）：每次执行一行，运行态与看板状态分离。
    // 同卡最多一个活跃 run 由部分唯一索引兜底；同样不升 user_version。
    //
    // R7-01 在同一张表上追加队列与角色链字段（queue_status / position /
    // parent_run_id / step_index / role / handoff_path / cost_ms）：队列条目与 run
    // 是同一行，不另开队列表；老库靠下面的 ensureColumn 逐列补（不升 user_version）。
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS task_run ("
            "  id TEXT PRIMARY KEY,"
            "  task_id TEXT NOT NULL,"
            "  agent TEXT NOT NULL DEFAULT '',"
            "  cwd TEXT NOT NULL DEFAULT '',"
            "  workflow TEXT NOT NULL DEFAULT '',"
            "  status TEXT NOT NULL,"
            "  attempt INTEGER NOT NULL DEFAULT 1,"
            "  started_at INTEGER,"
            "  finished_at INTEGER,"
            "  heartbeat_at INTEGER,"
            "  exit_code INTEGER,"
            "  last_error TEXT,"
            "  log_path TEXT,"
            "  scope_violation TEXT,"
            "  queue_status TEXT NOT NULL DEFAULT '',"
            "  position INTEGER NOT NULL DEFAULT 0,"
            "  parent_run_id TEXT NOT NULL DEFAULT '',"
            "  step_index INTEGER NOT NULL DEFAULT 0,"
            "  role TEXT NOT NULL DEFAULT '',"
            "  handoff_path TEXT NOT NULL DEFAULT '',"
            "  cost_ms INTEGER NOT NULL DEFAULT 0"
            ")"), error)) {
        return false;
    }
    // 老库补列：CREATE TABLE IF NOT EXISTS 对已存在的表不会加列，必须逐列 ALTER。
    // 全部走 IF NOT EXISTS 语义（先查 table_info），重开幂等。
    const struct {
        const char *name;
        const char *definition;
    } runColumns[] = {
        {"queue_status", "TEXT NOT NULL DEFAULT ''"},
        {"position", "INTEGER NOT NULL DEFAULT 0"},
        {"parent_run_id", "TEXT NOT NULL DEFAULT ''"},
        {"step_index", "INTEGER NOT NULL DEFAULT 0"},
        {"role", "TEXT NOT NULL DEFAULT ''"},
        {"handoff_path", "TEXT NOT NULL DEFAULT ''"},
        {"cost_ms", "INTEGER NOT NULL DEFAULT 0"},
    };
    for (const auto &column : runColumns) {
        if (!ensureColumn(QStringLiteral("task_run"), QLatin1String(column.name),
                          QLatin1String(column.definition), error)) {
            return false;
        }
    }
    if (!exec(QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_task_run_active "
            "ON task_run(task_id) WHERE status IN ('queued','running')"), error)) {
        return false;
    }
    // 调度器取队头（waiting 按 position）与队列视图都要扫这一列；
    // 部分索引让「只有排队的行」参与，不拖累历史 run 的写入。
    if (!exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_task_run_queue "
            "ON task_run(position) WHERE queue_status IN ('waiting','dispatching')"),
            error)) {
        return false;
    }

    // 项目维度（R10-01）：一张卡属于哪个项目。
    // 起因是 R4-01 的事件桥不区分来源项目 —— 别的项目的卡会直接落进同一张 task 表，
    // 看板上分不出来「这是哪个项目的事」。与其他侧表同一条纪律：独立表、
    // 不往 task 表加列、不升 user_version，老版本程序开同一个库不受影响。
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS project ("
            "  id TEXT PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  color TEXT NOT NULL DEFAULT '',"
            "  order_key INTEGER NOT NULL DEFAULT 0,"
            "  created_at INTEGER NOT NULL DEFAULT 0,"
            "  updated_at INTEGER NOT NULL DEFAULT 0"
            ")"), error)) {
        return false;
    }
    // 一卡一项目：task_id 做主键，改归属走 REPLACE 而不是追加，天然不留旧行。
    // 未归属的卡不入表（查不到行即「未归属」），不给存量卡先铺一遍空值。
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS task_project ("
            "  task_id TEXT PRIMARY KEY,"
            "  project_id TEXT NOT NULL,"
            "  created_at INTEGER NOT NULL DEFAULT 0"
            ")"), error)) {
        return false;
    }
    if (!exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_task_project_project "
            "ON task_project(project_id)"), error)) {
        return false;
    }

    // 卡片事件侧表（R8-04）：详情抽屉的时间线从这里读。
    // 和其他侧表同一条纪律：独立表、不往 task 表加列、不升 user_version ——
    // 老库打开就自动建出空表，历史任务一条都不受影响，老版本程序也照开不误。
    //
    // 排序用自增 id 而不是 at：一秒内连点两下（完成又恢复）at 会撞在一起，
    // 按时间排出来的顺序每次刷新都可能不一样。at 只用来显示。
    // 不加外键级联：卡删了事件要留着 —— 审计的意义就在于「这张卡存在过」。
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS task_event ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  task_id TEXT NOT NULL,"
            "  kind TEXT NOT NULL,"
            "  detail TEXT NOT NULL DEFAULT '',"
            "  at INTEGER NOT NULL DEFAULT 0"
            ")"), error)) {
        return false;
    }
    if (!exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_task_event_task ON task_event(task_id, id DESC)"),
            error)) {
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

int Database::userVersion() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next())
        return -1;
    return query.value(0).toInt();
}

bool Database::ensureSchema(QString *error)
{
    int version = userVersion();
    if (version < 0) {
        if (error)
            *error = QStringLiteral("读取数据库版本失败");
        return false;
    }
    if (version > kSchemaVersion) {
        if (error) {
            *error = QStringLiteral("数据库版本 %1 高于当前程序支持的版本 %2")
                         .arg(version).arg(kSchemaVersion);
        }
        return false;
    }

    // 逐级迁移：新建库也走 0 → 1 → 2 的同一条路径，避免「新库 schema」与
    // 「迁移后 schema」两套定义漂移（迁移写错只在老库上炸，是最难查的一类 bug）。
    if (version == 0) {
        if (!createSchemaV1(error))
            return false;
        version = 1;
    }
    if (version == 1) {
        if (!migrateToV2(error))
            return false;
    }
    return true;
}

bool Database::createSchemaV1(QString *error)
{
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
    return exec(QStringLiteral("COMMIT"), error);
}

bool Database::migrateToV2(QString *error)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    if (!exec(QStringLiteral("BEGIN"), error))
        return false;

    // 迁移步骤全部在同一个事务里：中途失败整体回滚，不允许留下「任务表已重建、
    // 看板表没建」的半迁移库（这种库 user_version 还是 1，下次启动会再撞一次）。
    const QStringList steps = {
        // 看板与列：列带 status，一张卡落在哪列由 task.column_id 决定。
        // status 仍是三态（todo/doing/done），列只是「同一状态的不同分段」，
        // 因此完成判定、撤销、提醒等既有逻辑不需要改语义。
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS board ("
            "  id TEXT PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  order_key INTEGER NOT NULL DEFAULT 0,"
            "  created_at INTEGER NOT NULL DEFAULT 0,"
            "  updated_at INTEGER NOT NULL DEFAULT 0"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS board_column ("
            "  id TEXT PRIMARY KEY,"
            "  board_id TEXT NOT NULL,"
            "  name TEXT NOT NULL,"
            "  status TEXT NOT NULL,"
            "  order_key INTEGER NOT NULL DEFAULT 0,"
            "  created_at INTEGER NOT NULL DEFAULT 0,"
            "  updated_at INTEGER NOT NULL DEFAULT 0"
            ")"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_board_column_board "
            "ON board_column(board_id, order_key)"),

        // 重建 task：SQLite 不支持删除列约束，只能建新表搬迁。
        // 去掉 CHECK(status IN ...) —— 列可自定义后，状态集合不再由建表语句锁死；
        // column_id 为空串表示「未指定列，按 status 落到默认列」，老库行全部走这个分支。
        QStringLiteral("DROP TABLE IF EXISTS task_v2"),
        QStringLiteral(
            "CREATE TABLE task_v2 ("
            "  id TEXT PRIMARY KEY,"
            "  title TEXT NOT NULL,"
            "  note TEXT NOT NULL DEFAULT '',"
            "  status TEXT NOT NULL,"
            "  column_id TEXT NOT NULL DEFAULT '',"
            "  priority INTEGER NOT NULL DEFAULT 0,"
            "  tags TEXT NOT NULL DEFAULT '[]',"
            "  order_key INTEGER NOT NULL,"
            "  due_at INTEGER,"
            "  created_at INTEGER NOT NULL,"
            "  updated_at INTEGER NOT NULL,"
            "  completed_at INTEGER"
            ")"),
        QStringLiteral(
            "INSERT INTO task_v2 (id, title, note, status, column_id, priority, tags,"
            "  order_key, due_at, created_at, updated_at, completed_at) "
            "SELECT id, title, note, status, '', priority, tags,"
            "  order_key, due_at, created_at, updated_at, completed_at FROM task"),
        QStringLiteral("DROP TABLE task"),
        QStringLiteral("ALTER TABLE task_v2 RENAME TO task"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_task_status_order "
            "ON task(status, order_key)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_task_column_order "
            "ON task(column_id, order_key)"),

        // 默认板 + 三列：id 固定，便于迁移回填、老数据对号入座，也方便外部工具识别。
        QStringLiteral(
            "INSERT OR IGNORE INTO board (id, name, order_key, created_at, updated_at) "
            "VALUES ('board-default', '默认看板', 1024, %1, %1)").arg(now),
        QStringLiteral(
            "INSERT OR IGNORE INTO board_column "
            "(id, board_id, name, status, order_key, created_at, updated_at) VALUES "
            "('col-todo', 'board-default', '待办', 'todo', 1024, %1, %1),"
            "('col-doing', 'board-default', '进行中', 'doing', 2048, %1, %1),"
            "('col-done', 'board-default', '已完成', 'done', 3072, %1, %1)").arg(now),
        // 回填：只填还空着的行，重跑迁移不会覆盖用户后来改过的列
        QStringLiteral(
            "UPDATE task SET column_id = CASE status "
            "WHEN 'doing' THEN 'col-doing' "
            "WHEN 'done' THEN 'col-done' "
            "ELSE 'col-todo' END "
            "WHERE column_id = ''"),

        QStringLiteral("PRAGMA user_version=2")
    };

    for (const QString &sql : steps) {
        if (!exec(sql, error)) {
            exec(QStringLiteral("ROLLBACK"));
            return false;
        }
    }
    return exec(QStringLiteral("COMMIT"), error);
}

QString Database::defaultBoardId() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT id FROM board ORDER BY order_key, id LIMIT 1"))
        || !query.next()) {
        return QString();
    }
    return query.value(0).toString();
}

QVariantList Database::boards() const
{
    QVariantList out;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT id, name, order_key FROM board ORDER BY order_key, id")))
        return out;
    while (query.next()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), query.value(0)},
            {QStringLiteral("name"), query.value(1)},
            {QStringLiteral("orderKey"), query.value(2)},
        });
    }
    return out;
}

QVariantList Database::boardColumns(const QString &boardId) const
{
    QVariantList out;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, name, status, order_key FROM board_column "
        "WHERE board_id = ? ORDER BY order_key, id"));
    query.addBindValue(boardId);
    if (!query.exec())
        return out;
    while (query.next()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), query.value(0)},
            {QStringLiteral("name"), query.value(1)},
            {QStringLiteral("status"), query.value(2)},
            {QStringLiteral("orderKey"), query.value(3)},
        });
    }
    return out;
}

QString Database::defaultColumnForStatus(const QString &status) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT c.id FROM board_column c "
        "JOIN board b ON b.id = c.board_id "
        "WHERE c.status = ? ORDER BY b.order_key, c.order_key, c.id LIMIT 1"));
    query.addBindValue(status);
    if (!query.exec() || !query.next())
        return QString();
    return query.value(0).toString();
}

QString Database::columnStatus(const QString &columnId) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT status FROM board_column WHERE id = ?"));
    query.addBindValue(columnId);
    if (!query.exec() || !query.next())
        return QString();
    return query.value(0).toString();
}

namespace {
// 三态是任务语义（完成判定 / 撤销 / 提醒都按它走），列只是同一状态的分段
bool isValidStatus(const QString &status)
{
    return status == QLatin1String("todo")
        || status == QLatin1String("doing")
        || status == QLatin1String("done");
}
} // namespace

QString Database::addColumn(const QString &name, const QString &status, QString *error)
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty()) {
        if (error)
            *error = QStringLiteral("列名不能为空");
        return {};
    }
    if (!isValidStatus(status)) {
        if (error)
            *error = QStringLiteral("未知状态：%1").arg(status);
        return {};
    }
    const QString boardId = defaultBoardId();
    if (boardId.isEmpty()) {
        if (error)
            *error = QStringLiteral("没有可用的看板");
        return {};
    }

    qint64 lastKey = 0;
    QSqlQuery keyQuery(m_db);
    keyQuery.prepare(QStringLiteral(
        "SELECT COALESCE(MAX(order_key), 0) FROM board_column WHERE board_id = ?"));
    keyQuery.addBindValue(boardId);
    if (keyQuery.exec() && keyQuery.next())
        lastKey = keyQuery.value(0).toLongLong();

    // id 用短随机后缀：可读、够唯一，也不依赖自增（多端同步时不给库加锁）
    const QString id = QStringLiteral("col-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO board_column (id, board_id, name, status, order_key, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(id);
    query.addBindValue(boardId);
    query.addBindValue(cleanName);
    query.addBindValue(status);
    query.addBindValue(lastKey + 1024);
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return {};
    }
    return id;
}

bool Database::renameColumn(const QString &columnId, const QString &name, QString *error)
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty()) {
        if (error)
            *error = QStringLiteral("列名不能为空");
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE board_column SET name = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(cleanName);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(columnId);
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        if (error)
            *error = QStringLiteral("列不存在：%1").arg(columnId);
        return false;
    }
    return true;
}

bool Database::moveColumn(const QString &columnId, int delta, QString *error)
{
    if (delta == 0)
        return true;

    // 按「列自己所属的看板」取同板列序：拿默认看板会把别的板的列一起重排，
    // 多板切换一上线顺序就错乱
    QString boardId;
    {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("SELECT board_id FROM board_column WHERE id = ?"));
        query.addBindValue(columnId);
        if (!query.exec() || !query.next())
            return false;
        boardId = query.value(0).toString();
    }

    QStringList ids;
    {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "SELECT id FROM board_column WHERE board_id = ? ORDER BY order_key, id"));
        query.addBindValue(boardId);
        if (!query.exec())
            return false;
        while (query.next())
            ids.append(query.value(0).toString());
    }

    const int from = static_cast<int>(ids.indexOf(columnId));
    const int to = from + delta;
    // 越界就是「已经到头了」：不改数据、也不报错升级，交给界面判断按钮可用性
    if (from < 0 || to < 0 || to >= ids.size())
        return false;

    ids.move(from, to);

    // 顺序整体重排：只换两个 order_key 会在多次移动后产生相同键，界面顺序变得随机
    if (!exec(QStringLiteral("BEGIN"), error))
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE board_column SET order_key = ? WHERE id = ?"));
    for (int i = 0; i < ids.size(); ++i) {
        query.addBindValue(static_cast<qint64>(i + 1) * 1024);
        query.addBindValue(ids.at(i));
        if (!query.exec()) {
            if (error)
                *error = query.lastError().text();
            exec(QStringLiteral("ROLLBACK"));
            return false;
        }
    }
    return exec(QStringLiteral("COMMIT"), error);
}

bool Database::deleteColumn(const QString &columnId, QString *error)
{
    // 一次拿到状态和所属看板：接卡的兜底列必须同板，否则卡片会被搬到别的看板上
    QString status;
    QString boardId;
    {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("SELECT status, board_id FROM board_column WHERE id = ?"));
        query.addBindValue(columnId);
        if (query.exec() && query.next()) {
            status = query.value(0).toString();
            boardId = query.value(1).toString();
        }
    }
    if (status.isEmpty()) {
        if (error)
            *error = QStringLiteral("列不存在：%1").arg(columnId);
        return false;
    }

    // 该状态的其它列（用来接住被删列里的卡片）；一个都没有就拒绝删除
    QString fallback;
    {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "SELECT id FROM board_column WHERE board_id = ? AND status = ? AND id <> ? "
            "ORDER BY order_key, id LIMIT 1"));
        query.addBindValue(boardId);
        query.addBindValue(status);
        query.addBindValue(columnId);
        if (query.exec() && query.next())
            fallback = query.value(0).toString();
    }
    if (fallback.isEmpty()) {
        if (error)
            *error = QStringLiteral("该状态只剩这一列，删掉后卡片无处安放");
        return false;
    }

    if (!exec(QStringLiteral("BEGIN"), error))
        return false;

    // 先迁卡再删列：顺序反了会留下 column_id 指向不存在列的孤儿卡片
    QSqlQuery move(m_db);
    move.prepare(QStringLiteral(
        "UPDATE task SET column_id = ?, updated_at = ? WHERE column_id = ?"));
    move.addBindValue(fallback);
    move.addBindValue(QDateTime::currentSecsSinceEpoch());
    move.addBindValue(columnId);
    if (!move.exec()) {
        if (error)
            *error = move.lastError().text();
        exec(QStringLiteral("ROLLBACK"));
        return false;
    }

    QSqlQuery drop(m_db);
    drop.prepare(QStringLiteral("DELETE FROM board_column WHERE id = ?"));
    drop.addBindValue(columnId);
    if (!drop.exec()) {
        if (error)
            *error = drop.lastError().text();
        exec(QStringLiteral("ROLLBACK"));
        return false;
    }
    return exec(QStringLiteral("COMMIT"), error);
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

bool Database::ensureColumn(const QString &table, const QString &column,
                           const QString &definition, QString *error)
{
    if (!m_db.isOpen()) {
        if (error)
            *error = QStringLiteral("数据库未打开");
        return false;
    }
    // PRAGMA table_info 不支持绑定参数，表名/列名由调用方以字面量给（都是本文件内的常量，
    // 不接受外部输入），这里仍做一次白名单式校验，防止误传拼出别的语句。
    static const QRegularExpression safeName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    if (!safeName.match(table).hasMatch() || !safeName.match(column).hasMatch()) {
        if (error)
            *error = QStringLiteral("非法的表名 / 列名：%1.%2").arg(table, column);
        return false;
    }

    QSqlQuery info(m_db);
    if (!info.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        if (error)
            *error = info.lastError().text();
        return false;
    }
    bool exists = false;
    while (info.next()) {
        if (info.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) {
            exists = true;
            break;
        }
    }
    if (exists)
        return true;
    return exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                    .arg(table, column, definition), error);
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
