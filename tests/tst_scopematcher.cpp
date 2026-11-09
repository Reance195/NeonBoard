// R6-05：越界检查 ScopeMatcher —— 临时 git 仓库 fixture，不碰真实工作区。
//
// 覆盖：
//   1. glob 匹配纯函数（** 跨层、* 不跨层、? 单字符、反斜杠归一）
//   2. porcelain 行解析（状态前缀 / 重命名取新路径 / 引号剥离）
//   3. 基线捕获 → 改文件 → changedPaths 只报差异
//   4. check：命中 scope 通过 / 越界 blocked 且列出路径 / scope 为空跳过 /
//      非 git 目录跳过
//   5. 基线里已有的脏文件不算本次 run 的改动

#include "ScopeMatcher.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {

bool runGit(const QString &cwd, const QStringList &args)
{
    QProcess proc;
    proc.setProgram(QStringLiteral("git"));
    proc.setArguments(args);
    proc.setWorkingDirectory(cwd);
    proc.start();
    if (!proc.waitForStarted(5000))
        return false;
    return proc.waitForFinished(30000) && proc.exitCode() == 0;
}

bool writeFile(const QString &path, const QByteArray &content = "x")
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(content);
    f.close();
    return true;
}

} // namespace

class TestScopeMatcher : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void globMatching();
    void porcelainPathParsing();
    void baselineAndChangedPaths();
    void checkHitAndViolation();
    void emptyScopeSkips();
    void nonGitDirSkips();
    void nonGitDirFailsClosed();
    void baselineCaptureFailsClosed();
    void porcelainOctalUnescaped();
    void vanishedBaselineEntriesCounted();
    void preExistingDirtNotCounted();

private:
    QTemporaryDir m_dir;
    ScopeMatcher m_matcher;
    bool m_hasGit = false;
    QString m_repo; // 临时 git 仓库路径
};

void TestScopeMatcher::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_hasGit = !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();
    if (!m_hasGit) {
        QWARN("本机无 git，涉及真实仓库的用例跳过");
        return;
    }
    // 建临时仓库：一次 init + 首个提交，作为所有仓库用例的干净基线
    m_repo = m_dir.filePath(QStringLiteral("repo"));
    QVERIFY(QDir().mkpath(m_repo));
    QVERIFY(runGit(m_repo, {QStringLiteral("init"), QStringLiteral("-b"), QStringLiteral("main")}));
    // 测试环境可能没配 user.*，用 -c 传参而不是改全局配置
    QVERIFY(writeFile(m_repo + QStringLiteral("/README.md"), "init"));
    QVERIFY(runGit(m_repo, {QStringLiteral("add"), QStringLiteral(".")}));
    QVERIFY(runGit(m_repo, {QStringLiteral("-c"), QStringLiteral("user.name=t"),
                            QStringLiteral("-c"), QStringLiteral("user.email=t@t"),
                            QStringLiteral("commit"), QStringLiteral("-m"),
                            QStringLiteral("init")}));
}

void TestScopeMatcher::globMatching()
{
    const QStringList globs{QStringLiteral("src/AcceptanceRunner.*"),
                            QStringLiteral("docs/**"),
                            QStringLiteral("tests/tst_?.cpp")};

    QVERIFY(ScopeMatcher::matches(QStringLiteral("src/AcceptanceRunner.cpp"), globs));
    QVERIFY(ScopeMatcher::matches(QStringLiteral("src/AcceptanceRunner.h"), globs));
    QVERIFY(!ScopeMatcher::matches(QStringLiteral("src/AcceptanceRunnerX.cpp"), globs));
    // * 不跨层级
    QVERIFY(!ScopeMatcher::matches(QStringLiteral("src/sub/AcceptanceRunner.cpp"), globs));
    // ** 跨层级（含零层）
    QVERIFY(ScopeMatcher::matches(QStringLiteral("docs/x.md"), globs));
    QVERIFY(ScopeMatcher::matches(QStringLiteral("docs/adr/0001.md"), globs));
    // ? 单字符
    QVERIFY(ScopeMatcher::matches(QStringLiteral("tests/tst_a.cpp"), globs));
    QVERIFY(!ScopeMatcher::matches(QStringLiteral("tests/tst_ab.cpp"), globs));
    // 反斜杠与 ./ 前缀归一
    QVERIFY(ScopeMatcher::matches(QStringLiteral("src\\AcceptanceRunner.h"), globs));
    QVERIFY(ScopeMatcher::matches(QStringLiteral("./src/AcceptanceRunner.h"), globs));
    // 空 globs 不命中
    QVERIFY(!ScopeMatcher::matches(QStringLiteral("src/a.cpp"), QStringList{}));
    QVERIFY(!ScopeMatcher::matches(QString(), globs));
}

void TestScopeMatcher::porcelainPathParsing()
{
    // XY + 空格 + 路径
    QCOMPARE(ScopeMatcher::pathFromPorcelain(QStringLiteral(" M src/a.cpp")),
             QStringLiteral("src/a.cpp"));
    QCOMPARE(ScopeMatcher::pathFromPorcelain(QStringLiteral("?? new.txt")),
             QStringLiteral("new.txt"));
    // 重命名取新路径
    QCOMPARE(ScopeMatcher::pathFromPorcelain(QStringLiteral("R  old.txt -> new.txt")),
             QStringLiteral("new.txt"));
    // 引号包裹（含空格路径）剥引号
    QCOMPARE(ScopeMatcher::pathFromPorcelain(QStringLiteral("?? \"a b.txt\"")),
             QStringLiteral("a b.txt"));
    // 反斜杠归一
    QCOMPARE(ScopeMatcher::pathFromPorcelain(QStringLiteral("?? dir\\x.txt")),
             QStringLiteral("dir/x.txt"));
    // 太短的行
    QVERIFY(ScopeMatcher::pathFromPorcelain(QStringLiteral("M")).isEmpty());
}

void TestScopeMatcher::baselineAndChangedPaths()
{
    if (!m_hasGit)
        QSKIP("无 git");

    const QVariantMap base = m_matcher.captureBaseline(m_repo);
    QVERIFY2(base.value(QStringLiteral("ok")).toBool(),
             qPrintable(base.value(QStringLiteral("error")).toString()));
    QVERIFY(base.value(QStringLiteral("lines")).toList().isEmpty());

    QVERIFY(writeFile(m_repo + QStringLiteral("/src.cpp")));
    const QVariantList changed =
        m_matcher.changedPaths(m_repo, base.value(QStringLiteral("lines")).toList());
    QCOMPARE(changed.size(), 1);
    QCOMPARE(changed.at(0).toString(), QStringLiteral("src.cpp"));

    // 恢复干净
    QVERIFY(QFile::remove(m_repo + QStringLiteral("/src.cpp")));
}

void TestScopeMatcher::checkHitAndViolation()
{
    if (!m_hasGit)
        QSKIP("无 git");

    const QVariantList base = m_matcher.captureBaseline(m_repo).value(QStringLiteral("lines")).toList();

    // 命中：src/a.cpp 落进 src/**
    QDir(m_repo).mkpath(QStringLiteral("src"));
    QVERIFY(writeFile(m_repo + QStringLiteral("/src/a.cpp")));
    QVariantMap r = m_matcher.check(
        m_repo, base, QVariantList{QStringLiteral("src/**")});
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("reason")).toString()));
    QVERIFY(!r.value(QStringLiteral("skipped")).toBool());
    QCOMPARE(r.value(QStringLiteral("changed")).toList().size(), 1);
    QVERIFY(r.value(QStringLiteral("violations")).toList().isEmpty());

    // 越界：docs/x.md 不在 src/** 里
    QDir(m_repo).mkpath(QStringLiteral("docs"));
    QVERIFY(writeFile(m_repo + QStringLiteral("/docs/x.md")));
    r = m_matcher.check(m_repo, base, QVariantList{QStringLiteral("src/**")});
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());
    const QStringList violations = r.value(QStringLiteral("violations")).toStringList();
    QVERIFY(violations.contains(QStringLiteral("docs/x.md")));
    QVERIFY(!violations.contains(QStringLiteral("src/a.cpp")));
    QVERIFY(r.value(QStringLiteral("reason")).toString().contains(QStringLiteral("docs/x.md")));

    QVERIFY(QFile::remove(m_repo + QStringLiteral("/src/a.cpp")));
    QVERIFY(QFile::remove(m_repo + QStringLiteral("/docs/x.md")));
}

void TestScopeMatcher::emptyScopeSkips()
{
    // scope 为空 → 永远通过（存量卡不阻断），且不要求 git
    const QVariantMap r = m_matcher.check(m_dir.path(), QVariantList{}, QVariantList{});
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    QVERIFY(r.value(QStringLiteral("skipped")).toBool());
}

void TestScopeMatcher::nonGitDirSkips()
{
    if (!m_hasGit)
        QSKIP("无 git");
    // 非 git 目录、scope 为空 → 这是「没得查」的跳过，不是「查不了」的放行
    const QString plain = m_dir.filePath(QStringLiteral("plain"));
    QVERIFY(QDir().mkpath(plain));
    const QVariantMap r =
        m_matcher.check(plain, QVariantList{}, QVariantList{});
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    QVERIFY(r.value(QStringLiteral("skipped")).toBool());
}

// DX-138：git 失败必须 fail-closed——「跳过检查」会把 .git 被删 / index.lock 残留
// / git 超时都当放行，安全边界不能 fail-open（旧实现返回 ok:true+skipped）
void TestScopeMatcher::nonGitDirFailsClosed()
{
    if (!m_hasGit)
        QSKIP("无 git");
    const QString plain = m_dir.filePath(QStringLiteral("plain"));
    QVERIFY(QDir().mkpath(plain));
    const QVariantMap r =
        m_matcher.check(plain, QVariantList{}, QVariantList{QStringLiteral("src/**")});
    QVERIFY2(!r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("reason")).toString()));
    QVERIFY(!r.value(QStringLiteral("skipped")).toBool());
    // 失败原因可读且进 violations，verdict 侧的「越界改动：」行直接可见
    QVERIFY(!r.value(QStringLiteral("violations")).toList().isEmpty());
    QVERIFY(r.value(QStringLiteral("reason")).toString().contains(QStringLiteral("git")));
}

// DX-138：captureBaseline 路径同样 fail-closed——git 失败给可观察的失败态
// （ok:false + error），消费方（triage 基线、workflow baseline）拿不到静默空基线
void TestScopeMatcher::baselineCaptureFailsClosed()
{
    if (!m_hasGit)
        QSKIP("无 git");
    const QString plain = m_dir.filePath(QStringLiteral("plain"));
    QVERIFY(QDir().mkpath(plain));
    const QVariantMap r = m_matcher.captureBaseline(plain);
    QVERIFY2(!r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("error")).toString()));
    QVERIFY(!r.value(QStringLiteral("error")).toString().isEmpty());
    QVERIFY(!r.contains(QStringLiteral("lines")));
}

// DX-138：porcelain 对非 ASCII 路径的 C 风格转义必须反转义成真实名字，
// 否则中文文件名的合法改动永远匹配不上 scope glob（整体假阳性）
void TestScopeMatcher::porcelainOctalUnescaped()
{
    // 数据驱动核心断言在 pathFromPorcelain：带引号 + 八进制转义 → 真实中文名。
    // git 默认 core.quotepath 对 "文档.md"（UTF-8 E6 96 87 E6 A1 A3）的输出形态。
    const QString line = QStringLiteral(
        " M \"docs/\\346\\226\\207\\346\\241\\243.md\"");
    QCOMPARE(ScopeMatcher::pathFromPorcelain(line),
             QStringLiteral("docs/文档.md"));

    // 反转义后的真实中文名要能命中中文 scope glob（卡片里写的是真实字符）
    QVERIFY(ScopeMatcher::matches(QStringLiteral("docs/文档.md"),
                                  QStringList{QStringLiteral("docs/文*")}));
    // 转义形态的字面串命中不了中文 glob —— 正是修复前的整体假阳性失效模式
    QVERIFY(!ScopeMatcher::matches(QStringLiteral("docs/\\346\\226\\207\\346\\241\\243.md"),
                                   QStringList{QStringLiteral("docs/文*")}));
}

// DX-138：双向比对——基线有、之后消失（还原用户改动 / 删 untracked）同样是改动
void TestScopeMatcher::vanishedBaselineEntriesCounted()
{
    if (!m_hasGit)
        QSKIP("无 git");
    QDir(m_repo).mkpath(QStringLiteral("src"));
    QVERIFY(writeFile(m_repo + QStringLiteral("/src/dirt.txt")));
    const QVariantList base =
        m_matcher.captureBaseline(m_repo).value(QStringLiteral("lines")).toList();
    QVERIFY(!base.isEmpty()); // 基线里有这条 untracked 脏文件

    // agent 把它删了：状态条目从 porcelain 里消失，但改动真实发生
    QVERIFY(QFile::remove(m_repo + QStringLiteral("/src/dirt.txt")));
    const QVariantMap r = m_matcher.check(
        m_repo, base, QVariantList{QStringLiteral("docs/**")});
    QVERIFY2(!r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("reason")).toString()));
    const QStringList violations = r.value(QStringLiteral("violations")).toStringList();
    QVERIFY2(violations.contains(QStringLiteral("src/dirt.txt")),
             qPrintable(violations.join(QStringLiteral(","))));

    // 对照：scope 覆盖到它就不算违规（方向正确性）
    QVERIFY(writeFile(m_repo + QStringLiteral("/src/dirt.txt")));
    const QVariantList base2 =
        m_matcher.captureBaseline(m_repo).value(QStringLiteral("lines")).toList();
    QVERIFY(QFile::remove(m_repo + QStringLiteral("/src/dirt.txt")));
    const QVariantMap r2 = m_matcher.check(
        m_repo, base2, QVariantList{QStringLiteral("src/**")});
    QVERIFY2(r2.value(QStringLiteral("ok")).toBool(),
             qPrintable(r2.value(QStringLiteral("reason")).toString()));
}

void TestScopeMatcher::preExistingDirtNotCounted()
{
    if (!m_hasGit)
        QSKIP("无 git");

    // 基线捕获前就脏的文件（模拟用户自己的未提交改动）不算 run 的越界
    QVERIFY(writeFile(m_repo + QStringLiteral("/dirty.txt")));
    const QVariantList base = m_matcher.captureBaseline(m_repo).value(QStringLiteral("lines")).toList();
    QVERIFY(!base.isEmpty());

    QVERIFY(writeFile(m_repo + QStringLiteral("/run-made.txt")));
    const QVariantMap r =
        m_matcher.check(m_repo, base, QVariantList{QStringLiteral("run-made.txt")});
    const QStringList changed = r.value(QStringLiteral("changed")).toStringList();
    QVERIFY2(!changed.contains(QStringLiteral("dirty.txt")), "基线脏文件被误算成本次改动");
    QVERIFY(changed.contains(QStringLiteral("run-made.txt")));
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
             qPrintable(r.value(QStringLiteral("reason")).toString()));

    QVERIFY(QFile::remove(m_repo + QStringLiteral("/dirty.txt")));
    QVERIFY(QFile::remove(m_repo + QStringLiteral("/run-made.txt")));
}

QTEST_GUILESS_MAIN(TestScopeMatcher)
#include "tst_scopematcher.moc"
