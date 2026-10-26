#include "WindowsIntegration.h"

#include "AppActions.h"

#include <QDir>
#include <QSet>
#include <QTest>

// 只测纯逻辑与只读查询：任何写入注册表的动作都不进单元测试，
// 否则跑测试就改了开发机的关联设置（不可接受）。
class TestWindowsIntegration : public QObject
{
    Q_OBJECT

private slots:
    void appUserModelIdIsStable();
    void buildOpenCommandQuotesPathWithSpaces();
    void buildOpenCommandKeepsShellPlaceholder();
    void packagePathFromArgs_data();
    void packagePathFromArgs();
    void associationStatusIsReadOnly();
    void jumpListTasksAreClickable();
};

void TestWindowsIntegration::appUserModelIdIsStable()
{
    // 这是与安装器/快捷方式共享的对外契约：改了会让通知归到"未知应用"
    QCOMPARE(WindowsIntegration::appUserModelId(), QStringLiteral("com.lin.neonboard"));
}

void TestWindowsIntegration::buildOpenCommandQuotesPathWithSpaces()
{
    const QString exe = QStringLiteral("C:/Program Files/NeonBoard/NeonBoard.exe");
    const QString command = WindowsIntegration::buildOpenCommand(exe);
    const QString native = QDir::toNativeSeparators(exe);

    // 带空格的路径必须整体被引号包裹，否则 shell 会把它当成多个参数
    QVERIFY2(command.contains(QLatin1Char('"') + native + QLatin1Char('"')),
             qPrintable(command));
    QVERIFY2(command.contains(QStringLiteral("--install-plugin-file")), qPrintable(command));
}

void TestWindowsIntegration::buildOpenCommandKeepsShellPlaceholder()
{
    const QString command = WindowsIntegration::buildOpenCommand(QStringLiteral("C:/nb/nb.exe"));

    // %1 必须原样保留：它由 shell 替换成被双击的文件路径，
    // 若被本地格式化吃掉，双击插件包就会"打开程序但不安装"。
    QVERIFY2(command.endsWith(QStringLiteral("\"%1\"")), qPrintable(command));
    QCOMPARE(command.count(QStringLiteral("%1")), 1);
}

void TestWindowsIntegration::packagePathFromArgs_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("expected");

    QTest::newRow("正常路径")
        << QStringList{QStringLiteral("NeonBoard.exe"), QStringLiteral("--install-plugin-file"),
                       QStringLiteral("C:/a b/demo.neon")}
        << QStringLiteral("C:/a b/demo.neon");
    QTest::newRow("缺少参数值")
        << QStringList{QStringLiteral("NeonBoard.exe"), QStringLiteral("--install-plugin-file")}
        << QString();
    QTest::newRow("没有该参数")
        << QStringList{QStringLiteral("NeonBoard.exe"), QStringLiteral("--smoke")} << QString();
    QTest::newRow("空参数表") << QStringList{} << QString();
}

void TestWindowsIntegration::packagePathFromArgs()
{
    QFETCH(QStringList, arguments);
    QFETCH(QString, expected);

    QCOMPARE(WindowsIntegration::packagePathFromArgs(arguments), expected);
}

void TestWindowsIntegration::associationStatusIsReadOnly()
{
    const QVariantMap status = WindowsIntegration::associationStatus();

    // 结构契约：调用方（--assoc-status / 排障）依赖这几个键必须存在
    QVERIFY(status.contains(QStringLiteral("registered")));
    QVERIFY(status.contains(QStringLiteral("classRoot")));
    QVERIFY(status.contains(QStringLiteral("extension")));
    QVERIFY(status.contains(QStringLiteral("command")));
    QVERIFY(status.contains(QStringLiteral("expectedCommand")));

    QCOMPARE(status.value(QStringLiteral("extension")).toString(), QStringLiteral(".neon"));
    QCOMPARE(status.value(QStringLiteral("classRoot")).toString(),
             QStringLiteral("NeonBoard.PluginPackage"));
    // registered 必须是布尔，不能是"空/非空"这种模糊语义
    QCOMPARE(status.value(QStringLiteral("registered")).typeId(), static_cast<int>(QMetaType::Bool));
}

void TestWindowsIntegration::jumpListTasksAreClickable()
{
    const QVector<WindowsIntegration::JumpListTask> tasks = WindowsIntegration::jumpListTasks();
    QVERIFY2(!tasks.isEmpty(), "跳转列表至少要有一个任务项");

    QSet<QString> seen;
    for (const WindowsIntegration::JumpListTask &task : tasks) {
        // 标题是菜单里唯一的可读文字：缺了会全部显示成 exe 名
        QVERIFY2(!task.title.isEmpty(), qPrintable(task.arguments));
        QVERIFY2(task.arguments.startsWith(QLatin1String("--")),
                 qPrintable(task.arguments));
        // 参数重复 = 两项行为完全一样，用户会以为是 bug
        QVERIFY2(!seen.contains(task.arguments), qPrintable(task.arguments));
        seen.insert(task.arguments);

        // 关键断言：点击后必须真的做点什么。参数若与动作层脱节（拼错/改过名），
        // 菜单就退化成"点了只亮窗口"的装饰。
        const QString action = AppActions::fromArguments(
            QStringList{QStringLiteral("NeonBoard.exe"), task.arguments});
        QVERIFY2(action != QLatin1String(AppActions::kShow), qPrintable(task.arguments));
        QVERIFY2(!AppActions::targetFor(action).method.isEmpty(), qPrintable(task.arguments));
    }
}

QTEST_GUILESS_MAIN(TestWindowsIntegration)

#include "tst_windowsintegration.moc"
