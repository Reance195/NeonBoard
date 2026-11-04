// DX-39：启动闪屏的画面与状态机单测。
//
// 覆盖：
//   1. 阶段推进：setStage 推目标进度与阶段标签（空标签 = 不改标签）
//   2. 进度条缓动：向目标收敛、不越界、目标被夹到 [0,1]
//   3. 标签切换：换标签重置闪光、同标签不重置
//   4. 大步长封顶：advance(10s) 只推进 0.12s（渲染线程被拖慢时动画放慢而不是瞬移）
//   5. 画面冒烟：画到 QImage 上 —— 圆角之外透明、中央不透明（半透明窗口的地基）
//   6. 图层缓存：换个设备像素比重画，画面照样有内容（分层缓存不能只在 1x 下能用）
//
// 注意（DX-39 踩坑）：本仓库的 Ninja 构建没开 /showIncludes，
// **改 SplashScene.h 后不一定会重编引用它的 TU** —— 会出现「测试二进制用旧类布局、
// 新实现按新布局写」的堆损坏（崩溃点报在 QImage::~QImage，跟真实原因没关系）。
// 改完头文件务必确认引用它的 .cpp 真的重编了（看 obj 的时间戳）。

#include "SplashScene.h"

#include <QImage>
#include <QPainter>
#include <QtTest>

class TestSplashScene : public QObject
{
    Q_OBJECT

private slots:
    void stagePushesTargetAndLabel();
    void progressEasesTowardTarget();
    void progressStaysInRange();
    void labelSwitchResetsFlash();
    void sameLabelKeepsFlash();
    void largeDeltaIsClamped();
    void paintDrawsOpaqueCenterAndTransparentCorner();
    void paintWorksAtHighDevicePixelRatio();
    void paintDrawsLogoGlyph();
    void paintDrawsTitle();
};

void TestSplashScene::stagePushesTargetAndLabel()
{
    Splash::Scene scene;
    QCOMPARE(scene.label(), QStringLiteral("BOOT SEQUENCE"));

    scene.setStage(0.42, QStringLiteral("OPENING LOCAL STORE"));
    QCOMPARE(scene.targetProgress(), 0.42);
    QCOMPARE(scene.label(), QStringLiteral("OPENING LOCAL STORE"));

    // 空标签 = 不改标签，只推进度
    scene.setStage(0.5, QString());
    QCOMPARE(scene.label(), QStringLiteral("OPENING LOCAL STORE"));
    QCOMPARE(scene.targetProgress(), 0.5);
}

void TestSplashScene::progressEasesTowardTarget()
{
    Splash::Scene scene;
    scene.setStage(0.8, QStringLiteral("WORKING"));

    // 刚推进时还没到位（是缓动，不是跳变）
    scene.advance(0.016);
    QVERIFY(scene.progress() < scene.targetProgress());
    QVERIFY(scene.progress() > 0.02);

    // 推进足够久后收敛到目标
    for (int i = 0; i < 400; ++i)
        scene.advance(0.016);
    QVERIFY2(qAbs(scene.progress() - scene.targetProgress()) < 0.001,
             qPrintable(QStringLiteral("progress=%1 target=%2")
                            .arg(scene.progress())
                            .arg(scene.targetProgress())));
}

void TestSplashScene::progressStaysInRange()
{
    Splash::Scene scene;
    // 目标越界要被夹住（进度条画布只有 0..1）
    scene.setStage(3.0, QStringLiteral("OVER"));
    QCOMPARE(scene.targetProgress(), 1.0);
    scene.setStage(-2.0, QStringLiteral("UNDER"));
    QCOMPARE(scene.targetProgress(), 0.0);

    for (int i = 0; i < 50; ++i)
        scene.advance(0.05);
    QVERIFY(scene.progress() >= 0.0);
    QVERIFY(scene.progress() <= 1.0);
}

void TestSplashScene::labelSwitchResetsFlash()
{
    Splash::Scene scene;
    scene.setStage(0.3, QStringLiteral("FIRST"));
    // 单帧最多推进 0.12s（见 largeDeltaIsClamped），所以要分几帧累积
    for (int i = 0; i < 6; ++i)
        scene.advance(0.1);
    QVERIFY(scene.secondsSinceLabelChanged() > 0.4);

    scene.setStage(0.4, QStringLiteral("SECOND"));
    QVERIFY2(scene.secondsSinceLabelChanged() < 0.001,
             "换标签后闪光计时必须重置");
}

void TestSplashScene::sameLabelKeepsFlash()
{
    Splash::Scene scene;
    scene.setStage(0.3, QStringLiteral("SAME"));
    for (int i = 0; i < 6; ++i)
        scene.advance(0.1);
    const qreal before = scene.secondsSinceLabelChanged();

    // 同一个标签再来一次：不重置闪光（否则每帧都闪一下）
    scene.setStage(0.4, QStringLiteral("SAME"));
    QVERIFY(qAbs(scene.secondsSinceLabelChanged() - before) < 0.001);
}

void TestSplashScene::largeDeltaIsClamped()
{
    Splash::Scene scene;
    scene.advance(10.0);
    // 单帧最多推进 0.12s：渲染线程被拖慢时动画放慢，而不是瞬移到下一相位
    QVERIFY2(scene.elapsed() <= 0.121,
             qPrintable(QStringLiteral("elapsed=%1").arg(scene.elapsed())));
}

void TestSplashScene::paintDrawsOpaqueCenterAndTransparentCorner()
{
    Splash::Scene scene;
    // 淡入到位（淡入系数是 0.12s × 5/帧，几次就满）
    for (int i = 0; i < 10; ++i)
        scene.advance(0.05);

    QImage image(700, 430, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        scene.paint(painter, QSizeF(700, 430), 1.0);
    }

    // 圆角之外必须透明（半透明窗口的地基：靠这个透出桌面）
    QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(image.pixel(699, 429)), 0);
    // 中央必须画到了东西（底板 + 网格）
    QVERIFY2(qAlpha(image.pixel(350, 300)) > 200,
             qPrintable(QStringLiteral("center alpha=%1").arg(qAlpha(image.pixel(350, 300)))));
    // 顶部标题区也画到了
    QVERIFY(qAlpha(image.pixel(350, 250)) > 200);
}

// 分层缓存是按设备像素比建的层：1.5x 下也必须画得出东西，
// 否则高 DPI 屏上闪屏会是空的。
void TestSplashScene::paintWorksAtHighDevicePixelRatio()
{
    Splash::Scene scene;
    for (int i = 0; i < 10; ++i)
        scene.advance(0.05);

    const qreal dpr = 1.5;
    QImage image(qRound(700 * dpr), qRound(430 * dpr), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.scale(dpr, dpr);
        scene.paint(painter, QSizeF(700, 430), 1.0);
    }

    QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
    QVERIFY2(qAlpha(image.pixel(qRound(350 * dpr), qRound(300 * dpr))) > 200,
             qPrintable(QStringLiteral("center alpha=%1")
                            .arg(qAlpha(image.pixel(qRound(350 * dpr), qRound(300 * dpr))))));
    // LOGO 字形区域也要有内容（静态层缓存的路径）
    QVERIFY2(qAlpha(image.pixel(qRound(350 * dpr), qRound(150 * dpr))) > 100,
             qPrintable(QStringLiteral("logo alpha=%1")
                            .arg(qAlpha(image.pixel(qRound(350 * dpr), qRound(150 * dpr))))));
}

// DX-39 踩坑：闪屏画面搬进渲染线程后，LOGO 字形与标题整段消失。
// 根因不是线程，是**坐标系**：图层化之后 renderLayer 把画笔放在场景坐标，
// 而 LOGO 字形路径是「字形局部坐标（基线在原点）」—— 少一次 translate(logoOrigin)
// 就被裁到图层缓冲外面；标题路径本来就是场景坐标，反而多了一次平移。
//
// 光靠「画面中央有东西」这类断言抓不住这种坑（轨道环、网格都在中央有东西），
// 所以这里按「墨迹区里有没有高亮的字形像素」来判。
namespace {

int countBrightPixels(const QImage &image, int x0, int y0, int x1, int y1)
{
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (qBlue(image.pixel(x, y)) > 150)
                ++count;
        }
    }
    return count;
}

} // namespace

void TestSplashScene::paintDrawsLogoGlyph()
{
    Splash::Scene scene;
    for (int i = 0; i < 40; ++i)
        scene.advance(0.05);

    QImage image(700, 430, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        scene.paint(painter, QSizeF(700, 430), 1.0);
    }

    // LOGO 墨迹区（场景坐标）：字形以 frame 中心为基准，基线在 frame.top()+35% 高处。
    const int bright = countBrightPixels(image, 300, 100, 400, 196);
    QVERIFY2(bright > 500,
             qPrintable(QStringLiteral("logo bright pixels=%1（LOGO 字形没画出来？）").arg(bright)));
}

void TestSplashScene::paintDrawsTitle()
{
    Splash::Scene scene;
    for (int i = 0; i < 40; ++i)
        scene.advance(0.05);

    QImage image(700, 430, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        scene.paint(painter, QSizeF(700, 430), 1.0);
    }

    // 标题行（NEONBOARD）在基线 frame.top()+56% 附近。
    const int bright = countBrightPixels(image, 180, 212, 520, 250);
    QVERIFY2(bright > 500,
             qPrintable(QStringLiteral("title bright pixels=%1（标题没画出来？）").arg(bright)));
}

QTEST_MAIN(TestSplashScene)
#include "tst_splashscene.moc"
