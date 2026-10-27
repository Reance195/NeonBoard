// R6-05：验收器 AcceptanceRunner —— command 白名单 / shell 语法拒绝 /
// assert file.exists / fail-fast / verdict 聚合。
//
// 命令载体用 cmd.exe 之外的白名单程序跑不动真实脚本，所以：
//   - command 通过用例：往临时目录写一个 .py 脚本，用 python 白名单程序执行；
//   - 若测试机没有 python，用「白名单外程序被拒」这条路径覆盖解析逻辑，
//     执行路径改测 parseItem 的 program/args 拼装（不起真进程）。
//   - 拒绝用例（shell 语法 / 白名单外）不需要真进程，解析期就拦下。

#include "AcceptanceRunner.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class TestAcceptance : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void tokenizesQuotedArguments();
    void rejectsShellSyntax();
    void rejectsNonWhitelistedProgram();
    void allowsScriptPathUnderCwd();
    void assertFileExistsHitAndMiss();
    void assertUnsupportedSyntaxFails();
    void runsCommandsFailFast();
    void verdictAggregatesAllSources();

private:
    QTemporaryDir m_dir;
    AcceptanceRunner m_runner;
    bool m_hasPython = false;
};

void TestAcceptance::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_hasPython = !QStandardPaths::findExecutable(QStringLiteral("python")).isEmpty();
}

void TestAcceptance::tokenizesQuotedArguments()
{
    const QStringList tokens =
        AcceptanceRunner::tokenize(QStringLiteral("pwsh -File \"my scripts/verify.ps1\" -Fast"));
    QCOMPARE(tokens.size(), 4);
    QCOMPARE(tokens.at(0), QStringLiteral("pwsh"));
    QCOMPARE(tokens.at(1), QStringLiteral("-File"));
    QCOMPARE(tokens.at(2), QStringLiteral("my scripts/verify.ps1"));
    QCOMPARE(tokens.at(3), QStringLiteral("-Fast"));
}

void TestAcceptance::rejectsShellSyntax()
{
    for (const QString &bad : {QStringLiteral("pwsh -File a.ps1 | tee log"),
                               QStringLiteral("cmake --build build > out.txt"),
                               QStringLiteral("git status && git push"),
                               QStringLiteral("node x.js; rm -rf y")}) {
        const QVariantMap parsed = m_runner.parseItem(bad, m_dir.path());
        QCOMPARE(parsed.value(QStringLiteral("kind")).toString(), QStringLiteral("command"));
        QVERIFY2(parsed.value(QStringLiteral("rejected")).toBool(), qPrintable(bad));
        QVERIFY(parsed.value(QStringLiteral("reason")).toString().contains(
            QStringLiteral("shell")));
    }
}

void TestAcceptance::rejectsNonWhitelistedProgram()
{
    const QVariantMap parsed =
        m_runner.parseItem(QStringLiteral("rm -rf /"), m_dir.path());
    QVERIFY(parsed.value(QStringLiteral("rejected")).toBool());
    QVERIFY(parsed.value(QStringLiteral("reason")).toString().contains(
        QStringLiteral("白名单")));
}

void TestAcceptance::allowsScriptPathUnderCwd()
{
    // 白名单外的程序名，但指向 cwd 内真实脚本文件 → 放行（脚本路径白名单语义）
    const QString scriptPath = QStringLiteral("tools/mytool.exe");
    QDir(m_dir.path()).mkpath(QStringLiteral("tools"));
    QFile f(m_dir.filePath(scriptPath));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("stub");
    f.close();

    const QVariantMap parsed = m_runner.parseItem(
        QStringLiteral("tools/mytool.exe --check"), m_dir.path());
    QVERIFY2(!parsed.value(QStringLiteral("rejected")).toBool(),
             qPrintable(parsed.value(QStringLiteral("reason")).toString()));
    QCOMPARE(parsed.value(QStringLiteral("program")).toString(),
             QStringLiteral("tools/mytool.exe"));
    QCOMPARE(parsed.value(QStringLiteral("args")).toStringList(),
             QStringList{QStringLiteral("--check")});
}

void TestAcceptance::assertFileExistsHitAndMiss()
{
    QFile f(m_dir.filePath(QStringLiteral("present.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    QVariantMap r =
        m_runner.evalAssert(QStringLiteral("file.exists(\"present.txt\")"), m_dir.path());
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(), qPrintable(r.value(QStringLiteral("reason")).toString()));

    r = m_runner.evalAssert(QStringLiteral("file.exists('absent.txt')"), m_dir.path());
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());
    QVERIFY(r.value(QStringLiteral("reason")).toString().contains(QStringLiteral("不存在")));

    // 不带引号形态
    r = m_runner.evalAssert(QStringLiteral("file.exists(present.txt)"), m_dir.path());
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
}

void TestAcceptance::assertUnsupportedSyntaxFails()
{
    const QVariantMap r = m_runner.evalAssert(
        QStringLiteral("http.get(\"http://127.0.0.1/health\").status == 200"), m_dir.path());
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());
    QVERIFY(r.value(QStringLiteral("reason")).toString().contains(
        QStringLiteral("暂不支持")));

    const QVariantMap unknown = m_runner.evalAssert(QStringLiteral("foo.bar()"), m_dir.path());
    QVERIFY(!unknown.value(QStringLiteral("ok")).toBool());
    QVERIFY(unknown.value(QStringLiteral("reason")).toString().contains(
        QStringLiteral("无法识别")));
}

void TestAcceptance::runsCommandsFailFast()
{
    // assert 混排：第一条过、第二条 file.exists 失败 → 停在第 2 条，第三条不执行
    QFile f(m_dir.filePath(QStringLiteral("ok.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("1");
    f.close();

    const QVariantList acceptance{
        QStringLiteral("assert file.exists(\"ok.txt\")"),
        QStringLiteral("assert file.exists(\"missing.txt\")"),
        QStringLiteral("assert file.exists(\"ok.txt\")"),
    };
    const QVariantMap r = m_runner.run(acceptance, m_dir.path());
    QCOMPARE(r.value(QStringLiteral("total")).toInt(), 3);
    QCOMPARE(r.value(QStringLiteral("passed")).toInt(), 1);
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());
    QCOMPARE(r.value(QStringLiteral("stoppedAt")).toInt(), 1);
    QCOMPARE(r.value(QStringLiteral("results")).toList().size(), 2); // 第三条未执行
    QVERIFY(r.value(QStringLiteral("lastError")).toString().contains(QStringLiteral("第 2 条")));

    // 全过：ok=true
    const QVariantList allOk{
        QStringLiteral("assert file.exists(\"ok.txt\")"),
        QStringLiteral("assert file.exists(ok.txt)"),
    };
    const QVariantMap r2 = m_runner.run(allOk, m_dir.path());
    QVERIFY2(r2.value(QStringLiteral("ok")).toBool(),
             qPrintable(r2.value(QStringLiteral("lastError")).toString()));
    QCOMPARE(r2.value(QStringLiteral("passed")).toInt(), 2);

    // 真实进程执行（python 可用时）：写一个 exit(0) 与 exit(3) 的脚本
    if (m_hasPython) {
        const QString okScript = m_dir.filePath(QStringLiteral("ok.py"));
        QFile sf(okScript);
        QVERIFY(sf.open(QIODevice::WriteOnly));
        sf.write("print('fine')\n");
        sf.close();
        const QVariantList cmdOk{QStringLiteral("python ok.py")};
        const QVariantMap rc = m_runner.run(cmdOk, m_dir.path());
        QVERIFY2(rc.value(QStringLiteral("ok")).toBool(),
                 qPrintable(rc.value(QStringLiteral("lastError")).toString()));

        const QString badScript = m_dir.filePath(QStringLiteral("bad.py"));
        QFile bf(badScript);
        QVERIFY(bf.open(QIODevice::WriteOnly));
        bf.write("import sys; sys.exit(3)\n");
        bf.close();
        const QVariantList cmdBad{QStringLiteral("python bad.py")};
        const QVariantMap rb = m_runner.run(cmdBad, m_dir.path());
        QVERIFY(!rb.value(QStringLiteral("ok")).toBool());
        const QVariantMap entry = rb.value(QStringLiteral("results")).toList().at(0).toMap();
        QCOMPARE(entry.value(QStringLiteral("exitCode")).toInt(), 3);
    } else {
        QWARN("本机无 python，跳过真实进程执行用例（解析路径已覆盖）");
    }
}

void TestAcceptance::verdictAggregatesAllSources()
{
    // 全绿
    QVariantMap accOk{{QStringLiteral("ok"), true}};
    QVariantMap scopeOk{{QStringLiteral("ok"), true}, {QStringLiteral("skipped"), false}};
    QVariantMap v = m_runner.verdict(0, accOk, scopeOk);
    QCOMPARE(v.value(QStringLiteral("status")).toString(), QStringLiteral("passed"));
    QVERIFY(!v.contains(QStringLiteral("lastError")));

    // 退出码非 0
    v = m_runner.verdict(7, accOk, scopeOk);
    QCOMPARE(v.value(QStringLiteral("status")).toString(), QStringLiteral("blocked"));
    QVERIFY(v.value(QStringLiteral("lastError")).toString().contains(QStringLiteral("7")));

    // acceptance 失败
    QVariantMap accBad{{QStringLiteral("ok"), false},
                       {QStringLiteral("lastError"), QStringLiteral("第 2 条验收失败：文件不存在")}};
    v = m_runner.verdict(0, accBad, scopeOk);
    QCOMPARE(v.value(QStringLiteral("status")).toString(), QStringLiteral("blocked"));
    QVERIFY(v.value(QStringLiteral("lastError")).toString().contains(QStringLiteral("第 2 条")));

    // 越界
    QVariantMap scopeBad{{QStringLiteral("ok"), false},
                         {QStringLiteral("skipped"), false},
                         {QStringLiteral("violations"),
                          QStringList{QStringLiteral("docs/x.md")}}};
    v = m_runner.verdict(0, accOk, scopeBad);
    QCOMPARE(v.value(QStringLiteral("status")).toString(), QStringLiteral("blocked"));
    QVERIFY(v.value(QStringLiteral("lastError")).toString().contains(QStringLiteral("docs/x.md")));

    // scope 跳过（未声明 / git 不可用）不算失败
    QVariantMap scopeSkip{{QStringLiteral("ok"), true}, {QStringLiteral("skipped"), true}};
    v = m_runner.verdict(0, accOk, scopeSkip);
    QCOMPARE(v.value(QStringLiteral("status")).toString(), QStringLiteral("passed"));

    // acceptance / scope 都为空 map（该卡无验收项）→ 只看退出码
    v = m_runner.verdict(0, {}, {});
    QCOMPARE(v.value(QStringLiteral("status")).toString(), QStringLiteral("passed"));

    // 多项失败同时汇总
    v = m_runner.verdict(2, accBad, scopeBad);
    QCOMPARE(v.value(QStringLiteral("status")).toString(), QStringLiteral("blocked"));
    const QString err = v.value(QStringLiteral("lastError")).toString();
    QVERIFY(err.contains(QStringLiteral("退出码 2")));
    QVERIFY(err.contains(QStringLiteral("第 2 条")));
    QVERIFY(err.contains(QStringLiteral("docs/x.md")));
}

QTEST_GUILESS_MAIN(TestAcceptance)
#include "tst_acceptance.moc"
