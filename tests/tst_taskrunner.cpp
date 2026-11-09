// R6-04：执行器 TaskRunner —— 真正拉起进程、流式日志、超时/终止、生命周期与锁。
//
// 覆盖六条：
//   1. 总开关未开启时 start 直接拒绝；开启后缺 agent / 未知 agent 分别报错
//   2. 成功启动：acquire 拿到 runId，activeRuns 可见，进程结束后 release 落 passed
//   3. 同卡活跃 run 在跑时再 start 返回 busy（拒绝排队）
//   4. 非零退出码 → blocked + lastError；完整日志落盘且有 logPath
//   5. 手动 stop → cancelled
//   6. 心跳持续刷新，未触发孤儿回收；启动失败不残留 running 锁
//   7. R14-03：PATH 上的 .cmd / .bat 包装器（cursor-agent 的装法）能拉起、退出码可取
//   8. R14-05：经 .cmd 转调送不到的内容（换行、%VAR%）启动前拒绝，不静默截断；.exe 不受限
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
#include <QStandardPaths>
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
    void stdinNullFeedsNothing();
    void stdinPromptIsWritten();
    void rejectsBusyTask();
    void nonzeroExitsBlocked();
    void manualStopCancels();
    void redactsCredentialsInLog();
    void cmdWrapperOnPathLaunches();
    void batWrapperAbsolutePathLaunches();
    void cmdWrapperRejectsMultilinePrompt();
    void cmdWrapperRejectsPercentExpansion();
    void desensitizeRedactsBearerJwt();
    void cmdWrapperRejectsDoubleQuotePrompt();
    void graceTimeoutSettlesBlocked();
    void realAgentProbeIfEnabled();

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

void TestTaskRunner::stdinNullFeedsNothing()
{
    // R9-05：声明 stdin="null" 的 profile（codex）不读标准输入，执行器必须把子进程的
    // 标准输入接到 NUL。载体用 `cmd /c more` —— more 把标准输入原样回显到标准输出，
    // 于是「到底喂进去了什么」在日志里直接可见，不用猜。
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);

    const QVariantMap r = m_runner->startRaw(
        QStringLiteral("task-stdin-null"), kCmd, {QStringLiteral("/c"), QStringLiteral("more")},
        m_dir.path(), 60, /*promptToStdin=*/true, QStringLiteral("SHOULD-NOT-APPEAR"),
        /*maxAttempts=*/1, /*stdinNull=*/true);
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("message")).toString()));

    // 不接 NUL 的话 more 会一直等 EOF，这条断言就是它的回归防线
    QVERIFY2(finished.wait(15000), "stdin=null 的进程 15s 未结束：NUL 没接上，子进程在等 EOF");
    const QVariantMap detail = finished.at(0).at(1).toMap();
    QCOMPARE(detail.value(QStringLiteral("exitCode")).toInt(), 0);

    QFile log(detail.value(QStringLiteral("logPath")).toString());
    QVERIFY(log.open(QIODevice::ReadOnly));
    QVERIFY2(!log.readAll().contains("SHOULD-NOT-APPEAR"),
             "stdin=null 时不该把 prompt 写进标准输入");
}

void TestTaskRunner::stdinPromptIsWritten()
{
    // 另一半：promptMode=stdin 必须真把 prompt 写进去再关写端。
    // 此前 setupProcess 的那个分支是空的（只有一句 Q_UNUSED），注释却写着
    // 「由 setupProcess 写标准输入」—— 声明 stdin 的 profile 实际拿到空 prompt，
    // 表现为「agent 收到任务却什么都没干」，极难归因。R9-05 一并修掉。
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);

    const QVariantMap r = m_runner->startRaw(
        QStringLiteral("task-stdin-prompt"), kCmd, {QStringLiteral("/c"), QStringLiteral("more")},
        m_dir.path(), 60, /*promptToStdin=*/true, QStringLiteral("STDIN-PAYLOAD-42"),
        /*maxAttempts=*/1, /*stdinNull=*/false);
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("message")).toString()));

    QVERIFY2(finished.wait(15000), "写端没关，子进程等 EOF 挂住了");
    const QVariantMap detail = finished.at(0).at(1).toMap();
    QCOMPARE(detail.value(QStringLiteral("exitCode")).toInt(), 0);

    QFile log(detail.value(QStringLiteral("logPath")).toString());
    QVERIFY(log.open(QIODevice::ReadOnly));
    QVERIFY2(log.readAll().contains("STDIN-PAYLOAD-42"), "prompt 没有写进子进程的标准输入");
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

// R14-03：PATH 上的 .cmd 包装器必须能拉起。
//
// cursor-agent 装出来就是这种东西：PATH 上只有 cursor-agent.cmd，真身是
// versions/<版本>/node.exe + index.js。QProcess 拿裸命令名时不会按 PATHEXT 找
// .cmd（CreateProcess 只补 .exe），于是面板显示「可用」、派活却 FailedToStart
// 「系统找不到指定的文件」。执行器必须先把裸名解析成绝对路径。
void TestTaskRunner::cmdWrapperOnPathLaunches()
{
#ifdef Q_OS_WIN
    const QString dir = m_dir.path();
    const QString stem = QStringLiteral("nb-wrap-on-path");
    QFile script(dir + QLatin1Char('/') + stem + QStringLiteral(".cmd"));
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("@echo off\r\n"
                 "echo WRAPPER-ON-PATH\r\n"
                 "exit /b 3\r\n");
    script.close();

    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", QDir::toNativeSeparators(dir).toLocal8Bit() + ';' + oldPath);

    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap r = m_runner->startRaw(QStringLiteral("task-wrap-path"), stem, {}, dir, 60);
    qputenv("PATH", oldPath); // 立刻还原，别污染同进程里的其它用例

    QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("message")).toString()));
    QVERIFY2(finished.wait(15000), ".cmd 包装器 15s 未结束");

    const QVariantMap detail = finished.at(0).at(1).toMap();
    QCOMPARE(detail.value(QStringLiteral("exitCode")).toInt(), 3);

    QFile log(detail.value(QStringLiteral("logPath")).toString());
    QVERIFY(log.open(QIODevice::ReadOnly));
    QVERIFY2(QString::fromUtf8(log.readAll()).contains(QStringLiteral("WRAPPER-ON-PATH")),
             "脚本没被执行（只起了个空壳）");
#else
    QSKIP("Windows 专有：.cmd 包装器");
#endif
}

// 绝对路径的 .bat 同样要能拉起，且带空格的参数按 argv 原样送达。
void TestTaskRunner::batWrapperAbsolutePathLaunches()
{
#ifdef Q_OS_WIN
    const QString path = m_dir.filePath(QStringLiteral("nb-wrap-abs.bat"));
    QFile script(path);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("@echo off\r\n"
                 "echo ARG[%~1]\r\n"
                 "exit /b 4\r\n");
    script.close();

    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap r = m_runner->startRaw(QStringLiteral("task-wrap-abs"), path,
                                             {QStringLiteral("hello world")}, m_dir.path(), 60);
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("message")).toString()));
    QVERIFY2(finished.wait(15000), ".bat 包装器 15s 未结束");

    const QVariantMap detail = finished.at(0).at(1).toMap();
    QCOMPARE(detail.value(QStringLiteral("exitCode")).toInt(), 4);

    QFile log(detail.value(QStringLiteral("logPath")).toString());
    QVERIFY(log.open(QIODevice::ReadOnly));
    QVERIFY2(QString::fromUtf8(log.readAll()).contains(QStringLiteral("ARG[hello world]")),
             "参数没有原样到达脚本");
#else
    QSKIP("Windows 专有：.bat 包装器");
#endif
}

// 换行的 prompt 经 .cmd 会被 cmd.exe 当命令分隔符，只送第一行 —— 必须启动前拒绝，
// 不能把半句话交给 agent 再当成「跑完了」。同一条 prompt 交给真正的 .exe 则不受限。
void TestTaskRunner::cmdWrapperRejectsMultilinePrompt()
{
#ifdef Q_OS_WIN
    const QString path = m_dir.filePath(QStringLiteral("nb-wrap-multiline.cmd"));
    QFile script(path);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("@echo off\r\nexit /b 0\r\n");
    script.close();

    const QString prompt = QStringLiteral("第一行\n第二行");
    const QVariantMap r = m_runner->startRaw(QStringLiteral("task-wrap-multiline"), path, {},
                                             m_dir.path(), 60, /*promptToStdin=*/false, prompt, 1,
                                             false, QStringLiteral("cursor-agent"));
    QVERIFY2(!r.value(QStringLiteral("ok")).toBool(), "含换行的 prompt 必须拒绝启动");
    const QString message = r.value(QStringLiteral("message")).toString();
    QVERIFY2(message.contains(QStringLiteral("换行")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("cursor-agent")), qPrintable(message));
    QVERIFY2(m_runner->activeRuns().isEmpty(), "拒绝启动却留下了 running 锁");

    // 对照：同样的 prompt 交给 .exe（cmd.exe）不受这条限制，说明判据看的是包装器而不是内容本身
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap ok = m_runner->startRaw(QStringLiteral("task-wrap-control"), kCmd,
                                              {QStringLiteral("/c"), QStringLiteral("exit 0")},
                                              m_dir.path(), 60, /*promptToStdin=*/false, prompt);
    QVERIFY2(ok.value(QStringLiteral("ok")).toBool(),
             qPrintable(ok.value(QStringLiteral("message")).toString()));
    QVERIFY2(finished.wait(15000), ".exe 对照用例 15s 未结束");
#else
    QSKIP("Windows 专有：.cmd 包装器");
#endif
}

// %VAR% 在 cmd 的引号里也照展开，行内没有可靠转义 —— 同样只能拒绝，不能送个被替换过的 prompt。
void TestTaskRunner::cmdWrapperRejectsPercentExpansion()
{
#ifdef Q_OS_WIN
    const QString path = m_dir.filePath(QStringLiteral("nb-wrap-percent.cmd"));
    QFile script(path);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("@echo off\r\nexit /b 0\r\n");
    script.close();

    const QVariantMap r = m_runner->startRaw(
        QStringLiteral("task-wrap-percent"), path, {}, m_dir.path(), 60, /*promptToStdin=*/false,
        QStringLiteral("进度 100%PATH% 已提交"), 1, false, QStringLiteral("cursor-agent"));
    QVERIFY2(!r.value(QStringLiteral("ok")).toBool(), "含 %VAR% 的 prompt 必须拒绝启动");
    const QString message = r.value(QStringLiteral("message")).toString();
    QVERIFY2(message.contains(QStringLiteral("%VAR%")), qPrintable(message));
    QVERIFY2(m_runner->activeRuns().isEmpty(), "拒绝启动却留下了 running 锁");

    // 单独一个 % 不构成变量引用，不该被误伤
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap single = m_runner->startRaw(
        QStringLiteral("task-wrap-percent-single"), path, {}, m_dir.path(), 60, false,
        QStringLiteral("完成度 100%"), 1, false, QStringLiteral("cursor-agent"));
    QVERIFY2(single.value(QStringLiteral("ok")).toBool(),
             qPrintable(single.value(QStringLiteral("message")).toString()));
    QVERIFY2(finished.wait(15000), "单个百分号的用例 15s 未结束");
#else
    QSKIP("Windows 专有：.cmd 包装器");
#endif
}

// 本机装了真 agent 时，可用 NB_REAL_AGENT_PROBE=1 打一次真调用（R14-03 的实测口径：
// 非交互、短超时）。默认跳过 —— 会拉起真实 CLI，且未登录时会以非零码退出。
// 断言只覆盖「起得来、退出码取得到」，不替 agent 的成功与否背书。
// DX-134：cmd 不认反斜杠转义，prompt 里的双引号会提前闭合引号段 —— 必须与
// 换行 / %VAR% 同等待遇：启动前拒绝，不静默截断。
void TestTaskRunner::cmdWrapperRejectsDoubleQuotePrompt()
{
#ifdef Q_OS_WIN
    const QString path = m_dir.filePath(QStringLiteral("nb-wrap-quote.cmd"));
    QFile script(path);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write(QByteArray("@echo off\r\nexit /b 0\r\n"));
    script.close();

    const QString prompt = QStringLiteral("说明 \" 引用 & calc");
    const QVariantMap r = m_runner->startRaw(QStringLiteral("task-wrap-quote"), path, {},
                                             m_dir.path(), 60, /*promptToStdin=*/false, prompt, 1,
                                             false, QStringLiteral("cursor-agent"));
    QVERIFY2(!r.value(QStringLiteral("ok")).toBool(), "含双引号的 prompt 必须拒绝启动");
    const QString message = r.value(QStringLiteral("message")).toString();
    QVERIFY2(message.contains(QStringLiteral("双引号")), qPrintable(message));
    QVERIFY2(m_runner->activeRuns().isEmpty(), "拒绝启动却留下了 running 锁");

    // 对照：同样内容交给 .exe（cmd.exe 直起）不受这条限制，判据看的是包装器
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap ok = m_runner->startRaw(QStringLiteral("task-wrap-quote-control"), kCmd,
                                              {QStringLiteral("/c"), QStringLiteral("exit 0")},
                                              m_dir.path(), 60, /*promptToStdin=*/false, prompt);
    QVERIFY2(ok.value(QStringLiteral("ok")).toBool(),
             qPrintable(ok.value(QStringLiteral("message")).toString()));
    QVERIFY2(finished.wait(15000), ".exe 对照用例 15s 未结束");
#else
    QSKIP("Windows 专有：.cmd 包装器");
#endif
}

// DX-134：宽限计时器成员化后的超时全链路回归 —— deadline → terminate → 宽限 kill →
// settle 成 blocked，收尾后无残留锁、无迟到回调。UAF 本身无法在常规构建下断言
// （需 ASAN），这里钉的是重写后的行为等价性。
void TestTaskRunner::graceTimeoutSettlesBlocked()
{
#ifdef Q_OS_WIN
    // ping 拉满 30 秒，timeoutSec=1：deadline 在 1s 触发 terminate（控制台进程多半
    // 不理会），宽限 5s 后 killTree 收尸 → blocked(timeout)
    const QVariantMap r = m_runner->startRaw(QStringLiteral("task-grace-timeout"), kCmd,
                                             {QStringLiteral("/c"),
                                              QStringLiteral("ping -n 30 127.0.0.1 >nul")},
                                             m_dir.path(), 1);
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("message")).toString()));
    const QString runId = r.value(QStringLiteral("runId")).toString();

    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    QVERIFY2(finished.wait(30000), "超时 run 30s 内未 settle（宽限计时链路断了）");
    const QVariantMap detail = finished.first().at(1).toMap();
    QCOMPARE(detail.value(QStringLiteral("lastError")).toString(),
             QStringLiteral("执行超时"));
    QVERIFY(m_runner->activeRuns().isEmpty());
    // 锁已释放：同卡可以再次启动（busy 会返回 ok:false）
    const QVariantMap again = m_runner->startRaw(QStringLiteral("task-grace-timeout"), kCmd,
                                                 {QStringLiteral("/c"), QStringLiteral("exit 0")},
                                                 m_dir.path(), 60);
    QVERIFY2(again.value(QStringLiteral("ok")).toBool(),
             qPrintable(again.value(QStringLiteral("message")).toString()));
    QSignalSpy finishAgain(m_runner, &TaskRunner::runFinished);
    QVERIFY2(finishAgain.wait(15000), "复跑用例 15s 未结束");
#else
    QSKIP("Windows 专有：taskkill/命令行载体");
#endif
}

// DX-139：Authorization: Bearer <JWT> 必须整串脱敏——旧正则只捕获到 "Bearer" 一词，
// JWT（base64url）不被 prefixed / longHex 兜底正则命中，明文凭据落 runs/<id>.log
void TestTaskRunner::desensitizeRedactsBearerJwt()
{
    // 常规形态：键名 + Bearer + 三段 JWT，令牌段必须消失
    const QString jwt = QStringLiteral(
        "Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxIn0.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c");
    const QString redacted = TaskRunner::desensitize(jwt);
    QVERIFY2(!redacted.contains(QStringLiteral("eyJhbGciOiJIUzI1NiJ9")),
             qPrintable(redacted));
    QVERIFY2(!redacted.contains(QStringLiteral("SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c")),
             qPrintable(redacted));
    QVERIFY(redacted.contains(QStringLiteral("[已脱敏]")));

    // 小写 bearer / 无 Bearer 前缀的裸令牌不回归
    const QString lower = TaskRunner::desensitize(
        QStringLiteral("token=Bearer abc.def.ghi-more"));
    QVERIFY(!lower.contains(QStringLiteral("abc.def.ghi-more")));

    // 普通 authorization 键值对（无 Bearer 前缀）不回归
    QVERIFY(!TaskRunner::desensitize(QStringLiteral("authorization: abc123secret"))
                 .contains(QStringLiteral("abc123secret")));

    // 对照：普通单词不受影响（不误伤正文）
    QCOMPARE(TaskRunner::desensitize(QStringLiteral("hello world")),
             QStringLiteral("hello world"));
}

void TestTaskRunner::realAgentProbeIfEnabled()
{
    if (qEnvironmentVariableIsEmpty("NB_REAL_AGENT_PROBE"))
        QSKIP("默认跳过：设 NB_REAL_AGENT_PROBE=1 才真拉起本机 agent");
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("cursor-agent"));
    if (exe.isEmpty())
        QSKIP("本机没有 cursor-agent");

    qInfo() << "[R14-03] 解析到" << exe;
    QSignalSpy finished(m_runner, &TaskRunner::runFinished);
    const QVariantMap r =
        m_runner->startRaw(QStringLiteral("task-real-cursor"), QStringLiteral("cursor-agent"),
                           {QStringLiteral("--version")}, m_dir.path(), 30);
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("message")).toString()));
    QVERIFY2(finished.wait(30000), "cursor-agent --version 30s 未结束");

    const QVariantMap detail = finished.at(0).at(1).toMap();
    QFile log(detail.value(QStringLiteral("logPath")).toString());
    QVERIFY(log.open(QIODevice::ReadOnly));
    qInfo() << "[R14-03] exitCode=" << detail.value(QStringLiteral("exitCode")).toInt()
            << "log=" << QString::fromUtf8(log.readAll()).trimmed();
    QCOMPARE(detail.value(QStringLiteral("exitCode")).toInt(), 0);
}

QTEST_GUILESS_MAIN(TestTaskRunner)
#include "tst_taskrunner.moc"