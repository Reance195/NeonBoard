#pragma once

#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <functional>

class QPainter;

namespace Splash {

/** 一颗星：位置与闪烁相位，构造后不再变化。 */
struct Star
{
    qreal x;
    qreal y;
    qreal size;
    qreal phase;
    qreal speed;
};

/**
 * 启动闪屏的画面与状态机（纯逻辑：不持有窗口、不碰线程）。
 *
 * 为什么要从窗口里拆出来（DX-39）：
 *  1) 渲染搬到专属线程之后，状态机仍是可单测的纯逻辑 —— 阶段推进、进度条缓动、
 *     标签切换这三条不用起窗口就能验。
 *  2) 「进度条卡顿」与「动画卡顿」原本是**同一个**原因（都绑在主线程，主线程一同步
 *     阻塞就一起停）。拆开后，动画与进度条的推进由渲染线程自己跑，主线程只推阶段值。
 *
 * 为什么内部还分了「图层」（DX-39 的第二版）：
 *  闪屏要**一直动**，就得一直重画；但逐帧重画那些一个像素都不会变的东西
 *  （底色渐变、扫描线、放射网格、LOGO 字形、标题、HUD）纯属浪费 ——
 *  实测每帧 46ms（其中 44ms 画的是静态内容），60fps 定时器根本跑不满，
 *  渲染线程被主线程一挤就出现 200ms+ 的停顿（画面还是会冻住），CPU 也翻倍。
 *
 *  现在按「变化速度」分层缓存：
 *  - 静态层（底色 + 扫描线 + 放射网格、LOGO 字形与辉光、标题每个字、HUD）：只画一次；
 *  - 慢速层（径向辉光 + 扫描带 + 星星）：约 6fps 刷新；
 *  - 中速层（滚动网格线）：约 12fps 刷新；
 *  - 快层（LOGO 轨道环 + 扫光）：约 30fps 刷新；
 *  - 每帧实时画：进度条、百分比、阶段标签、标题分隔线、边框。
 *  视觉不变（同一套绘制代码，只是换了承载），每帧成本从 46ms 降到约 5ms。
 *
 * 坐标约定（DX-39 踩过的坑，改这块前务必读）：
 *  renderLayer() 把画笔放在**场景坐标**里（device = scale·(user − origin)）。
 *  于是：LOGO 字形路径是「字形局部坐标（基线在原点）」→ 画之前必须 translate(logoOrigin)；
 *  标题路径是**场景坐标**（addText 时就带上了基线位置）→ 画之前**不能再平移**。
 *  两种约定混用时不会报错、也不会崩：元素只是被画到图层缓冲外面裁没了 ——
 *  症状是「LOGO 与标题整段消失」，看起来像字体/线程问题，其实是坐标系问题。
 */
class Scene
{
public:
    /** 分段绘制耗时（诊断用；setProfiling(false) 时不计时，开销可忽略）。 */
    struct Profile
    {
        // 全部按纳秒累计：QElapsedTimer::restart() 只给整毫秒，
        // 逐段截断会把小数全丢掉，六段加起来能和整帧差出好几毫秒。
        qint64 refreshBgNs = 0;    // 背景层重建累计
        qint64 refreshScrollNs = 0; // 滚动网格层重建累计
        qint64 refreshLogoNs = 0;  // LOGO 动态层重建累计
        qint64 frameNs = 0;        // 整帧累计
        qint64 prologueNs = 0;
        qint64 blitNs = 0;         // 背景层贴图累计
        qint64 logoNs = 0;         // LOGO 贴图累计
        qint64 liveNs = 0;         // 标题 + 进度条累计
        qint64 tailNs = 0;         // HUD 贴图 + 边框累计
        // 背景刷新内部拆解
        qint64 glowNs = 0;
        qint64 backdropNs = 0;
        qint64 gridNs = 0;
        int samples = 0;
    };

    Scene();

    /** 阶段标签用的字体（主线程与渲染线程共用同一个定义）。 */
    static QFont stageLabelFont();
    /** 百分比用的等宽字体。 */
    static QFont percentFont();

    /**
     * 渲染阶段标签的字图。
     *
     * **必须在主线程调用**：QML 引擎启动时会长时间持有字体库锁，
     * 渲染线程这时去画字会被整段阻塞（DX-39 实测：一次画字 = 250ms 画面冻结）。
     * 所以文字一律由主线程渲染好，渲染线程只负责贴图。
     */
    static QImage renderStageLabel(const QString &text, qreal dpr, qreal *baselineOffset);

    /**
     * 渲染百分比数字图集（0-9 与 %，等宽字体每格等宽）。
     * 同样在主线程渲染一次，渲染线程按格贴图拼出百分比。
     */
    static QImage renderDigitAtlas(qreal dpr, qreal *cellAdvance);

    /** 打开分段计时（全局开关：一个闪屏只有一个场景）。 */
    static void setProfiling(bool enabled);
    static bool profiling();

    /** 推进到某个阶段：设定进度目标与阶段标签。空标签表示不改标签。 */
    void setStage(qreal progress, const QString &label);
    /** 主线程把渲染好的标签字图一起送进来（渲染线程不碰字体）。 */
    void setStageLabelImage(const QImage &image, qreal ascent) {
        m_layers.stageLabel = image;
        m_layers.stageLabelAscent = ascent;
    }
    /** 主线程把数字图集送进来。 */
    void setDigitAtlas(const QImage &atlas, qreal advance, qreal ascent) {
        m_layers.digitAtlas = atlas;
        m_layers.digitAdvance = advance;
        m_layers.digitAscent = ascent;
    }

    /** 按经过的秒数推进动画时钟与进度条缓动。 */
    void advance(qreal deltaSeconds);

    /**
     * 画一帧。
     * @param logicalSize 窗口的逻辑尺寸（与设备像素无关）。
     * @param globalAlpha 整体淡出系数（1 = 完全不透明，0 = 全透明）。
     */
    void paint(QPainter &painter, const QSizeF &logicalSize, qreal globalAlpha) const;

    qreal progress() const { return m_progress; }
    qreal targetProgress() const { return m_targetProgress; }
    QString label() const { return m_label; }
    qreal elapsed() const { return m_elapsed; }
    /** 标签最近一次变化距今多少秒（标签切换后会闪一下，用它做衰减）。 */
    qreal secondsSinceLabelChanged() const { return m_elapsed - m_labelChangedAt; }
    /** 最近一帧的分段耗时。 */
    const Profile &profile() const { return m_profile; }

private:
    // —— 图层 ——
    struct Layers
    {
        QSize pixelSize;
        QSizeF sceneSize;      // 场景逻辑尺寸（= 窗口尺寸，含边距）
        qreal dpr = 1.0;
        bool ready = false;

        QImage bgStatic;    // 外框光晕 + 底色 + 扫描线 + 放射网格 + 地平线（静态）
        QImage bgDynamic;   // 径向辉光 + 扫描带 + 星星（慢速层）
        QImage bgScroll;    // 滚动网格线（中速层）
        QImage logoOrbits;   // LOGO 轨道环（中速层）
        QImage logoSweep;    // LOGO 扫光（中速层）
        QImage glyphMask;    // LOGO 字形遮罩（静态）
        QImage glyphGlow;    // LOGO 字形辉光（静态，脉动靠每帧调透明度）
        QImage glyph;        // LOGO 字形填充 + 描边（静态）
        QImage hud;          // HUD 文字与角标（静态）
        QImage digitAtlas;      // 数字图集（主线程渲染一次）
        QImage stageLabel;      // 阶段标签字图（主线程渲染好送进来）
        qreal digitAdvance = 0.0;
        qreal digitAscent = 0.0;
        qreal stageLabelAscent = 0.0;
        QRectF percentTextRect;
        QString progressLabelText;
        QString percentTextValue;
        QVector<QImage> titleChars;      // 标题每个字（静态）
        QVector<QRectF> titleCharRects;   // 标题每个字的位置（逻辑坐标）
        QVector<QPainterPath> titlePaths; // 标题每个字的路径（画故障偏移用）

        QPainterPath glyphPath;  // LOGO 字形路径（画故障偏移用）
        QRectF glyphBounds;      // 字形自身包围盒（字形局部坐标）
        QPointF logoOrigin;      // 字形局部原点在场景逻辑坐标里的位置
        QRectF glyphBox;         // 字形层在场景逻辑坐标里的位置
        QRectF orbitBox;         // 轨道层在场景逻辑坐标里的位置
        qreal lastBackground = -1.0;
        qreal lastOrbits = -1.0;
        qreal lastSweep = -1.0;
        qreal lastScroll = -1.0;
    };

    using LayerTask = std::function<void(QPainter &)>;

    // —— 绘制子过程（内容与改动前完全一致，只是拆开以便分别缓存）——
    void drawOuterGlow(QPainter &painter, const QRectF &frame) const;
    void drawBackdropBase(QPainter &painter, const QRectF &frame) const;
    void drawBackdropGlow(QPainter &painter, const QRectF &frame, qreal t) const;
    void drawBackdropScan(QPainter &painter, const QRectF &frame) const;
    void drawBackdropBand(QPainter &painter, const QRectF &frame, qreal t) const;
    void drawGridRays(QPainter &painter, const QRectF &frame) const;
    void drawGridScroll(QPainter &painter, const QRectF &frame, qreal t) const;
    void drawStars(QPainter &painter, const QRectF &frame, qreal t) const;
    void drawLogoGlow(QPainter &painter, Layers &layers) const;
    void drawLogoFill(QPainter &painter, Layers &layers) const;
    void drawLogoOrbits(QPainter &painter, const QRectF &frame, qreal t) const;
    void drawLogoSweep(QPainter &painter, Layers &layers, qreal t) const;
    void drawLogoGlitch(QPainter &painter, Layers &layers, qreal t) const;
    void drawTitleRule(QPainter &painter, const QRectF &frame) const;
    void drawProgress(QPainter &painter, Layers &layers, const QRectF &frame, qreal t) const;
    void drawHud(QPainter &painter, const QRectF &frame, qreal t) const;

    // —— 图层管理 ——
    void buildLayers(Layers &layers, const QRectF &frame, const QSizeF &sceneSize, qreal dpr) const;
    void refreshBackgroundStatic(Layers &layers, const QRectF &frame) const;
    void refreshBackgroundDynamic(Layers &layers, const QRectF &frame, qreal t) const;
    void refreshBackgroundScroll(Layers &layers, const QRectF &frame, qreal t) const;
    void refreshProgressText(Layers &layers, const QRectF &frame) const;
    void refreshLogoOrbits(Layers &layers, const QRectF &frame, qreal t) const;
    void refreshLogoSweep(Layers &layers, const QRectF &frame, qreal t) const;
    void renderLayer(QImage &target, qreal dpr, const QRectF &origin,
                     const LayerTask &draw) const;
    static QPainterPath roundedClip(const QRectF &frame, qreal radius);

    QVector<Star> m_stars;
    qreal m_elapsed = 0.0;
    qreal m_progress = 0.02;
    qreal m_targetProgress = 0.02;
    qreal m_fadeIn = 0.0;
    qreal m_labelChangedAt = -10.0;
    QString m_label;
    mutable Profile m_profile;
    mutable Layers m_layers;
};

} // namespace Splash
