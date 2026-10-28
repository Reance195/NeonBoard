// R7-04：预设 workflow + orchestrator 收口 —— 预设目录（可编辑，存 meta）/ 规模选型 /
// 跳过规则（qa 永不裁）/ 收口 summary / WorkflowRunner 接上预设后的选型与步骤展开。
//
// 覆盖：
//   1. 内置六条与 TaskExec::presetSteps 的角色序一致；每条必有产出方；skippable 不含受保护角色
//   2. 规模选型 routeFor（S/M/L/XL / 数字 / 未知兜底）
//   3. 跳过规则：只有小任务可裁，且只裁 pm/architect，qa 永不在列
//   4. 预设可编辑：非法被拒 / 覆盖写 meta / reset 复位 / 删最后一条退回内置 / 不升 user_version
//   5. 规模映射表可编辑：非法目标被拒
//   6. orchestrator 收口 summary 拼接
//   7. 端到端：WorkflowRunner 接上预设后，卡片未选 workflow → 自动选型并展开预设步骤执行

#include "AcceptanceRunner.h"
#include "AgentProfile.h"
#include "Database.h"
#include "ScopeMatcher.h"
#include "TaskExec.h"
#include "TaskModel.h"
#include "TaskRunner.h"
#include "TaskRunStore.h"
#include "WorkflowPresets.h"
#include "WorkflowRunner.h"

#include <QDir>
#include <QSet>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString kCmd = qEnvironmentVariable("ComSpec", "cmd.exe");

QStringList rolesOf(const QVariantList &steps)
{
    QStringList out;
    for (const QVariant &v : steps)
        out.append(v.toMap().value(QStringLiteral("role")).toString());
    return out;
}

// 预设定义里的 steps 是角色字符串数组（不是步骤 map），单独取。
QStringList presetRoles(const QVariant &value)
{
    QStringList out;
    for (const QVariant &v : value.toList())
        out.append(v.toString());
    return out;
}
} // namespace

class TestWorkflowPresets : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void builtinsMatchTaskExec();
    void routeForBySize();
    void skipRulesNeverDropQa();
    void presetsAreEditable();
    void routingIsEditable();
    void summaryIncludesRolesAndAdvice();
    void workflowRunnerSelectsAndExpands();

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
    WorkflowPresets *m_presets = nullptr;
    WorkflowRunner *m_workflow = nullptr;
};

QString TestWorkflowPresets::addCard(const QString &title)
{
    return m_tasks->addTask(title, QStringLiteral("todo"), 1, QString(), 0);
}

void TestWorkflowPresets::init()
{
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
    m_presets = new WorkflowPresets(m_db, this);
    m_workflow = new WorkflowRunner(m_db, m_exec, m_runs, m_runner, m_profiles, m_scope,
                                    m_acceptance, this);
    m_profiles->setExecutorEnabled(true);
}

void TestWorkflowPresets::cleanup()
{
    delete m_workflow;
    delete m_presets;
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
    m_presets = nullptr;
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

void TestWorkflowPresets::builtinsMatchTaskExec()
{
    const QVariantList builtins = WorkflowPresets::builtinPresets();
    QCOMPARE(builtins.size(), 6);

    QSet<QString> keys;
    for (const QVariant &value : builtins) {
        const QVariantMap preset = value.toMap();
        const QString key = preset.value(QStringLiteral("key")).toString();
        keys.insert(key);
        QVERIFY(!key.isEmpty());
        QVERIFY(preset.value(QStringLiteral("builtin")).toBool());
        QVERIFY(!preset.value(QStringLiteral("label")).toString().isEmpty());

        // 角色序必须与 TaskExec::presetSteps 完全一致（两处表达同一份事实）
        QCOMPARE(presetRoles(preset.value(QStringLiteral("steps"))),
                 rolesOf(TaskExec::presetSteps(key)));

        // 每条必有产出 / 收口方（engineer / writer / orchestrator）
        const QStringList roles = presetRoles(preset.value(QStringLiteral("steps")));
        QVERIFY(roles.contains(QStringLiteral("engineer"))
                || roles.contains(QStringLiteral("writer"))
                || roles.contains(QStringLiteral("orchestrator")));

        // skippable 不含受保护角色（qa 永不允许跳过）
        for (const QVariant &v : preset.value(QStringLiteral("skippable")).toList()) {
            QVERIFY(!WorkflowPresets::isProtectedRole(v.toString()));
        }
    }
    QCOMPARE(keys.size(), 6);
    QVERIFY(keys.contains(QStringLiteral("fix")));
    QVERIFY(keys.contains(QStringLiteral("delivery")));
}

void TestWorkflowPresets::routeForBySize()
{
    QCOMPARE(m_presets->routeFor(QStringLiteral("S")), QStringLiteral("fix"));
    QCOMPARE(m_presets->routeFor(QStringLiteral("M")), QStringLiteral("feature"));
    QCOMPARE(m_presets->routeFor(QStringLiteral("L")), QStringLiteral("delivery"));
    QCOMPARE(m_presets->routeFor(QStringLiteral("XL")), QStringLiteral("delivery"));
    // 数字等价
    QCOMPARE(m_presets->routeFor(QStringLiteral("1")), QStringLiteral("fix"));
    QCOMPARE(m_presets->routeFor(QStringLiteral("4")), QStringLiteral("delivery"));
    // 大小写不敏感
    QCOMPARE(m_presets->routeFor(QStringLiteral("m")), QStringLiteral("feature"));

    // 未知规模 → 卡片兜底：无卡片信息 → fix
    QCOMPARE(m_presets->routeFor(QString()), QStringLiteral("fix"));
    // 短标题 → fix；长标题 → delivery
    QCOMPARE(m_presets->routeFor(QString(), QVariantMap{{QStringLiteral("title"),
                                                         QStringLiteral("短标题")}}),
             QStringLiteral("fix"));
    QString longTitle;
    for (int i = 0; i < 80; ++i)
        longTitle += QLatin1Char('x');
    QCOMPARE(m_presets->routeFor(QString(), QVariantMap{{QStringLiteral("title"), longTitle}}),
             QStringLiteral("delivery"));
    // 高优先级 → delivery
    QCOMPARE(m_presets->routeFor(QString(), QVariantMap{{QStringLiteral("priority"), 3}}),
             QStringLiteral("delivery"));
}

void TestWorkflowPresets::skipRulesNeverDropQa()
{
    // feature@S 可跳 pm / architect（恰好两条），绝不含 qa
    const QVariantList smallSkips =
        m_presets->skippableSteps(QStringLiteral("feature"), QStringLiteral("S"));
    QCOMPARE(smallSkips.size(), 2);
    QVERIFY(smallSkips.contains(QStringLiteral("pm")));
    QVERIFY(smallSkips.contains(QStringLiteral("architect")));
    QVERIFY(!smallSkips.contains(QStringLiteral("qa")));

    // 非小任务：一律不裁
    QVERIFY(m_presets->skippableSteps(QStringLiteral("feature"), QStringLiteral("L")).isEmpty());
    QVERIFY(m_presets->skippableSteps(QStringLiteral("feature"), QString()).isEmpty());

    // feature@S 展开 = engineer, qa（pm / architect 被裁）
    QCOMPARE(rolesOf(m_presets->stepsFor(QStringLiteral("feature"), QStringLiteral("S"))),
             QStringList({QStringLiteral("engineer"), QStringLiteral("qa")}));
    // feature@M 展开 = 全四步
    QCOMPARE(rolesOf(m_presets->stepsFor(QStringLiteral("feature"), QStringLiteral("M"))),
             QStringList({QStringLiteral("pm"), QStringLiteral("architect"),
                          QStringLiteral("engineer"), QStringLiteral("qa")}));
    // delivery@S：裁掉 pm / architect，但 orchestrator（收口）保留
    QCOMPARE(rolesOf(m_presets->stepsFor(QStringLiteral("delivery"), QStringLiteral("S"))),
             QStringList({QStringLiteral("engineer"), QStringLiteral("qa"),
                          QStringLiteral("orchestrator")}));
    // fix@S：没有可裁项，原样
    QCOMPARE(rolesOf(m_presets->stepsFor(QStringLiteral("fix"), QStringLiteral("S"))),
             QStringList({QStringLiteral("engineer"), QStringLiteral("qa")}));
}

void TestWorkflowPresets::presetsAreEditable()
{
    // 非法：skippable 含 qa
    QVariantMap bad;
    bad.insert(QStringLiteral("key"), QStringLiteral("bad1"));
    bad.insert(QStringLiteral("steps"), QVariantList{QStringLiteral("engineer")});
    bad.insert(QStringLiteral("skippable"), QVariantList{QStringLiteral("qa")});
    QVERIFY(m_presets->normalizePreset(bad).isEmpty());
    QVERIFY(!m_presets->savePreset(bad));

    // 非法：没有产出方（只有 pm）
    QVariantMap noProducer;
    noProducer.insert(QStringLiteral("key"), QStringLiteral("bad2"));
    noProducer.insert(QStringLiteral("label"), QStringLiteral("无产出"));
    noProducer.insert(QStringLiteral("steps"), QVariantList{QStringLiteral("pm")});
    QVERIFY(!m_presets->savePreset(noProducer));

    // 非法：未知角色 / 非法 key
    QVERIFY(m_presets->normalizePreset(
                QVariantMap{{QStringLiteral("key"), QStringLiteral("bad3")},
                            {QStringLiteral("steps"), QVariantList{QStringLiteral("wizard")}}})
                .isEmpty());
    QVERIFY(m_presets->normalizePreset(
                QVariantMap{{QStringLiteral("key"), QStringLiteral("Bad Key!")},
                            {QStringLiteral("steps"), QVariantList{QStringLiteral("engineer")}}})
                .isEmpty());

    // 合法：自定义预设落 meta，覆盖后可见（skippable 必须在 steps 内）
    QVariantMap custom;
    custom.insert(QStringLiteral("key"), QStringLiteral("myflow"));
    custom.insert(QStringLiteral("label"), QStringLiteral("自定义流程"));
    custom.insert(QStringLiteral("steps"),
                  QVariantList{QStringLiteral("pm"), QStringLiteral("engineer"),
                               QStringLiteral("qa")});
    custom.insert(QStringLiteral("skippable"), QVariantList{QStringLiteral("pm")});
    QVERIFY(m_presets->savePreset(custom));
    QVERIFY(!m_presets->presetOf(QStringLiteral("myflow")).isEmpty());
    // meta 里确实写了（不往 task_exec 加列、不升 user_version）
    QSqlQuery meta(QSqlDatabase::database(m_db->connectionName()));
    meta.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
    meta.addBindValue(QStringLiteral("exec.presets"));
    QVERIFY(meta.exec());
    QVERIFY(meta.next());
    QVERIFY(meta.value(0).toString().contains(QStringLiteral("myflow")));

    QSqlQuery version(QSqlDatabase::database(m_db->connectionName()));
    QVERIFY(version.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(version.next());
    QCOMPARE(version.value(0).toInt(), Database::kSchemaVersion);

    // reset 复位为内置六条
    QVERIFY(m_presets->resetPresets());
    QVERIFY(m_presets->presetOf(QStringLiteral("myflow")).isEmpty());
    QCOMPARE(m_presets->presets().size(), 6);

    // removePreset：删掉一条自定义后仍保留内置；删空退回内置
    QVERIFY(m_presets->savePreset(custom));
    QVERIFY(m_presets->removePreset(QStringLiteral("myflow")));
    QVERIFY(m_presets->presetOf(QStringLiteral("myflow")).isEmpty());
    // 覆盖掉一条内置再删，预设表不为空 → 不会退回内置
    QVariantMap only;
    only.insert(QStringLiteral("key"), QStringLiteral("fix"));
    only.insert(QStringLiteral("label"), QStringLiteral("改过的 fix"));
    only.insert(QStringLiteral("steps"),
                QVariantList{QStringLiteral("engineer"), QStringLiteral("qa")});
    QVERIFY(m_presets->savePreset(only));
    QVERIFY(m_presets->removePreset(QStringLiteral("fix")));
    QVERIFY(m_presets->presetOf(QStringLiteral("fix")).isEmpty());
    QVERIFY(m_presets->resetPresets());
}

void TestWorkflowPresets::routingIsEditable()
{
    // 默认映射
    QCOMPARE(m_presets->routing().value(QStringLiteral("S")).toString(), QStringLiteral("fix"));
    // 改成 S → docs
    QVERIFY(m_presets->saveRouting(QVariantMap{{QStringLiteral("S"), QStringLiteral("docs")}}));
    QCOMPARE(m_presets->routeFor(QStringLiteral("S")), QStringLiteral("docs"));
    // 缺项用内置补齐：M 仍是 feature
    QCOMPARE(m_presets->routeFor(QStringLiteral("M")), QStringLiteral("feature"));
    // 指向未知预设 → 拒绝
    QVERIFY(!m_presets->saveRouting(QVariantMap{{QStringLiteral("S"), QStringLiteral("nope")}}));
    // 空映射 → 拒绝
    QVERIFY(!m_presets->saveRouting(QVariantMap()));
}

void TestWorkflowPresets::summaryIncludesRolesAndAdvice()
{
    const QVariantList passed{
        QVariantMap{{QStringLiteral("role"), QStringLiteral("engineer")},
                    {QStringLiteral("status"), QStringLiteral("passed")},
                    {QStringLiteral("costMs"), 12000}},
        QVariantMap{{QStringLiteral("role"), QStringLiteral("qa")},
                    {QStringLiteral("status"), QStringLiteral("passed")}},
    };
    const QString ok = WorkflowPresets::buildSummary(QStringLiteral("card-9"),
                                                     QStringLiteral("fix"), passed,
                                                     QStringLiteral("修个 bug"));
    QVERIFY(ok.contains(QStringLiteral("card-9")));
    QVERIFY(ok.contains(QStringLiteral("fix")));
    QVERIFY(ok.contains(QStringLiteral("engineer")));
    QVERIFY(ok.contains(QStringLiteral("qa")));
    QVERIFY(ok.contains(QStringLiteral("建议下一步")));
    QVERIFY(ok.contains(QStringLiteral("可交付")));
    QVERIFY(ok.contains(QStringLiteral("12s")));

    const QVariantList failed{
        QVariantMap{{QStringLiteral("role"), QStringLiteral("engineer")},
                    {QStringLiteral("status"), QStringLiteral("passed")}},
        QVariantMap{{QStringLiteral("role"), QStringLiteral("qa")},
                    {QStringLiteral("status"), QStringLiteral("blocked")}},
    };
    const QString bad = WorkflowPresets::buildSummary(QStringLiteral("card-9"),
                                                      QStringLiteral("fix"), failed,
                                                      QStringLiteral("修个 bug"));
    QVERIFY(bad.contains(QStringLiteral("fail-fast")));
    QVERIFY(bad.contains(QStringLiteral("建议下一步")));
}

void TestWorkflowPresets::workflowRunnerSelectsAndExpands()
{
    // cmd.exe profile：promptMode=stdin 让角色链拼的长 prompt 不进 argv
    const QVariantMap profile{
        {QStringLiteral("id"), QStringLiteral("cmd-preset")},
        {QStringLiteral("label"), QStringLiteral("测试用 cmd")},
        {QStringLiteral("exe"), kCmd},
        {QStringLiteral("args"), QStringList{QStringLiteral("/c"), QStringLiteral("echo"),
                                             QStringLiteral("MARKER")}},
        {QStringLiteral("readonlyArgs"), QStringList()},
        {QStringLiteral("promptMode"), QStringLiteral("stdin")},
        {QStringLiteral("output"), QStringLiteral("text")},
        {QStringLiteral("defaultTimeoutMin"), 2},
    };
    QVERIFY(m_profiles->saveProfile(profile));
    // 角色默认 agent：engineer / qa 都指向该 profile（验证角色映射生效）
    QVERIFY(m_profiles->setRoleAgent(QStringLiteral("engineer"), QStringLiteral("cmd-preset")));
    QVERIFY(m_profiles->setRoleAgent(QStringLiteral("qa"), QStringLiteral("cmd-preset")));
    QCOMPARE(m_profiles->agentForRole(QStringLiteral("engineer")), QStringLiteral("cmd-preset"));

    // 卡片不选 workflow、无步骤覆盖 → orchestrator 选型 + 预设展开
    const QString id = addCard(QStringLiteral("预设选型"));
    QVERIFY(!id.isEmpty());
    QVERIFY(m_exec->setExec(id, {{QStringLiteral("agent"), QStringLiteral("cmd-preset")},
                                 {QStringLiteral("cwd"), m_dir->path()}}));

    // 接上预设门面（生产路径走 setPresets；单测里显式注入）
    m_workflow->setPresets(m_presets);

    QSignalSpy started(m_workflow, &WorkflowRunner::stepStarted);
    QSignalSpy chainDone(m_workflow, &WorkflowRunner::chainFinished);

    const QVariantMap began = m_workflow->start(id);
    QVERIFY2(began.value(QStringLiteral("ok")).toBool(),
             qPrintable(began.value(QStringLiteral("message")).toString()));

    QTRY_VERIFY_WITH_TIMEOUT(chainDone.count() >= 1, 60000);

    // 选型兜底 = fix → 两步 engineer → qa（证明预设展开生效，而不是卡片覆盖）
    QCOMPARE(started.count(), 2);
    QCOMPARE(started.at(0).at(2).toMap().value(QStringLiteral("role")).toString(),
             QStringLiteral("engineer"));
    QCOMPARE(started.at(1).at(2).toMap().value(QStringLiteral("role")).toString(),
             QStringLiteral("qa"));
    const QVariantMap snapshot = m_workflow->chain();
    QVERIFY(snapshot.value(QStringLiteral("done")).toBool());
    QVERIFY(!snapshot.value(QStringLiteral("failed")).toBool());
    QCOMPARE(snapshot.value(QStringLiteral("steps")).toList().size(), 2);
}

QTEST_MAIN(TestWorkflowPresets)
#include "tst_workflowpresets.moc"
