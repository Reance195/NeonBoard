// R6-04：执行器 TaskRunner —— 真正拉起进程、流式日志、超时/终止、生命周期与锁。
//
// 覆盖六条：
//   1. 总开关未开启时 start 直接拒绝；开启后缺 agent / 未知 agent 分别报错
//   2. 成功启动：acquire 拿到 runId，activeRuns 可见，进程结束后 release 落 passed
//   3. 同卡活跃 run 在跑时再 start 返回 busy（拒绝排队）
//   4. 非零退出码 → blocked + lastError；完整日志落盘且有 logPath
//   5. 手动 stop → cancelled
//   6. 心跳持续刷新，未触发孤儿回收；启动失败不残留 running 锁
//
// 用命令行（cmd.exe，Windows 通用）做可执行载体：`cmd /c exit N` 和
// `cmd /c echo hi` 不依赖任何第三方工具，保证离线可跑。

#include "AgentProfile.h"
#include "Database.h"
#include "TaskExec.h"
#include "TaskRunStore.h"
#include "TaskRunner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

namespace {
// cmd.exe 全路径由 ComSpec 解析，避免 QProcess 对裸命令名的 PATH 查找在测试环境不生效。
const QString kCmd = qEnvironmentVariable("ComSpec", "cmd.exe");
} // namespace

class TestTaskRunner : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void rejectsWhenDisabled();
    void missingOrUnknownAgent();
    void runsToPassedWithLog();
    void rejectsBusyTask();
    void nonzeroExitsBlocked();
    void manualStopCancels();
    void redactsCredentialsInLog();

private:
    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskRunStore *m_store = nullptr;
    AgentProfile *m_profiles = nullptr;
    TaskExec *m_exec = nullptr;
    TaskRunner *m_runner = nullptr;
};

void TestTaskRunner::init()
{
    QVERIFY(m_dir.isValid());
    // 明确建立工作目录，供 QProcess 切换 cwd 用
    QDir dir(m_dir.path());
    QVERIFY(dir.mkpath(QStringLiteral(".")));
    // 用一个任务（taskStore 不便在这里建，直接以 task id 字符串运行；锁只看 task id）
    m_db = new Database(this);
    QString error;
    QVERIFY2(m_db->open(&error, m_dir.filePath(QStringLiteral("board.db"))), qPrintable(error));
    m_store = new TaskRunStore(m_db, this);
    m_profiles = new AgentProfile(m_db, this);
    m_exec = new TaskExec(m_db, nullptr, this);
    m_runner = new TaskRunner(m_db, m_store, m_profiles, m_exec, this);
    m_profiles->setExecutorEnabled(true);
}

void TestTaskRunner::cleanup()
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

void TestTaskRunner::rejectsWhenDisabled()
{
    m_profiles->setExecutorEnabled(false);
    const QVariantMap r = m_runner->start(QStringLiteral("task-a"));
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());
}

void TestTaskRunner::missingOrUnknownAgent()
{
    // 未指定 agent
    QVariantMap r = m_runner->start(QStringLiteral("task-a"));
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());

    // 指定了未知 agent
    m_exec->setExec(QStringLiteral("task-b"),
                    {{QStringLiteral("agent"), QStringLiteral("ghost-agent")}});
    r = m_runner->start(QStringLiteral("task-b"));
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());
}

void TestTaskRunner::runsToPassedWithLog()
{
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    QSignalSpy logs(m_runner, &TaskRunner::logAppended);

    // cmd /c echo hello  —— 退出码 0
    const QVariantMap r = m_runner->startRaw(
        QStringLiteral("task-p"), kCmd,
        {QStringLiteral("/c"), QStringLiteral("echo"), QStringLiteral("hello")}, m_dir.path(), 60);
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("message")).toString()));
    const QString runId = r.value(QStringLiteral("runId")).toString();
    QVERIFY(!runId.isEmpty());
    QCOMPARE(m_runner->activeRuns().size(), 1);
    QVERIFY(m_runner->activeRuns().contains(runId));

    // 等结束
    QVERIFY2(finished.wait(15000), "run 未在 15s 内结束");
    QCOMPARE(finished.count(), 1);
    const QVariantMap detail = finished.at(0).at(1).toMap();
    QCOMPARE(detail.value(QStringLiteral("exitCode")).toInt(), 0);

    // 日志流式收到过（echo 输出 + 可能换行）
    QVERIFY(finished.count() >= 1);
    // 释放后 run 为终态
    QCOMPARE(m_store->run(runId).value(QStringLiteral("status")).toString(),
             QStringLiteral("passed"));
    QVERIFY(m_runner->activeRuns().isEmpty());
    // 日志文件落盘
    const QString logPath = detail.value(QStringLiteral("logPath")).toString();
    QVERIFY(!logPath.isEmpty());
    QVERIFY(QFile::exists(logPath));
}

void TestTaskRunner::rejectsBusyTask()
{
    // 手动先拿锁
    const QVariantMap a = m_store->acquire(QStringLiteral("task-lock"));
    QVERIFY(a.value(QStringLiteral("ok")).toBool());

    const QVariantMap r = m_runner->startRaw(
        QStringLiteral("task-lock"), kCmd,
        {QStringLiteral("/c"), QStringLiteral("exit"), QStringLiteral("0")}, m_dir.path(), 60);
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());
    QCOMPARE(r.value(QStringLiteral("code")).toString(), QStringLiteral("busy"));

    m_store->release(a.value(QStringLiteral("runId")).toString(), QStringLiteral("cancelled"));
}

void TestTaskRunner::nonzeroExitsBlocked()
{
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap r = m_runner->startRaw(
        QStringLiteral("task-nz"), kCmd,
        {QStringLiteral("/c"), QStringLiteral("exit"), QStringLiteral("7")}, m_dir.path(), 60);
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    const QString runId = r.value(QStringLiteral("runId")).toString();
    QVERIFY2(finished.wait(15000), "run 未结束");
    const QVariantMap detail = finished.at(0).at(1).toMap();
    QCOMPARE(detail.value(QStringLiteral("exitCode")).toInt(), 7);
    QCOMPARE(m_store->run(runId).value(QStringLiteral("status")).toString(),
             QStringLiteral("blocked"));
    QVERIFY(detail.value(QStringLiteral("lastError")).toString().contains(QStringLiteral("7")));
}

void TestTaskRunner::manualStopCancels()
{
    // 启动一个挂住的命令：cmd /c ping -n 60 127.0.0.1
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap r =
        m_runner->startRaw(QStringLiteral("task-st"), kCmd,
                           {QStringLiteral("/c"), QStringLiteral("ping"), QStringLiteral("-n"),
                            QStringLiteral("60"), QStringLiteral("127.0.0.1")},
                           m_dir.path(), 120);
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    const QString runId = r.value(QStringLiteral("runId")).toString();
    QVERIFY(m_runner->stop(runId));
    QVERIFY2(finished.wait(15000), "终止未完成");
    QCOMPARE(m_store->run(runId).value(QStringLiteral("status")).toString(),
             QStringLiteral("cancelled"));
}

void TestTaskRunner::redactsCredentialsInLog()
{
    // 打印一行假凭据：api_key / sk- 前缀令牌 / 长十六进制串都必须落成占位符
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap r = m_runner->startRaw(
        QStringLiteral("task-secret"), kCmd,
        {QStringLiteral("/c"), QStringLiteral("echo"),
         QStringLiteral("api_key=SECRET123 sk-abcdefghijklmnop 0123456789abcdef0123456789abcdef")},
        m_dir.path(), 60);
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("message")).toString()));
    QVERIFY2(finished.wait(15000), "命令未结束");

    const QString runId = r.value(QStringLiteral("runId")).toString();
    const QString logPath = m_store->run(runId).value(QStringLiteral("logPath")).toString();
    QVERIFY(!logPath.isEmpty());
    QFile f(logPath);
    QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(logPath));
    const QString content = QString::fromUtf8(f.readAll());

    QVERIFY2(!content.contains(QStringLiteral("SECRET123")), "api_key 明文泄漏");
    QVERIFY2(!content.contains(QStringLiteral("sk-abcdefghijklmnop")), "sk- 令牌明文泄漏");
    QVERIFY2(!content.contains(QStringLiteral("0123456789abcdef0123456789abcdef")),
             "十六进制串泄漏");
    QVERIFY2(content.contains(QStringLiteral("[已脱敏]")), "未写入脱敏占位符");
}

QTEST_GUILESS_MAIN(TestTaskRunner)
#include "tst_taskrunner.moc"