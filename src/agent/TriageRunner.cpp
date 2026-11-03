#include "TriageRunner.h"

#include "AgentProfile.h"
#include "Database.h"
#include "ScopeMatcher.h"
#include "TaskExec.h"
#include "TaskModel.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <utility>

namespace {

constexpr int kDefaultTimeoutSec = 600;
constexpr double kExecutableConfidence = 0.5;

const QStringList kRequiredFields = {
    QStringLiteral("understanding"),    QStringLiteral("affected"),  QStringLiteral("scope_draft"),
    QStringLiteral("acceptance_draft"), QStringLiteral("questions"), QStringLiteral("estimate"),
    QStringLiteral("confidence"),
};

QString draftKey(const QString &taskId)
{
    return QStringLiteral("exec.triage.draft.%1").arg(taskId);
}

} // namespace

TriageRunner::TriageRunner(Database *database, AgentProfile *profiles, TaskExec *exec,
                           TaskModel *tasks, QObject *parent)
    : QObject(parent),
      m_database(database),
      m_profiles(profiles),
      m_exec(exec),
      m_tasks(tasks)
{}

TriageRunner::~TriageRunner()
{
    for (Session *s : std::as_const(m_sessions)) {
        if (s->proc && s->proc->state() != QProcess::NotRunning) {
            s->proc->disconnect(this);
            s->proc->kill();
            s->proc->waitForFinished(2000);
        }
    }
    qDeleteAll(m_sessions);
    m_sessions.clear();
}

int TriageRunner::revision() const
{
    return m_revision;
}

void TriageRunner::bump()
{
    ++m_revision;
    emit changed();
}

QJsonDocument TriageRunner::extractJson(const QString &text)
{
    // agent 输出常带解释性文字或代码围栏：找第一个平衡的 {...} 块。
    int start = text.indexOf(QLatin1Char('{'));
    while (start >= 0) {
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (int i = start; i < text.size(); ++i) {
            const QChar ch = text.at(i);
            if (inString) {
                if (escaped)
                    escaped = false;
                else if (ch == QLatin1Char('\\'))
                    escaped = true;
                else if (ch == QLatin1Char('"'))
                    inString = false;
                continue;
            }
            if (ch == QLatin1Char('"')) {
                inString = true;
            } else if (ch == QLatin1Char('{')) {
                ++depth;
            } else if (ch == QLatin1Char('}')) {
                --depth;
                if (depth == 0) {
                    QJsonParseError error{};
                    const QJsonDocument doc =
                        QJsonDocument::fromJson(text.mid(start, i - start + 1).toUtf8(), &error);
                    if (error.error == QJsonParseError::NoError && doc.isObject())
                        return doc;
                    break; // 这个候选块坏了，换下一个 '{' 再试
                }
            }
        }
        start = text.indexOf(QLatin1Char('{'), start + 1);
    }
    return {};
}

bool TriageRunner::isValidGlob(const QString &glob)
{
    const QString g = glob.trimmed();
    if (g.isEmpty())
        return false;
    if (g.contains(QLatin1Char('\\')))
        return false; // 反斜杠：check_scope.py 与 ScopeMatcher 都按正斜杠语义
    if (QDir::isAbsolutePath(g) || g.contains(QLatin1Char(':')))
        return false; // 绝对路径 / 盘符
    const QStringList parts = g.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.contains(QStringLiteral("..")))
        return false;
    return true;
}

QVariantMap TriageRunner::validateDraft(const QVariantMap &raw)
{
    QVariantMap out;
    QStringList missing;
    for (const QString &field : kRequiredFields) {
        if (!raw.contains(field))
            missing.append(field);
    }
    if (!missing.isEmpty()) {
        out.insert(QStringLiteral("ok"), false);
        QVariantList list;
        for (const QString &m : std::as_const(missing))
            list.append(m);
        out.insert(QStringLiteral("missing"), list);
        return out;
    }

    QStringList problems;
    QVariantMap draft;
    draft.insert(QStringLiteral("understanding"),
                 raw.value(QStringLiteral("understanding")).toString());

    const QVariantMap affected = raw.value(QStringLiteral("affected")).toMap();
    draft.insert(QStringLiteral("affected"), affected);

    // scope_draft：非法 glob 丢弃
    QVariantList scope;
    const QVariantList rawScope = raw.value(QStringLiteral("scope_draft")).toList();
    for (const QVariant &v : rawScope) {
        const QString g = v.toString();
        if (isValidGlob(g))
            scope.append(g);
        else
            problems.append(QStringLiteral("丢弃非法 glob：%1").arg(g));
    }
    draft.insert(QStringLiteral("scope_draft"), scope);

    // acceptance_draft：为空补默认
    QVariantList acceptance;
    const QVariantList rawAcceptance = raw.value(QStringLiteral("acceptance_draft")).toList();
    for (const QVariant &v : rawAcceptance) {
        const QString item = v.toString().trimmed();
        if (!item.isEmpty())
            acceptance.append(item);
    }
    if (acceptance.isEmpty()) {
        acceptance.append(QStringLiteral("pwsh -File scripts/verify.ps1"));
        problems.append(QStringLiteral("acceptance_draft 为空，已补默认 verify.ps1"));
    }
    draft.insert(QStringLiteral("acceptance_draft"), acceptance);

    // questions：截断到 3 条
    QVariantList questions;
    const QVariantList rawQuestions = raw.value(QStringLiteral("questions")).toList();
    for (const QVariant &v : rawQuestions) {
        if (questions.size() >= 3) {
            problems.append(QStringLiteral("questions 超过 3 条，已截断"));
            break;
        }
        const QString q = v.toString().trimmed();
        if (!q.isEmpty())
            questions.append(q);
    }
    draft.insert(QStringLiteral("questions"), questions);

    draft.insert(QStringLiteral("split"), raw.value(QStringLiteral("split")).toList());
    draft.insert(QStringLiteral("estimate"), raw.value(QStringLiteral("estimate")).toMap());

    const double confidence = raw.value(QStringLiteral("confidence")).toDouble();
    draft.insert(QStringLiteral("confidence"), confidence);
    draft.insert(QStringLiteral("executable"), confidence >= kExecutableConfidence);
    if (confidence < kExecutableConfidence)
        problems.append(QStringLiteral("置信度 %1 低于 0.5：禁止一键执行，先拆分或澄清")
                            .arg(confidence, 0, 'f', 2));

    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("draft"), draft);
    QVariantList problemList;
    for (const QString &p : std::as_const(problems))
        problemList.append(p);
    out.insert(QStringLiteral("problems"), problemList);
    return out;
}

QString TriageRunner::buildPrompt(const QString &title, const QString &note,
                                  const QString &workflow)
{
    QString prompt = QStringLiteral("你是只读分析器，不能修改任何文件、不能执行命令。\n"
                                    "先读仓库根目录的 AGENTS.md，再按需读取相关代码。\n"
                                    "任务：%1\n");
    prompt = prompt.arg(title);
    if (!note.trimmed().isEmpty())
        prompt += QStringLiteral("描述：%1\n").arg(note.trimmed());
    prompt += QStringLiteral(
        "请输出严格 JSON（字段见下），不要输出解释性文字。\n\n"
        "字段：understanding / affected{files,modules,risk} / scope_draft / acceptance_draft /\n"
        "      questions（≤3） / split / estimate{workflow,rounds,files,size} / confidence(0-1)\n");

    // workflow 专项段（docs/agent-triage.md 的 fix / feature / review 模板）
    if (workflow == QLatin1String("fix")) {
        prompt += QStringLiteral("\n重点回答：\n"
                                 "1. 复现路径是什么？现有测试能否覆盖？\n"
                                 "2. 根因最可能在哪几个函数？\n"
                                 "3. 最小改动集是什么（scope 尽量窄）？\n"
                                 "4. 需要补哪条回归测试？\n");
    } else if (workflow == QLatin1String("feature")) {
        prompt += QStringLiteral("\n重点回答：\n"
                                 "1. 现有架构里应该挂在哪里？有没有类似实现可复用？\n"
                                 "2. 需要动数据库 schema 吗？（能不动就不动，动就要单独说明迁移）\n"
                                 "3. 拆成几步？每步的验收是什么？\n"
                                 "4. 有没有破坏兼容的风险？\n");
    } else if (workflow == QLatin1String("review")) {
        prompt +=
            QStringLiteral("\n重点回答：\n"
                           "1. 这个 diff 改了什么？有没有超出预期的改动？\n"
                           "2. 有没有明显的正确性/性能/安全问题？\n"
                           "3. 测试是否覆盖了新行为？\n"
                           "输出 issues 数组（severity: high|medium|low），不要直接改代码。\n");
    }
    return prompt;
}

QVariantMap TriageRunner::draftOf(const QString &taskId) const
{
    if (!m_database || taskId.trimmed().isEmpty())
        return {};
    const QString text = m_database->metaValue(draftKey(taskId));
    if (text.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject())
        return {};
    return doc.object().toVariantMap();
}

bool TriageRunner::saveDraft(const QString &taskId, const QVariantMap &record)
{
    if (!m_database || taskId.trimmed().isEmpty())
        return false;
    QVariantMap full = record;
    full.insert(QStringLiteral("updatedAt"), QDateTime::currentSecsSinceEpoch());
    const QJsonDocument doc(QJsonObject::fromVariantMap(full));
    const bool ok = m_database->setMetaValue(draftKey(taskId),
                                             QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    if (ok)
        bump();
    return ok;
}

bool TriageRunner::clearDraft(const QString &taskId)
{
    if (!m_database || taskId.trimmed().isEmpty())
        return false;
    if (m_database->metaValue(draftKey(taskId)).isEmpty())
        return true;
    const bool ok = m_database->removeMetaValue(draftKey(taskId));
    if (ok)
        bump();
    return ok;
}

QVariantMap TriageRunner::start(const QString &taskId)
{
    if (!m_profiles->executorEnabled())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("执行器总开关未开启")}};

    for (const Session *s : std::as_const(m_sessions))
        if (s->taskId == taskId)
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("message"), QStringLiteral("这张卡已有分析在跑")}};

    const QVariantMap exec = m_exec->execOf(taskId);
    const QString agent = exec.value(QStringLiteral("agent")).toString().trimmed();
    QString effectiveAgent = agent.isEmpty() ? m_profiles->defaultAgent() : agent;
    if (effectiveAgent.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("未指定 agent")}};

    const QVariantMap profile = m_profiles->profileOf(effectiveAgent);
    if (profile.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("未知 agent：%1").arg(effectiveAgent)}};

    const QString cwd = exec.value(QStringLiteral("cwd")).toString();
    if (cwd.trimmed().isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("未指定工作目录")}};
    if (!m_profiles->isTrustedCwd(cwd))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("工作目录不在信任白名单")}};

    // 只读硬门之一：readonlyArgs 必须存在，否则该 profile 没有可信的只读模式
    const QStringList readonlyArgs = profile.value(QStringLiteral("readonlyArgs")).toStringList();
    if (readonlyArgs.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"),
                 QStringLiteral("agent %1 未配置只读参数，拒绝分析").arg(effectiveAgent)}};

    QString title = taskId;
    QString note;
    if (m_tasks) {
        const QVariantMap task = m_tasks->taskById(taskId);
        title = task.value(QStringLiteral("title")).toString();
        note = task.value(QStringLiteral("note")).toString();
    }
    const QString workflow = exec.value(QStringLiteral("workflow")).toString();
    const QString prompt = buildPrompt(title, note, workflow);

    int timeoutSec = exec.value(QStringLiteral("timeoutMin")).toInt() * 60;
    if (timeoutSec <= 0)
        timeoutSec = profile.value(QStringLiteral("defaultTimeoutMin")).toInt() * 60;
    if (timeoutSec <= 0)
        timeoutSec = kDefaultTimeoutSec;

    Session *s = new Session;
    s->taskId = taskId;
    s->attempt = 0;

    // 只读硬门之二：分析前 git status 基线
    bool gitOk = false;
    s->before = ScopeMatcher::porcelainStatus(cwd, &gitOk);
    s->gitBaselineOk = gitOk;
    m_sessions.append(s);

    const bool stdinPrompt =
        profile.value(QStringLiteral("promptMode")).toString() == QStringLiteral("stdin");
    const QStringList args =
        m_profiles->argv(effectiveAgent, /*readonly=*/true, stdinPrompt ? QString() : prompt);
    launch(*s, args.value(0), args.mid(1), cwd, prompt, stdinPrompt, timeoutSec);
    bump();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("taskId"), taskId}};
}

void TriageRunner::launch(Session &s, const QString &exe, const QStringList &args,
                          const QString &cwd, const QString &prompt, bool stdinPrompt,
                          int timeoutSec)
{
    // 重跑路径会带着已结束的旧进程进来：先摘干净再建新的
    if (s.proc) {
        s.proc->disconnect(this);
        s.proc->deleteLater();
        s.proc = nullptr;
    }
    QProcess *proc = s.proc = new QProcess(this);
    proc->setProgram(exe);
    proc->setArguments(args);
    proc->setWorkingDirectory(cwd);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    const QString taskId = s.taskId;
    QObject::connect(proc, &QProcess::readyReadStandardOutput, this, [&s, proc]() {
        s.output.append(proc->readAllStandardOutput());
    });

    QObject::connect(
        proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this, &s, prompt, stdinPrompt, timeoutSec](int exitCode, QProcess::ExitStatus) {
            s.output.append(s.proc->readAllStandardOutput());
            const QString text = QString::fromUtf8(s.output);
            const QString cwdNow = s.proc->workingDirectory();

            // 只读硬门之三：分析前后 git status 必须一致
            if (s.gitBaselineOk) {
                bool gitOk = false;
                const QStringList after = ScopeMatcher::porcelainStatus(cwdNow, &gitOk);
                if (gitOk) {
                    QSet<QString> beforeSet(s.before.begin(), s.before.end());
                    QStringList diff;
                    for (const QString &line : after)
                        if (!beforeSet.contains(line))
                            diff.append(ScopeMatcher::pathFromPorcelain(line));
                    if (!diff.isEmpty()) {
                        finish(s, {{QStringLiteral("ok"), false},
                                   {QStringLiteral("taskId"), s.taskId},
                                   {QStringLiteral("status"), QStringLiteral("readonly-violation")},
                                   {QStringLiteral("message"),
                                    QStringLiteral("只读失效：分析期间产生改动 %1")
                                        .arg(diff.join(QStringLiteral("、")))}});
                        return;
                    }
                }
            }

            if (exitCode != 0) {
                // 非零退出但无输出 → 进程起不来之类，直接失败
                finish(s, {{QStringLiteral("ok"), false},
                           {QStringLiteral("taskId"), s.taskId},
                           {QStringLiteral("status"), QStringLiteral("failed")},
                           {QStringLiteral("message"),
                            QStringLiteral("分析进程退出码 %1").arg(exitCode)},
                           {QStringLiteral("raw"), text.right(4000)}});
                return;
            }

            const QJsonDocument doc = extractJson(text);
            if (doc.isNull()) {
                if (s.attempt == 0) {
                    // 严格 JSON 失败 → 重跑一次（同一命令，agent 可能自行修正）
                    s.attempt = 1;
                    s.output.clear();
                    const QStringList rerun = s.proc->arguments();
                    const QString exeNow = s.proc->program();
                    launch(s, exeNow, rerun, cwdNow, prompt, stdinPrompt, timeoutSec);
                    return;
                }
                finish(s, {{QStringLiteral("ok"), false},
                           {QStringLiteral("taskId"), s.taskId},
                           {QStringLiteral("status"), QStringLiteral("draft")},
                           {QStringLiteral("message"),
                            QStringLiteral("重跑后仍不是合法 JSON，保留原始输出")},
                           {QStringLiteral("raw"), text.right(4000)}});
                return;
            }

            const QVariantMap validated = validateDraft(doc.object().toVariantMap());
            if (!validated.value(QStringLiteral("ok")).toBool()) {
                if (s.attempt == 0) {
                    s.attempt = 1;
                    s.output.clear();
                    launch(s, s.proc->program(), s.proc->arguments(), cwdNow, prompt, stdinPrompt,
                           timeoutSec);
                    return;
                }
                const QStringList missingFields =
                    validated.value(QStringLiteral("missing")).toStringList();
                finish(
                    s,
                    {{QStringLiteral("ok"), false},
                     {QStringLiteral("taskId"), s.taskId},
                     {QStringLiteral("status"), QStringLiteral("draft")},
                     {QStringLiteral("message"),
                      QStringLiteral("缺必填字段：%1").arg(missingFields.join(QLatin1Char(',')))},
                     {QStringLiteral("raw"), text.right(4000)}});
                return;
            }

            QVariantMap result{
                {QStringLiteral("ok"), true},
                {QStringLiteral("taskId"), s.taskId},
                {QStringLiteral("status"), QStringLiteral("ok")},
                {QStringLiteral("draft"), validated.value(QStringLiteral("draft"))},
                {QStringLiteral("problems"), validated.value(QStringLiteral("problems"))}};
            finish(s, result);
        });

    // 超时兜底：kill 后 finished 信号走退出码非 0 分支。
    // QPointer 防悬空：proc 可能在重试/结束时被 deleteLater，定时器晚到时自动置空。
    QPointer<QProcess> guard(proc);
    QTimer::singleShot(timeoutSec * 1000, this, [guard]() {
        if (guard && guard->state() != QProcess::NotRunning)
            guard->kill();
    });

    proc->start();
    if (!proc->waitForStarted(5000)) {
        const QString err = proc->errorString();
        finish(s, {{QStringLiteral("ok"), false},
                   {QStringLiteral("taskId"), s.taskId},
                   {QStringLiteral("status"), QStringLiteral("failed")},
                   {QStringLiteral("message"), QStringLiteral("分析进程启动失败：%1").arg(err)}});
        return;
    }
    if (stdinPrompt) {
        proc->write(prompt.toUtf8());
        proc->closeWriteChannel();
    }
}

void TriageRunner::finish(Session &s, const QVariantMap &result)
{
    // 成功 / draft / readonly-violation 都落 meta：界面能看到「上次分析的结论」
    if (result.value(QStringLiteral("status")).toString() != QStringLiteral("failed")) {
        QVariantMap record{{QStringLiteral("status"), result.value(QStringLiteral("status"))}};
        if (result.contains(QStringLiteral("draft")))
            record.insert(QStringLiteral("draft"), result.value(QStringLiteral("draft")));
        if (result.contains(QStringLiteral("problems")))
            record.insert(QStringLiteral("problems"), result.value(QStringLiteral("problems")));
        if (result.contains(QStringLiteral("raw")))
            record.insert(QStringLiteral("raw"), result.value(QStringLiteral("raw")));
        if (result.contains(QStringLiteral("message")))
            record.insert(QStringLiteral("message"), result.value(QStringLiteral("message")));
        saveDraft(s.taskId, record);
    }

    if (s.proc) {
        s.proc->disconnect(this);
        s.proc->deleteLater();
        s.proc = nullptr;
    }
    m_sessions.removeOne(&s);
    delete &s;
    bump();
    emit triageFinished(result.value(QStringLiteral("taskId")).toString(), result);
}
