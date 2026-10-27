// R6-07：Triage 只读分析 —— JSON 抠取 / glob 合法性 / 草案校验清洗 /
// prompt 模板 / 草案存 meta（board.db）往返 / 只读参数强制。
//
// 真实 agent 进程不进单测（离线可跑、无时序抖动）：start() 的前置拒绝
// （总开关关 / 无 agent / cwd 不受信 / profile 无只读参数）都是纯本地判定，可测。

#include "AgentProfile.h"
#include "Database.h"
#include "TaskExec.h"
#include "TaskModel.h"
#include "TriageRunner.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QVariantMap fullDraft(double confidence = 0.8)
{
    return {
        {QStringLiteral("understanding"), QStringLiteral("一句话复述")},
        {QStringLiteral("affected"),
         QVariantMap{{QStringLiteral("files"), QVariantList{QStringLiteral("src/a.cpp")}},
                     {QStringLiteral("modules"), QVariantList{QStringLiteral("模型层")}},
                     {QStringLiteral("risk"), QStringLiteral("低 —— 改动面小")}}},
        {QStringLiteral("scope_draft"),
         QVariantList{QStringLiteral("src/a.cpp"), QStringLiteral("tests/**")}},
        {QStringLiteral("acceptance_draft"),
         QVariantList{QStringLiteral("pwsh -File scripts/verify.ps1")}},
        {QStringLiteral("questions"), QVariantList{QStringLiteral("要不要兼容旧库？")}},
        {QStringLiteral("split"), QVariantList{}},
        {QStringLiteral("estimate"),
         QVariantMap{{QStringLiteral("workflow"), QStringLiteral("fix")},
                     {QStringLiteral("rounds"), 1},
                     {QStringLiteral("files"), 2},
                     {QStringLiteral("size"), QStringLiteral("S")}}},
        {QStringLiteral("confidence"), confidence},
    };
}

} // namespace

class TestTriage : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void extractsJsonFromNoisyOutput();
    void globValidity();
    void validateCompleteDraft();
    void validateTruncatesAndDefaults();
    void validateMissingFields();
    void confidenceBlocksExecution();
    void promptTemplate();
    void draftPersistedInMeta();
    void startRejectsWhenNotConfigured();

private:
    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_tasks = nullptr;
    AgentProfile *m_profiles = nullptr;
    TaskExec *m_exec = nullptr;
    TriageRunner *m_triage = nullptr;
};

void TestTriage::init()
{
    m_db = new Database(this);
    QString error;
    QVERIFY2(m_db->open(&error, m_dir.filePath(QStringLiteral("board.db"))), qPrintable(error));
    m_tasks = new TaskModel(m_db, this);
    m_profiles = new AgentProfile(m_db, this);
    m_exec = new TaskExec(m_db, m_tasks, this);
    m_triage = new TriageRunner(m_db, m_profiles, m_exec, m_tasks, this);
}

void TestTriage::cleanup()
{
    delete m_triage;
    delete m_exec;
    delete m_profiles;
    delete m_tasks;
    delete m_db;
    m_triage = nullptr;
    m_exec = nullptr;
    m_profiles = nullptr;
    m_tasks = nullptr;
    m_db = nullptr;
}

void TestTriage::extractsJsonFromNoisyOutput()
{
    // 代码围栏 + 解释性文字
    const QJsonDocument doc = TriageRunner::extractJson(
        QStringLiteral("好的，分析如下：\n```json\n{\"understanding\":\"修按钮\","
                       "\"confidence\":0.7}\n```\n以上。"));
    QVERIFY(!doc.isNull());
    QCOMPARE(doc.object().value(QStringLiteral("understanding")).toString(),
             QStringLiteral("修按钮"));

    // JSON 字符串里的花括号与转义引号不干扰配平
    const QJsonDocument tricky =
        TriageRunner::extractJson(QStringLiteral("x {\"a\":\"值含 } 和 \\\" 引号\"} y"));
    QVERIFY(!tricky.isNull());
    QVERIFY(tricky.object().contains(QStringLiteral("a")));

    // 前面有个坏块，后面有合法块：应取到合法的
    const QJsonDocument second =
        TriageRunner::extractJson(QStringLiteral("{bad json} 然后 {\"ok\":true}"));
    QVERIFY(!second.isNull());
    QVERIFY(second.object().value(QStringLiteral("ok")).toBool());

    QVERIFY(TriageRunner::extractJson(QStringLiteral("没有任何 JSON")).isNull());
}

void TestTriage::globValidity()
{
    QVERIFY(TriageRunner::isValidGlob(QStringLiteral("src/**")));
    QVERIFY(TriageRunner::isValidGlob(QStringLiteral("tests/tst_?.cpp")));
    QVERIFY(TriageRunner::isValidGlob(QStringLiteral("docs/adr/0001.md")));

    QVERIFY(!TriageRunner::isValidGlob(QString()));
    QVERIFY(!TriageRunner::isValidGlob(QStringLiteral("   ")));
    QVERIFY(!TriageRunner::isValidGlob(QStringLiteral("C:/abs/path")));
    QVERIFY(!TriageRunner::isValidGlob(QStringLiteral("/etc/passwd")));
    QVERIFY(!TriageRunner::isValidGlob(QStringLiteral("src\\a.cpp")));
    QVERIFY(!TriageRunner::isValidGlob(QStringLiteral("../evil/**")));
    QVERIFY(!TriageRunner::isValidGlob(QStringLiteral("src/../../evil")));
}

void TestTriage::validateCompleteDraft()
{
    const QVariantMap result = TriageRunner::validateDraft(fullDraft());
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), "合法草案被拒");
    const QVariantMap draft = result.value(QStringLiteral("draft")).toMap();
    QCOMPARE(draft.value(QStringLiteral("understanding")).toString(), QStringLiteral("一句话复述"));
    QCOMPARE(draft.value(QStringLiteral("scope_draft")).toList().size(), 2);
    QCOMPARE(draft.value(QStringLiteral("questions")).toList().size(), 1);
    QVERIFY(draft.value(QStringLiteral("executable")).toBool());
    QVERIFY(result.value(QStringLiteral("problems")).toList().isEmpty());
}

void TestTriage::validateTruncatesAndDefaults()
{
    QVariantMap raw = fullDraft();
    // questions 5 条 → 截断到 3
    raw.insert(QStringLiteral("questions"),
               QVariantList{QStringLiteral("q1"), QStringLiteral("q2"), QStringLiteral("q3"),
                            QStringLiteral("q4"), QStringLiteral("q5")});
    // scope 混入非法 glob → 丢弃
    raw.insert(QStringLiteral("scope_draft"),
               QVariantList{QStringLiteral("src/**"), QStringLiteral("bad\\path"),
                            QStringLiteral("C:/x"), QStringLiteral("")});
    // acceptance 空 → 补默认
    raw.insert(QStringLiteral("acceptance_draft"), QVariantList{});

    const QVariantMap result = TriageRunner::validateDraft(raw);
    QVERIFY(result.value(QStringLiteral("ok")).toBool());
    const QVariantMap draft = result.value(QStringLiteral("draft")).toMap();

    QCOMPARE(draft.value(QStringLiteral("questions")).toList().size(), 3);
    QCOMPARE(draft.value(QStringLiteral("scope_draft")).toList().size(), 1);
    const QVariantList acceptance = draft.value(QStringLiteral("acceptance_draft")).toList();
    QCOMPARE(acceptance.size(), 1);
    QCOMPARE(acceptance.at(0).toString(), QStringLiteral("pwsh -File scripts/verify.ps1"));

    const QStringList problems = result.value(QStringLiteral("problems")).toStringList();
    QVERIFY(!problems.isEmpty());
    QVERIFY(problems.join(QLatin1Char(';')).contains(QStringLiteral("截断")));
    QVERIFY(problems.join(QLatin1Char(';')).contains(QStringLiteral("非法 glob")));
    QVERIFY(problems.join(QLatin1Char(';')).contains(QStringLiteral("verify.ps1")));
}

void TestTriage::validateMissingFields()
{
    QVariantMap raw = fullDraft();
    raw.remove(QStringLiteral("affected"));
    raw.remove(QStringLiteral("confidence"));

    const QVariantMap result = TriageRunner::validateDraft(raw);
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    const QStringList missing = result.value(QStringLiteral("missing")).toStringList();
    QCOMPARE(missing.size(), 2);
    QVERIFY(missing.contains(QStringLiteral("affected")));
    QVERIFY(missing.contains(QStringLiteral("confidence")));
}

void TestTriage::confidenceBlocksExecution()
{
    const QVariantMap low = TriageRunner::validateDraft(fullDraft(0.3));
    QVERIFY(low.value(QStringLiteral("ok")).toBool());
    const QVariantMap draft = low.value(QStringLiteral("draft")).toMap();
    QVERIFY(!draft.value(QStringLiteral("executable")).toBool());
    QVERIFY(low.value(QStringLiteral("problems"))
                .toStringList()
                .join(QLatin1Char(';'))
                .contains(QStringLiteral("0.5")));

    // 恰好 0.5 → 可执行（>=）
    const QVariantMap half = TriageRunner::validateDraft(fullDraft(0.5));
    QVERIFY(
        half.value(QStringLiteral("draft")).toMap().value(QStringLiteral("executable")).toBool());
}

void TestTriage::promptTemplate()
{
    const QString base = TriageRunner::buildPrompt(QStringLiteral("修按钮"), QString(), QString());
    QVERIFY(base.contains(QStringLiteral("只读分析器")));
    QVERIFY(base.contains(QStringLiteral("修按钮")));
    QVERIFY(base.contains(QStringLiteral("understanding")));
    QVERIFY(!base.contains(QStringLiteral("复现路径"))); // 无 workflow 不带专项段

    const QString fix =
        TriageRunner::buildPrompt(QStringLiteral("t"), QStringLiteral("n"), QStringLiteral("fix"));
    QVERIFY(fix.contains(QStringLiteral("复现路径")));
    QVERIFY(fix.contains(QStringLiteral("描述：n")));

    const QString feature =
        TriageRunner::buildPrompt(QStringLiteral("t"), QString(), QStringLiteral("feature"));
    QVERIFY(feature.contains(QStringLiteral("schema")));

    const QString review =
        TriageRunner::buildPrompt(QStringLiteral("t"), QString(), QStringLiteral("review"));
    QVERIFY(review.contains(QStringLiteral("severity")));
}

void TestTriage::draftPersistedInMeta()
{
    // 草案只写 meta（board.db）：写 → 读回 → 清除；不建表、不升 user_version
    const QString taskId = QStringLiteral("task-triage");
    QVERIFY(m_triage->draftOf(taskId).isEmpty());

    const QVariantMap validated = TriageRunner::validateDraft(fullDraft());
    QVERIFY(validated.value(QStringLiteral("ok")).toBool());
    QVERIFY(m_db->setMetaValue(QStringLiteral("exec.triage.draft.%1").arg(taskId),
                               QStringLiteral("{\"status\":\"ok\"}")));
    QCOMPARE(m_triage->draftOf(taskId).value(QStringLiteral("status")).toString(),
             QStringLiteral("ok"));
    QVERIFY(m_triage->clearDraft(taskId));
    QVERIFY(m_triage->draftOf(taskId).isEmpty());
    // 幂等：再清一次也成功
    QVERIFY(m_triage->clearDraft(taskId));
}

void TestTriage::startRejectsWhenNotConfigured()
{
    // setExec 要求任务真实存在，先建卡
    const QString taskA =
        m_tasks->addTask(QStringLiteral("分析卡A"), QStringLiteral("todo"), 1, QString(), 0);
    const QString taskB =
        m_tasks->addTask(QStringLiteral("分析卡B"), QStringLiteral("todo"), 1, QString(), 0);
    const QString taskC =
        m_tasks->addTask(QStringLiteral("分析卡C"), QStringLiteral("todo"), 1, QString(), 0);
    const QString taskD =
        m_tasks->addTask(QStringLiteral("分析卡D"), QStringLiteral("todo"), 1, QString(), 0);
    QVERIFY(!taskA.isEmpty() && !taskB.isEmpty() && !taskC.isEmpty() && !taskD.isEmpty());

    // 总开关关 → 拒绝
    m_profiles->setExecutorEnabled(false);
    QVariantMap r = m_triage->start(taskA);
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());

    // 开了总开关但没定义 agent → 拒绝
    m_profiles->setExecutorEnabled(true);
    r = m_triage->start(taskA);
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());

    // 有 agent 但没 cwd → 拒绝
    QVERIFY(m_exec->setExec(taskB, {{QStringLiteral("agent"), QStringLiteral("opencode")}}));
    r = m_triage->start(taskB);
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());

    // 有 cwd 但不在信任白名单 → 拒绝
    QVERIFY(m_exec->setExec(taskC, {{QStringLiteral("agent"), QStringLiteral("opencode")},
                                    {QStringLiteral("cwd"), m_dir.path()}}));
    r = m_triage->start(taskC);
    QVERIFY2(!r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("message")).toString()));
    QVERIFY2(r.value(QStringLiteral("message")).toString().contains(QStringLiteral("信任")),
             qPrintable(r.value(QStringLiteral("message")).toString()));

    // 信任后：用一个「有只读参数但 exe 不存在」的 profile，确保 start 的前置校验全过
    // （走到进程拉起，waitForStarted 失败 → 异步 finish failed），不会真起 agent。
    QVERIFY(m_profiles->trustCwd(m_dir.path()));
    QVERIFY(m_profiles->saveProfile(
        {{QStringLiteral("id"), QStringLiteral("fake-ro")},
         {QStringLiteral("exe"), QStringLiteral("neonboard-no-such-agent")},
         {QStringLiteral("readonlyArgs"), QVariantList{QStringLiteral("--plan")}}}));
    QVERIFY(m_exec->setExec(taskC, {{QStringLiteral("agent"), QStringLiteral("fake-ro")},
                                    {QStringLiteral("cwd"), m_dir.path()}}));
    r = m_triage->start(taskC);
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("message")).toString()));

    // profile 没有只读参数 → 硬拒（自定义一个无 readonlyArgs 的 profile）
    QVERIFY(m_profiles->saveProfile({{QStringLiteral("id"), QStringLiteral("plain")},
                                     {QStringLiteral("exe"), QStringLiteral("plain-agent")}}));
    QVERIFY(m_exec->setExec(taskD, {{QStringLiteral("agent"), QStringLiteral("plain")},
                                    {QStringLiteral("cwd"), m_dir.path()}}));
    r = m_triage->start(taskD);
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());
    QVERIFY(r.value(QStringLiteral("message")).toString().contains(QStringLiteral("只读")));
}

QTEST_GUILESS_MAIN(TestTriage)
#include "tst_triage.moc"
