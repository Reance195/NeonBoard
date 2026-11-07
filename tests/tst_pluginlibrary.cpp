#include "PluginLibrary.h"

#include <QSignalSpy>
#include <QTest>

// DX-132：uninstall 的目录名派生防御 —— 派生结果为空（".." / "." / 含分隔符 / 盘符相对）
// 时必须整单拒绝，绝不能把 pluginDir 上级或自身 removeRecursively。
// 四个依赖全传 nullptr：本用例只走「id 防御」分支，不触 loader/market/runtime/database。
class TestPluginLibrary : public QObject
{
    Q_OBJECT

private slots:
    void uninstallRejectsUnsafeId();
    void uninstallSafeButMissingIdIsPlainFailure();
};

void TestPluginLibrary::uninstallRejectsUnsafeId()
{
    PluginLibrary library(nullptr, nullptr, nullptr, nullptr);

    QSignalSpy spy(&library, &PluginLibrary::operationFinished);

    QVERIFY(!library.uninstall(QStringLiteral("..")));
    QVERIFY(!library.uninstall(QStringLiteral(".")));
    QVERIFY(!library.uninstall(QStringLiteral("a/../b")));
    QVERIFY(!library.uninstall(QStringLiteral("C:evil")));

    // 每次拒绝都必须带「非法」文案，证明走的是 id 防御分支而不是「路径不存在」分支
    QCOMPARE(spy.size(), 4);
    for (const QList<QVariant> &args : spy) {
        const QString message = args.at(3).toString();
        QVERIFY2(message.contains(QStringLiteral("非法")), qPrintable(message));
    }
}

void TestPluginLibrary::uninstallSafeButMissingIdIsPlainFailure()
{
    PluginLibrary library(nullptr, nullptr, nullptr, nullptr);

    QSignalSpy spy(&library, &PluginLibrary::operationFinished);

    // 合法 id 但用户安装区没有它：拒绝原因必须是「不在用户安装区」，而不是「非法」
    QVERIFY(!library.uninstall(QStringLiteral("com.test.absent")));
    QCOMPARE(spy.size(), 1);
    QVERIFY2(spy.first().at(3).toString().contains(QStringLiteral("用户安装区")),
             qPrintable(spy.first().at(3).toString()));
}

QTEST_GUILESS_MAIN(TestPluginLibrary)

#include "tst_pluginlibrary.moc"
