// R6-02：运行记录侧表（task_run）与同卡单活跃 run 的锁。
//
// 覆盖五条：
//   1. 取锁后二次 acquire 被拒（busy），释放后可再取；终态 run 不能二次结算
//   2. acquire 字段落库、release 写回 ending 明细（退出码 / 摘要 / 日志 / 越界）
//   3. 孤儿回收：心跳超过 90 秒的 running → 重新打开 store 后变 blocked 且锁释放
//   4. 表与部分唯一索引存在
//   5. schema 版本不变（user_version 仍为 2）

#include "Database.h"
#include "TaskRunStore.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class TestTaskRuns : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void acquireRejectsSecondActiveRun();
    void releaseWritesDetail();
    void orphanRunBecomesBlocked();
    void schemaVersionStaysAtV2();

private:
    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskRunStore *m_store = nullptr;
};

void TestTaskRuns::init()
{
    const QString path = m_dir.filePath(QStringLiteral("board.db"));
    m_db = new Database(this);
    QString error;
    QVERIFY2(m_db->open(&error, path), qPrintable(error));
    m_store = new TaskRunStore(m_db, this);
}

void TestTaskRuns::cleanup()
{
    delete m_store;
    delete m_db;
    m_store = nullptr;
    m_db = nullptr;
}

void TestTaskRuns::acquireRejectsSecondActiveRun()
{
    const QVariantMap first = m_store->acquire(QStringLiteral("task-a"));
    QVERIFY(first.value(QStringLiteral("ok")).toBool());
    const QString runId = first.value(QStringLiteral("runId")).toString();
    QVERIFY(!runId.isEmpty());
    QCOMPARE(first.value(QStringLiteral("status")).toString(), QStringLiteral("running"));

    QCOMPARE(m_store->activeRun(QStringLiteral("task-a")).value(QStringLiteral("id")).toString(),
             runId);

    const QVariantMap second = m_store->acquire(QStringLiteral("task-a"));
    QVERIFY(!second.value(QStringLiteral("ok")).toBool());
    QCOMPARE(second.value(QStringLiteral("code")).toString(), QStringLiteral("busy"));
    QCOMPARE(second.value(QStringLiteral("runId")).toString(), runId);

    QVERIFY(m_store->heartbeat(runId));
    QVERIFY(m_store->release(runId, QStringLiteral("passed")));
    QVERIFY(m_store->activeRun(QStringLiteral("task-a")).isEmpty());
    QCOMPARE(m_store->run(runId).value(QStringLiteral("status")).toString(),
             QStringLiteral("passed"));

    const QVariantMap again = m_store->acquire(QStringLiteral("task-a"));
    QVERIFY(again.value(QStringLiteral("ok")).toBool());
    QVERIFY(m_store->release(again.value(QStringLiteral("runId")).toString(),
                             QStringLiteral("cancelled")));

    // 已经结算过的 run 不能再改状态
    QVERIFY(!m_store->release(runId, QStringLiteral("blocked")));
}

void TestTaskRuns::releaseWritesDetail()
{
    QVariantMap fields;
    fields.insert(QStringLiteral("agent"), QStringLiteral("opencode"));
    fields.insert(QStringLiteral("cwd"), QStringLiteral("E:/projects/NeonBoard"));
    fields.insert(QStringLiteral("workflow"), QStringLiteral("fix"));
    fields.insert(QStringLiteral("attempt"), 2);
    const QVariantMap created = m_store->acquire(QStringLiteral("task-b"), fields);
    QVERIFY(created.value(QStringLiteral("ok")).toBool());
    const QString runId = created.value(QStringLiteral("runId")).toString();

    const QVariantMap before = m_store->run(runId);
    QCOMPARE(before.value(QStringLiteral("agent")).toString(), QStringLiteral("opencode"));
    QCOMPARE(before.value(QStringLiteral("cwd")).toString(), QStringLiteral("E:/projects/NeonBoard"));
    QCOMPARE(before.value(QStringLiteral("workflow")).toString(), QStringLiteral("fix"));
    QCOMPARE(before.value(QStringLiteral("attempt")).toInt(), 2);
    QVERIFY(before.value(QStringLiteral("startedAt")).toLongLong() > 0);

    QVariantMap detail;
    detail.insert(QStringLiteral("exitCode"), 3);
    detail.insert(QStringLiteral("lastError"), QStringLiteral("构建失败"));
    detail.insert(QStringLiteral("logPath"), QStringLiteral("runs/demo.log"));
    detail.insert(QStringLiteral("scopeViolation"), QStringLiteral("docs/x.md"));
    QVERIFY(m_store->release(runId, QStringLiteral("blocked"), detail));

    const QVariantMap after = m_store->run(runId);
    QCOMPARE(after.value(QStringLiteral("status")).toString(), QStringLiteral("blocked"));
    QCOMPARE(after.value(QStringLiteral("exitCode")).toInt(), 3);
    QCOMPARE(after.value(QStringLiteral("lastError")).toString(), QStringLiteral("构建失败"));
    QCOMPARE(after.value(QStringLiteral("logPath")).toString(), QStringLiteral("runs/demo.log"));
    QCOMPARE(after.value(QStringLiteral("scopeViolation")).toString(), QStringLiteral("docs/x.md"));
    QVERIFY(after.value(QStringLiteral("finishedAt")).toLongLong() > 0);

    QCOMPARE(m_store->runsOf(QStringLiteral("task-b")).size(), 1);
}

void TestTaskRuns::orphanRunBecomesBlocked()
{
    const QVariantMap created = m_store->acquire(QStringLiteral("task-c"));
    QVERIFY(created.value(QStringLiteral("ok")).toBool());
    const QString runId = created.value(QStringLiteral("runId")).toString();

    // 心跳拨回 120 秒前，模拟进程崩溃后库里的残留
    const QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE task_run SET heartbeat_at = ? WHERE id = ?"));
    query.addBindValue(QDateTime::currentSecsSinceEpoch() - 120);
    query.addBindValue(runId);
    QVERIFY(query.exec());

    // 重新构造 = 应用重启，构造时即回收孤儿
    TaskRunStore restarted(m_db, this);
    QVERIFY(restarted.activeRun(QStringLiteral("task-c")).isEmpty());
    const QVariantMap blocked = restarted.run(runId);
    QCOMPARE(blocked.value(QStringLiteral("status")).toString(), QStringLiteral("blocked"));
    QVERIFY(blocked.value(QStringLiteral("lastError")).toString().contains(
        QStringLiteral("进程中断")));
}

void TestTaskRuns::schemaVersionStaysAtV2()
{
    const QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='task_run'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);

    QVERIFY(query.exec(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='idx_task_run_active'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    // 必须是部分索引（只锁活跃状态），否则同卡的终态历史 run 会把索引撞满
    QVERIFY(query.exec(QStringLiteral(
        "SELECT sql FROM sqlite_master WHERE type='index' AND name='idx_task_run_active'")));
    QVERIFY(query.next());
    QVERIFY(query.value(0).toString().contains(QStringLiteral("WHERE"), Qt::CaseInsensitive));

    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), Database::kSchemaVersion);
    QCOMPARE(Database::kSchemaVersion, 2);
}

QTEST_GUILESS_MAIN(TestTaskRuns)
#include "tst_taskruns.moc"
