// R5-01：软件更新的纯逻辑单测。
//
// 只覆盖「不依赖真实网络 / 注册表 / 进程」的部分：版本比较、清单解析、
// 相对地址解析、便携版自更新脚本生成、zip 包根定位。
// 下载与安装涉及网络与进程替换，留给端到端与人工验收，不进自动化测试。

#include <QtTest>

#include "UpdateManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

class TestUpdateManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // 隔离 QSettings：UpdateManager 构造会读更新设置，别碰开发机的真实配置
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir::temp().absoluteFilePath(QStringLiteral("neonboard-test-settings")));
    }

    void compareVersion_data()
    {
        QTest::addColumn<QString>("left");
        QTest::addColumn<QString>("right");
        QTest::addColumn<int>("expected");

        QTest::newRow("小版本落后") << QStringLiteral("0.1.0") << QStringLiteral("0.2.0") << -1;
        QTest::newRow("小版本领先") << QStringLiteral("0.2.0") << QStringLiteral("0.1.0") << 1;
        QTest::newRow("完全相等") << QStringLiteral("0.1.0") << QStringLiteral("0.1.0") << 0;
        QTest::newRow("容忍 v 前缀") << QStringLiteral("v0.2.0") << QStringLiteral("0.2.0") << 0;
        QTest::newRow("预发布更小") << QStringLiteral("0.2.0-beta.1") << QStringLiteral("0.2.0") << -1;
        QTest::newRow("预发布之间") << QStringLiteral("0.2.0-beta.1")
                                    << QStringLiteral("0.2.0-beta.2") << -1;
        QTest::newRow("两位数按数值比") << QStringLiteral("0.10.0") << QStringLiteral("0.9.0") << 1;
        QTest::newRow("主版本优先") << QStringLiteral("1.0.0") << QStringLiteral("0.99.99") << 1;
        QTest::newRow("段数不等补零") << QStringLiteral("0.2") << QStringLiteral("0.2.0") << 0;
        QTest::newRow("空串视为最小") << QString() << QStringLiteral("0.0.1") << -1;
    }

    void compareVersion()
    {
        QFETCH(QString, left);
        QFETCH(QString, right);
        QFETCH(int, expected);
        QCOMPARE(UpdateManager::compareVersion(left, right), expected);
        // 反向比较必须自洽：a<b 等价于 b>a
        QCOMPARE(UpdateManager::compareVersion(right, left), -expected);
    }

    void parseUpdateJson()
    {
        // JSON 样本刻意不用 C++ raw string：moc 的词法分析器不认识 raw string，
        // 里面的 https:// 会被它当成注释起点，整个测试类被"吞掉"——
        // 症状是 tst_*.moc 生成为空、链接时报 metaObject / qt_metacast 未定义，
        // 而编译期毫无提示。普通字符串拼接能被 moc 正确跳过。
        const QByteArray json =
            "{"
            "\"version\": 1,"
            "\"channel\": \"stable\","
            "\"latest\": \"0.3.0\","
            "\"publishedAt\": \"2026-10-26\","
            "\"notes\": \"修了三个问题\","
            "\"htmlUrl\": \"https://example.com/releases/latest\","
            "\"artifacts\": ["
            "  { \"kind\": \"setup\", \"url\": \"NeonBoard-Setup-0.3.0.exe\","
            "    \"sha256\": \"AAbb0011223344556677889900aabbccddeeff00112233445566778899aabbcc\","
            "    \"size\": 1024 },"
            "  { \"kind\": \"portable\", \"url\": \"NeonBoard-0.3.0-portable.zip\","
            "    \"sha256\": \"bbcc0011223344556677889900aabbccddeeff00112233445566778899aabbcc\","
            "    \"size\": 2048 }"
            "]"
            "}";

        QString error;
        const auto info = UpdateManager::parseManifest(json, &error);
        QVERIFY2(info.valid(), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(info.version, QStringLiteral("0.3.0"));
        QCOMPARE(info.notes, QStringLiteral("修了三个问题"));
        QCOMPARE(info.publishedAt, QStringLiteral("2026-10-26"));
        QCOMPARE(info.htmlUrl, QStringLiteral("https://example.com/releases/latest"));
        QCOMPARE(info.setupUrl, QStringLiteral("NeonBoard-Setup-0.3.0.exe"));
        QCOMPARE(info.portableUrl, QStringLiteral("NeonBoard-0.3.0-portable.zip"));
        QCOMPARE(info.setupSize, 1024);
        QCOMPARE(info.portableSize, 2048);
        // 校验值统一小写，便于与 QCryptographicHash 的十六进制输出直接比对
        QCOMPARE(info.setupSha256, info.setupSha256.toLower());
    }

    void parseGitHubRelease()
    {
        // 回退路径：Release API 不给 SHA-256，靠发布脚本写在说明里的两行抓回来。
        // 同样避开 raw string，原因见 parseUpdateJson 里的注释。
        const QByteArray json =
            "{"
            "\"tag_name\": \"v0.4.0\","
            "\"published_at\": \"2026-10-26T10:00:00Z\","
            "\"html_url\": \"https://github.com/Reance195/NeonBoard/releases/tag/v0.4.0\","
            "\"body\": \"更新说明\\n\\n"
            "setup-sha256: "
            "1111111111111111" "1111111111111111" "1111111111111111" "1111111111111111"
            "\\nportable-sha256: "
            "2222222222222222" "2222222222222222" "2222222222222222" "2222222222222222"
            "\","
            "\"assets\": ["
            "  { \"name\": \"NeonBoard-Setup-0.4.0.exe\","
            "    \"browser_download_url\": \"https://github.com/x/NeonBoard-Setup-0.4.0.exe\","
            "    \"size\": 4096 },"
            "  { \"name\": \"NeonBoard-0.4.0-portable.zip\","
            "    \"browser_download_url\": \"https://github.com/x/NeonBoard-0.4.0-portable.zip\","
            "    \"size\": 8192 }"
            "]"
            "}";

        QString error;
        const auto info = UpdateManager::parseManifest(json, &error);
        QVERIFY2(info.valid(), qPrintable(error));
        QCOMPARE(info.version, QStringLiteral("0.4.0")); // 去掉 v 前缀
        QCOMPARE(info.publishedAt, QStringLiteral("2026-10-26")); // 只留日期
        QVERIFY(info.setupUrl.endsWith(QStringLiteral(".exe")));
        QVERIFY(info.portableUrl.endsWith(QStringLiteral(".zip")));
        QCOMPARE(info.setupSha256, QString(64, QLatin1Char('1')));
        QCOMPARE(info.portableSha256, QString(64, QLatin1Char('2')));
    }

    void parseInvalid()
    {
        QString error;
        QVERIFY(!UpdateManager::parseManifest(QByteArray(), &error).valid());
        QVERIFY(!error.isEmpty());

        error.clear();
        QVERIFY(!UpdateManager::parseManifest(QByteArrayLiteral("{not json"), &error).valid());
        QVERIFY(!error.isEmpty());

        error.clear();
        // 有结构但没版本号 —— 不能当成「无更新」悄悄放过
        const QByteArray noVersion =
            "{\"artifacts\": [{\"kind\":\"setup\",\"url\":\"a.exe\"}]}";
        QVERIFY(!UpdateManager::parseManifest(noVersion, &error).valid());
        QVERIFY(!error.isEmpty());
    }

    void resolveRelativeUrl()
    {
        const QString base = QStringLiteral("https://host/neonboard/update.json");
        // 绝对地址原样返回
        QCOMPARE(UpdateManager::resolveUrl(QStringLiteral("https://cdn/x.exe"), base),
                 QStringLiteral("https://cdn/x.exe"));
        QCOMPARE(UpdateManager::resolveUrl(QStringLiteral("file:///d:/x.exe"), base),
                 QStringLiteral("file:///d:/x.exe"));
        // 只写文件名时按清单所在目录解析（与插件市场同一套约定）
        const QString resolved = UpdateManager::resolveUrl(QStringLiteral("NeonBoard-Setup-0.3.0.exe"), base);
        QCOMPARE(resolved, QStringLiteral("https://host/neonboard/NeonBoard-Setup-0.3.0.exe"));
        // 空地址保持为空，不要凭空造出指向清单自身的链接
        QVERIFY(UpdateManager::resolveUrl(QString(), base).isEmpty());
    }

    void portableScriptShape()
    {
        const QString source = QStringLiteral("C:/tmp/payload");
        const QString target = QStringLiteral("C:/apps/NeonBoard");
        const QString script = UpdateManager::buildPortableUpdateScript(source, target);
        QVERIFY(script.contains(QStringLiteral("robocopy")));
        // 脚本交给 cmd 执行，路径一律转成原生分隔符（Windows 下是反斜杠）
        QVERIFY(script.contains(QDir::toNativeSeparators(target)));
        QVERIFY(script.contains(QDir::toNativeSeparators(source)));
        // 必须等主进程退出再覆盖，否则 exe 被占用、替换失败
        QVERIFY(script.contains(QStringLiteral("tasklist")));
        // 覆盖完要把程序重新拉起来，否则用户以为更新把它关了
        QVERIFY(script.contains(QStringLiteral("NeonBoardHost.exe")));

        // bat 由 cmd 按本地代码页解析：任何非 ASCII 字符都可能让脚本语法错误
        const QByteArray raw = script.toLatin1();
        for (char c : raw)
            QVERIFY2(static_cast<unsigned char>(c) < 128, "自更新脚本必须是纯 ASCII");
    }

    void locatePayloadRoot()
    {
        QTemporaryDir outer;
        QVERIFY(outer.isValid());
        // zip 常见形态：多包一层 NeonBoard-0.3.0/
        const QString nested = outer.path() + QStringLiteral("/NeonBoard-0.3.0");
        QVERIFY(QDir().mkpath(nested));
        QFile marker(nested + QStringLiteral("/NeonBoard.exe"));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.write("x");
        marker.close();

        const QString found = UpdateManager::locatePayloadRoot(outer.path());
        QCOMPARE(QDir::fromNativeSeparators(found), QDir::fromNativeSeparators(nested));

        // 平铺形态（没有子目录）时返回解压目录本身
        QTemporaryDir flat;
        QVERIFY(flat.isValid());
        QFile flatMarker(flat.path() + QStringLiteral("/NeonBoardHost.exe"));
        QVERIFY(flatMarker.open(QIODevice::WriteOnly));
        flatMarker.write("x");
        flatMarker.close();
        QCOMPARE(QDir::fromNativeSeparators(UpdateManager::locatePayloadRoot(flat.path())),
                 QDir::fromNativeSeparators(flat.path()));

        // 空目录：兜底返回解压目录，不能返回空串让后续 robocopy 指向垃圾路径
        QTemporaryDir empty;
        QVERIFY(empty.isValid());
        QCOMPARE(QDir::fromNativeSeparators(UpdateManager::locatePayloadRoot(empty.path())),
                 QDir::fromNativeSeparators(empty.path()));
    }

    void metadataIsSane()
    {
        UpdateManager manager;
        // 版本号不能为空：它既用于比较，也展示在设置面板上
        QVERIFY(!manager.currentVersion().isEmpty());
        QVERIFY(manager.currentVersion().contains(QLatin1Char('.')));
        // 安装形态只有两种取值，界面文案依赖这个约定
        const QString kind = manager.installKind();
        QVERIFY(kind == QLatin1String("installed") || kind == QLatin1String("portable"));
        // 默认源不能为空，否则启动检查等于没有地址可用
        QVERIFY(!manager.updateSources().isEmpty());
        QCOMPARE(manager.state(), QStringLiteral("idle"));
        QVERIFY(!manager.updateAvailable());
    }
};

// GUILESS：本用例只测纯逻辑，不需要 QApplication（与项目其它单测保持一致）
QTEST_GUILESS_MAIN(TestUpdateManager)
#include "tst_updatemanager.moc"
