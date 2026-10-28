// R7-03：角色链 workflow 引擎 —— 步骤数组 / 预设 / 交接物 / 逐步执行与 fail-fast。
//
// 覆盖六条：
//   1. 预设步骤：六个预设各自的角色序列；未知 workflow 返回空表
//   2. 步骤校验：未知角色 / 非字符串 acceptance 被拒；归一化保留 role/agent/prompt
//   3. 单据覆盖：setSteps 存 meta（不碰 task_exec、不升 user_version）、clearSteps 复位
//   4. 交接物路径契约：<root>/<taskId>/<role>.md；prompt 注入上一步全文
//   5. 交接物摘要：超长截断保留尾部
//   6. 端到端：两步链按序执行、handoff 落盘且下一步读到、每步独立 run（parent/step 标注）、
//      某步失败即停（后续步骤不启动）
//
// 载体用 cmd.exe（ComSpec）：离线可跑、稳定可重复，不依赖真实 agent。

#include "AcceptanceRunner.h"
#include "AgentProfile.h"
#include "Database.h"
#include "ScopeMatcher.h"
#include "TaskExec.h"
#include "TaskModel.h"
#include "TaskRunner.h"
#include "TaskRunStore.h"
#include "WorkflowRunner.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString kCmd = qEnvironmentVariable("ComSpec", "cmd.exe");

// cmd 里写日志：echo 写入指定文件（路径用正斜杠，cmd 认）
QString scriptWrite(const QString &path, const QString &text)
{
    return QStringLiteral("echo %1> \"%2\"").arg(text, QDir::toNativeSeparators(path));
}
} // namespace

class TestWorkflow : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void presetStepsByWorkflow();
    void normalizeRejectsUnknownRole();
    void stepOverrideStoredInMeta();
    void handoffPathAndPromptInjection();
    void summarizeKeepsTail();
    void twoStepChainRunsInOrder();
    void failedStepStopsChain();
    void startRejectsUnsafePreconditions();

private:
    QString addCard(const QString &title);

    QTemporaryDir *m_dir = nullptr;
    Database *m_db = nullptr;
    TaskModel *m_tasks = nullptr;
    TaskExec *m_exec = nullptr;
    TaskRunStore *m_runs = nullptr;
    AgentProfile *m_profiles = nullptr;
    TaskRunner *m_runner = nullptr;
    ScopeMatcher *m_scope = nullptr;
    AcceptanceRunner *m_acceptance = nullptr;
    WorkflowRunner *m_workflow = nullptr;
};

QString TestWorkflow::addCard(const QString &title)
{
    return m_tasks->addTask(title, QStringLiteral("todo"), 1, QString(), 0);
}

void TestWorkflow::init()
{
    // 每个用例一个全新临时库 + 独立数据目录（交接物落 NEONBOARD_DATA_DIR 下）
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    qputenv("NEONBOARD_DATA_DIR", m_dir->path().toUtf8());

    m_db = new Database(this);
    QString error;
    QVERIFY2(m_db->open(&error, m_dir->filePath(QStringLiteral("board.db"))), qPrintable(error));
    m_tasks = new TaskModel(m_db, this);
    m_tasks->load();
    m_exec = new TaskExec(m_db, m_tasks, this);
    m_runs = new TaskRunStore(m_db, this);
    m_profiles = new AgentProfile(m_db, this);
    m_runner = new TaskRunner(m_db, m_runs, m_profiles, m_exec, this);
    m_scope = new ScopeMatcher(this);
    m_acceptance = new AcceptanceRunner(this);
    m_workflow = new WorkflowRunner(m_db, m_exec, m_runs, m_runner, m_profiles, m_scope,
                                    m_acceptance, this);
    m_profiles->setExecutorEnabled(true);
}

void TestWorkflow::cleanup()
{
    delete m_workflow;
    delete m_acceptance;
    delete m_scope;
    delete m_runner;
    delete m_profiles;
    delete m_runs;
    delete m_exec;
    delete m_tasks;
    delete m_db;
    delete m_dir;
    m_workflow = nullptr;
    m_acceptance = nullptr;
    m_scope = nullptr;
    m_runner = nullptr;
    m_profiles = nullptr;
    m_runs = nullptr;
    m_exec = nullptr;
    m_tasks = nullptr;
    m_db = nullptr;
    m_dir = nullptr;
}

void TestWorkflow::presetStepsByWorkflow()
{
    // fix = engineer → qa（qa 不能被省）
    const QVariantList fix = TaskExec::presetSteps(QStringLiteral("fix"));
    QCOMPARE(fix.size(), 2);
    QCOMPARE(fix.at(0).toMap().value(QStringLiteral("role")).toString(), QStringLiteral("engineer"));
    QCOMPARE(fix.at(1).toMap().value(QStringLiteral("role")).toString(), QStringLiteral("qa"));

    // feature = pm → architect → engineer → qa
    const QVariantList feature = TaskExec::presetSteps(QStringLiteral("feature"));
    QCOMPARE(feature.size(), 4);
    QCOMPARE(feature.at(0).toMap().value(QStringLiteral("role")).toString(), QStringLiteral("pm"));
    QCOMPARE(feature.at(1).toMap().value(QStringLiteral("role")).toString(),
             QStringLiteral("architect"));
    QCOMPARE(feature.at(3).toMap().value(QStringLiteral("role")).toString(), QStringLiteral("qa"));

    // delivery = 五步，最后是 orchestrator（收口）
    const QVariantList delivery = TaskExec::presetSteps(QStringLiteral("delivery"));
    QCOMPARE(delivery.size(), 5);
    QCOMPARE(delivery.last().toMap().value(QStringLiteral("role")).toString(),
             QStringLiteral("orchestrator"));

    // docs / review / hotfix
    QCOMPARE(TaskExec::presetSteps(QStringLiteral("docs")).size(), 1);
    QCOMPARE(TaskExec::presetSteps(QStringLiteral("review")).size(), 1);
    QCOMPARE(TaskExec::presetSteps(QStringLiteral("hotfix")).size(), 2);
    // 未知 workflow：空表（不猜）
    QVERIFY(TaskExec::presetSteps(QStringLiteral("no-such")).isEmpty());
    QVERIFY(TaskExec::presetSteps(QString()).isEmpty());
}

void TestWorkflow::normalizeRejectsUnknownRole()
{
    // 未知角色
    const QVariantMap bad = TaskExec::normalizeSteps(
        {QVariantMap{{QStringLiteral("role"), QStringLiteral("wizard")}}});
    QVERIFY(!bad.value(QStringLiteral("ok")).toBool());
    QVERIFY(bad.value(QStringLiteral("error")).toString().contains(QStringLiteral("未知角色")));

    // acceptance 里混进空串
    const QVariantMap badList = TaskExec::normalizeSteps({QVariantMap{
        {QStringLiteral("role"), QStringLiteral("qa")},
        {QStringLiteral("acceptance"), QVariantList{QStringLiteral("ok"), QString()}},
    }});
    QVERIFY(!badList.value(QStringLiteral("ok")).toBool());

    // 空数组
    QVERIFY(!TaskExec::normalizeSteps({}).value(QStringLiteral("ok")).toBool());

    // 合法：字段归一化（角色小写、agent/prompt 去空白）
    const QVariantMap good = TaskExec::normalizeSteps({QVariantMap{
        {QStringLiteral("role"), QStringLiteral("  QA  ")},
        {QStringLiteral("agent"), QStringLiteral(" opencode ")},
        {QStringLiteral("prompt"), QStringLiteral("  复核  ")},
        {QStringLiteral("acceptance"), QVariantList{QStringLiteral("--selftest")}},
        {QStringLiteral("scope"), QVariantList{QStringLiteral("src/**")}},
    }});
    QVERIFY(good.value(QStringLiteral("ok")).toBool());
    const QVariantList steps = good.value(QStringLiteral("steps")).toList();
    QCOMPARE(steps.size(), 1);
    const QVariantMap step = steps.first().toMap();
    QCOMPARE(step.value(QStringLiteral("role")).toString(), QStringLiteral("qa"));
    QCOMPARE(step.value(QStringLiteral("agent")).toString(), QStringLiteral("opencode"));
    QCOMPARE(step.value(QStringLiteral("prompt")).toString(), QStringLiteral("复核"));
    QCOMPARE(step.value(QStringLiteral("acceptance")).toList().size(), 1);
}

void TestWorkflow::stepOverrideStoredInMeta()
{
    const QString id = addCard(QStringLiteral("wf-override"));
    QVERIFY(!id.isEmpty());
    QVERIFY(m_exec->setExec(id, {{QStringLiteral("workflow"), QStringLiteral("fix")},
                                 {QStringLiteral("agent"), QStringLiteral("opencode")}}));

    // 未覆盖时 = 预设
    QCOMPARE(m_exec->stepsOf(id).size(), 2);

    // 覆盖：只留一步（卡片换 agent / 调 prompt）
    const QVariantList custom{QVariantMap{
        {QStringLiteral("role"), QStringLiteral("engineer")},
        {QStringLiteral("agent"), QStringLiteral("claude")},
        {QStringLiteral("prompt"), QStringLiteral("只改这一处")},
    }};
    QVERIFY(m_exec->setSteps(id, custom));
    const QVariantList steps = m_exec->stepsOf(id);
    QCOMPARE(steps.size(), 1);
    QCOMPARE(steps.first().toMap().value(QStringLiteral("agent")).toString(),
             QStringLiteral("claude"));

    // 覆盖项落 meta，不往 task_exec 加列、不升 user_version
    QSqlQuery meta(QSqlDatabase::database(m_db->connectionName()));
    meta.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
    meta.addBindValue(QStringLiteral("exec.workflowSteps.") + id);
    QVERIFY(meta.exec());
    QVERIFY(meta.next());
    QVERIFY(!meta.value(0).toString().isEmpty());

    QSqlQuery columns(QSqlDatabase::database(m_db->connectionName()));
    QVERIFY(columns.exec(QStringLiteral("PRAGMA table_info(task_exec)")));
    QSet<QString> names;
    while (columns.next())
        names.insert(columns.value(1).toString());
    QVERIFY(!names.contains(QStringLiteral("steps")));
    QVERIFY(!names.contains(QStringLiteral("workflow_steps")));

    QSqlQuery version(QSqlDatabase::database(m_db->connectionName()));
    QVERIFY(version.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(version.next());
    QCOMPARE(version.value(0).toInt(), Database::kSchemaVersion);

    // clearSteps 回到预设
    QVERIFY(m_exec->clearSteps(id));
    QCOMPARE(m_exec->stepsOf(id).size(), 2);

    // 删卡要清掉覆盖项，别留孤儿 meta
    QVERIFY(m_exec->setSteps(id, custom));
    m_tasks->deleteTask(id);
    QSqlQuery after(QSqlDatabase::database(m_db->connectionName()));
    after.prepare(QStringLiteral("SELECT COUNT(*) FROM meta WHERE key = ?"));
    after.addBindValue(QStringLiteral("exec.workflowSteps.") + id);
    QVERIFY(after.exec());
    QVERIFY(after.next());
    QCOMPARE(after.value(0).toInt(), 0);
}

void TestWorkflow::handoffPathAndPromptInjection()
{
    const QString root = m_dir->filePath(QStringLiteral("handoff"));
    QCOMPARE(WorkflowRunner::handoffPath(root, QStringLiteral("card-1"), QStringLiteral("qa")),
             QDir(root).filePath(QStringLiteral("qa.md")));

    const QVariantMap step{{QStringLiteral("role"), QStringLiteral("engineer")}};
    // 第一步：没有上游交接物
    const QString first = WorkflowRunner::buildStepPrompt(
        QStringLiteral("card-1"), QStringLiteral("feature"), step, 0, 4,
        QStringLiteral("实现登录页"), QString());
    QVERIFY(first.contains(QStringLiteral("第一步")));
    QVERIFY(first.contains(QStringLiteral("实现登录页")));
    QVERIFY(first.contains(QStringLiteral("1/4")));

    // 第二步：上一步交接物全文必须被注入
    const QString previous = QStringLiteral("## 上一步结论\n接口已就绪");
    const QString second = WorkflowRunner::buildStepPrompt(
        QStringLiteral("card-1"), QStringLiteral("feature"), step, 1, 4,
        QStringLiteral("复核"), previous);
    QVERIFY(second.contains(previous));
    QVERIFY(second.contains(QStringLiteral("2/4")));
    // 交接物结束标记得在全文之后
    QVERIFY(second.indexOf(QStringLiteral("交接物结束")) > second.indexOf(previous));
    // 硬约束必须写进 prompt
    QVERIFY(second.contains(QStringLiteral("不要执行 git push")));
}

void TestWorkflow::summarizeKeepsTail()
{
    QByteArray log;
    for (int i = 0; i < 400; ++i)
        log.append(QStringLiteral("line-%1-payload\n").arg(i).toUtf8());
    log.append("FINAL-CONCLUSION\n");
    const QString summary = WorkflowRunner::summarizeHandoff(log, 500);
    QVERIFY(summary.size() < 700);
    QVERIFY(summary.contains(QStringLiteral("FINAL-CONCLUSION"))); // 尾部保留
    QVERIFY(summary.contains(QStringLiteral("前文省略")));

    // 空日志 → 空串（不写无意义的交接物）
    QVERIFY(WorkflowRunner::summarizeHandoff(QByteArray()).isEmpty());
}

void TestWorkflow::twoStepChainRunsInOrder()
{
    // 真实端到端：注册两个「走 cmd.exe」的 profile（promptMode=stdin，所以角色链拼的
    // 长 prompt 不会进 argv，命令只由 profile 的 args 决定），让角色链真的按序起两个进程。
    // 每步输出一行独有标记 → 该标记成为本步的交接物内容，用来验证「按序 + 落盘」。
    const QVariantMap stepA{
        {QStringLiteral("id"), QStringLiteral("cmd-step-a")},
        {QStringLiteral("label"), QStringLiteral("测试用 cmd（engineer）")},
        {QStringLiteral("exe"), kCmd},
        {QStringLiteral("args"), QStringList{QStringLiteral("/c"), QStringLiteral("echo"),
                                             QStringLiteral("MARKER-ENGINEER")}},
        {QStringLiteral("readonlyArgs"), QStringList()},
        {QStringLiteral("promptMode"), QStringLiteral("stdin")},
        {QStringLiteral("output"), QStringLiteral("text")},
        {QStringLiteral("defaultTimeoutMin"), 2},
    };
    QVariantMap stepB = stepA;
    stepB.insert(QStringLiteral("id"), QStringLiteral("cmd-step-b"));
    stepB.insert(QStringLiteral("label"), QStringLiteral("测试用 cmd（qa）"));
    stepB.insert(QStringLiteral("args"), QStringList{QStringLiteral("/c"), QStringLiteral("echo"),
                                                    QStringLiteral("MARKER-QA")});
    QVERIFY(m_profiles->saveProfile(stepA));
    QVERIFY(m_profiles->saveProfile(stepB));

    const QString id = addCard(QStringLiteral("wf-two-step"));
    QVERIFY(!id.isEmpty());
    QVERIFY(m_exec->setExec(id, {{QStringLiteral("workflow"), QString()},
                                 {QStringLiteral("agent"), QStringLiteral("cmd-step-a")},
                                 {QStringLiteral("cwd"), m_dir->path()}}));

    const QVariantList steps{
        QVariantMap{
            {QStringLiteral("role"), QStringLiteral("engineer")},
            {QStringLiteral("agent"), QStringLiteral("cmd-step-a")},
        },
        QVariantMap{
            {QStringLiteral("role"), QStringLiteral("qa")},
            {QStringLiteral("agent"), QStringLiteral("cmd-step-b")},
        },
    };
    QVERIFY(m_exec->setSteps(id, steps));

    QSignalSpy started(m_workflow, &WorkflowRunner::stepStarted);
    QSignalSpy finished(m_workflow, &WorkflowRunner::stepFinished);
    QSignalSpy chainDone(m_workflow, &WorkflowRunner::chainFinished);

    const QVariantMap began = m_workflow->start(id);
    QVERIFY2(began.value(QStringLiteral("ok")).toBool(),
             qPrintable(began.value(QStringLiteral("message")).toString()));
    QVERIFY(m_workflow->running());

    QTRY_VERIFY_WITH_TIMEOUT(chainDone.count() >= 1, 60000);
    QCOMPARE(started.count(), 2);
    QCOMPARE(finished.count(), 2);

    // 顺序：engineer 先于 qa
    QCOMPARE(started.at(0).at(2).toMap().value(QStringLiteral("role")).toString(),
             QStringLiteral("engineer"));
    QCOMPARE(started.at(1).at(2).toMap().value(QStringLiteral("role")).toString(),
             QStringLiteral("qa"));

    // 交接物落盘且内容来自各自那一步（不是互相覆盖）
    const QString dir = m_workflow->handoffDir(id);
    const QString engineerHandoff = QDir(dir).filePath(QStringLiteral("engineer.md"));
    const QString qaHandoff = QDir(dir).filePath(QStringLiteral("qa.md"));
    QVERIFY(QFile::exists(engineerHandoff));
    QVERIFY(QFile::exists(qaHandoff));
    auto readAll = [](const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    QVERIFY(readAll(engineerHandoff).contains(QStringLiteral("MARKER-ENGINEER")));
    QVERIFY(readAll(qaHandoff).contains(QStringLiteral("MARKER-QA")));

    // 链快照：两步都 passed、done，且各自一条独立 runId
    const QVariantMap snapshot = m_workflow->chain();
    QVERIFY(snapshot.value(QStringLiteral("done")).toBool());
    QVERIFY(!snapshot.value(QStringLiteral("failed")).toBool());
    const QVariantList chainSteps = snapshot.value(QStringLiteral("steps")).toList();
    QCOMPARE(chainSteps.size(), 2);
    for (const QVariant &value : chainSteps) {
        const QVariantMap step = value.toMap();
        QCOMPARE(step.value(QStringLiteral("status")).toString(), QStringLiteral("passed"));
        QVERIFY(!step.value(QStringLiteral("runId")).toString().isEmpty());
    }
    QVERIFY(chainSteps.at(0).toMap().value(QStringLiteral("runId")).toString()
            != chainSteps.at(1).toMap().value(QStringLiteral("runId")).toString());

    // 每步一条 run 行，role / step_index 标注进 run（「每步独立 run」）
    const QVariantList runs = m_runs->runsOf(id, 10);
    QCOMPARE(runs.size(), 2);
    QSet<QString> roles;
    QSet<int> indexes;
    for (const QVariant &value : runs) {
        const QVariantMap row = value.toMap();
        roles.insert(row.value(QStringLiteral("role")).toString());
        indexes.insert(row.value(QStringLiteral("stepIndex")).toInt());
    }
    QVERIFY(roles.contains(QStringLiteral("engineer")));
    QVERIFY(roles.contains(QStringLiteral("qa")));
    QVERIFY(indexes.contains(0));
    QVERIFY(indexes.contains(1));
}

void TestWorkflow::failedStepStopsChain()
{
    // 同上：profile 指到 cmd，但第一步故意非 0 退出 → 链必须就地收口，第二步不跑。
    const QVariantMap failProfile{
        {QStringLiteral("id"), QStringLiteral("cmd-fail")},
        {QStringLiteral("label"), QStringLiteral("测试用 cmd（失败）")},
        {QStringLiteral("exe"), kCmd},
        {QStringLiteral("args"), QStringList{QStringLiteral("/c"), QStringLiteral("exit"),
                                             QStringLiteral("7")}},
        {QStringLiteral("readonlyArgs"), QStringList()},
        {QStringLiteral("promptMode"), QStringLiteral("stdin")},
        {QStringLiteral("output"), QStringLiteral("text")},
        {QStringLiteral("defaultTimeoutMin"), 2},
    };
    QVERIFY(m_profiles->saveProfile(failProfile));

    const QString id = addCard(QStringLiteral("wf-fail"));
    QVERIFY(!id.isEmpty());
    QVERIFY(m_exec->setExec(id, {{QStringLiteral("workflow"), QString()},
                                 {QStringLiteral("agent"), QStringLiteral("cmd-fail")},
                                 {QStringLiteral("cwd"), m_dir->path()}}));

    const QVariantList steps{
        QVariantMap{
            {QStringLiteral("role"), QStringLiteral("engineer")},
            {QStringLiteral("agent"), QStringLiteral("cmd-fail")},
        },
        QVariantMap{
            {QStringLiteral("role"), QStringLiteral("qa")},
            {QStringLiteral("agent"), QStringLiteral("cmd-fail")},
        },
    };
    QVERIFY(m_exec->setSteps(id, steps));

    QSignalSpy started(m_workflow, &WorkflowRunner::stepStarted);
    QSignalSpy chainDone(m_workflow, &WorkflowRunner::chainFinished);
    const QVariantMap began = m_workflow->start(id);
    QVERIFY2(began.value(QStringLiteral("ok")).toBool(),
             qPrintable(began.value(QStringLiteral("message")).toString()));

    QTRY_VERIFY_WITH_TIMEOUT(chainDone.count() >= 1, 60000);
    // fail-fast：只有第一步启动过，第二步一个进程都没起
    QCOMPARE(started.count(), 1);
    QCOMPARE(started.at(0).at(2).toMap().value(QStringLiteral("role")).toString(),
             QStringLiteral("engineer"));

    const QVariantMap snapshot = m_workflow->chain();
    QVERIFY(snapshot.value(QStringLiteral("done")).toBool());
    QVERIFY(snapshot.value(QStringLiteral("failed")).toBool());
    const QVariantList chainSteps = snapshot.value(QStringLiteral("steps")).toList();
    QCOMPARE(chainSteps.at(0).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("blocked"));
    // 第二步没启动，仍是 pending
    QCOMPARE(chainSteps.at(1).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("pending"));

    const QVariantMap result = chainDone.at(0).at(1).toMap();
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QCOMPARE(result.value(QStringLiteral("failedStep")).toInt(), 0);
}

void TestWorkflow::startRejectsUnsafePreconditions()
{
    const QString id = addCard(QStringLiteral("wf-precondition"));
    QVERIFY(!id.isEmpty());

    // 1) 缺 agent：不能起一条注定失败的链
    QVERIFY(m_exec->setExec(id, {{QStringLiteral("workflow"), QStringLiteral("fix")},
                                 {QStringLiteral("agent"), QString()}}));
    m_profiles->setDefaultAgent(QString());
    const QVariantMap noAgent = m_workflow->start(id);
    QVERIFY(!noAgent.value(QStringLiteral("ok")).toBool());
    QVERIFY(noAgent.value(QStringLiteral("message")).toString().contains(QStringLiteral("agent")));

    // 2) 没有可执行步骤（workflow 未选且无覆盖）
    m_profiles->setDefaultAgent(QStringLiteral("opencode"));
    QVERIFY(m_exec->setExec(id, {{QStringLiteral("workflow"), QString()}}));
    const QVariantMap noSteps = m_workflow->start(id);
    QVERIFY(!noSteps.value(QStringLiteral("ok")).toBool());

    // 3) 执行器总开关关着：安全默认必须拒绝（与未引入执行器时一致）
    m_exec->setExec(id, {{QStringLiteral("workflow"), QStringLiteral("fix")}});
    m_profiles->setExecutorEnabled(false);
    const QVariantMap disabled = m_workflow->start(id);
    QVERIFY(!disabled.value(QStringLiteral("ok")).toBool());
    QVERIFY(disabled.value(QStringLiteral("message")).toString()
                .contains(QStringLiteral("总开关")));
    QVERIFY(!m_workflow->running());
    QVERIFY(m_workflow->chain().value(QStringLiteral("chainId")).toString().isEmpty());
}

QTEST_MAIN(TestWorkflow)
#include "tst_workflow.moc"