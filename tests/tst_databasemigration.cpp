// R4-03：schema 迁移测试（v1 老库 → v2 看板/列）。
//
// 只跑 sqlite 临时库，不碰真实数据；每个用例独立 QTemporaryDir，可并行。
// 这里刻意**不复用**生产代码的建表语句：v1 的 DDL 是历史快照，抄进测试才能证明
// 「老版本程序建出来的库」确实能被读到 —— 复用生产 DDL 会让迁移测试变成自证。

#include "Database.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace {

QString uniqueConnection()
{
    return QStringLiteral("migration_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// 一次性裸连接：用完后必须出作用域再 removeDatabase，否则 Qt 会告警连接仍在使用。
class RawDb
{
public:
    explicit RawDb(const QString &path)
        : m_name(uniqueConnection())
    {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        m_db.setDatabaseName(path);
        m_ok = m_db.open();
        if (!m_ok)
            m_error = m_db.lastError().text();
    }

    ~RawDb()
    {
        if (m_db.isOpen())
            m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_name);
    }

    bool ok() const { return m_ok; }
    QString error() const { return m_error; }
    QSqlDatabase &db() { return m_db; }

    bool exec(const QString &sql)
    {
        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            m_error = query.lastError().text() + QStringLiteral(" | SQL: ") + sql;
            return false;
        }
        return true;
    }

    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(m_db);
        if (!query.exec(sql) || !query.next())
            return {};
        return query.value(0);
    }

private:
    QString m_name;
    QSqlDatabase m_db;
    bool m_ok = false;
    QString m_error;
};

// v1 建库语句（历史快照，含 status CHECK）—— 与迁移前生产代码逐字一致。
bool createV1Database(const QString &path, QString *error)
{
    RawDb raw(path);
    if (!raw.ok()) {
        *error = raw.error();
        return false;
    }
    const QStringList ddl = {
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
        QStringLiteral("CREATE INDEX idx_task_status_order ON task(status, order_key)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS plugin_kv (plugin_id TEXT NOT NULL, key TEXT NOT NULL,"
                       " value TEXT, PRIMARY KEY (plugin_id, key))"),
        // 三条老卡：覆盖三种状态、带 note/tags/截止时间，用来验字段搬迁是否逐个到位
        QStringLiteral("INSERT INTO task (id, title, note, status, priority, tags, order_key,"
                       " due_at, created_at, updated_at, completed_at) VALUES"
                       " ('t-todo', '待办卡', '备注A', 'todo', 2, '[\"R4\"]', 1024, 1793000000, 1792900000, 1792900000, NULL),"
                       " ('t-doing', '进行中卡', '', 'doing', 3, '[\"R4\",\"架构\"]', 2048, NULL, 1792900001, 1792900001, NULL),"
                       " ('t-done', '已完成卡', '已完成备注', 'done', 1, '[]', 3072, NULL, 1792900002, 1792900003, 1792900003)"),
        QStringLiteral("PRAGMA user_version=1")
    };
    for (const QString &sql : ddl) {
        if (!raw.exec(sql)) {
            *error = raw.error();
            return false;
        }
    }
    return true;
}

} // namespace

class TestDatabaseMigration : public QObject
{
    Q_OBJECT

private slots:
    void freshDatabaseIsCreatedAtV2();
    void v1DatabaseMigratesWithoutDataLoss();
    void v2ReopenIsIdempotent();
    void newerSchemaIsRejected();
};

void TestDatabaseMigration::freshDatabaseIsCreatedAtV2()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("fresh.db"));

    Database db;
    QString error;
    QVERIFY2(db.open(&error, path), qPrintable(error));

    // 新建库也必须走「0 → 1 → 2」的迁移路径，落地结果与老库迁移后一致
    RawDb raw(path);
    QVERIFY2(raw.ok(), qPrintable(raw.error()));
    QCOMPARE(raw.scalar(QStringLiteral("PRAGMA user_version")).toInt(), Database::kSchemaVersion);
    QCOMPARE(raw.scalar(QStringLiteral("SELECT COUNT(*) FROM board")).toInt(), 1);
    QCOMPARE(raw.scalar(QStringLiteral("SELECT COUNT(*) FROM board_column")).toInt(), 3);

    // 默认板与三列的状态映射是外部工具依赖的契约，id 固定不许漂移
    QCOMPARE(db.defaultBoardId(), QStringLiteral("board-default"));
    const QVariantList columns = db.boardColumns(db.defaultBoardId());
    QCOMPARE(columns.size(), 3);
    QCOMPARE(columns.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("col-todo"));
    QCOMPARE(columns.at(0).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("todo"));
    QCOMPARE(columns.at(1).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("doing"));
    QCOMPARE(columns.at(2).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("done"));
    QCOMPARE(db.defaultColumnForStatus(QStringLiteral("doing")), QStringLiteral("col-doing"));

    const QVariantList boards = db.boards();
    QCOMPARE(boards.size(), 1);
    QCOMPARE(boards.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("默认看板"));
}

void TestDatabaseMigration::v1DatabaseMigratesWithoutDataLoss()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("legacy.db"));

    QString error;
    QVERIFY2(createV1Database(path, &error), qPrintable(error));

    {
        Database db;
        QVERIFY2(db.open(&error, path), qPrintable(error));
        QCOMPARE(db.taskCount(), 3);
        QCOMPARE(db.defaultBoardId(), QStringLiteral("board-default"));
        QCOMPARE(db.boardColumns(db.defaultBoardId()).size(), 3);
    }

    RawDb raw(path);
    QVERIFY2(raw.ok(), qPrintable(raw.error()));
    QCOMPARE(raw.scalar(QStringLiteral("PRAGMA user_version")).toInt(), 2);

    // 字段逐个核对：重建表最容易在这里悄悄丢 note / 时间戳
    QCOMPARE(raw.scalar(QStringLiteral("SELECT title FROM task WHERE id='t-todo'")).toString(),
             QStringLiteral("待办卡"));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT note FROM task WHERE id='t-done'")).toString(),
             QStringLiteral("已完成备注"));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT priority FROM task WHERE id='t-doing'")).toInt(), 3);
    QCOMPARE(raw.scalar(QStringLiteral("SELECT tags FROM task WHERE id='t-doing'")).toString(),
             QStringLiteral("[\"R4\",\"架构\"]"));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT due_at FROM task WHERE id='t-todo'")).toLongLong(),
             qint64(1793000000));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT completed_at FROM task WHERE id='t-done'")).toLongLong(),
             qint64(1792900003));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT COUNT(*) FROM task")).toInt(), 3);

    // 回填：老库三张卡按 status 落到默认列
    QCOMPARE(raw.scalar(QStringLiteral("SELECT column_id FROM task WHERE id='t-todo'")).toString(),
             QStringLiteral("col-todo"));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT column_id FROM task WHERE id='t-doing'")).toString(),
             QStringLiteral("col-doing"));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT column_id FROM task WHERE id='t-done'")).toString(),
             QStringLiteral("col-done"));

    // CHECK 已移除：状态集合不再由建表语句锁死（自定义列的前提）
    QVERIFY2(raw.exec(QStringLiteral(
                 "INSERT INTO task (id, title, status, order_key, created_at, updated_at)"
                 " VALUES ('t-arch', '归档卡', 'archived', 4096, 1792900004, 1792900004)")),
             qPrintable(raw.error()));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT COUNT(*) FROM task")).toInt(), 4);
}

void TestDatabaseMigration::v2ReopenIsIdempotent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("reopen.db"));

    QString error;
    QVERIFY2(createV1Database(path, &error), qPrintable(error));

    {
        Database db;
        QVERIFY2(db.open(&error, path), qPrintable(error));
    }

    // 用户改过列之后再次打开：不许被迁移的分支回填逻辑重置
    {
        RawDb raw(path);
        QVERIFY2(raw.ok(), qPrintable(raw.error()));
        QVERIFY2(raw.exec(QStringLiteral("UPDATE task SET column_id='col-doing' WHERE id='t-todo'")),
                 qPrintable(raw.error()));
    }

    Database db;
    QVERIFY2(db.open(&error, path), qPrintable(error));
    QCOMPARE(db.boards().size(), 1);
    QCOMPARE(db.boardColumns(db.defaultBoardId()).size(), 3);

    RawDb raw(path);
    QVERIFY2(raw.ok(), qPrintable(raw.error()));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT column_id FROM task WHERE id='t-todo'")).toString(),
             QStringLiteral("col-doing"));
    QCOMPARE(raw.scalar(QStringLiteral("PRAGMA user_version")).toInt(), 2);
}

void TestDatabaseMigration::newerSchemaIsRejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("future.db"));

    QString error;
    QVERIFY2(createV1Database(path, &error), qPrintable(error));
    {
        RawDb raw(path);
        QVERIFY2(raw.ok(), qPrintable(raw.error()));
        QVERIFY2(raw.exec(QStringLiteral("PRAGMA user_version=99")), qPrintable(raw.error()));
    }

    Database db;
    QVERIFY(!db.open(&error, path));
    QVERIFY2(error.contains(QStringLiteral("高于")), qPrintable(error));
}

QTEST_GUILESS_MAIN(TestDatabaseMigration)

#include "tst_databasemigration.moc"
