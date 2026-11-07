// DX-135：BoardSync 的领用刷新在 git 拉不起来时必须复位 m_refreshing，
// 否则每 5 秒的轮询全部被开头 guard 吞掉 —— 领用标牌与自动流转静默停摆。
//
// 锚点用 anchorRootOverride 指向临时目录（手工种一份带 claims 的台账）；
// git 拉不起来用「把 PATH 指向空目录」实现，确定性触发 FailedToStart。

#include "BoardSync.h"

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

QTEST_GUILESS_MAIN(TestBoardSync)

#include "tst_boardsync.moc"
