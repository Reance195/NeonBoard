// R4-07：首启动引导的落盘与「该不该弹」判定。
//
// 这段逻辑的坑都在 QSettings 的落盘与去重：
//   1) 标记只该在真的变了时写盘并发信号（否则每帧属性绑定都会打一次 ini）；
//   2) 跳过/完成都要落盘，否则每次启动都重弹；
//   3) 是否弹由「未完成过 且 空板」两个条件共同决定，老用户换机重装不该被挡。
// 用 QTemporaryDir 当 QSettings 的 UserScope，不碰开发机真实设置。

#include "UiSettings.h"

#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestUiSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void onboardingOffByDefault();
    void shouldOnboardOnlyForEmptyBoard();
    void finishingPersistsAcrossInstances();
    void settingSameValueDoesNotEmit();
    void qualityIsClamped();

private:
    QTemporaryDir m_dir;
};

void TestUiSettings::initTestCase()
{
    QVERIFY(m_dir.isValid());
    // 与 main.cpp 同一条约定：默认 IniFormat，再把 UserScope 指到临时目录
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("NeonBoardTest"));
    QCoreApplication::setApplicationName(QStringLiteral("UiSettingsTest"));
}

void TestUiSettings::init()
{
    QSettings().clear();
}

void TestUiSettings::cleanup()
{
    QSettings().clear();
}

void TestUiSettings::onboardingOffByDefault()
{
    UiSettings settings;
    QVERIFY(!settings.onboardingDone());
}

void TestUiSettings::shouldOnboardOnlyForEmptyBoard()
{
    UiSettings settings;

    // 空板 + 没引导过 → 弹
    QVERIFY(settings.shouldOnboard(0));
    // 有卡的老用户 → 不弹（换机重装也一样，看板不是空的）
    QVERIFY(!settings.shouldOnboard(1));
    QVERIFY(!settings.shouldOnboard(89));

    // 引导过以后，即使看板又清空了也不再弹
    settings.setOnboardingDone(true);
    QVERIFY(!settings.shouldOnboard(0));
}

void TestUiSettings::finishingPersistsAcrossInstances()
{
    {
        UiSettings settings;
        QVERIFY(!settings.onboardingDone());
        settings.setOnboardingDone(true);
        QVERIFY(settings.shouldOnboard(0) == false);
        // 析构时 QSettings 落盘
    }

    // 重开一次应用：跳过/完成过就不再打扰
    UiSettings reopened;
    QVERIFY(reopened.onboardingDone());
    QVERIFY(!reopened.shouldOnboard(0));
}

void TestUiSettings::settingSameValueDoesNotEmit()
{
    UiSettings settings;
    QSignalSpy spy(&settings, &UiSettings::onboardingDoneChanged);

    settings.setOnboardingDone(true);
    QCOMPARE(spy.count(), 1);

    // 重复设同一个值不该再发信号（属性绑定会反复写，重复发信号会拉动整树重算）
    settings.setOnboardingDone(true);
    QCOMPARE(spy.count(), 1);

    settings.setOnboardingDone(false);
    QCOMPARE(spy.count(), 2);
}

void TestUiSettings::qualityIsClamped()
{
    UiSettings settings;
    settings.setQuality(5);
    QCOMPARE(settings.quality(), 2);
    settings.setQuality(-3);
    QCOMPARE(settings.quality(), 0);
    settings.setQuality(1);
    QCOMPARE(settings.quality(), 1);
}

QTEST_GUILESS_MAIN(TestUiSettings)

#include "tst_uisettings.moc"
