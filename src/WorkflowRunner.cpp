#include "WorkflowRunner.h"

#include "AcceptanceRunner.h"
#include "AgentProfile.h"
#include "AppPaths.h"
#include "Database.h"
#include "ScopeMatcher.h"
#include "TaskExec.h"
#include "TaskRunner.h"
#include "TaskRunStore.h"
#include "WorkflowPresets.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {

constexpr int kDefaultStepTimeoutSec = 1800; // 单步 30 分钟兜底，卡片可覆盖

// 角色 → 交付物说明（写进 prompt，让 agent 知道这一步该产出什么）
QString roleDeliverable(const QString &role)
{
    if (role == QLatin1String("pm"))
        return QStringLiteral("spec：需求拆解、验收项清单、明确不做什么");
    if (role == QLatin1String("architect"))
        return QStringLiteral("design：实现方案、改动文件清单、风险与回退");
    if (role == QLatin1String("engineer"))
        return QStringLiteral("实现与 git diff 摘要：改了哪些文件、为什么这么改");
    if (role == QLatin1String("qa"))
        return QStringLiteral("qa：验收结论（通过 / 不通过）、发现的缺陷、复现步骤");
    if (role == QLatin1String("writer"))
        return QStringLiteral("文档改动：改了哪几节、给谁看");
    if (role == QLatin1String("orchestrator"))
        return QStringLiteral("summary：整体结论、剩余风险、建议的下一步");
    return QStringLiteral("本步结论");
}

} // namespace

WorkflowRunner::WorkflowRunner(Database *database, TaskExec *exec, TaskRunStore *runs,
                               TaskRunner *runner, AgentProfile *profiles, ScopeMatcher *scope,
                               AcceptanceRunner *acceptance, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_exec(exec)
    , m_runs(runs)
    , m_runner(runner)
    , m_profiles(profiles)
    , m_scope(scope)
    , m_acceptance(acceptance)
{
    // 一步跑完 → 验收 → 决定是否继续下一步。
    // 只认自己链上那一步的 runId（TaskRunner 是全局单例，别的 run 也会发这里）。
    if (m_runner) {
        connect(m_runner, &TaskRunner::runFinished, this,
                [this](const QString &runId, const QVariantMap &detail) {
                    if (m_activeIndex < 0 || m_activeIndex >= m_chain.steps.size())
                        return;
                    if (m_chain.steps.at(m_activeIndex).runId != runId)
                        return;
                    const int exitCode =
                        detail.value(QStringLiteral("exitCode"), -1).toInt();
                    finishStep(m_activeIndex, exitCode, detail);
                });
    }
}

void WorkflowRunner::setPresets(WorkflowPresets *presets)
{
    m_presets = presets;
}

int WorkflowRunner::revision() const
{
    return m_revision;
}

bool WorkflowRunner::running() const
{
    return !m_chain.chainId.isEmpty() && !m_chain.done;
}

void WorkflowRunner::bump()
{
    ++m_revision;
    emit changed();
}

QString WorkflowRunner::handoffDir(const QString &taskId) const
{
    // 与 runs/ 同待遇：落数据目录，绝不进仓库
    return QDir(AppPaths::dataDir())
        .filePath(QStringLiteral(".neonboard/handoff/") + taskId);
}

QString WorkflowRunner::handoffPath(const QString &root, const QString &taskId,
                                    const QString &role)
{
    return QDir(root).filePath(role + QStringLiteral(".md"));
}

QString WorkflowRunner::buildStepPrompt(const QString &taskId, const QString &workflow,
                                        const QVariantMap &step, int stepIndex, int stepCount,
                                        const QString &stepGoal,
                                        const QString &previousHandoff)
{
    const QString role = step.value(QStringLiteral("role")).toString();
    QStringList parts;
    parts << QStringLiteral("【角色链 · 第 %1/%2 步：%3】").arg(stepIndex + 1).arg(stepCount).arg(role);
    parts << QStringLiteral("任务：%1（%2）").arg(taskId, workflow);

    if (!previousHandoff.trimmed().isEmpty()) {
        parts << QStringLiteral("== 上一步的交接物（全文）==");
        parts << previousHandoff.trimmed();
        parts << QStringLiteral("== 交接物结束 ==");
    } else {
        parts << QStringLiteral("你是这条角色链的第一步，没有上游交接物。");
    }

    const QString goal = stepGoal.trimmed();
    if (!goal.isEmpty())
        parts << QStringLiteral("本步任务：%1").arg(goal);

    parts << QStringLiteral("本步交付物：%1").arg(roleDeliverable(role));
    parts << QStringLiteral("约束：只做本步范围内的事，不要改超出卡片 scope 的文件；"
                            "不要执行 git push。");
    return parts.join(QStringLiteral("\n"));
}

QString WorkflowRunner::summarizeHandoff(const QByteArray &log, int maxChars)
{
    QString text = QString::fromUtf8(log).trimmed();
    if (text.isEmpty())
        return {};
    const int limit = qMax(200, maxChars);
    if (text.size() > limit) {
        // 结论通常在末尾：留尾部并显式标注前面被截断
        text = QStringLiteral("（前文省略 %1 字符）\n").arg(text.size() - limit)
               + text.right(limit);
    }
    return text;
}

// R7-04：卡片步骤覆盖项存于 TaskExec 的 meta 键（TaskExec.cpp 的 stepsKey）。
// 这里只**读**一次做「有没有覆盖」判定：有覆盖就用覆盖（尊重卡片自定义），
// 没有才用可编辑预设展开。仅读不写，键语义与 TaskExec 保持一致。
namespace {
QLatin1String kStepOverridePrefix("exec.workflowSteps.");
}

QString WorkflowRunner::sizeHintFor(const QString &taskId) const
{
    if (!m_database || taskId.isEmpty())
        return {};
    const QString text = m_database->metaValue(QStringLiteral("exec.triage.draft.") + taskId);
    if (text.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject())
        return {};
    QVariantMap record = doc.object().toVariantMap();
    // 草案记录形如 {status, draft:{…estimate{size}…}}；也兼容把 estimate 直接放顶层。
    if (!record.contains(QStringLiteral("estimate"))
        && record.value(QStringLiteral("draft")).toMap().contains(QStringLiteral("estimate")))
        record = record.value(QStringLiteral("draft")).toMap();
    return record.value(QStringLiteral("estimate"))
        .toMap()
        .value(QStringLiteral("size"))
        .toString()
        .trimmed();
}

QVariantMap WorkflowRunner::cardHintFor(const QString &taskId) const
{
    QVariantMap out;
    if (!m_database || taskId.isEmpty())
        return out;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return out;
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT title, priority FROM task WHERE id = ?"));
    query.addBindValue(taskId);
    if (!query.exec() || !query.next())
        return out;
    out.insert(QStringLiteral("title"), query.value(0).toString());
    out.insert(QStringLiteral("priority"), query.value(1).toInt());
    return out;
}

QVariantMap WorkflowRunner::start(const QString &taskId)
{
    if (!m_profiles || !m_profiles->executorEnabled())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("执行器总开关未开启")}};
    if (!m_exec || !m_runner || !m_runs)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("执行器未装配完整")}};
    if (running())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("已有一条角色链在跑，先等它结束或取消")}};
    if (taskId.trimmed().isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("任务 id 为空")}};

    const QVariantMap exec = m_exec->execOf(taskId);
    const QString cardAgent = exec.value(QStringLiteral("agent")).toString().trimmed();
    const QString defaultAgent = m_profiles->defaultAgent().trimmed();
    const QString agent = cardAgent.isEmpty() ? defaultAgent : cardAgent;
    if (agent.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("这张卡没有指定 agent")}};

    // R7-04：orchestrator 选 workflow —— 卡片没指定时按规模（triage estimate.size，
    // 缺失则按卡片规模兜底）自动挑一个。选择只作用于本次链，不回写卡片定义。
    const QString sizeHint = sizeHintFor(taskId);
    QString workflow = exec.value(QStringLiteral("workflow")).toString().trimmed();
    if (workflow.isEmpty() && m_presets)
        workflow = m_presets->routeFor(sizeHint, cardHintFor(taskId));

    // 步骤来源：卡片覆盖优先（尊重自定义）；无覆盖时用可编辑预设展开（按规模裁剪
    // pm / architect，qa 永不裁）；没有 presets 门面（老路径 / 单测）时退回 TaskExec。
    const bool hasOverride =
        m_database && !m_database->metaValue(kStepOverridePrefix + taskId).isEmpty();
    QVariantList rawSteps;
    if (!hasOverride && m_presets && !workflow.isEmpty())
        rawSteps = m_presets->stepsFor(workflow, sizeHint);
    if (rawSteps.isEmpty())
        rawSteps = m_exec->stepsOf(taskId);
    if (rawSteps.isEmpty()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"),
                 QStringLiteral("这张卡没有可执行的角色链步骤（先选 workflow 或配步骤）")}};
    }
    const QVariantMap normalized = TaskExec::normalizeSteps(rawSteps);
    if (!normalized.value(QStringLiteral("ok")).toBool()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"),
                 normalized.value(QStringLiteral("error")).toString()}};
    }

    Chain chain;
    chain.chainId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    chain.taskId = taskId;
    chain.workflow = workflow.isEmpty() ? QStringLiteral("custom") : workflow;
    chain.agent = agent;
    chain.cwd = exec.value(QStringLiteral("cwd")).toString();
    chain.goal = exec.value(QStringLiteral("goal")).toString();

    // 交接物目录先建好：每步落盘时不至于因目录缺失而丢交接物
    const QString dir = handoffDir(taskId);
    if (!QDir().mkpath(dir)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("无法创建交接物目录：%1").arg(dir)}};
    }

    for (const QVariant &value : normalized.value(QStringLiteral("steps")).toList()) {
        const QVariantMap map = value.toMap();
        Step step;
        step.role = map.value(QStringLiteral("role")).toString();
        step.agent = map.value(QStringLiteral("agent")).toString();
        // R7-04：步骤没指定 agent 时先按角色取默认 agent（exec.roleAgents），
        // 再回落卡片的 agent / 全局默认 —— 让「同一角色固定用某个 agent」不必每步手填。
        if (step.agent.isEmpty() && m_profiles)
            step.agent = m_profiles->agentForRole(step.role);
        if (step.agent.isEmpty())
            step.agent = agent;
        step.prompt = map.value(QStringLiteral("prompt")).toString();
        step.acceptance = map.value(QStringLiteral("acceptance")).toList();
        step.scope = map.value(QStringLiteral("scope")).toList();
        step.status = QStringLiteral("pending");
        step.handoffPath = handoffPath(dir, taskId, step.role);
        chain.steps.append(step);
    }
    if (chain.steps.isEmpty()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("步骤数组归一化后为空")}};
    }

    m_chain = chain;
    m_activeIndex = -1;
    emit changed();
    qInfo().noquote() << "Workflow: start" << taskId << m_chain.workflow
                      << m_chain.steps.size() << "step(s)";
    startStep(0);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("chainId"), m_chain.chainId}};
}

bool WorkflowRunner::cancel()
{
    if (!running())
        return false;
    m_chain.cancelled = true;
    const int index = m_activeIndex;
    if (index >= 0 && index < m_chain.steps.size()) {
        Step &step = m_chain.steps[index];
        step.status = QStringLiteral("cancelled");
        if (m_runner && !step.runId.isEmpty())
            m_runner->stop(step.runId);
    }
    m_activeIndex = -1;
    m_chain.done = true;
    m_chain.failed = false;
    QVariantMap result;
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("message"), QStringLiteral("已取消"));
    emit chainFinished(m_chain.chainId, result);
    bump();
    return true;
}

void WorkflowRunner::markFailed(Chain &chain, const QString &message)
{
    chain.done = true;
    chain.failed = true;
    chain.failMessage = message;
    m_activeIndex = -1;
    qWarning().noquote() << "Workflow: failed" << chain.taskId << message;
    QVariantMap result;
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("message"), message);
    result.insert(QStringLiteral("failedStep"), chain.current);
    emit chainFinished(chain.chainId, result);
    bump();
}

void WorkflowRunner::startStep(int index)
{
    if (!running() || index < 0 || index >= m_chain.steps.size()) {
        // 全部步骤跑完
        m_chain.done = true;
        m_activeIndex = -1;
        QVariantMap result;
        result.insert(QStringLiteral("ok"), !m_chain.cancelled);
        result.insert(QStringLiteral("message"), m_chain.cancelled
                                                    ? QStringLiteral("已取消")
                                                    : QStringLiteral("角色链完成"));
        emit chainFinished(m_chain.chainId, result);
        bump();
        return;
    }

    Chain &chain = m_chain;
    chain.current = index;
    Step &step = chain.steps[index];
    step.status = QStringLiteral("running");

    // 上一步的交接物全文：第一步没有
    QString previous;
    if (index > 0) {
        const QString path = chain.steps.at(index - 1).handoffPath;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            previous = QString::fromUtf8(file.readAll());
    }

    const QString stepGoal = step.prompt.isEmpty() ? chain.goal : step.prompt;
    const QString prompt = buildStepPrompt(chain.taskId, chain.workflow, QVariantMap{
                                               {QStringLiteral("role"), step.role},
                                           },
                                           index, chain.steps.size(), stepGoal, previous);

    const QVariantMap profile = m_profiles->profileOf(step.agent);
    if (profile.isEmpty()) {
        step.status = QStringLiteral("blocked");
        markFailed(chain, QStringLiteral("第 %1 步（%2）：未知 agent %3")
                              .arg(index + 1).arg(step.role, step.agent));
        return;
    }
    const bool stdinPrompt =
        profile.value(QStringLiteral("promptMode")).toString() == QStringLiteral("stdin");
    const QStringList argv =
        m_profiles->argv(step.agent, /*readonly=*/false, stdinPrompt ? QString() : prompt);
    if (argv.isEmpty()) {
        step.status = QStringLiteral("blocked");
        markFailed(chain, QStringLiteral("第 %1 步（%2）：无法拼出命令行")
                              .arg(index + 1).arg(step.role));
        return;
    }

    // 本步跑前的 git 基线（越界检查用；scope 为空时不比对，但基线照记）
    if (m_scope && !chain.cwd.isEmpty())
        step.baseline = m_scope->captureBaseline(chain.cwd)
                            .value(QStringLiteral("lines")).toList();

    const int timeoutSec = kDefaultStepTimeoutSec;
    const QVariantMap started = m_runner->startRaw(
        chain.taskId, argv.value(0), argv.mid(1), chain.cwd, timeoutSec, stdinPrompt, prompt,
        /*maxAttempts=*/1);
    if (!started.value(QStringLiteral("ok")).toBool()) {
        step.status = QStringLiteral("blocked");
        markFailed(chain, QStringLiteral("第 %1 步（%2）启动失败：%3")
                              .arg(index + 1).arg(step.role,
                                                  started.value(QStringLiteral("message")).toString()));
        return;
    }

    step.runId = started.value(QStringLiteral("runId")).toString();
    m_activeIndex = index;
    step.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    // 把 role / step_index 补进 run 行（队列视图与成本账要用）。
    // 用 TaskRunStore::annotate：只更新运行态字段，不结算 run。
    if (m_runs) {
        m_runs->annotate(step.runId, QVariantMap{
                                         {QStringLiteral("role"), step.role},
                                         {QStringLiteral("stepIndex"), index},
                                     });
    }

    QVariantMap info;
    info.insert(QStringLiteral("role"), step.role);
    info.insert(QStringLiteral("stepIndex"), index);
    info.insert(QStringLiteral("runId"), step.runId);
    info.insert(QStringLiteral("agent"), step.agent);
    info.insert(QStringLiteral("handoffPath"), step.handoffPath);
    emit stepStarted(chain.chainId, index, info);
    bump();
}

void WorkflowRunner::finishStep(int index, int exitCode, const QVariantMap &detail)
{
    if (!running() || index < 0 || index >= m_chain.steps.size())
        return;
    Chain &chain = m_chain;
    Step &step = chain.steps[index];
    m_activeIndex = -1;

    const QString logPath = detail.value(QStringLiteral("logPath")).toString();

    // 1) 落交接物：本步日志摘要写进 .neonboard/handoff/<task>/<role>.md
    QByteArray log;
    if (!logPath.isEmpty()) {
        QFile file(logPath);
        if (file.open(QIODevice::ReadOnly))
            log = file.readAll();
    }
    const QString handoff = summarizeHandoff(log);
    if (!handoff.isEmpty()) {
        QDir().mkpath(QFileInfo(step.handoffPath).absolutePath());
        QSaveFile out(step.handoffPath);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(handoff.toUtf8());
            if (!out.commit())
                qWarning() << "Workflow: handoff write failed" << step.handoffPath;
        }
    }

    // 2) 本步验收（复用 R6-05）：acceptance + 越界（scope 非空才比）
    QVariantMap acceptanceResult;
    if (m_acceptance && !step.acceptance.isEmpty())
        acceptanceResult = m_acceptance->run(step.acceptance, chain.cwd);
    QVariantMap scopeResult;
    if (m_scope && !step.scope.isEmpty() && !chain.cwd.isEmpty())
        scopeResult = m_scope->check(chain.cwd, step.baseline, step.scope);

    QVariantMap verdict;
    if (m_acceptance) {
        verdict = m_acceptance->verdict(exitCode, acceptanceResult, scopeResult);
    } else {
        verdict.insert(QStringLiteral("status"),
                       exitCode == 0 ? QStringLiteral("passed") : QStringLiteral("blocked"));
    }
    const bool passed = verdict.value(QStringLiteral("status")).toString()
                        == QStringLiteral("passed");
    step.status = passed ? QStringLiteral("passed") : QStringLiteral("blocked");

    // 3) 成本：把这一步的时长记到 run 与链上（R7-06 的 cost_ms 口径）。
    // runFinished 的 detail 不带 startedAt，直接用本步开始时刻（startStep 记的）
    // 与现在的差值 —— 比从 run 行再查一次更准，也不依赖 detail 字段。
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    step.costMs = step.startedAtMs > 0 ? qMax<qint64>(0, nowMs - step.startedAtMs) : 0;

    QVariantMap info;
    info.insert(QStringLiteral("role"), step.role);
    info.insert(QStringLiteral("stepIndex"), index);
    info.insert(QStringLiteral("status"), step.status);
    info.insert(QStringLiteral("runId"), step.runId);
    info.insert(QStringLiteral("handoffPath"), step.handoffPath);
    info.insert(QStringLiteral("costMs"), step.costMs);
    info.insert(QStringLiteral("exitCode"), exitCode);
    if (!passed)
        info.insert(QStringLiteral("lastError"),
                    verdict.value(QStringLiteral("lastError")).toString());
    emit stepFinished(chain.chainId, index, info);
    bump();

    // 4) fail-fast：这一步不过就地收口，后面步骤一个都不启动
    if (!passed) {
        const QString reason = verdict.value(QStringLiteral("lastError")).toString();
        markFailed(chain, QStringLiteral("第 %1 步（%2）未通过：%3")
                              .arg(index + 1)
                              .arg(step.role, reason.isEmpty() ? QStringLiteral("退出码 %1").arg(exitCode)
                                                               : reason));
        return;
    }

    startStep(index + 1);
}

QVariantMap WorkflowRunner::chain() const
{
    QVariantMap out;
    out.insert(QStringLiteral("chainId"), m_chain.chainId);
    out.insert(QStringLiteral("taskId"), m_chain.taskId);
    out.insert(QStringLiteral("workflow"), m_chain.workflow);
    out.insert(QStringLiteral("currentIndex"), m_chain.current);
    out.insert(QStringLiteral("done"), m_chain.done);
    out.insert(QStringLiteral("failed"), m_chain.failed);
    out.insert(QStringLiteral("cancelled"), m_chain.cancelled);
    out.insert(QStringLiteral("failMessage"), m_chain.failMessage);

    QVariantList steps;
    for (const Step &step : m_chain.steps) {
        steps.append(QVariantMap{
            {QStringLiteral("role"), step.role},
            {QStringLiteral("agent"), step.agent},
            {QStringLiteral("status"), step.status},
            {QStringLiteral("runId"), step.runId},
            {QStringLiteral("handoffPath"), step.handoffPath},
            {QStringLiteral("costMs"), step.costMs},
        });
    }
    out.insert(QStringLiteral("steps"), steps);
    return out;
}
