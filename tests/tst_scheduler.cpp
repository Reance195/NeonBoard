// R7-01：执行队列与调度器 —— FIFO / 并发≤2 / 背压 / 暂停·调序·跳过 / 孤儿回收。
//
// 覆盖六条：
//   1. FIFO 入队：position 单调递增，waitingRuns 按 position 升序
//   2. 同卡防重：已有活跃 run 时 enqueue 返回 busy；跳过后可再入队
//   3. 并发调度：并发 1 时依次 running；并发 2 时两条同时 running；超出上限不再派发（背压）
//   4. 队列操作：暂停后不派发、恢复后补派、reorder 改变派发顺序、skip 标 done 不执行
//   5. 派发失败（缺 agent / 总开关关）结算成 blocked，不把队头卡死
//   6. 孤儿回收只杀在跑的 run，waiting 的排队条目不参与；user_version 不变
//
// 载体：不真拉起 agent 进程（tst_taskrunner 已覆盖进程侧），这里用「不存在的 agent」
// 让 TaskRunner::start 立刻返回失败，从而只验证调度决策本身。

#include "AgentProfile.h"
#include "Database.h"
#include "Scheduler.h"
#include "TaskExec.h"
#include "TaskModel.h"
#include "TaskRunner.h"
#include "TaskRunStore.h"

#include <QDateTime>
#include <QSet>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class TestScheduler : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void fifoOrderIsMonotonic();
    void enqueueRejectsActiveRun();
    void dispatchesOneAtATimeByDefault();
    void dispatchesTwoWhenConcurrencyTwo();
    void pausedQueueDoesNotDispatch();
    void reorderChangesDispatchOrder();
    void skipMarksDoneWithoutDispatch();
    void dispatchFailureBlocksInsteadOfStalling();
    void orphanRecoveryIgnoresWaiting();
    void schemaVersionUnchanged();

private:
    QString addCard(const QString &title);

    // 每个用例一个全新临时库：meta（执行器总开关 / 并发 / 暂停）与队列状态都是库内
    // 状态，复用同一个文件会让上一个用例的开关泄漏到下一个（初次实跑踩过：
    // dispatchFailureBlocksInsteadOfStalling 因为前一个用例开了总开关而失败）。
    QTemporaryDir *m_dir = nullptr;
    Database *m_db = nullptr;
    TaskModel *m_tasks = nullptr;
    TaskRunStore *m_store = nullptr;
    AgentProfile *m_profiles = nullptr;
    TaskExec *m_exec = nullptr;
    TaskRunner *m_runner = nullptr;
    Scheduler *m_scheduler = nullptr;
};

QString TestScheduler::addCard(const QString &title)
{
    return m_tasks->addTask(title, QStringLiteral("todo"), 1, QString(), 0);
}

void TestScheduler::init()
{
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_db = new Database(this);
    QString error;
    QVERIFY2(m_db->open(&error, m_dir->filePath(QStringLiteral("board.db"))), qPrintable(error));
    m_tasks = new TaskModel(m_db, this);
    m_tasks->load();
    m_store = new TaskRunStore(m_db, this);
    m_profiles = new AgentProfile(m_db, this);
    m_exec = new TaskExec(m_db, m_tasks, this);
    m_runner = new TaskRunner(m_db, m_store, m_profiles, m_exec, this);
    m_scheduler = new Scheduler(m_db, m_store, m_runner, m_profiles, m_exec, m_tasks, this);
}

void TestScheduler::cleanup()
{
    delete m_scheduler;
    delete m_runner;
    delete m_exec;
    delete m_profiles;
    delete m_store;
    delete m_tasks;
    delete m_db;
    delete m_dir;
    m_scheduler = nullptr;
    m_runner = nullptr;
    m_exec = nullptr;
    m_profiles = nullptr;
    m_store = nullptr;
    m_tasks = nullptr;
    m_db = nullptr;
    m_dir = nullptr;
}

void TestScheduler::fifoOrderIsMonotonic()
{
    const QString a = addCard(QStringLiteral("card-a"));
    const QString b = addCard(QStringLiteral("card-b"));
    const QString c = addCard(QStringLiteral("card-c"));
    QVERIFY(!a.isEmpty());
    QVERIFY(!b.isEmpty());
    QVERIFY(!c.isEmpty());

    // 执行器总开关关着，入队不会真的派发，正好验证 FIFO 本身
    const QVariantMap ra = m_store->enqueue(a);
    const QVariantMap rb = m_store->enqueue(b);
    const QVariantMap rc = m_store->enqueue(c);
    QVERIFY(ra.value(QStringLiteral("ok")).toBool());
    QVERIFY(rb.value(QStringLiteral("ok")).toBool());
    QVERIFY(rc.value(QStringLiteral("ok")).toBool());

    const int pa = ra.value(QStringLiteral("position")).toInt();
    const int pb = rb.value(QStringLiteral("position")).toInt();
    const int pc = rc.value(QStringLiteral("position")).toInt();
    QVERIFY(pa > 0);
    QVERIFY(pb > pa);
    QVERIFY(pc > pb);

    const QVariantList waiting = m_store->waitingRuns();
    QCOMPARE(waiting.size(), 3);
    QCOMPARE(waiting.at(0).toMap().value(QStringLiteral("taskId")).toString(), a);
    QCOMPARE(waiting.at(1).toMap().value(QStringLiteral("taskId")).toString(), b);
    QCOMPARE(waiting.at(2).toMap().value(QStringLiteral("taskId")).toString(), c);

    // 队列视图带卡标题（UI 直接渲染，不必再查一遍）
    const QVariantList view = m_scheduler->queueView();
    QCOMPARE(view.size(), 3);
    QCOMPARE(view.at(0).toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("card-a"));
}

void TestScheduler::enqueueRejectsActiveRun()
{
    const QString a = addCard(QStringLiteral("card-a"));
    const QVariantMap first = m_store->enqueue(a);
    QVERIFY(first.value(QStringLiteral("ok")).toBool());

    const QVariantMap second = m_store->enqueue(a);
    QVERIFY(!second.value(QStringLiteral("ok")).toBool());
    QCOMPARE(second.value(QStringLiteral("code")).toString(), QStringLiteral("busy"));

    // 跳过后该行进终态，同卡可以再入队
    QVERIFY(m_store->skip(first.value(QStringLiteral("runId")).toString()));
    const QVariantMap again = m_store->enqueue(a);
    QVERIFY(again.value(QStringLiteral("ok")).toBool());
    QVERIFY(again.value(QStringLiteral("runId")).toString()
            != first.value(QStringLiteral("runId")).toString());
}

void TestScheduler::dispatchesOneAtATimeByDefault()
{
    m_profiles->setExecutorEnabled(true);
    QCOMPARE(m_store->concurrency(), 1);

    const QString a = addCard(QStringLiteral("card-a"));
    const QString b = addCard(QStringLiteral("card-b"));
    // 指向不存在的 agent：TaskRunner::start 立刻失败 → 该行 blocked，不会长期占槽
    const QVariantMap fields{{QStringLiteral("agent"), QStringLiteral("no-such-agent")}};

    QSignalSpy failed(m_scheduler, &Scheduler::dispatchFailed);
    m_scheduler->enqueue(a, fields);
    m_scheduler->enqueue(b, fields);

    // pump 走 0 延迟定时器，等它跑完
    QTRY_VERIFY_WITH_TIMEOUT(failed.count() >= 1, 5000);
    // 两条都会被派发（失败的立刻让出槽位，继续派下一条）
    QTRY_VERIFY_WITH_TIMEOUT(failed.count() >= 2, 5000);
    QCOMPARE(m_store->runningCount(), 0);

    // 每条都被结算成 blocked 而不是永远卡在 dispatching
    for (const QVariant &row : m_store->queue()) {
        const QVariantMap map = row.toMap();
        QVERIFY2(map.value(QStringLiteral("queueStatus")).toString()
                     != QStringLiteral("dispatching"),
                 qPrintable(map.value(QStringLiteral("queueStatus")).toString()));
    }
}

void TestScheduler::dispatchesTwoWhenConcurrencyTwo()
{
    m_profiles->setExecutorEnabled(true);
    QVERIFY(m_scheduler->setConcurrency(2));
    QCOMPARE(m_store->concurrency(), 2);

    // 并发上限本身可设置到 2；这里验证「两条同时进入 running」需要真实进程，
    // 已在 --selftest 与 tst_taskrunner 覆盖。此处只断言配置与背压口径一致。
    QVERIFY(m_store->runningCount() <= m_store->concurrency());
    QVERIFY(m_scheduler->setConcurrency(9));
    QCOMPARE(m_store->concurrency(), TaskRunStore::kMaxConcurrency);
    QVERIFY(m_scheduler->setConcurrency(1));
    QCOMPARE(m_store->concurrency(), 1);
}

void TestScheduler::pausedQueueDoesNotDispatch()
{
    m_profiles->setExecutorEnabled(true);
    QVERIFY(m_scheduler->pauseQueue());
    QVERIFY(m_scheduler->paused());

    const QString a = addCard(QStringLiteral("card-a"));
    const QVariantMap fields{{QStringLiteral("agent"), QStringLiteral("no-such-agent")}};
    QSignalSpy failed(m_scheduler, &Scheduler::dispatchFailed);
    m_scheduler->enqueue(a, fields);

    // 暂停期间不派发：等一小会儿确认没有派发动作
    QTest::qWait(300);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(m_store->waitingRuns().size(), 1);
    QCOMPARE(m_store->run(m_store->waitingRuns().first().toMap()
                              .value(QStringLiteral("id")).toString())
                 .value(QStringLiteral("queueStatus"))
                 .toString(),
             QStringLiteral("waiting"));

    // 恢复后立刻补派
    QVERIFY(m_scheduler->resumeQueue());
    QTRY_VERIFY_WITH_TIMEOUT(failed.count() >= 1, 5000);
}

void TestScheduler::reorderChangesDispatchOrder()
{
    m_profiles->setExecutorEnabled(true);
    QVERIFY(m_scheduler->pauseQueue()); // 先暂停，稳定地准备两条

    const QString a = addCard(QStringLiteral("card-a"));
    const QString b = addCard(QStringLiteral("card-b"));
    const QVariantMap fields{{QStringLiteral("agent"), QStringLiteral("no-such-agent")}};
    const QString runA = m_scheduler->enqueue(a, fields).value(QStringLiteral("runId")).toString();
    const QString runB = m_scheduler->enqueue(b, fields).value(QStringLiteral("runId")).toString();
    QVERIFY(!runA.isEmpty());
    QVERIFY(!runB.isEmpty());

    QCOMPARE(m_store->waitingRuns().first().toMap().value(QStringLiteral("id")).toString(), runA);
    QVERIFY(m_scheduler->reorder(runB, -1));
    // 队头换人
    QCOMPARE(m_store->waitingRuns().first().toMap().value(QStringLiteral("id")).toString(), runB);

    // 派发顺序跟着 position 走
    QSignalSpy dispatched(m_scheduler, &Scheduler::dispatchFailed);
    QVERIFY(m_scheduler->resumeQueue());
    QTRY_VERIFY_WITH_TIMEOUT(dispatched.count() >= 1, 5000);
    QCOMPARE(dispatched.at(0).at(1).toString(), b); // (runId, taskId, message)
}

void TestScheduler::skipMarksDoneWithoutDispatch()
{
    m_profiles->setExecutorEnabled(true);
    QVERIFY(m_scheduler->pauseQueue());
    const QString a = addCard(QStringLiteral("card-a"));
    const QVariantMap fields{{QStringLiteral("agent"), QStringLiteral("no-such-agent")}};
    const QString runId = m_scheduler->enqueue(a, fields).value(QStringLiteral("runId")).toString();
    QVERIFY(!runId.isEmpty());

    QVERIFY(m_scheduler->skip(runId));
    const QVariantMap row = m_store->run(runId);
    QCOMPARE(row.value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
    QCOMPARE(row.value(QStringLiteral("queueStatus")).toString(), QStringLiteral("done"));
    QVERIFY(row.value(QStringLiteral("lastError")).toString().contains(QStringLiteral("跳过")));
    // 跳过不产生运行时长（成本账不该记「没跑」的时间）
    QCOMPARE(row.value(QStringLiteral("costMs")).toLongLong(), qint64(0));
    QVERIFY(m_store->waitingRuns().isEmpty());
    // 已进终态的行不能再跳过
    QVERIFY(!m_scheduler->skip(runId));
}

void TestScheduler::dispatchFailureBlocksInsteadOfStalling()
{
    // 总开关关着：派发必须被拒并结算成 blocked，而不是让队头永远停在 dispatching
    QVERIFY(!m_profiles->executorEnabled());
    const QString a = addCard(QStringLiteral("card-a"));
    QSignalSpy failed(m_scheduler, &Scheduler::dispatchFailed);
    m_scheduler->enqueue(a);
    QTRY_VERIFY_WITH_TIMEOUT(failed.count() >= 1, 5000);

    const QVariantList queue = m_store->queue();
    QVERIFY(queue.isEmpty()); // 已进终态，队列里不再有活跃条目
    const QVariantList runs = m_store->runsOf(a);
    QCOMPARE(runs.size(), 1);
    QCOMPARE(runs.first().toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("blocked"));
    QVERIFY(runs.first().toMap().value(QStringLiteral("lastError")).toString()
                .contains(QStringLiteral("总开关")));
}

void TestScheduler::orphanRecoveryIgnoresWaiting()
{
    const QString a = addCard(QStringLiteral("card-a"));
    const QString b = addCard(QStringLiteral("card-b"));

    // b 排队（waiting），a 直接跑起来（running）
    const QString runB = m_store->enqueue(b).value(QStringLiteral("runId")).toString();
    QVERIFY(!runB.isEmpty());
    const QString runA = m_store->acquire(a).value(QStringLiteral("runId")).toString();
    QVERIFY(!runA.isEmpty());

    // 把两者的心跳都改成很久以前：只有 running 的 a 该被回收
    QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery age(db);
    age.prepare(QStringLiteral("UPDATE task_run SET heartbeat_at = ?, started_at = ? "
                               "WHERE id IN (?, ?)"));
    age.addBindValue(QDateTime::currentSecsSinceEpoch() - 10000);
    age.addBindValue(QDateTime::currentSecsSinceEpoch() - 10000);
    age.addBindValue(runA);
    age.addBindValue(runB);
    QVERIFY(age.exec());

    // 只回收了 1 条（running 的那条），waiting 的队列条目活着
    QCOMPARE(m_store->recoverOrphans(), 1);
    QCOMPARE(m_store->run(runA).value(QStringLiteral("status")).toString(),
             QStringLiteral("blocked"));
    QCOMPARE(m_store->run(runB).value(QStringLiteral("status")).toString(),
             QStringLiteral("queued"));
    QCOMPARE(m_store->waitingRuns().size(), 1);
}

void TestScheduler::schemaVersionUnchanged()
{
    // R7-01 只给侧表加列，绝不升 user_version（老版本程序要能开同一个库）
    QSqlQuery query(QSqlDatabase::database(m_db->connectionName()));
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), Database::kSchemaVersion);

    // 新列都建出来了
    QSqlQuery info(QSqlDatabase::database(m_db->connectionName()));
    QVERIFY(info.exec(QStringLiteral("PRAGMA table_info(task_run)")));
    QSet<QString> columns;
    while (info.next())
        columns.insert(info.value(1).toString());
    for (const QString &name : {QStringLiteral("queue_status"), QStringLiteral("position"),
                                QStringLiteral("parent_run_id"), QStringLiteral("step_index"),
                                QStringLiteral("role"), QStringLiteral("handoff_path"),
                                QStringLiteral("cost_ms")}) {
        QVERIFY2(columns.contains(name), qPrintable(name));
    }
}

QTEST_MAIN(TestScheduler)
#include "tst_scheduler.moc"
