// R6-09：台账卡单向导入（ADR-0001）—— 只补空值 / 冲突只报告 / 未建卡只报告 /
// 幂等（连续导入两次，第二次零变更）/ note 首段作 goal。
//
// 台账 fixture 写进临时目录，不碰真实 docs/board-ledger.json。

#include "Database.h"
#include "LedgerImport.h"
#include "TaskExec.h"
#include "TaskModel.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// 三张卡的迷你台账：X1-01 全字段、X1-02 只有 acceptance、X1-03 无对应任务。
// 用函数拼 JSON（不用原始字符串字面量：内嵌的花括号会让 AutoMoc 的解析器卡住）。
QString ledgerJson()
{
    return QStringLiteral("{") + "\"version\": 1," + "\"cards\": {" + "  \"X1-01\": {"
           + "    \"id\": \"id-0001\"," + "    \"title\": \"第一张卡\","
           + "    \"note\": \"起因：背景很长。目标：把导入做出来。交付（2026-10-28）：细节略。\","
           + "    \"acceptance\": [\"ctest -R tst_x\", \"app --selftest 退出码 0\"],"
           + "    \"scope\": [\"src/X.*\", \"tests/**\"]" + "  }," + "  \"X1-02\": {"
           + "    \"id\": \"id-0002\"," + "    \"title\": \"第二张卡\"," + "    \"note\": \"\","
           + "    \"acceptance\": [\"pwsh -File scripts/verify.ps1\"]," + "    \"scope\": []"
           + "  }," + "  \"X1-03\": {" + "    \"id\": \"id-0003\","
           + "    \"title\": \"没有建卡的台账条目\"," + "    \"note\": \"目标：不该被自动建卡。\","
           + "    \"acceptance\": [\"x\"]," + "    \"scope\": [\"y\"]" + "  }" + "},"
           + "\"baseline\": {}" + "}";
}

bool writeLedger(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(ledgerJson().toUtf8());
    f.close();
    return true;
}

} // namespace

class TestLedgerImport : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void parsesLedgerFile();
    void goalFromNoteTakesTargetSentence();
    void importsFillsEmptyOnly();
    void secondImportIsIdempotent();
    void conflictReportedNotOverwritten();
    void missingCardReportedNotCreated();
    void importSingleCardByCode();

private:
    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_tasks = nullptr;
    TaskExec *m_exec = nullptr;
    LedgerImport *m_import = nullptr;
    QString m_ledgerPath;
    QString m_task1; // 标题内嵌 #X1-01
    QString m_task2; // id 精确匹配 id-0002
};

void TestLedgerImport::init()
{
    QVERIFY(m_dir.isValid());
    m_ledgerPath = m_dir.filePath(QStringLiteral("board-ledger.json"));
    QVERIFY(writeLedger(m_ledgerPath));

    m_db = new Database(this);
    QString error;
    QVERIFY2(m_db->open(&error, m_dir.filePath(QStringLiteral("board.db"))), qPrintable(error));
    m_tasks = new TaskModel(m_db, this);
    m_exec = new TaskExec(m_db, m_tasks, this);
    m_import = new LedgerImport(m_db, m_tasks, m_exec, this);

    // 任务一：标题内嵌短号（RoadmapSync 导入后的常见形态）。
    // 用 upsertTask：addTask 的快速录入解析会把 #X1-01 拆成标签，标题就丢了短号。
    m_task1 = m_tasks->upsertTask(QString(),
                                  {{QStringLiteral("title"), QStringLiteral("第一张卡 #X1-01")},
                                   {QStringLiteral("status"), QStringLiteral("todo")}});
    // 任务二：id 与台账一致（无内嵌短号，走 id 精确匹配）
    m_task2 = m_tasks->upsertTask(QStringLiteral("id-0002"),
                                  {{QStringLiteral("title"), QStringLiteral("第二张卡")},
                                   {QStringLiteral("status"), QStringLiteral("todo")}});
    QVERIFY(!m_task1.isEmpty() && !m_task2.isEmpty());
}

void TestLedgerImport::cleanup()
{
    delete m_import;
    delete m_exec;
    delete m_tasks;
    delete m_db;
    m_import = nullptr;
    m_exec = nullptr;
    m_tasks = nullptr;
    m_db = nullptr;
}

void TestLedgerImport::parsesLedgerFile()
{
    const QVariantMap parsed = LedgerImport::parseLedgerFile(m_ledgerPath);
    QVERIFY(parsed.value(QStringLiteral("ok")).toBool());
    const QVariantList cards = parsed.value(QStringLiteral("cards")).toList();
    QCOMPARE(cards.size(), 3);

    // 坏路径 / 坏 JSON
    QVERIFY(!LedgerImport::parseLedgerFile(m_dir.filePath(QStringLiteral("nope.json")))
                 .value(QStringLiteral("ok"))
                 .toBool());
    const QString badPath = m_dir.filePath(QStringLiteral("bad.json"));
    QFile f(badPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{ not json");
    f.close();
    const QVariantMap bad = LedgerImport::parseLedgerFile(badPath);
    QVERIFY(!bad.value(QStringLiteral("ok")).toBool());
    QVERIFY(bad.value(QStringLiteral("error")).toString().contains(QStringLiteral("解析失败")));
}

void TestLedgerImport::goalFromNoteTakesTargetSentence()
{
    // 「目标：」段优先
    QCOMPARE(LedgerImport::goalFromNote(QStringLiteral(
                 "起因：背景很长。目标：把导入做出来。交付（2026-10-28）：细节略。")),
             QStringLiteral("把导入做出来。"));
    // 无「目标：」取首段（句号切）
    QCOMPARE(LedgerImport::goalFromNote(QStringLiteral("第一段说明。第二段不取。")),
             QStringLiteral("第一段说明。"));
    // 空 note
    QVERIFY(LedgerImport::goalFromNote(QString()).isEmpty());
    // 超长截断 300
    const QString longNote = QStringLiteral("目标：") + QString(400, QLatin1Char('长'));
    QVERIFY(LedgerImport::goalFromNote(longNote).size() <= 300);
}

void TestLedgerImport::importsFillsEmptyOnly()
{
    const QVariantMap r = m_import->importAll(m_ledgerPath);
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    const QStringList imported = r.value(QStringLiteral("imported")).toStringList();
    QVERIFY(imported.contains(QStringLiteral("X1-01")));
    QVERIFY(imported.contains(QStringLiteral("X1-02")));
    // X1-03 未建卡 → 只报告
    QVERIFY(r.value(QStringLiteral("missing")).toStringList().contains(QStringLiteral("X1-03")));

    // 任务一：goal（note 目标段）+ acceptance + scope 三项补齐
    const QVariantMap exec1 = m_exec->execOf(m_task1);
    QCOMPARE(exec1.value(QStringLiteral("goal")).toString(), QStringLiteral("把导入做出来。"));
    QCOMPARE(exec1.value(QStringLiteral("acceptance")).toList().size(), 2);
    QCOMPARE(exec1.value(QStringLiteral("scope")).toList().size(), 2);

    // 任务二：note 为空 → goal 不补；acceptance 补 1 条；scope 台账为空 → 不补
    const QVariantMap exec2 = m_exec->execOf(m_task2);
    QVERIFY(exec2.value(QStringLiteral("goal")).toString().isEmpty());
    QCOMPARE(exec2.value(QStringLiteral("acceptance")).toList().size(), 1);
    QVERIFY(exec2.value(QStringLiteral("scope")).toList().isEmpty());

    // 未建卡不自动建：任务总数不变
    QCOMPARE(m_tasks->count(), 2);
}

void TestLedgerImport::secondImportIsIdempotent()
{
    const QVariantMap first = m_import->importAll(m_ledgerPath);
    QVERIFY(first.value(QStringLiteral("ok")).toBool());
    QVERIFY(!first.value(QStringLiteral("imported")).toList().isEmpty());

    const QVariantMap second = m_import->importAll(m_ledgerPath);
    QVERIFY(second.value(QStringLiteral("ok")).toBool());
    QVERIFY(second.value(QStringLiteral("imported")).toList().isEmpty()); // 零变更
    QCOMPARE(second.value(QStringLiteral("unchanged")).toList().size(), 2);
}

void TestLedgerImport::conflictReportedNotOverwritten()
{
    // 看板已有不同的 acceptance/scope/goal → 冲突只报告，原值保留
    QVERIFY(m_exec->setExec(
        m_task1, {{QStringLiteral("goal"), QStringLiteral("看板自己的目标")},
                  {QStringLiteral("acceptance"), QVariantList{QStringLiteral("看板自己的验收")}},
                  {QStringLiteral("scope"), QVariantList{QStringLiteral("qml/**")}}}));

    const QVariantMap r = m_import->importAll(m_ledgerPath);
    QVERIFY(r.value(QStringLiteral("ok")).toBool());

    bool sawX101Conflict = false;
    const QVariantList conflicts = r.value(QStringLiteral("conflicts")).toList();
    for (const QVariant &v : conflicts) {
        const QVariantMap entry = v.toMap();
        if (entry.value(QStringLiteral("code")).toString() == QLatin1String("X1-01"))
            sawX101Conflict = entry.value(QStringLiteral("conflicts")).toList().size() == 3;
    }
    QVERIFY2(sawX101Conflict, "X1-01 的三项冲突没有全部报出");

    // 原值不被覆盖
    const QVariantMap exec1 = m_exec->execOf(m_task1);
    QCOMPARE(exec1.value(QStringLiteral("goal")).toString(), QStringLiteral("看板自己的目标"));
    QCOMPARE(exec1.value(QStringLiteral("acceptance")).toList().at(0).toString(),
             QStringLiteral("看板自己的验收"));
    QCOMPARE(exec1.value(QStringLiteral("scope")).toList().at(0).toString(),
             QStringLiteral("qml/**"));
}

void TestLedgerImport::missingCardReportedNotCreated()
{
    // 台账 X1-03 没有对应任务：只出现在 missing，不建卡
    const QVariantMap r = m_import->importCard(QStringLiteral("X1-03"), m_ledgerPath);
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    QCOMPARE(r.value(QStringLiteral("result")).toString(), QStringLiteral("missing"));
    QCOMPARE(m_tasks->count(), 2);
}

void TestLedgerImport::importSingleCardByCode()
{
    // 单卡导入：短号大小写不敏感
    QVariantMap r = m_import->importCard(QStringLiteral("x1-01"), m_ledgerPath);
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    QCOMPARE(r.value(QStringLiteral("result")).toString(), QStringLiteral("imported"));
    QCOMPARE(r.value(QStringLiteral("filled")).toInt(), 3);

    // 再来一次 → unchanged
    r = m_import->importCard(QStringLiteral("X1-01"), m_ledgerPath);
    QCOMPARE(r.value(QStringLiteral("result")).toString(), QStringLiteral("unchanged"));

    // 台账里不存在的短号 → ok=false
    r = m_import->importCard(QStringLiteral("ZZ-99"), m_ledgerPath);
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());
}

QTEST_GUILESS_MAIN(TestLedgerImport)
#include "tst_ledgerimport.moc"
