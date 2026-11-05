// DX-39：启动闪屏的画面与状态机单测。
//
// 覆盖：
//   1. 阶段推进：setStage 推目标进度与阶段标签（空标签 = 不改标签）
//   2. 进度条缓动：向目标收敛、不越界、目标被夹到 [0,1]
//   3. 标签切换：换标签重置闪光、同标签不重置
//   4. 大步长封顶：advance(10s) 只推进 0.12s（渲染线程被拖慢时动画放慢而不是瞬移）
//   5. 画面冒烟：画到 QImage 上 —— 圆角之外透明、中央不透明（半透明窗口的地基）
//   6. 图层缓存：换个设备像素比重画，画面照样有内容（分层缓存不能只在 1x 下能用）
//   7. 百分比读数：dpr=1.0 与 dpr=1.5 下字形一致（DX-69：高 DPI 下读数乱码）
//
// 注意（DX-39 踩坑）：本仓库的 Ninja 构建没开 /showIncludes，
// **改 SplashScene.h 后不一定会重编引用它的 TU** —— 会出现「测试二进制用旧类布局、
// 新实现按新布局写」的堆损坏（崩溃点报在 QImage::~QImage，跟真实原因没关系）。
// 改完头文件务必确认引用它的 .cpp 真的重编了（看 obj 的时间戳）。

#include "SplashScene.h"

#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QtTest>

#include <cmath>

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
    void percentReadoutMatchesAcrossDevicePixelRatios();
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

// DX-69：启动闪屏的百分比读数在高 DPI 下变乱码。
//
// 根因：数字图集是**设备像素**图（renderDigitAtlas 内部 scale(dpr) 后按 dpr 放大），
// 而 drawProgress 拿**逻辑像素**当源矩形去采样 —— dpr>1 时横向错位半格、
// 纵向只截到图集 2/3 高度，百分比就碎成一片（dpr=1.0 时 ×dpr 与 /dpr 恰好抵消，
// 所以只有 125%/150% 这类高 DPI 屏才复现）。
//
// 判据：同一场景在 dpr=1.0 与 dpr=1.5 下画出的读数，字形必须一致。
// 做法：画两遍（有图集 / 无图集）作差 —— 差值即纯读数墨迹（背景被抵消），
// 再比较两者「逐逻辑列的墨迹纵向覆盖」分布，相关性必须 > 0.8。
namespace {

// 读数所在区域（逻辑坐标）。公式与 drawProgress 保持一致：轨道右端 + 14 起是读数，
// 基线在 track.top()+4。
struct PercentRegion
{
    qreal x = 0.0;
    qreal y = 0.0;
    qreal w = 0.0;
    qreal h = 0.0;
};

PercentRegion percentRegion(const QSizeF &logicalSize, qreal cell)
{
    constexpr qreal margin = 14.0;
    const QRectF frame(margin, margin, logicalSize.width() - margin * 2.0,
                       logicalSize.height() - margin * 2.0);
    const qreal barWidth = frame.width() * 0.52;
    const QRectF track(frame.center().x() - barWidth / 2.0,
                       frame.top() + frame.height() * 0.815, barWidth, 3.0);
    PercentRegion region;
    region.x = track.right() + 12.0;
    region.y = track.top() - 12.0;
    region.w = cell * 3.0 + 4.0;
    region.h = 26.0;
    return region;
}

// 画一帧闪屏。withAtlas=false 时不喂数字图集，于是画面里没有百分比读数。
QImage renderSplashFrame(qreal dpr, bool withAtlas)
{
    const QSizeF logicalSize(700.0, 430.0);
    Splash::Scene scene;
    scene.setStage(0.42, QStringLiteral("TEST STAGE"));
    // 进度是缓动的：推到目标附近，读数才会稳定在 42%。
    for (int i = 0; i < 400; ++i)
        scene.advance(0.016);
    if (withAtlas) {
        qreal cell = 0.0;
        const QImage atlas = Splash::Scene::renderDigitAtlas(dpr, &cell);
        const QFontMetricsF metrics(Splash::Scene::percentFont());
        scene.setDigitAtlas(atlas, cell, metrics.ascent());
    }
    QImage image(qRound(logicalSize.width() * dpr), qRound(logicalSize.height() * dpr),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.scale(dpr, dpr);
        scene.paint(painter, logicalSize, 1.0);
    }
    return image;
}

// 逐逻辑列的墨迹纵向覆盖：有图集 - 无图集 = 纯读数墨迹。
// 设备像素列按 dpr 归并到逻辑列，两种 dpr 才能逐列比。
QVector<double> inkColumnProfile(const QImage &withAtlas, const QImage &withoutAtlas,
                                  const PercentRegion &region, qreal dpr)
{
    QVector<double> profile;
    const int columns = qCeil(region.w);
    const int y0 = qRound(region.y * dpr);
    const int y1 = qRound((region.y + region.h) * dpr);
    for (int c = 0; c < columns; ++c) {
        const int x0 = qRound((region.x + c) * dpr);
        const int x1 = qMax(x0 + 1, qRound((region.x + c + 1) * dpr));
        int covered = 0;
        for (int y = y0; y < y1 && y < withAtlas.height() && y < withoutAtlas.height(); ++y) {
            bool hit = false;
            for (int x = x0; x < x1 && x < withAtlas.width() && x < withoutAtlas.width(); ++x) {
                // 差值即字形覆盖度：背景在两幅图里完全相同，会被抵消。
                const int ink = qBlue(withAtlas.pixel(x, y)) - qBlue(withoutAtlas.pixel(x, y));
                if (ink > 30) {
                    hit = true;
                    break;
                }
            }
            if (hit)
                ++covered;
        }
        profile.append(double(covered));
    }
    return profile;
}

double profileCorrelation(const QVector<double> &a, const QVector<double> &b)
{
    if (a.size() != b.size() || a.isEmpty())
        return 0.0;
    double ma = 0.0;
    double mb = 0.0;
    for (int i = 0; i < a.size(); ++i) {
        ma += a.at(i);
        mb += b.at(i);
    }
    ma /= a.size();
    mb /= b.size();
    double num = 0.0;
    double da = 0.0;
    double db = 0.0;
    for (int i = 0; i < a.size(); ++i) {
        const double x = a.at(i) - ma;
        const double y = b.at(i) - mb;
        num += x * y;
        da += x * x;
        db += y * y;
    }
    if (da <= 0.0 || db <= 0.0)
        return 0.0;
    return num / std::sqrt(da * db);
}

} // namespace

void TestSplashScene::percentReadoutMatchesAcrossDevicePixelRatios()
{
    // 参考：dpr=1.0 —— 逻辑像素与设备像素一致，采样天然正确。
    const QImage plain100 = renderSplashFrame(1.0, true);
    const QImage plain100NoAtlas = renderSplashFrame(1.0, false);
    const QImage plain150 = renderSplashFrame(1.5, true);
    const QImage plain150NoAtlas = renderSplashFrame(1.5, false);

    qreal cell = 0.0;
    Splash::Scene::renderDigitAtlas(1.0, &cell);
    const PercentRegion region = percentRegion(QSizeF(700.0, 430.0), cell);

    // 自检：喂了图集就必须真的画出读数，否则下面的相关性没有意义。
    const QVector<double> profile100 = inkColumnProfile(plain100, plain100NoAtlas, region, 1.0);
    double ink100 = 0.0;
    for (double v : profile100)
        ink100 += v;
    QVERIFY2(ink100 > 0.0, "读数区域没有任何墨迹 —— 读数根本没画出来？");

    // 正题：两种 dpr 下读数字形必须一致。
    const double correlation = profileCorrelation(
        profile100, inkColumnProfile(plain150, plain150NoAtlas, region, 1.5));
    QVERIFY2(correlation > 0.8,
             qPrintable(QStringLiteral("1.0x 与 1.5x 的读数字形不一致，correlation=%1")
                            .arg(correlation)));
}

QTEST_MAIN(TestSplashScene)
#include "tst_splashscene.moc"
