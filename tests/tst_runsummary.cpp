// R6-06：执行结果回写 —— 摘要（状态/耗时/退出码/末尾日志）、重试 attempt 递增、
// 取消不改看板列。
//
// 覆盖五条：
//   1. 无 run 的卡 summaryOf 返回 hasRun=false
//   2. 失败 run 的摘要：status=blocked + lastError + durationSec + logTail 末尾行
//   3. 重试：缺省 attempt 自动 = 上一次 + 1；显式 attempt 仍被尊重；历史 run 不覆盖
//   4. latestRun 活跃优先：终态历史 run 在，新活跃 run 时 latestRun 返回活跃的
//   5. 取消 run 不改看板列：task 表状态保持 todo，user_version 仍为 2

#include "Database.h"
#include "TaskModel.h"
#include "TaskRunStore.h"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class TestRunSummary : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void summaryWithoutRun();
    void failedRunSummary();
    void retryAttemptIncrements();
    void latestRunPrefersActive();
    void cancelKeepsBoardColumn();

private:
    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_tasks = nullptr;
    TaskRunStore *m_store = nullptr;
};

void TestRunSummary::init()
{
    m_db = new Database(this);
    QString error;
    QVERIFY2(m_db->open(&error, m_dir.filePath(QStringLiteral("board.db"))), qPrintable(error));
    m_tasks = new TaskModel(m_db, this);
    m_store = new TaskRunStore(m_db, this);
}

void TestRunSummary::cleanup()
{
    delete m_store;
    delete m_tasks;
    delete m_db;
    m_store = nullptr;
    m_tasks = nullptr;
    m_db = nullptr;
}

void TestRunSummary::summaryWithoutRun()
{
    const QVariantMap s = m_store->summaryOf(QStringLiteral("task-none"));
    QVERIFY(!s.value(QStringLiteral("hasRun")).toBool());
    QVERIFY(m_store->latestRun(QStringLiteral("task-none")).isEmpty());
}

void TestRunSummary::failedRunSummary()
{
    // 造一个 run：写日志文件 → release blocked 带上 exitCode / lastError / logPath
    const QVariantMap created = m_store->acquire(QStringLiteral("task-fail"));
    QVERIFY(created.value(QStringLiteral("ok")).toBool());
    const QString runId = created.value(QStringLiteral("runId")).toString();

    const QString logPath = m_dir.filePath(QStringLiteral("run.log"));
    QFile f(logPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    for (int i = 1; i <= 40; ++i)
        f.write(QByteArray("line ") + QByteArray::number(i) + "\n");
    f.close();

    QVariantMap detail;
    detail.insert(QStringLiteral("exitCode"), 3);
    detail.insert(QStringLiteral("lastError"), QStringLiteral("构建失败"));
    detail.insert(QStringLiteral("logPath"), logPath);
    QVERIFY(m_store->release(runId, QStringLiteral("blocked"), detail));

    const QVariantMap s = m_store->summaryOf(QStringLiteral("task-fail"), 5);
    QVERIFY(s.value(QStringLiteral("hasRun")).toBool());
    QCOMPARE(s.value(QStringLiteral("status")).toString(), QStringLiteral("blocked"));
    QCOMPARE(s.value(QStringLiteral("exitCode")).toInt(), 3);
    QCOMPARE(s.value(QStringLiteral("lastError")).toString(), QStringLiteral("构建失败"));
    QVERIFY(s.value(QStringLiteral("durationSec")).toLongLong() >= 0);
    // logTail 只带末尾 5 行
    const QStringList tail = s.value(QStringLiteral("logTail")).toString().split(QLatin1Char('\n'));
    QCOMPARE(tail.size(), 5);
    QCOMPARE(tail.first(), QStringLiteral("line 36"));
    QCOMPARE(tail.last(), QStringLiteral("line 40"));
}

void TestRunSummary::retryAttemptIncrements()
{
    // 第一轮：缺省 attempt = 1
    QVariantMap r1 = m_store->acquire(QStringLiteral("task-retry"));
    QVERIFY(r1.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_store->run(r1.value(QStringLiteral("runId")).toString())
                 .value(QStringLiteral("attempt")).toInt(), 1);
    QVERIFY(m_store->release(r1.value(QStringLiteral("runId")).toString(),
                             QStringLiteral("blocked")));

    // 重试：缺省 attempt 自动 = 2；历史 run 仍在（不覆盖）
    QVariantMap r2 = m_store->acquire(QStringLiteral("task-retry"));
    QVERIFY(r2.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_store->run(r2.value(QStringLiteral("runId")).toString())
                 .value(QStringLiteral("attempt")).toInt(), 2);
    QVERIFY(m_store->release(r2.value(QStringLiteral("runId")).toString(),
                             QStringLiteral("blocked")));

    QCOMPARE(m_store->runsOf(QStringLiteral("task-retry")).size(), 2);
    QCOMPARE(m_store->run(r1.value(QStringLiteral("runId")).toString())
                 .value(QStringLiteral("attempt")).toInt(), 1);

    // 第三轮 = 3
    QVariantMap r3 = m_store->acquire(QStringLiteral("task-retry"));
    QVERIFY(r3.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_store->run(r3.value(QStringLiteral("runId")).toString())
                 .value(QStringLiteral("attempt")).toInt(), 3);
    QVERIFY(m_store->release(r3.value(QStringLiteral("runId")).toString(),
                             QStringLiteral("passed")));

    // 显式 attempt 仍被尊重（R6-08 的循环重试要能指定轮次）
    QVariantMap r4 = m_store->acquire(QStringLiteral("task-retry"),
                                       {{QStringLiteral("attempt"), 9}});
    QVERIFY(r4.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_store->run(r4.value(QStringLiteral("runId")).toString())
                 .value(QStringLiteral("attempt")).toInt(), 9);
    QVERIFY(m_store->release(r4.value(QStringLiteral("runId")).toString(),
                             QStringLiteral("cancelled")));
}

void TestRunSummary::latestRunPrefersActive()
{
    QVariantMap done = m_store->acquire(QStringLiteral("task-latest"));
    QVERIFY(m_store->release(done.value(QStringLiteral("runId")).toString(),
                             QStringLiteral("passed")));
    QCOMPARE(m_store->latestRun(QStringLiteral("task-latest"))
                 .value(QStringLiteral("status")).toString(), QStringLiteral("passed"));

    QVariantMap active = m_store->acquire(QStringLiteral("task-latest"));
    QVERIFY(active.value(QStringLiteral("ok")).toBool());
    // 活跃 run 优先于终态历史
    QCOMPARE(m_store->latestRun(QStringLiteral("task-latest"))
                 .value(QStringLiteral("id")).toString(),
             active.value(QStringLiteral("runId")).toString());
    QVERIFY(m_store->release(active.value(QStringLiteral("runId")).toString(),
                             QStringLiteral("cancelled")));
}

void TestRunSummary::cancelKeepsBoardColumn()
{
    // 建真实任务卡，跑一个 run 再取消：看板列必须纹丝不动（程序不自动改列）
    const QString taskId = m_tasks->addTask(QStringLiteral("取消不改列"),
                                             QStringLiteral("todo"), 1, QString(), 0);
    QVERIFY(!taskId.isEmpty());

    const QVariantMap r = m_store->acquire(taskId);
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    QVERIFY(m_store->release(r.value(QStringLiteral("runId")).toString(),
                             QStringLiteral("cancelled"),
                             {{QStringLiteral("lastError"), QStringLiteral("已手动终止")}}));

    QCOMPARE(m_tasks->taskById(taskId).value(QStringLiteral("status")).toString(),
             QStringLiteral("todo"));

    const QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);
}

QTEST_GUILESS_MAIN(TestRunSummary)
#include "tst_runsummary.moc"
