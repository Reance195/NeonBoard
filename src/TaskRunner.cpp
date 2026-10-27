#include "TaskRunner.h"

#include "AgentProfile.h"
#include "AppPaths.h"
#include "TaskExec.h"
#include "TaskRunStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

constexpr int kHeartbeatMs = 10000; // 心跳间隔（卡片要求 10 秒），远小于孤儿判定 90 秒
constexpr int kGraceMs = 5000;      // terminate 后等待其自行退出的宽限期

QVariantMap profileEnv(const QVariantMap &profile)
{
    return profile.value(QStringLiteral("env")).toMap();
}

// 常见凭据形态：key=value 里的敏感键、Bearer / sk- 前缀令牌、独立的长十六进制串。
// 宁可多脱一点也不能让凭据落进日志——诊断时看占位符就够判断是哪一类了。
const QRegularExpression &credentialPattern()
{
    static const QRegularExpression re(
        QStringLiteral(R"((?:api[_-]?key|token|secret|passwd|password|authorization|bearer)(\s*[=:]\s*)(\S+))"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &prefixedTokenPattern()
{
    static const QRegularExpression re(
        QStringLiteral(R"(\b(?:sk-|ghp_|gho_|xox[baprs]-)[A-Za-z0-9_\-]{12,})"));
    return re;
}

const QRegularExpression &longHexPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(\b[0-9a-f]{32,}\b)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

} // namespace

TaskRunner::TaskRunner(Database *database, TaskRunStore *runs, AgentProfile *profiles,
                       TaskExec *exec, QObject *parent)
    : QObject(parent),
      m_database(database),
      m_runs(runs),
      m_profiles(profiles),
      m_exec(exec)
{}

TaskRunner::~TaskRunner()
{
    for (Process *p : std::as_const(m_processes)) {
        if (p->proc && p->proc->state() != QProcess::NotRunning) {
            p->proc->disconnect(this);
            killTree(*p); // 连同子孙进程一起回收，别留后台残留
        }
        closeJob(*p);
    }
    qDeleteAll(m_processes);
    m_processes.clear();
}

int TaskRunner::revision() const
{
    return m_revision;
}

int TaskRunner::activeCount() const
{
    return m_processes.size();
}

QStringList TaskRunner::activeRuns() const
{
    QStringList out;
    for (const Process *p : std::as_const(m_processes))
        out.append(p->runId);
    return out;
}

QVariantMap TaskRunner::start(const QString &taskId)
{
    if (!m_profiles->executorEnabled())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("执行器总开关未开启")}};
    if (!m_exec)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("缺少执行定义门面")}};

    const QVariantMap exec = m_exec->execOf(taskId);
    const QString agent = exec.value(QStringLiteral("agent")).toString().trimmed();
    if (agent.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("这张卡没有指定 agent")}};

    const QVariantMap profile = m_profiles->profileOf(agent);
    if (profile.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("未知 agent：%1").arg(agent)}};

    const QString goal = exec.value(QStringLiteral("goal")).toString();
    const QString prompt =
        goal.trimmed().isEmpty() ? QStringLiteral("按卡片执行定义完成该任务") : goal;
    const QString cwd = exec.value(QStringLiteral("cwd")).toString();
    const bool stdinPrompt =
        profile.value(QStringLiteral("promptMode")).toString() == QStringLiteral("stdin");
    const QStringList args =
        m_profiles->argv(agent, /*readonly=*/false, stdinPrompt ? QString() : prompt);

    int timeoutSec = exec.value(QStringLiteral("timeoutMin")).toInt() * 60;
    if (timeoutSec <= 0)
        timeoutSec = profile.value(QStringLiteral("defaultTimeoutMin")).toInt() * 60;
    if (timeoutSec <= 0)
        timeoutSec = 600;

    return startRaw(taskId, args.value(0), args.mid(1), cwd, timeoutSec, stdinPrompt, prompt);
}

QVariantMap TaskRunner::startRaw(const QString &taskId, const QString &exe, const QStringList &args,
                                 const QString &cwd, int timeoutSec, bool promptToStdin,
                                 const QString &prompt)
{
    if (exe.trimmed().isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("缺少可执行文件")}};

    const QVariantMap acquired = m_runs->acquire(taskId);
    if (!acquired.value(QStringLiteral("ok")).toBool())
        return acquired; // busy 等错误原样带回

    const QString runId = acquired.value(QStringLiteral("runId")).toString();

    Process *p = new Process;
    p->runId = runId;
    p->taskId = taskId;
    m_processes.append(p);

    if (!setupProcess(*p, cwd, QStringList{exe} + args, promptToStdin, prompt, timeoutSec)) {
        // 启动失败（进程无法拉起）：补一条结束记录，别让 run 挂在 running 上
        const QString err = p->startError.isEmpty()
                                ? QStringLiteral("进程启动失败")
                                : QStringLiteral("进程启动失败：%1").arg(p->startError);
        QVariantMap detail;
        detail.insert(QStringLiteral("exitCode"), -1);
        detail.insert(QStringLiteral("lastError"), err);
        m_runs->release(runId, QStringLiteral("blocked"), detail);
        m_processes.removeOne(p);
        delete p;
        bump();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), err}};
    }

    bump();
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("runId"), runId},
            {QStringLiteral("status"), QStringLiteral("running")}};
}

bool TaskRunner::setupProcess(Process &p, const QString &cwd, const QStringList &args,
                              bool promptToStdin, const QString &prompt, int timeoutSec)
{
    QProcess *proc = p.proc = new QProcess(this);
    proc->setProgram(args.value(0));
    proc->setArguments(args.mid(1));
    if (!cwd.trimmed().isEmpty())
        proc->setWorkingDirectory(cwd);
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    if (!promptToStdin && !prompt.trimmed().isEmpty()) {
        // 目前 prompt 已在 argv 里（AgentProfile::argv 已追加）；此处仅兜底记录。
        Q_UNUSED(promptToStdin);
    }

    // stdout / stderr 增量拉取，合并进日志缓冲并流式外发
    QObject::connect(proc, &QProcess::readyReadStandardOutput, this, [this, &p]() {
        accumulate(p, p.proc->readAllStandardOutput());
    });
    QObject::connect(proc, &QProcess::readyReadStandardError, this, [this, &p]() {
        accumulate(p, p.proc->readAllStandardError());
    });

    const QString runId = p.runId;
    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                     [this, &p](int exitCode, QProcess::ExitStatus) {
                         onProcessFinished(p, exitCode);
                     });

    // 心跳：定时写入，避免被 recoverOrphans 当孤儿回收
    p.heartbeat = new QTimer(this);
    p.heartbeat->setInterval(kHeartbeatMs);
    QObject::connect(p.heartbeat, &QTimer::timeout, this, [this, &p]() {
        if (m_runs)
            m_runs->heartbeat(p.runId);
    });

    // 倒计时：超时先 terminate，宽限后 kill，结束记 blocked
    p.deadline = new QTimer(this);
    p.deadline->setSingleShot(true);
    p.deadline->setInterval(timeoutSec * 1000);
    QObject::connect(p.deadline, &QTimer::timeout, this, [this, &p]() {
        p.ending = QStringLiteral("timeout");
        if (p.proc && p.proc->state() != QProcess::NotRunning)
            p.proc->terminate();
        QTimer::singleShot(kGraceMs, this, [this, &p]() {
            if (p.proc && p.proc->state() != QProcess::NotRunning) {
                killTree(p);
                if (p.ending.isEmpty())
                    p.ending = QStringLiteral("timeout");
            }
        });
    });

    proc->start();
    if (!proc->waitForStarted(3000)) {
        p.startError = proc->errorString();
        p.deadline->stop();
        p.heartbeat->stop();
        return false;
    }
    attachJob(p);
    p.heartbeat->start();
    p.deadline->start();
    return true;
}

QString TaskRunner::desensitize(const QString &text)
{
    QString out = text;
    out.replace(credentialPattern(), QStringLiteral("\\1[已脱敏]"));
    out.replace(prefixedTokenPattern(), QStringLiteral("[已脱敏]"));
    out.replace(longHexPattern(), QStringLiteral("[已脱敏]"));
    return out;
}

void TaskRunner::accumulate(Process &p, const QByteArray &chunk)
{
    if (chunk.isEmpty())
        return;
    // 脱敏后再入库/上屏：凭据绝不能跟着 runs/<id>.log 留在磁盘上。
    const QString text = desensitize(QString::fromUtf8(chunk));
    p.log.append(text.toUtf8());
    emit logAppended(p.runId, text);
}

void TaskRunner::onProcessFinished(Process &p, int exitCode)
{
    if (p.deadline)
        p.deadline->stop();
    if (p.heartbeat)
        p.heartbeat->stop();

    QString status;
    QVariantMap detail;
    detail.insert(QStringLiteral("exitCode"), exitCode);

    if (p.ending == QStringLiteral("cancelled")) {
        status = QStringLiteral("cancelled");
        detail.insert(QStringLiteral("lastError"), QStringLiteral("已手动终止"));
    } else if (p.ending == QStringLiteral("timeout")) {
        status = QStringLiteral("blocked");
        detail.insert(QStringLiteral("lastError"), QStringLiteral("执行超时"));
    } else if (exitCode == 0) {
        status = QStringLiteral("passed");
    } else {
        status = QStringLiteral("blocked");
        detail.insert(QStringLiteral("lastError"), QStringLiteral("进程退出码 %1").arg(exitCode));
    }

    settle(p, status, detail);
}

void TaskRunner::settle(Process &p, const QString &status, const QVariantMap &detail)
{
    QVariantMap finalDetail = detail;
    // 流程已结束，此时再读 readyRead 可能还有余量；一次性收净后落盘
    if (p.proc) {
        accumulate(p, p.proc->readAllStandardOutput());
        accumulate(p, p.proc->readAllStandardError());
    }
    finalDetail.insert(QStringLiteral("logPath"), writeLog(p));
    if (m_runs)
        m_runs->release(p.runId, status, finalDetail);

    emit runFinished(p.runId, finalDetail);
    cleanup(p);
    bump();
}

void TaskRunner::attachJob(Process &p)
{
#ifdef Q_OS_WIN
    if (!p.proc)
        return;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    // QProcess 给的是进程 pid；用 OpenProcess 拿句柄再挂进作业对象
    HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                           static_cast<DWORD>(p.proc->processId()));
    if (!h || !AssignProcessToJobObject(job, h)) {
        if (h)
            CloseHandle(h);
        CloseHandle(job);
        return;
    }
    CloseHandle(h);
    p.job = job;
#else
    Q_UNUSED(p);
#endif
}

void TaskRunner::killTree(Process &p)
{
#ifdef Q_OS_WIN
    if (p.job) {
        TerminateJobObject(static_cast<HANDLE>(p.job), 1);
        return;
    }
#endif
    if (p.proc)
        p.proc->kill();
}

void TaskRunner::closeJob(Process &p)
{
#ifdef Q_OS_WIN
    if (p.job) {
        CloseHandle(static_cast<HANDLE>(p.job));
        p.job = nullptr;
    }
#endif
}

bool TaskRunner::stop(const QString &runId)
{
    Process *p = byRunId(runId);
    if (!p || !p->proc || p->proc->state() == QProcess::NotRunning)
        return false;
    p->ending = QStringLiteral("cancelled");
    killTree(*p);
    return true;
}

QString TaskRunner::writeLog(const Process &p) const
{
    // 日志落到数据目录 runs/<runId>.log；目录沿用日志目录约定
    const QFileInfo info(AppPaths::logFilePath());
    QDir dir = info.dir();
    dir.mkpath(QStringLiteral("runs"));
    const QString path =
        dir.filePath(QStringLiteral("runs") + QLatin1Char('/') + p.runId + QStringLiteral(".log"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    f.write(p.log);
    f.close();
    return path;
}

void TaskRunner::cleanup(Process &p)
{
    if (p.deadline) {
        p.deadline->stop();
        p.deadline->deleteLater();
    }
    if (p.heartbeat) {
        p.heartbeat->stop();
        p.heartbeat->deleteLater();
    }
    closeJob(p);
    if (p.proc) {
        p.proc->disconnect(this);
        p.proc->deleteLater();
    }
    m_processes.removeOne(&p);
    delete &p;
}

TaskRunner::Process *TaskRunner::byRunId(const QString &runId)
{
    for (Process *p : std::as_const(m_processes))
        if (p->runId == runId)
            return p;
    return nullptr;
}

void TaskRunner::bump()
{
    ++m_revision;
    emit changed();
}