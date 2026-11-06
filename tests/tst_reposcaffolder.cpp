// R18-03：无台账仓库脚手架（RepoScaffolder）的 fail-closed 行为。
//
// 覆盖四条：
//   1. 合法 git 仓库：生成 v3 空快照 + 空台账，结构正确
//   2. 既有文件一律不覆盖：两个都在 → 拒绝；只缺一个 → 也不写（不写半套）
//   3. 非法路径（相对 / 不存在 / 非 git 目录）拒绝，且不写入任何文件
//   4. 生成后 hasScaffold 转真（「已接入」判据闭环）
//
// 纯文件操作，全部在 QTemporaryDir 里做，不碰真实仓库。

#include "RepoScaffolder.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// 造一个最小 git 仓库骨架（含 docs/，脚手架只查 .git 与台账文件存在性，不真跑 git）。
// 注意：QVERIFY 展开为 return;，在带返回值的函数里非法，这里用 qFatal。
QString makeGitRepo(const QString &path)
{
    QDir dir;
    if (!dir.mkpath(path) || !dir.mkpath(path + QStringLiteral("/docs")))
        qFatal("mkpath 失败：%s", qPrintable(path));
    dir.mkpath(path + QStringLiteral("/.git"));
    return path;
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.object();
}

} // namespace

class TestRepoScaffolder : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void scaffoldCreatesEmptyBoardFiles();
    void scaffoldNeverOverwritesExisting();
    void scaffoldRefusesIllegalPaths();
    void hasScaffoldFlipsAfterScaffold();

private:
    QString repoPath() const;

    QTemporaryDir m_dir;
    // QtTest 单实例跑完全部用例，QTemporaryDir 跨用例复用：
    // repo 固定路径会让上一个用例生成的 scaffold 残留进来（tst_projectstore 同款坑）
    int m_seq = 0;
};

void TestRepoScaffolder::init()
{
    QVERIFY(m_dir.isValid());
    ++m_seq;
}

QString TestRepoScaffolder::repoPath() const
{
    return m_dir.filePath(QStringLiteral("repo-%1").arg(m_seq));
}

void TestRepoScaffolder::cleanup()
{
}

void TestRepoScaffolder::scaffoldCreatesEmptyBoardFiles()
{
    const QString repo = makeGitRepo(repoPath());
    QVERIFY(RepoScaffolder::validateRepoPath(repo).isEmpty());
    QVERIFY(!RepoScaffolder::hasScaffold(repo));

    const QString error = RepoScaffolder::scaffold(repo);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // v3 空快照：version=3 + items 空数组
    const QJsonObject progress = readJson(repo + QStringLiteral("/progress.json"));
    QCOMPARE(progress.value(QStringLiteral("version")).toInt(), 3);
    QVERIFY(progress.value(QStringLiteral("items")).toArray().isEmpty());

    // 空台账：顶层三键，均为空对象
    const QJsonObject ledger = readJson(repo + QStringLiteral("/docs/board-ledger.json"));
    QVERIFY(ledger.contains(QStringLiteral("baseline")));
    QVERIFY(ledger.contains(QStringLiteral("cards")));
    QVERIFY(ledger.contains(QStringLiteral("claims")));
}

void TestRepoScaffolder::scaffoldNeverOverwritesExisting()
{
    const QString repo = makeGitRepo(repoPath());

    // 两个文件都在 → 拒绝，既有内容一个字节都不动
    const QString originalProgress = QStringLiteral("{\"version\":3,\"items\":[{\"id\":\"keep-1\"}]}");
    const QString originalLedger = QStringLiteral("{\"baseline\":{},\"cards\":{},\"claims\":{}}");
    QFile progressFile(repo + QStringLiteral("/progress.json"));
    QVERIFY(progressFile.open(QIODevice::WriteOnly));
    progressFile.write(originalProgress.toUtf8());
    progressFile.close();
    QFile ledgerFile(repo + QStringLiteral("/docs/board-ledger.json"));
    QVERIFY(ledgerFile.open(QIODevice::WriteOnly));
    ledgerFile.write(originalLedger.toUtf8());
    ledgerFile.close();

    const QString error = RepoScaffolder::scaffold(repo);
    QVERIFY(!error.isEmpty());
    QCOMPARE(readJson(repo + QStringLiteral("/progress.json"))
                 .value(QStringLiteral("items"))
                 .toArray()
                 .first()
                 .toObject()
                 .value(QStringLiteral("id"))
                 .toString(),
             QStringLiteral("keep-1"));
    QFile ledgerCheck(repo + QStringLiteral("/docs/board-ledger.json"));
    QVERIFY(ledgerCheck.open(QIODevice::ReadOnly));
    QCOMPARE(ledgerCheck.readAll(), originalLedger.toUtf8());
    ledgerCheck.close();

    // 只缺一个（progress.json 没了）→ 也不写半套
    QVERIFY(progressFile.remove());
    const QString error2 = RepoScaffolder::scaffold(repo);
    QVERIFY(!error2.isEmpty());
    QVERIFY(!QFile::exists(repo + QStringLiteral("/progress.json")));
}

void TestRepoScaffolder::scaffoldRefusesIllegalPaths()
{
    // 相对路径
    const QString relative = QStringLiteral("some/repo");
    QVERIFY(!RepoScaffolder::validateRepoPath(relative).isEmpty());
    QVERIFY(!RepoScaffolder::scaffold(relative).isEmpty());

    // 不存在的目录
    const QString missing = m_dir.filePath(QStringLiteral("no-such-dir"));
    QVERIFY(!RepoScaffolder::validateRepoPath(missing).isEmpty());
    QVERIFY(!RepoScaffolder::scaffold(missing).isEmpty());
    QVERIFY(!QFileInfo::exists(missing));

    // 非 git 目录
    const QString plain = m_dir.filePath(QStringLiteral("plain"));
    QVERIFY(QDir().mkpath(plain));
    QVERIFY(!RepoScaffolder::validateRepoPath(plain).isEmpty());
    const QString error = RepoScaffolder::scaffold(plain);
    QVERIFY(!error.isEmpty());
    // 拒绝路径零写入
    QVERIFY(!QFile::exists(plain + QStringLiteral("/progress.json")));
    QVERIFY(!QFile::exists(plain + QStringLiteral("/docs/board-ledger.json")));
}

void TestRepoScaffolder::hasScaffoldFlipsAfterScaffold()
{
    const QString repo = makeGitRepo(repoPath());
    QVERIFY(!RepoScaffolder::hasScaffold(repo));
    QVERIFY(RepoScaffolder::scaffold(repo).isEmpty());
    QVERIFY(RepoScaffolder::hasScaffold(repo));
}

QTEST_MAIN(TestRepoScaffolder)
#include "tst_reposcaffolder.moc"
