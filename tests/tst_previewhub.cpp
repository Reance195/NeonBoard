// DX-135：QProcess FailedToStart 黑洞的回归用例。
//
// Qt 语义：FailedToStart 只发 errorOccurred，永远不发 finished —— 只接 finished
// 的状态机（构建锁 / 打包队列）会永久卡死。本用例以一个不存在的程序走一次
// 异步构建阶段，钉住「状态机必须自行解锁」的契约。

#include "PreviewHub.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class TestPreviewHub : public QObject
{
    Q_OBJECT

private slots:
    void failedToStartUnlocksBuildState();
};

void TestPreviewHub::failedToStartUnlocksBuildState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    PreviewHub hub;
    const QString agentId = QStringLiteral("t1");
    QVERIFY(!hub.isBuilding(agentId));

    hub.runBuildStage(agentId, dir.path(),
                      QStringLiteral("Z:/definitely-missing/cmake.exe"));
    // 锁必须已登记 —— 否则下面的「解锁」断言没有对照意义
    if (!hub.isBuilding(agentId))
        QSKIP("spawn 未登记构建锁（平台差异），无法验证解锁");

    // FailedToStart → errorOccurred → 自行收尾：锁被清、日志给出可读原因
    QTRY_VERIFY_WITH_TIMEOUT(!hub.isBuilding(agentId), 10000);
    QVERIFY2(hub.buildLog(agentId).contains(QStringLiteral("启动失败")),
             qPrintable(hub.buildLog(agentId)));

    // 解锁后能再次进入构建（状态机可复用，没有残留 once-only 卡死）
    hub.runBuildStage(agentId, dir.path(),
                      QStringLiteral("Z:/definitely-missing/cmake.exe"));
    QTRY_VERIFY_WITH_TIMEOUT(!hub.isBuilding(agentId), 10000);
}

QTEST_GUILESS_MAIN(TestPreviewHub)

#include "tst_previewhub.moc"
