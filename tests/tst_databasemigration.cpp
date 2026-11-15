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

// DX-168：手工造一个「v2 老库 + 撕裂行」的现场。刻意不复用生产 DDL：
// 老库里 status 与 column_id 各自可写，导入路径只改 status 不搬列，于是留下
// done 的卡挂在待办列。meta 里没有迁移凭证，用来验证首次开新版本时的一次性纠正。
bool createV2TornDatabase(const QString &path, QString *error)
{
    RawDb raw(path);
    if (!raw.ok()) {
        *error = raw.error();
        return false;
    }
    const QStringList ddl = {
        QStringLiteral(
            "CREATE TABLE board (id TEXT PRIMARY KEY, name TEXT NOT NULL,"
            " order_key INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL DEFAULT 0,"
            " updated_at INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral(
            "CREATE TABLE board_column (id TEXT PRIMARY KEY, board_id TEXT NOT NULL,"
            " name TEXT NOT NULL, status TEXT NOT NULL, order_key INTEGER NOT NULL DEFAULT 0,"
            " created_at INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral(
            "CREATE TABLE task (id TEXT PRIMARY KEY, title TEXT NOT NULL,"
            " note TEXT NOT NULL DEFAULT '', status TEXT NOT NULL,"
            " column_id TEXT NOT NULL DEFAULT '', priority INTEGER NOT NULL DEFAULT 0,"
            " tags TEXT NOT NULL DEFAULT '[]', order_key INTEGER NOT NULL, due_at INTEGER,"
            " created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, completed_at INTEGER)"),
        QStringLiteral("CREATE INDEX idx_task_status_order ON task(status, order_key)"),
        QStringLiteral("CREATE INDEX idx_task_column_order ON task(column_id, order_key)"),
        QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE plugin_kv (plugin_id TEXT NOT NULL, key TEXT NOT NULL,"
                       " value TEXT, PRIMARY KEY (plugin_id, key))"),
        QStringLiteral("INSERT INTO board VALUES ('board-default', '默认看板', 1024, 0, 0)"),
        QStringLiteral(
            "INSERT INTO board_column VALUES"
            " ('col-todo', 'board-default', '待办', 'todo', 1024, 0, 0),"
            " ('col-doing', 'board-default', '进行中', 'doing', 2048, 0, 0),"
            " ('col-done', 'board-default', '已完成', 'done', 3072, 0, 0)"),
        QStringLiteral(
            "INSERT INTO task (id, title, status, column_id, order_key, created_at,"
            " updated_at, completed_at) VALUES"
            " ('t-done', '撕裂的完成卡', 'done', 'col-todo', 1024, 1000, 1000, 1793127238),"
            " ('t-todo', '正常待办卡', 'todo', 'col-todo', 2048, 1000, 1000, NULL),"
            " ('t-orphan', '列已不存在', 'todo', 'col-gone', 3072, 1000, 1000, NULL)"),
        QStringLiteral("PRAGMA user_version=2")
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
    // R16-03：metaKeys 按前缀枚举 —— 插件清扫自己命名空间的依赖面
    void metaKeysEnumeratesByPrefix();
    // DX-168：存量撕裂行一次性纠正 + 触发器把 status 锁成列状态的投影
    void tornRowsRepairedAndStatusProjectedFromColumn();
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

    // CHECK 仍是移除状态（自定义列的前提），但 status 不再由写入方决定：
    // DX-168 的 INSERT 触发器把任意写法纠正成 column_id 指向的列状态。
    QVERIFY2(raw.exec(QStringLiteral(
                 "INSERT INTO task (id, title, status, column_id, order_key, created_at, updated_at)"
                 " VALUES ('t-arch', '归档卡', 'archived', 'col-doing', 4096, 1792900004, 1792900004)")),
             qPrintable(raw.error()));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT COUNT(*) FROM task")).toInt(), 4);
    QCOMPARE(raw.scalar(QStringLiteral("SELECT status FROM task WHERE id='t-arch'")).toString(),
             QStringLiteral("doing"));
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
    // 换列就是换状态：重开不能把触发器派生出的 status 又改回旧值
    QCOMPARE(raw.scalar(QStringLiteral("SELECT status FROM task WHERE id='t-todo'")).toString(),
             QStringLiteral("doing"));
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

void TestDatabaseMigration::metaKeysEnumeratesByPrefix()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Database db;
    QString error;
    QVERIFY2(db.open(&error, dir.filePath(QStringLiteral("metakeys.db"))), qPrintable(error));

    db.setMetaValue(QStringLiteral("agentbridge.opencode.s1.t1"), QStringLiteral("uuid-1"));
    db.setMetaValue(QStringLiteral("agentbridge.opencode.s1.t2"), QStringLiteral("uuid-2"));
    db.setMetaValue(QStringLiteral("agentbridge.status"), QStringLiteral("ok"));
    db.setMetaValue(QStringLiteral("plan.rules"), QStringLiteral("{}"));

    // 前缀枚举：只含匹配键，升序，不误伤他键
    const QStringList keys = db.metaKeys(QStringLiteral("agentbridge."));
    QCOMPARE(keys.size(), 3);
    QCOMPARE(keys.at(0), QStringLiteral("agentbridge.opencode.s1.t1"));
    QCOMPARE(keys.at(1), QStringLiteral("agentbridge.opencode.s1.t2"));
    QCOMPARE(keys.at(2), QStringLiteral("agentbridge.status"));

    // 无匹配返回空；空前缀返回全部键（meta 权限本就覆盖整表读写，无新增暴露面）
    QVERIFY(db.metaKeys(QStringLiteral("nomatch.")).isEmpty());
    QCOMPARE(db.metaKeys().size(), 4);

    // LIKE 通配符按字面量匹配：键里的 % _ 不当通配符使。
    // 判别式：若 _ 被当通配符，"weird_%" 会命中 "weird%key_x"（应为 0）。
    db.setMetaValue(QStringLiteral("weird%key_x"), QStringLiteral("v"));
    QCOMPARE(db.metaKeys(QStringLiteral("weird%key_")).size(), 1);
    QCOMPARE(db.metaKeys(QStringLiteral("weird_")).size(), 0);

    // schema 不动：user_version 仍是当前版本
    QCOMPARE(db.metaKeys(QStringLiteral("agentbridge.")).size(), 3);
    RawDb raw(dir.filePath(QStringLiteral("metakeys.db")));
    QVERIFY2(raw.ok(), qPrintable(raw.error()));
    QCOMPARE(raw.scalar(QStringLiteral("PRAGMA user_version")).toInt(), Database::kSchemaVersion);
}

void TestDatabaseMigration::tornRowsRepairedAndStatusProjectedFromColumn()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("torn.db"));

    QString error;
    QVERIFY2(createV2TornDatabase(path, &error), qPrintable(error));
    {
        Database db;
        QVERIFY2(db.open(&error, path), qPrintable(error));
    }

    RawDb raw(path);
    QVERIFY2(raw.ok(), qPrintable(raw.error()));
    // 不升 user_version：旧版程序仍能打开同一个库
    QCOMPARE(raw.scalar(QStringLiteral("PRAGMA user_version")).toInt(), 2);

    // 一次性纠正：以 status 为历史事实把 column_id 补到该状态的列，完成时刻不丢
    QCOMPARE(raw.scalar(QStringLiteral("SELECT column_id FROM task WHERE id='t-done'")).toString(),
             QStringLiteral("col-done"));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT status FROM task WHERE id='t-done'")).toString(),
             QStringLiteral("done"));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT completed_at FROM task WHERE id='t-done'")).toLongLong(),
             qint64(1793127238));
    // 列已不存在的行也要落到有效列，否则界面上凭空消失
    QCOMPARE(raw.scalar(QStringLiteral("SELECT column_id FROM task WHERE id='t-orphan'")).toString(),
             QStringLiteral("col-todo"));
    QCOMPARE(raw.scalar(QStringLiteral(
                            "SELECT COUNT(*) FROM task t JOIN board_column c ON c.id=t.column_id "
                            "WHERE t.status<>c.status")).toInt(),
             0);

    // 触发器：直写 status 被纠正，INSERT 只表达列，列状态变更时列内卡重投影
    QVERIFY2(raw.exec(QStringLiteral("UPDATE task SET status='todo' WHERE id='t-done'")),
             qPrintable(raw.error()));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT status FROM task WHERE id='t-done'")).toString(),
             QStringLiteral("done"));
    QVERIFY2(raw.exec(QStringLiteral(
                 "INSERT INTO task (id, title, status, column_id, order_key, created_at, updated_at)"
                 " VALUES ('t-new', '外部插入', 'todo', 'col-doing', 4096, 1000, 1000)")),
             qPrintable(raw.error()));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT status FROM task WHERE id='t-new'")).toString(),
             QStringLiteral("doing"));
    QVERIFY2(raw.exec(QStringLiteral(
                 "UPDATE board_column SET status='done' WHERE id='col-doing'")),
             qPrintable(raw.error()));
    QCOMPARE(raw.scalar(QStringLiteral("SELECT status FROM task WHERE id='t-new'")).toString(),
             QStringLiteral("done"));

    // 重开幂等：迁移凭证已写，不再把 status 当源回写列，也不重置完成时刻
    {
        Database db2;
        QVERIFY2(db2.open(&error, path), qPrintable(error));
    }
    RawDb reopened(path);
    QVERIFY2(reopened.ok(), qPrintable(reopened.error()));
    QCOMPARE(reopened.scalar(QStringLiteral(
                                 "SELECT meta.value FROM meta WHERE meta.key='schema.status_single_source'"))
                 .toString(),
             QStringLiteral("1"));
    QCOMPARE(reopened.scalar(QStringLiteral("SELECT completed_at FROM task WHERE id='t-done'"))
                 .toLongLong(),
             qint64(1793127238));
}

QTEST_GUILESS_MAIN(TestDatabaseMigration)

#include "tst_databasemigration.moc"
