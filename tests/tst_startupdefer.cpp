// R0-13：Database 两阶段打开的竞态面。
// 覆盖：openDeferred 后的空态、finalizeSchema 的就绪信号、重复 finalize 幂等、
// 老库（v1）在两阶段打开后照常迁移与读取。
// R0-15：空态期构造的组件（TaskTree / TaskRunStore）把一次性初始化打在
// 不存在的表上，就绪后必须能重放成功（main.cpp 的 nbConnectStartupReplay 调的
// 就是这里验证的同一组方法）。
#include "Database.h"
#include "TaskModel.h"
#include "TaskRunStore.h"
#include "TaskTree.h"

#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

class TestStartupDefer : public QObject
{
    Q_OBJECT

private slots:
    void deferredIsEmptyUntilFinalized();
    void finalizeIsIdempotent();
    void legacyDatabaseSurvivesDeferredOpen();
    void emptyStateInitializationsReplayAfterFinalize();
};

static bool taskTableReadable(Database *db)
{
    QSqlDatabase conn = QSqlDatabase::database(db->connectionName());
    QSqlQuery query(conn);
    return query.exec(QStringLiteral("SELECT count(*) FROM task"));
}

static bool tableReadable(Database *db, const QString &table)
{
    QSqlDatabase conn = QSqlDatabase::database(db->connectionName());
    QSqlQuery query(conn);
    return query.exec(QStringLiteral("SELECT count(*) FROM %1").arg(table));
}

void TestStartupDefer::deferredIsEmptyUntilFinalized()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    QVERIFY2(db.openDeferred(&error, tmp.filePath(QStringLiteral("board.db"))),
             qPrintable(error));
    QVERIFY(!db.schemaReady());
    QVERIFY(!taskTableReadable(&db));   // 空态：表尚未建立

    QSignalSpy spy(&db, &Database::schemaFinalized);
    QVERIFY2(db.finalizeSchema(&error), qPrintable(error));
    QVERIFY(db.schemaReady());
    QCOMPARE(spy.count(), 1);
    QVERIFY(taskTableReadable(&db));    // 就绪：任务表可读写
}

void TestStartupDefer::finalizeIsIdempotent()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    QVERIFY2(db.openDeferred(&error, tmp.filePath(QStringLiteral("board.db"))),
             qPrintable(error));
    QVERIFY2(db.finalizeSchema(&error), qPrintable(error));
    QVERIFY2(db.finalizeSchema(&error), qPrintable(error));  // 迁移幂等
    QVERIFY(db.schemaReady());
    QVERIFY(taskTableReadable(&db));
}

void TestStartupDefer::legacyDatabaseSurvivesDeferredOpen()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("legacy.db"));
    {
        QSqlDatabase legacy = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("tst_startupdefer_legacy"));
        legacy.setDatabaseName(path);
        QVERIFY(legacy.open());
        QSqlQuery query(legacy);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO meta (key, value) VALUES ('legacy', '1')")));
        legacy.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("tst_startupdefer_legacy"));

    Database db;
    QString error;
    QVERIFY2(db.openDeferred(&error, path), qPrintable(error));   // 老库照常打开
    QVERIFY2(db.finalizeSchema(&error), qPrintable(error));       // 迁移延后执行
    QVERIFY(db.schemaReady());
    QCOMPARE(db.metaValue(QStringLiteral("legacy")), QStringLiteral("1"));
    QCOMPARE(db.defaultBoardId(), QStringLiteral("board-default"));
    QVERIFY(taskTableReadable(&db));
}

// R0-15 核心用例：空态跳过 → 就绪后重放成功。
// 空态期构造 TaskTree / TaskRunStore：构造期初始化被 schemaReady 守卫跳过
// （不再产出 no such table 失败行），finalizeSchema 之后按 nbConnectStartupReplay
// 的同一组调用重放，初始化落地，且重复重放幂等。
void TestStartupDefer::emptyStateInitializationsReplayAfterFinalize()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    QVERIFY2(db.openDeferred(&error, tmp.filePath(QStringLiteral("board.db"))),
             qPrintable(error));

    TaskModel tasks(&db);
    tasks.load();
    TaskTree tree(&db, &tasks);   // 空态：构造期初始化被守卫跳过
    TaskRunStore runs(&db);       // 空态：同上
    QVERIFY(!tableReadable(&db, QStringLiteral("task_link")));
    QVERIFY(!tableReadable(&db, QStringLiteral("task_run")));

    QVERIFY2(db.finalizeSchema(&error), qPrintable(error));
    QVERIFY(tableReadable(&db, QStringLiteral("task_link")));
    QVERIFY(tableReadable(&db, QStringLiteral("task_run")));
    tasks.load();   // 与生产一致：defer 回调里 finalizeSchema 后紧跟 taskStore.load()

    // —— 重放（与 main.cpp 的 nbConnectStartupReplay 同一组调用）——
    tree.reload();   // R0-15 起 reload 自带快照刷新
    runs.startupMaintenance();

    // 链接边读写正常：建两卡、挂父子，reload 后关系仍在（重放后 TaskTree 真正可用）
    const QString parent = tasks.addTask(QStringLiteral("父卡"));
    const QString child = tasks.addTask(QStringLiteral("子卡"));
    QVERIFY(!parent.isEmpty() && !child.isEmpty());
    QVERIFY(tree.setParent(child, parent));
    QCOMPARE(tree.parentId(child), parent);
    tree.reload();
    QCOMPARE(tree.parentId(child), parent);

    // 维护幂等：再跑一遍不炸、不误伤（孤儿恢复只动心跳超时的活跃 run）
    runs.startupMaintenance();
    QCOMPARE(tree.parentId(child), parent);

    // 孤儿恢复真实生效：手工插一条心跳早已超时的 running 行，重放后应被恢复为失败态
    QSqlDatabase conn = QSqlDatabase::database(db.connectionName());
    QSqlQuery query(conn);
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO task_run (id, task_id, status, attempt, heartbeat_at) "
        "VALUES ('run-orphan', '%1', 'running', 1, 1)").arg(child)));
    runs.startupMaintenance();
    QVERIFY(query.exec(QStringLiteral(
        "SELECT status FROM task_run WHERE id = 'run-orphan'")));
    QVERIFY(query.next());
    QVERIFY2(query.value(0).toString() != QStringLiteral("running"),
             "心跳超时的孤儿 run 应被 startupMaintenance 恢复");
}

QTEST_MAIN(TestStartupDefer)
#include "tst_startupdefer.moc"
