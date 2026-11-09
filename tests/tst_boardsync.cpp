// DX-135：BoardSync 的领用刷新在 git 拉不起来时必须复位 m_refreshing，
// 否则每 5 秒的轮询全部被开头 guard 吞掉 —— 领用标牌与自动流转静默停摆。
//
// 锚点用 anchorRootOverride 指向临时目录（手工种一份带 claims 的台账）；
// git 拉不起来用「把 PATH 指向空目录」实现，确定性触发 FailedToStart。

#include "BoardSync.h"

#include "Database.h"
#include "TaskModel.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestBoardSync : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void gitStartFailureResetsRefreshing();
    void claimTitleMatchRespectsWordBoundary();

private:
    QTemporaryDir m_dir;
    QByteArray m_savedPath;
};

void TestBoardSync::initTestCase()
{
    QVERIFY(m_dir.isValid());
    // 种一份最小台账：collectClaims 只读 claims 对象（code/branch 必填）
    QVERIFY(QDir(m_dir.path()).mkpath(QStringLiteral("docs")));
    QFile ledger(QDir(m_dir.path()).filePath(QStringLiteral("docs/board-ledger.json")));
    QVERIFY(ledger.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ledger.write("{"
                 "\"claims\":{"
                 "\"T-TEST\":{\"agent\":\"tester\",\"branch\":\"feature/never-merged\","
                 "\"head\":\"0000000\",\"claimedAt\":1}"
                 "}"
                 "}");
    ledger.close();

    m_savedPath = qgetenv("PATH");
}

void TestBoardSync::cleanupTestCase()
{
    qputenv("PATH", m_savedPath);
}

void TestBoardSync::gitStartFailureResetsRefreshing()
{
    // 把 PATH 指向空目录：QProcess 对裸命令 "git" 的解析会失败 → FailedToStart
    qputenv("PATH", QDir::toNativeSeparators(m_dir.path()).toLocal8Bit());

    BoardSync sync(nullptr, nullptr, m_dir.path());
    QSignalSpy changedSpy(&sync, &BoardSync::changed);

    // 第一轮：git 起不来 → 必须仍然收尾（changed 照发），不能楔死
    sync.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(changedSpy.size(), 1, 10000);

    // 第二轮：guard 已复位，能再来一轮（楔死的话这里永远等不到第二次）
    sync.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(changedSpy.size(), 2, 10000);
}

// DX-143：标题回退匹配必须带词边界——"#R1-12 长号卡" 不得被 code R1-1 的领用
// 命中（contains 无边界，前缀短号互撞时标牌挂错卡）；与 taskIdForCode 同口径
void TestBoardSync::claimTitleMatchRespectsWordBoundary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("docs")));
    QFile ledger(QDir(dir.path()).filePath(QStringLiteral("docs/board-ledger.json")));
    QVERIFY(ledger.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ledger.write("{"
                 "\"claims\":{"
                 "\"R1-1\":{\"agent\":\"short\",\"branch\":\"feature/short\","
                 "\"head\":\"0000000\",\"claimedAt\":1},"
                 "\"R1-12\":{\"agent\":\"long\",\"branch\":\"feature/long\","
                 "\"head\":\"0000000\",\"claimedAt\":2}"
                 "}"
                 "}");
    ledger.close();

    Database db;
    QString error;
    QVERIFY2(db.open(&error, QDir(dir.path()).filePath(QStringLiteral("board.db"))),
             qPrintable(error));
    TaskModel model(&db);
    model.load();

    BoardSync sync(nullptr, &model, dir.path());
    sync.refresh();

    // addTask 的快速录入解析会把「#R1-12」当标签拆走，标题里就剩不下 #短号；
    // 先占卡再用 updateTask 原样写标题（编辑路径不走解析，与真实标牌卡一致）
    const QString longCard = model.addTask(QStringLiteral("长短号卡"));
    QVERIFY(!longCard.isEmpty());
    QVERIFY(model.updateTask(longCard,
                             QVariantMap{{QStringLiteral("title"), QStringLiteral("#R1-12 长短号卡")}}));
    const QString shortCard = model.addTask(QStringLiteral("短号卡"));
    QVERIFY(!shortCard.isEmpty());
    QVERIFY(model.updateTask(shortCard,
                             QVariantMap{{QStringLiteral("title"), QStringLiteral("#R1-1 短号卡")}}));
    const QString plainCard = model.addTask(QStringLiteral("无关卡"));
    QVERIFY(!plainCard.isEmpty());

    // 长号标题必须命中长号领用，而不是前缀更短的 R1-1
    QCOMPARE(sync.claimForTask(longCard).value(QStringLiteral("agent")).toString(),
             QStringLiteral("long"));
    QCOMPARE(sync.claimForTask(shortCard).value(QStringLiteral("agent")).toString(),
             QStringLiteral("short"));
    QVERIFY(sync.claimForTask(plainCard).isEmpty());
}

QTEST_GUILESS_MAIN(TestBoardSync)

#include "tst_boardsync.moc"
