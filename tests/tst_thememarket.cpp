// R8-02：主题包市场 —— 浏览 / 安装 / 卸载。
//
// 三条验收各自对着一种真实故障：
//   1. **断网列表不能空**：拉不到索引时要退回内置目录并标「离线」，
//      空列表会让用户以为市场坏了，而不是网断了。
//   2. **安装中途断掉不许留半个包**：写临时文件 + rename，重启时清掉 .tmp；
//      扫描只认 *.json，所以半成品永远不会被当成主题读进来。
//   3. **卸载要回到内置预设**：删完包目录里不许再有它的令牌。

#include "ThemeMarket.h"
#include "ThemePack.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestThemeMarket : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void offlineCatalogIsNotEmpty();
    void offlineAfterFailedFetch();
    void catalogMarksInstalled();
    void installWritesNormalizedFile();
    void installRejectsInvalidPack();
    void interruptedInstallLeavesNoHalfPack();
    void uninstallRemovesOnlyThatPack();
    void uninstallUnknownIsFalse();

private:
    static QByteArray packJson(const QString &id, const QString &name);
    QString themeFile(const QString &name) const;
    QString m_themeDir;

    QTemporaryDir m_data;
    ThemePack *m_packs = nullptr;
    ThemeMarket *m_market = nullptr;
};

QByteArray TestThemeMarket::packJson(const QString &id, const QString &name)
{
    QJsonObject tokens;
    tokens.insert(QStringLiteral("bg"), QStringLiteral("#0A0F1E"));
    tokens.insert(QStringLiteral("text"), QStringLiteral("#EAF2FF"));
    tokens.insert(QStringLiteral("muted"), QStringLiteral("#7C8BA1"));
    tokens.insert(QStringLiteral("cyan"), QStringLiteral("#5EEAD4"));
    tokens.insert(QStringLiteral("magenta"), QStringLiteral("#F472B6"));
    tokens.insert(QStringLiteral("violet"), QStringLiteral("#A78BFA"));
    tokens.insert(QStringLiteral("success"), QStringLiteral("#34D399"));
    tokens.insert(QStringLiteral("warning"), QStringLiteral("#FBBF24"));
    tokens.insert(QStringLiteral("danger"), QStringLiteral("#FB7185"));

    QJsonObject root;
    root.insert(QStringLiteral("id"), id);
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("tokens"), tokens);
    return QJsonDocument(root).toJson();
}

QString TestThemeMarket::themeFile(const QString &name) const
{
    return QDir(m_themeDir).filePath(name);
}

void TestThemeMarket::initTestCase()
{
    // 主题目录由 AppPaths 从 NEONBOARD_DATA_DIR 推出来，整份测试只指一次
    QVERIFY(m_data.isValid());
    qputenv("NEONBOARD_DATA_DIR", m_data.path().toUtf8());
    m_themeDir = QDir(m_data.path()).filePath(QStringLiteral("themes"));
    QVERIFY(QDir().mkpath(m_themeDir));
}

void TestThemeMarket::init()
{
    // 每个用例从「一个包都没装」开始
    QDir dir(m_themeDir);
    for (const QString &name : dir.entryList(QDir::Files))
        QVERIFY(dir.remove(name));
    m_packs = new ThemePack(m_themeDir);
    m_market = new ThemeMarket(m_packs);
}

void TestThemeMarket::cleanup()
{
    delete m_market;
    m_market = nullptr;
    delete m_packs;
    m_packs = nullptr;
}

void TestThemeMarket::offlineCatalogIsNotEmpty()
{
    // 没配索引地址 = 纯离线：列表必须有内置目录，并且标了 offline
    QVERIFY(m_market->offline());
    QSignalSpy spy(m_market, &ThemeMarket::refreshed);
    m_market->refresh();
    QCOMPARE(spy.count(), 1);
    QVERIFY(m_market->offline());

    const QVariantList catalog = m_market->catalog();
    QVERIFY2(catalog.size() >= 3, "离线时市场列表不许是空的");
    const QVariantMap first = catalog.first().toMap();
    QVERIFY(!first.value(QStringLiteral("key")).toString().isEmpty());
    QVERIFY(!first.value(QStringLiteral("name")).toString().isEmpty());
    QVERIFY(first.value(QStringLiteral("official")).toBool());
    QVERIFY(!first.value(QStringLiteral("installed")).toBool());
}

void TestThemeMarket::offlineAfterFailedFetch()
{
    // 指向一个必定连不上的本地端口：不依赖外网，也跑得很快
    m_market->setIndexUrl(QStringLiteral("http://127.0.0.1:1/index.json"));
    QSignalSpy spy(m_market, &ThemeMarket::refreshed);
    m_market->refresh();
    // 第一次 refreshed 是「开始加载」，第二次才是结果
    QVERIFY(spy.wait(5000) || spy.count() >= 1);
    QTRY_VERIFY_WITH_TIMEOUT(!m_market->loading(), 5000);
    QVERIFY2(m_market->offline(), "拉不到索引时必须进入离线态");
    QVERIFY2(m_market->catalog().size() >= 3, "离线时列表要退回内置目录，不能空");
}

void TestThemeMarket::catalogMarksInstalled()
{
    QVERIFY(m_market->installJson(packJson(QStringLiteral("ocean"), QStringLiteral("深海蓝")))
                    .value(QStringLiteral("ok")).toBool());
    // ocean 是内置目录里就有的 id：装了之后要显示成「已安装」，而不是多出一行
    const QVariantList catalog = m_market->catalog();
    int oceanRows = 0;
    bool installed = false;
    for (const QVariant &item : catalog) {
        const QVariantMap entry = item.toMap();
        if (entry.value(QStringLiteral("key")).toString() != QStringLiteral("ocean"))
            continue;
        ++oceanRows;
        installed = entry.value(QStringLiteral("installed")).toBool();
    }
    QCOMPARE(oceanRows, 1);
    QVERIFY(installed);
}

void TestThemeMarket::installWritesNormalizedFile()
{
    // 文件名五花八门也要规整成 <id>.json：目录里得一眼看出装的是谁
    const QVariantMap result =
            m_market->installJson(packJson(QStringLiteral("aurora"), QStringLiteral("极光")),
                                  QStringLiteral("downloaded(1)"));
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(result.value(QStringLiteral("reason")).toString()));
    QVERIFY(QFile::exists(themeFile(QStringLiteral("aurora.json"))));
    QCOMPARE(m_packs->count(), 1);
    QCOMPARE(m_packs->packs().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("极光"));
    // 装完不该留下临时文件
    QVERIFY(!QFile::exists(themeFile(QStringLiteral("aurora.json.tmp"))));
}

void TestThemeMarket::installRejectsInvalidPack()
{
    const QVariantMap result = m_market->installJson("{ not a pack");
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QVERIFY(!result.value(QStringLiteral("reason")).toString().isEmpty());
    // 坏包一个字节都不许落盘：落了之后每次扫描都会跳过它，用户只会觉得「装了没反应」
    QCOMPARE(QDir(m_themeDir).entryList(QDir::Files).size(), 0);
    QCOMPARE(m_packs->count(), 0);

    // 缺令牌同理
    QVERIFY(!m_market->installJson(QStringLiteral("{\"id\":\"x\",\"name\":\"X\",\"tokens\":{}}")
                                            .toUtf8())
                     .value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_packs->count(), 0);
}

void TestThemeMarket::interruptedInstallLeavesNoHalfPack()
{
    QVERIFY(m_market->installJson(packJson(QStringLiteral("aurora"), QStringLiteral("极光")))
                    .value(QStringLiteral("ok")).toBool());
    const int before = m_packs->count();

    // 模拟「写到一半断电」：目录里留下一个 .tmp，没有对应的 .json
    QFile half(themeFile(QStringLiteral("halfbaked.json.tmp")));
    QVERIFY(half.open(QIODevice::WriteOnly));
    half.write(packJson(QStringLiteral("halfbaked"), QStringLiteral("半成品")).left(40));
    half.close();

    // 重启：新实例扫描时不许把 .tmp 当成主题（扫描只认 *.json）
    ThemePack restarted(m_themeDir);
    QCOMPARE(restarted.count(), before);

    // 新实例的市场构造时顺手清掉半成品
    ThemeMarket market(&restarted);
    QVERIFY2(!QFile::exists(themeFile(QStringLiteral("halfbaked.json.tmp"))),
             "重启后不该还留着半成品临时文件");
    QCOMPARE(restarted.count(), before);

    // 主题列表回到安装前状态：还是那一个包
    QCOMPARE(restarted.packs().first().toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("aurora"));
}

void TestThemeMarket::uninstallRemovesOnlyThatPack()
{
    // ocean 是内置目录里就有的包；aurora 是用户自己丢进来的。
    // 两个都要能卸，卸完的文件与列表状态不一样：
    //   · ocean 卸了之后仍留在目录里（可以再装回来），只是不再标「已安装」
    //   · aurora 卸了就从列表里彻底消失
    QVERIFY(m_market->installJson(packJson(QStringLiteral("ocean"), QStringLiteral("深海蓝")))
                    .value(QStringLiteral("ok")).toBool());
    QVERIFY(m_market->installJson(packJson(QStringLiteral("aurora"), QStringLiteral("极光")))
                    .value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_packs->count(), 2);

    QSignalSpy spy(m_market, &ThemeMarket::refreshed);
    QVERIFY(m_market->uninstall(QStringLiteral("ocean")));
    QCOMPARE(m_packs->count(), 1);
    QCOMPARE(m_packs->packs().first().toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("aurora"));
    QVERIFY(spy.count() >= 1);

    // 「用户主题令牌无残留」：文件没了，目录里也不该再有它
    QVERIFY(!QFile::exists(themeFile(QStringLiteral("ocean.json"))));

    bool oceanListed = false;
    bool oceanInstalled = true;
    bool auroraInstalled = false;
    for (const QVariant &item : m_market->catalog()) {
        const QVariantMap entry = item.toMap();
        const QString key = entry.value(QStringLiteral("key")).toString();
        if (key == QStringLiteral("ocean")) {
            oceanListed = true;
            oceanInstalled = entry.value(QStringLiteral("installed")).toBool();
        }
        if (key == QStringLiteral("aurora"))
            auroraInstalled = entry.value(QStringLiteral("installed")).toBool();
    }
    QVERIFY(oceanListed);
    QVERIFY(!oceanInstalled);
    QVERIFY(auroraInstalled);
}

void TestThemeMarket::uninstallUnknownIsFalse()
{
    QVERIFY(!m_market->uninstall(QStringLiteral("never-installed")));
    QVERIFY(!m_market->uninstall(QString()));
}

QTEST_GUILESS_MAIN(TestThemeMarket)

#include "tst_thememarket.moc"
