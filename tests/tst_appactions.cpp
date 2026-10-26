#include "AppActions.h"

#include <QTest>

// 动作层是「命令行 / 托盘 / 二次启动 / 跳转列表」的共同入口。
// 这些用例守住三件事：
//   ① 参数 → 动作的优先级必须确定（同一组参数永远同一个结果）；
//   ② 动作 → QML 调用的映射不能退化成"只亮窗口"；
//   ③ IPC 帧的解析边界（旧版裸消息、垃圾数据、超长、换行注入）。
class TestAppActions : public QObject
{
    Q_OBJECT

private slots:
    void fromArguments_data();
    void fromArguments();
    void autostartAloneIsNotAPanelAction();
    void paletteSkipsFollowingFlag();
    void isKnownRejectsGarbage();
    void targetForMapsToQmlMethods();
    void targetForPaletteCarriesEmptyQuery();
    void frameRoundTrip_data();
    void frameRoundTrip();
    void frameRejectsForeignPayload();
    void frameCollapsesNewlines();
    void frameKeepsUtf8Query();
};

void TestAppActions::fromArguments_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("expected");

    QTest::newRow("无面板开关") << QStringList{QStringLiteral("NeonBoard.exe")}
                                << QStringLiteral("show");
    QTest::newRow("新建任务") << QStringList{QStringLiteral("NeonBoard.exe"),
                                        QStringLiteral("--newtask")}
                              << QStringLiteral("newtask");
    QTest::newRow("偏好设置") << QStringList{QStringLiteral("--settings")}
                              << QStringLiteral("settings");
    QTest::newRow("插件面板") << QStringList{QStringLiteral("--plugins")}
                              << QStringLiteral("plugins");
    QTest::newRow("外观面板") << QStringList{QStringLiteral("--appearance")}
                              << QStringLiteral("appearance");
    QTest::newRow("插件库") << QStringList{QStringLiteral("--library")}
                            << QStringLiteral("library");
    QTest::newRow("命令面板带查询") << QStringList{QStringLiteral("--palette"),
                                             QStringLiteral("周报")}
                                    << QStringLiteral("palette:周报");
    QTest::newRow("命令面板空查询") << QStringList{QStringLiteral("--palette")}
                                    << QStringLiteral("palette:");
    // 优先级写死成一条链：同时给多个开关时结果必须可预期
    QTest::newRow("newtask 优先于 settings") << QStringList{QStringLiteral("--settings"),
                                                        QStringLiteral("--newtask")}
                                             << QStringLiteral("newtask");
    QTest::newRow("palette 优先于一切") << QStringList{QStringLiteral("--library"),
                                                   QStringLiteral("--newtask"),
                                                   QStringLiteral("--palette"),
                                                   QStringLiteral("x")}
                                        << QStringLiteral("palette:x");
}

void TestAppActions::fromArguments()
{
    QFETCH(QStringList, arguments);
    QFETCH(QString, expected);

    QCOMPARE(AppActions::fromArguments(arguments), expected);
}

void TestAppActions::autostartAloneIsNotAPanelAction()
{
    // --autostart 只是"别弹窗"的启动策略，不是面板动作；
    // 主实例在单实例转发时把它换成 noop（见 main.cpp）
    QCOMPARE(AppActions::fromArguments(QStringList{QStringLiteral("NeonBoard.exe"),
                                                   QStringLiteral("--autostart")}),
             QStringLiteral("show"));
}

void TestAppActions::paletteSkipsFollowingFlag()
{
    // `--palette --settings`：下一个 token 是开关而不是查询串，
    // 不能把它当成搜索词吞掉（否则设置面板永远打不开）
    QCOMPARE(AppActions::fromArguments(QStringList{QStringLiteral("--palette"),
                                                   QStringLiteral("--settings")}),
             QStringLiteral("palette:"));
}

void TestAppActions::isKnownRejectsGarbage()
{
    QVERIFY(AppActions::isKnown(QStringLiteral("show")));
    QVERIFY(AppActions::isKnown(QStringLiteral("newtask")));
    QVERIFY(AppActions::isKnown(QStringLiteral("noop")));
    QVERIFY(AppActions::isKnown(QStringLiteral("palette:")));
    QVERIFY(AppActions::isKnown(QStringLiteral("palette:任意查询")));

    // IPC 收端白名单：本地管道名可猜，不能让对端指定任意 QML 方法名
    QVERIFY(!AppActions::isKnown(QString()));
    QVERIFY(!AppActions::isKnown(QStringLiteral("openSettings")));
    QVERIFY(!AppActions::isKnown(QStringLiteral("SHOW")));
    QVERIFY(!AppActions::isKnown(QStringLiteral("showx")));
    QVERIFY(!AppActions::isKnown(QStringLiteral(" palette:x")));
}

void TestAppActions::targetForMapsToQmlMethods()
{
    const AppActions::Target newTask = AppActions::targetFor(QStringLiteral("newtask"));
    QCOMPARE(newTask.method, QStringLiteral("openTaskComposer"));
    QCOMPARE(newTask.argument, QStringLiteral("todo"));
    QVERIFY(newTask.hasArgument);

    const AppActions::Target settings = AppActions::targetFor(QStringLiteral("settings"));
    QCOMPARE(settings.method, QStringLiteral("openSettings"));
    // 无参方法不能被塞一个空参：QMetaObject 会匹配不上，面板静默不开
    QVERIFY(!settings.hasArgument);

    QCOMPARE(AppActions::targetFor(QStringLiteral("plugins")).method,
             QStringLiteral("openPlugins"));
    QCOMPARE(AppActions::targetFor(QStringLiteral("appearance")).method,
             QStringLiteral("openAppearance"));
    QCOMPARE(AppActions::targetFor(QStringLiteral("library")).method,
             QStringLiteral("openPluginLibrary"));

    // show / noop 只需要亮窗口（noop 连窗口都不亮，由调用方短路）
    QVERIFY(AppActions::targetFor(QStringLiteral("show")).method.isEmpty());
    QVERIFY(AppActions::targetFor(QStringLiteral("noop")).method.isEmpty());
}

void TestAppActions::targetForPaletteCarriesEmptyQuery()
{
    const AppActions::Target palette = AppActions::targetFor(QStringLiteral("palette:"));
    QCOMPARE(palette.method, QStringLiteral("openPalette"));
    QCOMPARE(palette.argument, QString());
    // 空查询也要传参：CommandPalette.openPalette(query) 没有默认值，
    // 无参调用会拿到 undefined 而不是空串
    QVERIFY(palette.hasArgument);
}

void TestAppActions::frameRoundTrip_data()
{
    QTest::addColumn<QString>("action");

    QTest::newRow("show") << QStringLiteral("show");
    QTest::newRow("newtask") << QStringLiteral("newtask");
    QTest::newRow("noop") << QStringLiteral("noop");
    QTest::newRow("空查询") << QStringLiteral("palette:");
    QTest::newRow("带查询") << QStringLiteral("palette:abc 123");
}

void TestAppActions::frameRoundTrip()
{
    QFETCH(QString, action);

    QCOMPARE(AppActions::decodeFrame(AppActions::encodeFrame(action)), action);
}

void TestAppActions::frameRejectsForeignPayload()
{
    // 旧版（只发裸 "show"）与其它进程写进来的内容都不该被当成动作解析：
    // 返回空串 → 调用方回退到"亮窗口"，与历史行为一致
    QCOMPARE(AppActions::decodeFrame(QByteArray("show")), QString());
    QCOMPARE(AppActions::decodeFrame(QByteArray()), QString());
    QCOMPARE(AppActions::decodeFrame(QByteArray("NB1 ")), QString());
    QCOMPARE(AppActions::decodeFrame(QByteArray("NB1 openSettings")), QString());
    // 超长：管道名可猜，收端不能跟着无限缓冲
    QCOMPARE(AppActions::decodeFrame(QByteArray(5000, 'x')), QString());
}

void TestAppActions::frameCollapsesNewlines()
{
    // 单行协议：查询串里的换行会被当成分帧边界，必须折叠成空格
    const QString decoded = AppActions::decodeFrame(
        AppActions::encodeFrame(QStringLiteral("palette:a\nb\rc")));
    QCOMPARE(decoded, QStringLiteral("palette:a b c"));
}

void TestAppActions::frameKeepsUtf8Query()
{
    // 命令面板查询常是中文：帧按 UTF-8 传输，不能退化成 Latin-1
    const QString action = QStringLiteral("palette:写周报");
    QCOMPARE(AppActions::decodeFrame(AppActions::encodeFrame(action)), action);
}

QTEST_GUILESS_MAIN(TestAppActions)

#include "tst_appactions.moc"
