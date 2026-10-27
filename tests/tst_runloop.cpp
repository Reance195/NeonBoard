// R6-08：Loop 失败摘要回灌与有限重试。
//
// 覆盖五条：
//   1. maxAttempts 钳制：默认 1（不循环，行为与 R6-04 一致）、上限 5
//   2. 失败自动重试：新 run attempt+1、每轮日志独立、历史 run 保留 blocked
//   3. 失败摘要注入：重试轮的 prompt 里带「上一轮失败摘要」与 lastError
//   4. 人取消后不再自动重试
//   5. 轮数耗尽：终态 blocked 且 lastError 提示「已重试 N 轮，需人工介入」
//
// 载体用 cmd.exe（ComSpec 全路径）：`cmd /c exit N` 稳定可重复。
// 重试注入验证：cmd /c echo %* 不可靠，改用「第三轮才成功」的退出码序列不可行
// （cmd 无状态），所以用 startRaw + 退出码非 0 的固定命令验证轮数与 attempt；
// prompt 注入内容由 failureSummary 静态函数单测覆盖。

#include "AgentProfile.h"
#include "Database.h"
#include "TaskExec.h"
#include "TaskModel.h"
#include "TaskRunStore.h"
#include "TaskRunner.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString kCmd = qEnvironmentVariable("ComSpec", "cmd.exe");
} // namespace

class TestRunLoop : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void defaultNoRetry();
    void retriesUpToMaxAttempts();
    void cancelStopsRetrying();
    void exhaustedSaysManualIntervention();
    void failureSummaryTruncates();

private:
    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskRunStore *m_store = nullptr;
    AgentProfile *m_profiles = nullptr;
    TaskExec *m_exec = nullptr;
    TaskRunner *m_runner = nullptr;
};

void TestRunLoop::init()
{
    QVERIFY(m_dir.isValid());
    m_db = new Database(this);
    QString error;
    QVERIFY2(m_db->open(&error, m_dir.filePath(QStringLiteral("board.db"))), qPrintable(error));
    m_store = new TaskRunStore(m_db, this);
    m_profiles = new AgentProfile(m_db, this);
    m_exec = new TaskExec(m_db, nullptr, this);
    m_runner = new TaskRunner(m_db, m_store, m_profiles, m_exec, this);
    m_profiles->setExecutorEnabled(true);
}

void TestRunLoop::cleanup()
{
    delete m_runner;
    delete m_exec;
    delete m_profiles;
    delete m_store;
    delete m_db;
    m_runner = nullptr;
    m_exec = nullptr;
    m_profiles = nullptr;
    m_store = nullptr;
    m_db = nullptr;
}

void TestRunLoop::defaultNoRetry()
{
    // maxAttempts 缺省 = 1：失败只留一条 blocked run，与 R6-04 行为一致
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap r = m_runner->startRaw(
        QStringLiteral("task-single"), kCmd,
        {QStringLiteral("/c"), QStringLiteral("exit"), QStringLiteral("4")}, m_dir.path(), 60);
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    QVERIFY2(finished.wait(15000), "run 未结束");
    QCOMPARE(finished.count(), 1);

    const QVariantList runs = m_store->runsOf(QStringLiteral("task-single"));
    QCOMPARE(runs.size(), 1);
    QCOMPARE(runs.at(0).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("blocked"));
    QCOMPARE(runs.at(0).toMap().value(QStringLiteral("attempt")).toInt(), 1);
}

void TestRunLoop::retriesUpToMaxAttempts()
{
    // maxAttempts=3：固定失败命令 → 3 条 run（attempt 1/2/3），全部 blocked，
    // 每轮日志独立落盘
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap r =
        m_runner->startRaw(QStringLiteral("task-loop"), kCmd,
                           {QStringLiteral("/c"), QStringLiteral("exit"), QStringLiteral("5")},
                           m_dir.path(), 60, false, QStringLiteral("完成任务"), 3);
    QVERIFY(r.value(QStringLiteral("ok")).toBool());

    // 每轮结束发一次 runFinished：等满 3 次
    for (int i = finished.count(); i < 3; ++i)
        QVERIFY2(finished.wait(15000), "第 %d 轮未结束");
    QCOMPARE(finished.count(), 3);

    const QVariantList runs = m_store->runsOf(QStringLiteral("task-loop"));
    QCOMPARE(runs.size(), 3);
    QSet<int> attempts;
    QSet<QString> logPaths;
    for (const QVariant &v : runs) {
        const QVariantMap run = v.toMap();
        QCOMPARE(run.value(QStringLiteral("status")).toString(), QStringLiteral("blocked"));
        attempts.insert(run.value(QStringLiteral("attempt")).toInt());
        const QString logPath = run.value(QStringLiteral("logPath")).toString();
        QVERIFY(!logPath.isEmpty());
        QVERIFY(QFile::exists(logPath));
        logPaths.insert(logPath);
    }
    QCOMPARE(attempts, (QSet<int>{1, 2, 3}));
    QCOMPARE(logPaths.size(), 3); // 每轮日志独立文件

    // 锁已释放：终态后能再取
    QVERIFY(m_store->activeRun(QStringLiteral("task-loop")).isEmpty());
}

void TestRunLoop::cancelStopsRetrying()
{
    // 挂住的命令 + maxAttempts=3：人取消 → cancelled，且不再自动重试
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap r =
        m_runner->startRaw(QStringLiteral("task-cancel"), kCmd,
                           {QStringLiteral("/c"), QStringLiteral("ping"), QStringLiteral("-n"),
                            QStringLiteral("60"), QStringLiteral("127.0.0.1")},
                           m_dir.path(), 120, false, QString(), 3);
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    const QString runId = r.value(QStringLiteral("runId")).toString();

    QVERIFY(m_runner->stop(runId));
    QVERIFY2(finished.wait(15000), "取消未生效");
    QCOMPARE(finished.count(), 1); // 只有取消这一轮，没有后续重试

    const QVariantList runs = m_store->runsOf(QStringLiteral("task-cancel"));
    QCOMPARE(runs.size(), 1);
    QCOMPARE(runs.at(0).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("cancelled"));

    // 等一拍确认没有延迟重试进来
    QTest::qWait(500);
    QCOMPARE(m_store->runsOf(QStringLiteral("task-cancel")).size(), 1);
}

void TestRunLoop::exhaustedSaysManualIntervention()
{
    // maxAttempts=2：耗尽后最后一条 run 的 lastError 带「需人工介入」提示
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap r =
        m_runner->startRaw(QStringLiteral("task-exhaust"), kCmd,
                           {QStringLiteral("/c"), QStringLiteral("exit"), QStringLiteral("6")},
                           m_dir.path(), 60, false, QString(), 2);
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    for (int i = finished.count(); i < 2; ++i)
        QVERIFY2(finished.wait(15000), "run 未结束");
    QCOMPARE(finished.count(), 2);

    const QVariantMap latest = m_store->latestRun(QStringLiteral("task-exhaust"));
    QCOMPARE(latest.value(QStringLiteral("status")).toString(), QStringLiteral("blocked"));
    QCOMPARE(latest.value(QStringLiteral("attempt")).toInt(), 2);
    QVERIFY2(
        latest.value(QStringLiteral("lastError")).toString().contains(QStringLiteral("需人工介入")),
        qPrintable(latest.value(QStringLiteral("lastError")).toString()));
}

void TestRunLoop::failureSummaryTruncates()
{
    // 静态摘要函数：末尾 200 行 + 8000 字符截断 + 单行化 + lastError 前置
    QByteArray log;
    for (int i = 1; i <= 300; ++i)
        log += QByteArray("line ") + QByteArray::number(i) + "\n";

    const QString summary = TaskRunner::failureSummary(log, QStringLiteral("进程退出码 3"));
    QVERIFY(summary.startsWith(QStringLiteral("进程退出码 3")));
    QVERIFY(!summary.contains(QLatin1Char('\n')));         // 单行化
    QVERIFY(summary.contains(QStringLiteral("line 300"))); // 保留末尾
    QVERIFY(!summary.contains(QStringLiteral("line 1 "))); // 掐掉开头
    QVERIFY(summary.size() <= 8100);

    // 空日志只有 lastError
    const QString only = TaskRunner::failureSummary(QByteArray(), QStringLiteral("超时"));
    QCOMPARE(only, QStringLiteral("超时"));
}

QTEST_GUILESS_MAIN(TestRunLoop)
#include "tst_runloop.moc"
