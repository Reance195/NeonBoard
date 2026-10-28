// R7-05：夜间批量模式 —— 多选卡一次入队 / 预计总时长估算 / 单卡失败不阻断其余。
//
// 覆盖：
//   1. enqueueMany 按传入顺序入队，rejected 为空
//   2. 预计总时长：无历史用兜底值、按并发折算；有单卡历史则用该卡均值
//   3. enqueueNightBatch 返回 queued + estimate
//   4. 单卡失败（缺 agent）结算成 blocked，不阻断其余卡继续跑
//   5. schema 版本不变（本卡不加列 / 不改表）
//
// 载体：好卡走 cmd.exe（ComSpec，离线可跑），坏卡指向不存在的 agent 让它立刻失败。

#include "AgentProfile.h"
#include "Database.h"
#include "Scheduler.h"
#include "TaskExec.h"
#include "TaskModel.h"
#include "TaskRunner.h"
#include "TaskRunStore.h"

#include <QDir>
#include <QSet>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString kCmd = qEnvironmentVariable("ComSpec", "cmd.exe");
}

class TestNightBatch : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void enqueueManyKeepsOrder();
    void estimateUsesFallbackAndConcurrency();
    void estimateUsesPerCardHistory();
    void enqueueNightBatchReturnsEstimate();
    void oneFailureDoesNotBlockOthers();
    void schemaVersionUnchanged();

private:
    QString addCard(const QString &title);

    QTemporaryDir *m_dir = nullptr;
    Database *m_db = nullptr;
    TaskModel *m_tasks = nullptr;
    TaskRunStore *m_store = nullptr;
    AgentProfile *m_profiles = nullptr;
    TaskExec *m_exec = nullptr;
    TaskRunner *m_runner = nullptr;
    Scheduler *m_scheduler = nullptr;
};

QString TestNightBatch::addCard(const QString &title)
{
    return m_tasks->addTask(title, QStringLiteral("todo"), 1, QString(), 0);
}

void TestNightBatch::init()
{
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    qputenv("NEONBOARD_DATA_DIR", m_dir->path().toUtf8());

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

void TestNightBatch::cleanup()
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

void TestNightBatch::enqueueManyKeepsOrder()
{
    // 总开关关着 → 入队不派发，正好验证顺序
    const QString a = addCard(QStringLiteral("n-a"));
    const QString b = addCard(QStringLiteral("n-b"));
    const QString c = addCard(QStringLiteral("n-c"));

    const QVariantMap res = m_scheduler->enqueueMany({a, b, c});
    QVERIFY(res.value(QStringLiteral("ok")).toBool());
    QCOMPARE(res.value(QStringLiteral("queued")).toList().size(), 3);
    QVERIFY(res.value(QStringLiteral("rejected")).toList().isEmpty());

    const QVariantList waiting = m_store->waitingRuns();
    QCOMPARE(waiting.size(), 3);
    QCOMPARE(waiting.at(0).toMap().value(QStringLiteral("taskId")).toString(), a);
    QCOMPARE(waiting.at(1).toMap().value(QStringLiteral("taskId")).toString(), b);
    QCOMPARE(waiting.at(2).toMap().value(QStringLiteral("taskId")).toString(), c);

    // 同卡重复入队被拒，且计入 rejected，不影响其余
    const QVariantMap dup = m_scheduler->enqueueMany({a, a});
    QCOMPARE(dup.value(QStringLiteral("rejected")).toList().size(), 2);
}

void TestNightBatch::estimateUsesFallbackAndConcurrency()
{
    const QString a = addCard(QStringLiteral("n-a"));
    const QString b = addCard(QStringLiteral("n-b"));
    const QString c = addCard(QStringLiteral("n-c"));

    // 无历史：兜底 300s/张，并发 1 → 900s
    m_store->setConcurrency(1);
    const QVariantMap e1 = m_scheduler->nightBatchEstimate({a, b, c}, 300);
    QVERIFY(e1.value(QStringLiteral("ok")).toBool());
    QCOMPARE(e1.value(QStringLiteral("concurrency")).toInt(), 1);
    QCOMPARE(e1.value(QStringLiteral("totalSec")).toInt(), 900);
    QCOMPARE(e1.value(QStringLiteral("perCard")).toList().size(), 3);
    QVERIFY(e1.value(QStringLiteral("finishAt")).toLongLong() > 0);

    // 并发 2 → ceil(900 / 2) = 450
    m_store->setConcurrency(2);
    const QVariantMap e2 = m_scheduler->nightBatchEstimate({a, b, c}, 300);
    QCOMPARE(e2.value(QStringLiteral("concurrency")).toInt(), 2);
    QCOMPARE(e2.value(QStringLiteral("totalSec")).toInt(), 450);

    // 空列表 → 0，不报错
    const QVariantMap empty = m_scheduler->nightBatchEstimate({}, 300);
    QVERIFY(empty.value(QStringLiteral("ok")).toBool());
    QCOMPARE(empty.value(QStringLiteral("totalSec")).toInt(), 0);
    QCOMPARE(empty.value(QStringLiteral("finishAt")).toLongLong(), qint64(0));
}

void TestNightBatch::estimateUsesPerCardHistory()
{
    m_store->setConcurrency(1);
    const QString a = addCard(QStringLiteral("n-a"));
    const QString b = addCard(QStringLiteral("n-b"));

    // 给 a 造一条「跑过 60 秒」的历史 run
    const QString runId = m_store->acquire(a).value(QStringLiteral("runId")).toString();
    QVERIFY(!runId.isEmpty());
    QVERIFY(m_store->release(runId, QStringLiteral("passed"),
                             QVariantMap{{QStringLiteral("costMs"), 60000}}));

    const QVariantMap est = m_scheduler->nightBatchEstimate({a, b}, 300);
    const QVariantList perCard = est.value(QStringLiteral("perCard")).toList();
    QCOMPARE(perCard.size(), 2);
    // a 用自身历史（60s）；b 无历史 → 回落全局均值（也只有 a 那条 = 60s）
    QCOMPARE(perCard.at(0).toMap().value(QStringLiteral("sec")).toInt(), 60);
    QCOMPARE(perCard.at(1).toMap().value(QStringLiteral("sec")).toInt(), 60);
    QCOMPARE(est.value(QStringLiteral("totalSec")).toInt(), 120);
}

void TestNightBatch::enqueueNightBatchReturnsEstimate()
{
    QVERIFY(m_scheduler->pauseQueue()); // 暂停派发，稳定观察入队结果
    const QString a = addCard(QStringLiteral("n-a"));
    const QString b = addCard(QStringLiteral("n-b"));

    const QVariantMap res = m_scheduler->enqueueNightBatch({a, b});
    QCOMPARE(res.value(QStringLiteral("queued")).toList().size(), 2);
    const QVariantMap est = res.value(QStringLiteral("estimate")).toMap();
    QVERIFY(est.value(QStringLiteral("ok")).toBool());
    QVERIFY(est.value(QStringLiteral("totalSec")).toInt() > 0);
    QCOMPARE(m_store->waitingRuns().size(), 2);
}

void TestNightBatch::oneFailureDoesNotBlockOthers()
{
    m_profiles->setExecutorEnabled(true);

    // cmd.exe profile：promptMode=stdin，角色链/执行器拼的 prompt 不进 argv
    const QVariantMap profile{
        {QStringLiteral("id"), QStringLiteral("cmd-ok")},
        {QStringLiteral("label"), QStringLiteral("测试用 cmd")},
        {QStringLiteral("exe"), kCmd},
        {QStringLiteral("args"), QStringList{QStringLiteral("/c"), QStringLiteral("echo"),
                                             QStringLiteral("OK")}},
        {QStringLiteral("readonlyArgs"), QStringList()},
        {QStringLiteral("promptMode"), QStringLiteral("stdin")},
        {QStringLiteral("output"), QStringLiteral("text")},
        {QStringLiteral("defaultTimeoutMin"), 2},
    };
    QVERIFY(m_profiles->saveProfile(profile));

    const QString bad = addCard(QStringLiteral("n-bad"));
    const QString good1 = addCard(QStringLiteral("n-good1"));
    const QString good2 = addCard(QStringLiteral("n-good2"));
    for (const QString &id : {good1, good2}) {
        QVERIFY(m_exec->setExec(id, {{QStringLiteral("agent"), QStringLiteral("cmd-ok")},
                                     {QStringLiteral("cwd"), m_dir->path()}}));
    }
    QVERIFY(m_exec->setExec(bad, {{QStringLiteral("agent"), QStringLiteral("no-such-agent")},
                                  {QStringLiteral("cwd"), m_dir->path()}}));

    const QVariantMap res = m_scheduler->enqueueNightBatch({bad, good1, good2});
    QCOMPARE(res.value(QStringLiteral("queued")).toList().size(), 3);

    // 三条都要进终态：坏卡 blocked，好卡 passed —— 坏卡不阻断其余
    auto allSettled = [this, &bad, &good1, &good2]() {
        auto settled = [this](const QString &id) {
            const QVariantMap latest = m_store->latestRun(id);
            const QString status = latest.value(QStringLiteral("status")).toString();
            return status == QStringLiteral("passed") || status == QStringLiteral("blocked")
                   || status == QStringLiteral("cancelled");
        };
        return settled(bad) && settled(good1) && settled(good2) && m_store->waitingRuns().isEmpty();
    };
    QTRY_VERIFY_WITH_TIMEOUT(allSettled(), 60000);

    QCOMPARE(m_store->latestRun(bad).value(QStringLiteral("status")).toString(),
             QStringLiteral("blocked"));
    QCOMPARE(m_store->latestRun(good1).value(QStringLiteral("status")).toString(),
             QStringLiteral("passed"));
    QCOMPARE(m_store->latestRun(good2).value(QStringLiteral("status")).toString(),
             QStringLiteral("passed"));
}

void TestNightBatch::schemaVersionUnchanged()
{
    QSqlQuery query(QSqlDatabase::database(m_db->connectionName()));
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), Database::kSchemaVersion);
}

QTEST_MAIN(TestNightBatch)
#include "tst_nightbatch.moc"
