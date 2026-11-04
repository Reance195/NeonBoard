#include "SplashScene.h"

#include <QElapsedTimer>
#include <QDebug>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QtMath>

#include <algorithm>
#include <cmath>

// 主程序由 CMake 注入版本号；测试目标不注入，给个占位值免得编译不过。
#ifndef NEONBOARD_VERSION_STRING
#define NEONBOARD_VERSION_STRING "0.0.0"
#endif

namespace {

const QColor kCyan(34, 211, 238);
const QColor kPurple(168, 85, 247);
const QColor kPink(244, 114, 182);
const QColor kInk(230, 241, 255);
const QColor kMuted(124, 139, 161);

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(std::clamp(alpha, 0.0, 1.0));
    return color;
}

qreal wave(qreal t)
{
    return 0.5 + 0.5 * std::sin(t);
}

// 各图层的刷新间隔（秒）。慢的东西刷得少，省下来的就是 CPU。
constexpr qreal kDynamicInterval = 1.0 / 6.0;   // 辉光 + 扫描带 + 星星：动得很慢
constexpr qreal kScrollInterval = 1.0 / 12.0;   // 滚动网格：看得见位移
constexpr qreal kLogoInterval = 1.0 / 30.0;     // 轨道环 + 扫光：转得快
constexpr qreal kTitleGlitchPeriod = 4.3;
constexpr qreal kTitleGlitchWidth = 0.05;
constexpr qreal kGlyphPad = 18.0;                  // 字形辉光向外扩的余量
constexpr qreal kOrbitExtent = 165.0;              // 轨道环的半边长（含描边余量）
constexpr qreal kTitleCharPad = 12.0;              // 标题每个字左右的余量
constexpr qreal kTitleCharTop = 34.0;              // 标题每个字上方的余量
constexpr qreal kTitleCharHeight = 52.0;           // 标题每个字的高度（含出现时的下沉）

} // namespace

namespace Splash {

namespace {
bool g_profiling = false;
}

void Scene::setProfiling(bool enabled)
{
    g_profiling = enabled;
}

bool Scene::profiling()
{
    return g_profiling;
}

Scene::Scene()
{
    QRandomGenerator rng(0x4e454f4e);
    m_stars.reserve(150);
    for (int i = 0; i < 150; ++i) {
        m_stars.append(Star{
            rng.bounded(1.0),
            std::pow(rng.bounded(1.0), 1.5) * 0.62,
            0.5 + rng.bounded(1.0) * 1.6,
            rng.bounded(6.2831853),
            0.5 + rng.bounded(1.8),
        });
    }
    m_label = QStringLiteral("BOOT SEQUENCE");
}

QFont Scene::stageLabelFont()
{
    QFont labelFont(QStringLiteral("Microsoft YaHei UI"));
    labelFont.setPixelSize(10);
    labelFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.6);
    return labelFont;
}

QFont Scene::percentFont()
{
    QFont mono(QStringLiteral("Consolas"));
    mono.setPixelSize(10);
    return mono;
}

QImage Scene::renderStageLabel(const QString &text, qreal dpr, qreal *baselineOffset)
{
    const QFont font = stageLabelFont();
    const QFontMetricsF metrics(font);
    const qreal ascent = metrics.ascent();
    const qreal descent = metrics.descent();
    const qreal width = std::max(1.0, metrics.horizontalAdvance(text));
    const QSize pixels(qMax(1, qRound(width * dpr)),
                       qMax(1, qRound((ascent + descent) * dpr)));
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    painter.scale(dpr, dpr);
    painter.setFont(font);
    painter.setPen(kMuted);
    painter.drawText(QPointF(0.0, ascent), text);
    painter.end();
    if (baselineOffset)
        *baselineOffset = ascent;
    return image;
}

QImage Scene::renderDigitAtlas(qreal dpr, qreal *cellAdvance)
{
    const QFont font = percentFont();
    const QFontMetricsF metrics(font);
    const qreal advance = metrics.horizontalAdvance(QLatin1Char('0'));
    const qreal ascent = metrics.ascent();
    const qreal descent = metrics.descent();
    const QString chars = QStringLiteral("0123456789%");
    const QSize pixels(qMax(1, qRound(advance * chars.size() * dpr)),
                       qMax(1, qRound((ascent + descent) * dpr)));
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    painter.scale(dpr, dpr);
    painter.setFont(font);
    painter.setPen(kCyan);
    for (int i = 0; i < chars.size(); ++i)
        painter.drawText(QPointF(i * advance, ascent), QString(chars.at(i)));
    painter.end();
    if (cellAdvance)
        *cellAdvance = advance;
    return image;
}

void Scene::setStage(qreal progress, const QString &label)
{
    m_targetProgress = std::clamp(progress, 0.0, 1.0);
    if (!label.isEmpty() && label != m_label) {
        m_label = label;
        m_labelChangedAt = m_elapsed;
    }
}

void Scene::advance(qreal deltaSeconds)
{
    // delta 封顶 0.12s：渲染线程自己被拖慢时，动画放慢而不是瞬移。
    const qreal delta = std::clamp(deltaSeconds, 0.0, 0.12);
    m_elapsed += delta;
    m_fadeIn = std::min(1.0, m_fadeIn + delta * 5.0);
    m_progress += (m_targetProgress - m_progress) * std::clamp(delta * 6.0, 0.0, 1.0);
    m_progress = std::clamp(m_progress, 0.0, 1.0);
}

QPainterPath Scene::roundedClip(const QRectF &frame, qreal radius)
{
    QPainterPath clip;
    clip.addRoundedRect(frame, radius, radius);
    return clip;
}

void Scene::renderLayer(QImage &target, qreal dpr, const QRectF &origin,
                         const LayerTask &draw) const
{
    QPainter painter(&target);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    painter.scale(dpr, dpr);
    // 图层内部用「盒子局部坐标」：把场景逻辑坐标的原点挪到盒子左上角。
    painter.translate(-origin.left(), -origin.top());
    draw(painter);
    painter.end();
}

namespace {
/** 准备一块可复用的图层缓冲：尺寸不变就不重新分配（重新分配要付页错误的钱）。 */
void prepareLayer(QImage &image, const QSize &pixelSize)
{
    if (image.size() != pixelSize || image.isNull())
        image = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
}
} // namespace

void Scene::buildLayers(Layers &layers, const QRectF &frame, const QSizeF &sceneSize,
                        qreal dpr) const
{
    // 缓冲按**场景**尺寸分配（不是圆角内框）：图层整幅贴到窗口上，
    // 差那 14px 边距就会整幅缩放，底色与 HUD/进度条会对不齐。
    layers.pixelSize = QSize(qMax(1, qRound(sceneSize.width() * dpr)),
                              qMax(1, qRound(sceneSize.height() * dpr)));
    layers.dpr = dpr;
    layers.sceneSize = sceneSize;
    const QRectF sceneRect(QPointF(), sceneSize);

    // —— LOGO 几何（字形 + 轨道）——
    const QPointF center(frame.center().x(), frame.top() + frame.height() * 0.335);
    QFont logoFont(QStringLiteral("Segoe UI"));
    logoFont.setPixelSize(128);
    logoFont.setWeight(QFont::Black);
    QPainterPath glyph;
    glyph.addText(QPointF(0.0, 0.0), logoFont, QStringLiteral("N"));
    layers.glyphPath = glyph;
    layers.glyphBounds = glyph.boundingRect();
    layers.logoOrigin = QPointF(center.x() - layers.glyphBounds.center().x(),
                                 center.y() - layers.glyphBounds.center().y());
    layers.glyphBox = QRectF(layers.logoOrigin.x() + layers.glyphBounds.left() - kGlyphPad,
                              layers.logoOrigin.y() + layers.glyphBounds.top() - kGlyphPad,
                              layers.glyphBounds.width() + kGlyphPad * 2.0,
                              layers.glyphBounds.height() + kGlyphPad * 2.0);
    layers.orbitBox = QRectF(center.x() - kOrbitExtent, center.y() - kOrbitExtent,
                              kOrbitExtent * 2.0, kOrbitExtent * 2.0);

    const QSize glyphPixels(qMax(1, qRound(layers.glyphBox.width() * dpr)),
                            qMax(1, qRound(layers.glyphBox.height() * dpr)));

    // 字形路径是**字形局部坐标**（基线在原点），要画到场景位置得先平移 logoOrigin ——
    // 这正是改动前 QWidget 版本里的 painter.translate(center - bounds.center())。
    // 少这一下平移，字形会落在场景左上角、被图层缓冲裁掉 —— 表现为「LOGO 整段消失」。
    prepareLayer(layers.glyphGlow, glyphPixels);
    renderLayer(layers.glyphGlow, dpr, layers.glyphBox,
                [this, &layers](QPainter &painter) {
                    drawLogoGlow(painter, layers);
                });
    prepareLayer(layers.glyph, glyphPixels);
    renderLayer(layers.glyph, dpr, layers.glyphBox,
                [this, &layers](QPainter &painter) {
                    drawLogoFill(painter, layers);
                });
    prepareLayer(layers.glyphMask, glyphPixels);
    renderLayer(layers.glyphMask, dpr, layers.glyphBox,
                [this, &layers](QPainter &painter) {
                    painter.save();
                    painter.translate(layers.logoOrigin);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(Qt::white);
                    painter.drawPath(layers.glyphPath);
                    painter.restore();
                });

    // —— 静态层：HUD ——
    const QPainterPath clip = roundedClip(frame, 24.0);
    prepareLayer(layers.hud, layers.pixelSize);
    renderLayer(layers.hud, dpr, QRectF(QPointF(), layers.sceneSize),
                [this, &frame, &clip](QPainter &painter) {
                    painter.setClipPath(clip);
                    drawHud(painter, frame, 0.0);
                });

    // —— 静态层：标题的每一个字（出现动画靠逐字调透明度 + 下沉位移实现）——
    QFont titleFont(QStringLiteral("Segoe UI"));
    titleFont.setPixelSize(29);
    titleFont.setWeight(QFont::DemiBold);
    const QFontMetricsF titleMetrics(titleFont);
    const QString title = QStringLiteral("NEONBOARD");
    constexpr qreal spacing = 9.0;
    qreal total = 0.0;
    for (const QChar ch : title)
        total += titleMetrics.horizontalAdvance(ch) + spacing;
    total -= spacing;

    const qreal baseline = frame.top() + frame.height() * 0.560;

    qreal x = frame.center().x() - total / 2.0;
    for (int i = 0; i < title.size(); ++i) {
        const QChar ch = title.at(i);
        const qreal advance = titleMetrics.horizontalAdvance(ch);
        QPainterPath path;
        path.addText(QPointF(x, baseline), titleFont, QString(ch));
        layers.titlePaths.append(path);

        const QRectF rect(x - kTitleCharPad, baseline - kTitleCharTop,
                          advance + kTitleCharPad * 2.0, kTitleCharHeight);
        layers.titleCharRects.append(rect);
        const QSize pixels(qMax(1, qRound(rect.width() * dpr)),
                           qMax(1, qRound(rect.height() * dpr)));
        layers.titleChars.append([&]() {
            QImage image;
            prepareLayer(image, pixels);
            renderLayer(image, dpr, rect,
                        [this, &path, &baseline](QPainter &painter) {
                            // 标题路径是**场景坐标**（基线就在场景里），renderLayer 的画笔
                            // 已经是场景坐标 —— 这里不能再平移一次，否则整行会错位。
                            // 渐变是竖直的，逐字切出来与整行一致。
                            QLinearGradient fill(0.0, baseline - 26.0, 0.0, baseline);
                            fill.setColorAt(0.0, QColor(246, 254, 255));
                            fill.setColorAt(0.48, QColor(178, 246, 253));
                            fill.setColorAt(1.0, QColor(34, 211, 238));
                            painter.strokePath(path, QPen(withAlpha(kCyan, 0.10), 7.0));
                            painter.strokePath(path, QPen(withAlpha(kCyan, 0.20), 2.6));
                            painter.fillPath(path, fill);
                        });
            return image;
        }());
        x += advance + spacing;
    }

    layers.ready = true;
    layers.lastBackground = -1.0;
    layers.lastScroll = -1.0;
    layers.lastOrbits = -1.0;
    layers.lastSweep = -1.0;
    if (g_profiling) {
        qInfo() << "SPLASH buildLayers glyphElems" << layers.glyphPath.elementCount()
                << "bounds" << layers.glyphBounds
                << "box" << layers.glyphBox
                << "glyphImg" << layers.glyph.size()
                << "titleChars" << layers.titleChars.size()
                << "dpr" << dpr << "scene" << layers.sceneSize
                << "pixel" << layers.pixelSize;
    }
    // 静态背景只画一次
    refreshBackgroundStatic(layers, frame);
}

void Scene::refreshBackgroundStatic(Layers &layers, const QRectF &frame) const
{
    const QPainterPath clip = roundedClip(frame, 24.0);
    prepareLayer(layers.bgStatic, layers.pixelSize);
    renderLayer(layers.bgStatic, layers.dpr, QRectF(QPointF(), layers.sceneSize),
                [this, &frame, &clip](QPainter &painter) {
                    // 外框光晕在圆角裁剪之外（与改动前一致）
                    drawOuterGlow(painter, frame);
                    painter.setClipPath(clip);
                    drawBackdropBase(painter, frame);
                    drawBackdropScan(painter, frame);
                    drawGridRays(painter, frame);
                });
}

void Scene::refreshBackgroundDynamic(Layers &layers, const QRectF &frame, qreal t) const
{
    const QPainterPath clip = roundedClip(frame, 24.0);
    prepareLayer(layers.bgDynamic, layers.pixelSize);
    renderLayer(layers.bgDynamic, layers.dpr, QRectF(QPointF(), layers.sceneSize),
                [this, &frame, &clip, t](QPainter &painter) {
                    painter.setClipPath(clip);
                    drawBackdropGlow(painter, frame, t);
                    drawBackdropBand(painter, frame, t);
                    drawStars(painter, frame, t);
                });
    layers.lastBackground = t;
}

void Scene::refreshBackgroundScroll(Layers &layers, const QRectF &frame, qreal t) const
{
    const QPainterPath clip = roundedClip(frame, 24.0);
    prepareLayer(layers.bgScroll, layers.pixelSize);
    renderLayer(layers.bgScroll, layers.dpr, QRectF(QPointF(), layers.sceneSize),
                [this, &frame, &clip, t](QPainter &painter) {
                    painter.setClipPath(clip);
                    drawGridScroll(painter, frame, t);
                });
    layers.lastScroll = t;
}

void Scene::refreshProgressText(Layers &layers, const QRectF &frame) const
{
    Q_UNUSED(frame);
    Q_UNUSED(layers);
    // 渲染线程不再画任何文字：标签字图与数字图集都由主线程渲染好送进来
    // （见 renderStageLabel / renderDigitAtlas 的说明）。
}

void Scene::refreshLogoOrbits(Layers &layers, const QRectF &frame, qreal t) const
{
    const QPainterPath clip = roundedClip(frame, 24.0);
    const QSize pixels(qMax(1, qRound(layers.orbitBox.width() * layers.dpr)),
                       qMax(1, qRound(layers.orbitBox.height() * layers.dpr)));
    prepareLayer(layers.logoOrbits, pixels);
    renderLayer(layers.logoOrbits, layers.dpr, layers.orbitBox,
                [this, &frame, &clip, t](QPainter &painter) {
                    painter.setClipPath(clip);
                    drawLogoOrbits(painter, frame, t);
                });
    layers.lastOrbits = t;
}

void Scene::refreshLogoSweep(Layers &layers, const QRectF &frame, qreal t) const
{
    Q_UNUSED(frame);
    const QSize pixels(qMax(1, qRound(layers.glyphBox.width() * layers.dpr)),
                       qMax(1, qRound(layers.glyphBox.height() * layers.dpr)));
    prepareLayer(layers.logoSweep, pixels);
    renderLayer(layers.logoSweep, layers.dpr, layers.glyphBox,
                [this, &layers, t](QPainter &painter) {
                    drawLogoSweep(painter, layers, t);
                    // 只保留字形内部的扫光：SourceIn 用遮罩裁掉其余部分。
                    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    painter.setOpacity(1.0);
                    painter.drawImage(QRectF(layers.glyphBox.left(),
                                            layers.glyphBox.top(),
                                            layers.glyphBox.width(),
                                            layers.glyphBox.height()),
                                     layers.glyphMask);
                });
    layers.lastSweep = t;
}

void Scene::paint(QPainter &painter, const QSizeF &logicalSize, qreal globalAlpha) const
{
    const bool timing = g_profiling;
    QElapsedTimer frameTimer;
    if (timing)
        frameTimer.start();

    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    painter.setOpacity(std::clamp(globalAlpha, 0.0, 1.0) * m_fadeIn);

    constexpr qreal margin = 14.0;
    constexpr qreal radius = 24.0;
    const QRectF frame(margin, margin, logicalSize.width() - margin * 2.0,
                       logicalSize.height() - margin * 2.0);

    // 图层按设备像素比与尺寸缓存：尺寸或 dpr 变了才重建。
    const QPaintDevice *device = painter.device();
    if (device && device->width() > 0 && device->height() > 0) {
        const QSize pixelSize(device->width(), device->height());
        const qreal dpr = painter.transform().m11();
        if (!m_layers.ready || m_layers.pixelSize != pixelSize
            || !qFuzzyCompare(m_layers.dpr, dpr)) {
            buildLayers(m_layers, frame, logicalSize, dpr);
        }
    }

    Layers &layers = m_layers;
    if (!layers.ready)
        return;

    const qreal t = m_elapsed;
    QElapsedTimer phaseTimer;
    if (timing) {
        phaseTimer.start();
        m_profile.prologueNs += phaseTimer.nsecsElapsed(); phaseTimer.restart();
    }
    // 到点才重画慢速图层。
    if (timing) {
        QElapsedTimer refreshTimer;
        refreshTimer.start();
        if (t - layers.lastBackground >= kDynamicInterval) {
            refreshBackgroundDynamic(layers, frame, t);
            m_profile.refreshBgNs += refreshTimer.nsecsElapsed(); refreshTimer.restart();
        }
        if (t - layers.lastScroll >= kScrollInterval) {
            refreshBackgroundScroll(layers, frame, t);
            m_profile.refreshScrollNs += refreshTimer.nsecsElapsed(); refreshTimer.restart();
        }
        if (t - layers.lastOrbits >= kLogoInterval) {
            refreshLogoOrbits(layers, frame, t);
            m_profile.refreshLogoNs += refreshTimer.nsecsElapsed(); refreshTimer.restart();
        }
        if (t - layers.lastSweep >= kLogoInterval) {
            refreshLogoSweep(layers, frame, t);
            m_profile.refreshLogoNs += refreshTimer.nsecsElapsed(); refreshTimer.restart();
        }
    } else {
        if (t - layers.lastBackground >= kDynamicInterval)
            refreshBackgroundDynamic(layers, frame, t);
        if (t - layers.lastScroll >= kScrollInterval)
            refreshBackgroundScroll(layers, frame, t);
        if (t - layers.lastOrbits >= kLogoInterval)
            refreshLogoOrbits(layers, frame, t);
        if (t - layers.lastSweep >= kLogoInterval)
            refreshLogoSweep(layers, frame, t);
    }

    QElapsedTimer sectionTimer;
    if (timing)
        sectionTimer.start();

    painter.save();

    // —— 贴图一律在**设备坐标**下做 ——
    // 带缩放变换的 drawImage 走的是通用慢路径（实测一个 75x78 的小图要 0.5ms），
    // 30fps 下光贴图就吃掉 8ms。图层本来就是按设备像素画的，1:1 贴回去即可。
    const qreal dpr = layers.dpr;
    const auto toDevice = [dpr](const QRectF &rect) {
        return QRectF(rect.left() * dpr, rect.top() * dpr,
                      rect.width() * dpr, rect.height() * dpr);
    };
    painter.resetTransform();

    // 背景三层：静态（光晕/底色/扫描线/放射网格）→ 慢速（辉光/扫描带/星星）→ 滚动网格线
    painter.drawImage(QPointF(0, 0), layers.bgStatic);
    painter.drawImage(QPointF(0, 0), layers.bgDynamic);
    painter.drawImage(QPointF(0, 0), layers.bgScroll);
    if (timing)
        m_profile.blitNs += sectionTimer.nsecsElapsed(); sectionTimer.restart();

    // LOGO：轨道 → 辉光（按脉动调透明度）→ 字形 → 扫光 → 故障偏移
    painter.drawImage(toDevice(layers.orbitBox), layers.logoOrbits);
    const qreal glowPulse = 0.82 + 0.18 * wave(t * 2.6);
    painter.save();
    painter.setOpacity(painter.opacity() * glowPulse);
    painter.drawImage(toDevice(layers.glyphBox), layers.glyphGlow);
    painter.restore();
    painter.drawImage(toDevice(layers.glyphBox), layers.glyph);
    painter.drawImage(toDevice(layers.glyphBox), layers.logoSweep);
    if (timing)
        m_profile.logoNs += sectionTimer.nsecsElapsed(); sectionTimer.restart();

    // 标题：逐字贴缓存好的字（出现动画 = 透明度 + 下沉位移）
    const bool glitch = std::fmod(t, kTitleGlitchPeriod) < kTitleGlitchWidth;
    for (int i = 0; i < layers.titleChars.size(); ++i) {
        const qreal appear = std::clamp((t - 0.24 - i * 0.05) * 3.4, 0.0, 1.0);
        if (appear <= 0.0)
            continue;
        const qreal rise = (1.0 - appear) * 7.0;
        QRectF rect = layers.titleCharRects.at(i);
        rect.moveTop(rect.top() + rise);
        painter.save();
        painter.setOpacity(painter.opacity() * appear);
        painter.drawImage(toDevice(rect), layers.titleChars.at(i));
        painter.restore();
    }
    painter.restore();

    // —— 逻辑坐标：故障偏移 + 标题分隔线 + 进度条（跟着进度走，必须实时画）——
    painter.save();

    // LOGO 故障偏移：粉色/青色的字形副本（很少发生，实时画即可）
    drawLogoGlitch(painter, layers, t);

    // 标题故障偏移：字形路径的粉色/青色副本
    if (glitch) {
        for (int i = 0; i < layers.titlePaths.size(); ++i) {
            const qreal appear = std::clamp((t - 0.24 - i * 0.05) * 3.4, 0.0, 1.0);
            if (appear <= 0.0)
                continue;
            const qreal rise = (1.0 - appear) * 7.0;
            const QPainterPath &path = layers.titlePaths.at(i);
            painter.save();
            painter.translate(2.5, rise);
            painter.fillPath(path, withAlpha(kPink, 0.30));
            painter.restore();
            painter.save();
            painter.translate(-2.5, rise);
            painter.fillPath(path, withAlpha(kCyan, 0.38));
            painter.restore();
        }
    }

    drawTitleRule(painter, frame);

    refreshProgressText(layers, frame);
    drawProgress(painter, layers, frame, t);
    if (timing)
        m_profile.liveNs += sectionTimer.nsecsElapsed(); sectionTimer.restart();
    painter.restore();

    // —— HUD（静态层）——
    painter.save();
    painter.resetTransform();
    painter.drawImage(QPointF(0, 0), layers.hud);
    painter.restore();
    if (timing)
        m_profile.tailNs += sectionTimer.nsecsElapsed(); sectionTimer.restart();

    // 边框（与改动前一致：画在所有内容之上）
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(withAlpha(kCyan, 0.28 + 0.12 * wave(m_elapsed * 1.4)), 1.0));
    painter.drawRoundedRect(frame, radius, radius);
    painter.setPen(QPen(withAlpha(kCyan, 0.08), 1.0));
    painter.drawRoundedRect(frame.adjusted(3.0, 3.0, -3.0, -3.0), radius - 3.0, radius - 3.0);

    if (timing) {
        m_profile.frameNs += frameTimer.nsecsElapsed();
        ++m_profile.samples;
    }
}

// ---------------------------------------------------------------- 背景

void Scene::drawOuterGlow(QPainter &painter, const QRectF &frame) const
{
    constexpr qreal radius = 24.0;
    painter.setBrush(Qt::NoBrush);
    for (int i = 8; i >= 1; --i) {
        const qreal spread = i * 3.0;
        painter.setPen(QPen(withAlpha(kCyan, 0.055 - i * 0.005), 1.2));
        painter.drawRoundedRect(frame.adjusted(-spread, -spread, spread, spread),
                                radius + spread, radius + spread);
    }
}

void Scene::drawBackdropBase(QPainter &painter, const QRectF &frame) const
{
    QLinearGradient background(frame.topLeft(), frame.bottomRight());
    background.setColorAt(0.0, QColor(8, 13, 28));
    background.setColorAt(0.42, QColor(4, 6, 13));
    background.setColorAt(1.0, QColor(19, 7, 31));
    painter.fillRect(frame, background);
}

void Scene::drawBackdropGlow(QPainter &painter, const QRectF &frame, qreal t) const
{
    const qreal pulse = wave(t * 1.7);

    QRadialGradient core(frame.center().x(), frame.top() + frame.height() * 0.34,
                         frame.width() * 0.62);
    core.setColorAt(0.0, withAlpha(kCyan, 0.150 + 0.060 * pulse));
    core.setColorAt(0.45, withAlpha(kPurple, 0.090));
    core.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.fillRect(frame, core);

    QRadialGradient edge(frame.right(), frame.bottom(), frame.width() * 0.72);
    edge.setColorAt(0.0, withAlpha(kPink, 0.075));
    edge.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.fillRect(frame, edge);
}

void Scene::drawBackdropScan(QPainter &painter, const QRectF &frame) const
{
    painter.save();
    painter.setPen(QPen(QColor(255, 255, 255, 5), 1.0));
    for (qreal y = frame.top(); y < frame.bottom(); y += 3.0)
        painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
    painter.restore();
}

void Scene::drawBackdropBand(QPainter &painter, const QRectF &frame, qreal t) const
{
    const qreal bandY = frame.top() + std::fmod(t * 0.085, 1.0) * frame.height();
    constexpr qreal bandH = 72.0;
    QLinearGradient band(0.0, bandY - bandH, 0.0, bandY + bandH);
    band.setColorAt(0.0, QColor(34, 211, 238, 0));
    band.setColorAt(0.5, QColor(34, 211, 238, 15));
    band.setColorAt(1.0, QColor(34, 211, 238, 0));
    painter.fillRect(QRectF(frame.left(), bandY - bandH, frame.width(), bandH * 2.0), band);
}

void Scene::drawGridRays(QPainter &painter, const QRectF &frame) const
{
    const qreal horizon = frame.top() + frame.height() * 0.60;
    const qreal cx = frame.center().x();

    painter.save();
    painter.setClipRect(QRectF(frame.left(), horizon, frame.width(), frame.bottom() - horizon));

    painter.setPen(QPen(withAlpha(kCyan, 0.13), 1.0));
    for (int i = -12; i <= 12; ++i) {
        const qreal bottomX = cx + i * frame.width() * 0.085;
        painter.drawLine(QPointF(cx, horizon), QPointF(bottomX, frame.bottom()));
    }
    painter.restore();

    QLinearGradient horizonLine(frame.left(), 0.0, frame.right(), 0.0);
    horizonLine.setColorAt(0.0, QColor(34, 211, 238, 0));
    horizonLine.setColorAt(0.5, QColor(34, 211, 238, 140));
    horizonLine.setColorAt(1.0, QColor(34, 211, 238, 0));
    painter.setPen(QPen(QBrush(horizonLine), 1.4));
    painter.drawLine(QPointF(frame.left(), horizon), QPointF(frame.right(), horizon));
}

void Scene::drawGridScroll(QPainter &painter, const QRectF &frame, qreal t) const
{
    const qreal horizon = frame.top() + frame.height() * 0.60;

    painter.save();
    painter.setClipRect(QRectF(frame.left(), horizon, frame.width(), frame.bottom() - horizon));

    const qreal scroll = std::fmod(t * 0.30, 1.0);
    for (int i = 0; i < 16; ++i) {
        const qreal k = (i + scroll) / 16.0;
        const qreal y = horizon + std::pow(k, 2.1) * (frame.bottom() - horizon);
        painter.setPen(QPen(withAlpha(kCyan, 0.03 + 0.16 * k * k), 1.0));
        painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
    }
    painter.restore();
}

void Scene::drawStars(QPainter &painter, const QRectF &frame, qreal t) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    for (const Star &star : m_stars) {
        const qreal x = frame.left() + star.x * frame.width();
        const qreal y = frame.top() + star.y * frame.height();
        const qreal twinkle = 0.45 + 0.55 * std::sin(t * star.speed + star.phase);
        painter.setBrush(withAlpha(kInk, 0.16 + 0.42 * twinkle));
        painter.drawEllipse(QPointF(x, y), star.size, star.size);
    }
    painter.restore();
}

// ---------------------------------------------------------------- LOGO

void Scene::drawLogoGlow(QPainter &painter, Layers &layers) const
{
    painter.save();
    // 字形路径是字形局部坐标（基线在原点）→ 先平移到场景位置。
    painter.translate(layers.logoOrigin);
    // 辉光的 10 圈描边 alpha 都乘同一个脉动系数，所以整层按脉动调透明度即可复现原效果。
    for (int i = 10; i >= 1; --i) {
        const qreal alpha = 0.045 * (1.0 - (i - 1) / 10.0 * 0.7);
        painter.strokePath(layers.glyphPath, QPen(withAlpha(kCyan, alpha), i * 2.4,
                                                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    painter.restore();
}

void Scene::drawLogoFill(QPainter &painter, Layers &layers) const
{
    painter.save();
    painter.translate(layers.logoOrigin);

    const QRectF bounds = layers.glyphBounds;
    QLinearGradient fill(bounds.topLeft(), bounds.bottomRight());
    fill.setColorAt(0.00, QColor(236, 254, 255));
    fill.setColorAt(0.38, QColor(103, 232, 249));
    fill.setColorAt(0.72, QColor(34, 211, 238));
    fill.setColorAt(1.00, QColor(168, 85, 247));
    painter.fillPath(layers.glyphPath, fill);
    painter.strokePath(layers.glyphPath, QPen(withAlpha(QColor(255, 255, 255), 0.35), 1.2));

    painter.restore();
}

void Scene::drawLogoOrbits(QPainter &painter, const QRectF &frame, qreal t) const
{
    const QPointF center(frame.center().x(), frame.top() + frame.height() * 0.335);

    struct Orbit
    {
        qreal rx;
        qreal ry;
        qreal tilt;
        qreal spin;
        qreal width;
        qreal alpha;
    };

    const Orbit orbits[] = {
        {128.0, 38.0, -16.0, 13.0, 1.2, 0.26},
        {96.0, 30.0, 58.0, -19.0, 1.0, 0.16},
        {150.0, 46.0, 96.0, 8.0, 1.0, 0.12},
    };

    for (const Orbit &orbit : orbits) {
        painter.save();
        painter.translate(center);
        painter.rotate(orbit.tilt + std::sin(t * 0.35 + orbit.tilt) * 5.0);
        painter.setBrush(Qt::NoBrush);

        QPen pen(withAlpha(kCyan, orbit.alpha), orbit.width);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);

        const qreal start = std::fmod(t * orbit.spin * 12.0, 360.0) * 16.0;
        painter.drawArc(QRectF(-orbit.rx, -orbit.ry, orbit.rx * 2.0, orbit.ry * 2.0),
                        start, 255.0 * 16.0);

        const qreal angle = qDegreesToRadians(start / 16.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(withAlpha(kPink, 0.85));
        painter.drawEllipse(QPointF(orbit.rx * std::cos(angle), orbit.ry * std::sin(angle)),
                            2.1, 2.1);
        painter.setBrush(withAlpha(kCyan, 0.65));
        const qreal tail = qDegreesToRadians(start / 16.0 + 255.0);
        painter.drawEllipse(QPointF(orbit.rx * std::cos(tail), orbit.ry * std::sin(tail)),
                            1.4, 1.4);
        painter.restore();
    }
}

void Scene::drawLogoSweep(QPainter &painter, Layers &layers, qreal t) const
{
    painter.save();
    painter.translate(layers.logoOrigin);

    const QRectF bounds = layers.glyphBounds;
    const qreal sweepY = bounds.top() - 40.0
                         + std::fmod(t * 105.0, bounds.height() + 140.0);
    QLinearGradient sweep(0.0, sweepY - 28.0, 0.0, sweepY + 28.0);
    sweep.setColorAt(0.0, QColor(255, 255, 255, 0));
    sweep.setColorAt(0.5, QColor(255, 255, 255, 135));
    sweep.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(bounds.adjusted(-30.0, -60.0, 30.0, 60.0), sweep);

    painter.restore();
}

void Scene::drawLogoGlitch(QPainter &painter, Layers &layers, qreal t) const
{
    const qreal glitchPhase = std::fmod(t, 3.7);
    if (glitchPhase >= 0.05)
        return;
    painter.save();
    painter.translate(layers.logoOrigin);
    painter.save();
    painter.translate(3.0, 0.0);
    painter.fillPath(layers.glyphPath, withAlpha(kPink, 0.30));
    painter.restore();
    painter.save();
    painter.translate(-3.0, 0.0);
    painter.fillPath(layers.glyphPath, withAlpha(kCyan, 0.34));
    painter.restore();
    painter.restore();
}

// ---------------------------------------------------------------- 标题

void Scene::drawTitleRule(QPainter &painter, const QRectF &frame) const
{
    const qreal baseline = frame.top() + frame.height() * 0.560;
    const qreal ruleY = baseline + 21.0;
    const qreal half = 16.0 + 150.0 * std::clamp(m_progress * 1.35, 0.0, 1.0);
    QLinearGradient rule(frame.center().x() - half, 0.0, frame.center().x() + half, 0.0);
    rule.setColorAt(0.0, withAlpha(kCyan, 0.0));
    rule.setColorAt(0.5, withAlpha(kCyan, 0.60));
    rule.setColorAt(1.0, withAlpha(kCyan, 0.0));
    painter.setPen(QPen(QBrush(rule), 1.0));
    painter.drawLine(QPointF(frame.center().x() - half, ruleY),
                     QPointF(frame.center().x() + half, ruleY));
    // 「PERSONAL WORKBENCH // LOADING」是固定文字，已经烘焙进 HUD 层。
}

// ---------------------------------------------------------------- 进度条 / HUD

void Scene::drawProgress(QPainter &painter, Layers &layers, const QRectF &frame, qreal t) const
{
    const qreal barWidth = frame.width() * 0.52;
    const QRectF track(frame.center().x() - barWidth / 2.0,
                       frame.top() + frame.height() * 0.815,
                       barWidth, 3.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 14));
    painter.drawRoundedRect(track, 1.5, 1.5);

    const qreal filled = track.width() * m_progress;
    if (filled > 0.5) {
        QLinearGradient fill(track.topLeft(), track.topRight());
        fill.setColorAt(0.0, kCyan);
        fill.setColorAt(0.55, QColor(103, 232, 249));
        fill.setColorAt(0.82, kPurple);
        fill.setColorAt(1.0, kPink);
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(track.left(), track.top(), filled, track.height()),
                                1.5, 1.5);

        const QPointF head(track.left() + filled, track.center().y());
        QRadialGradient halo(head, 24.0);
        halo.setColorAt(0.0, QColor(255, 255, 255, 150));
        halo.setColorAt(0.22, QColor(103, 232, 249, 110));
        halo.setColorAt(1.0, QColor(34, 211, 238, 0));
        painter.setBrush(halo);
        painter.drawEllipse(head, 24.0, 24.0);
    }

    // 百分比：用数字图集拼（渲染线程不碰字体）
    if (!layers.digitAtlas.isNull() && layers.digitAdvance > 0.0) {
        const QString percent = QString::number(qRound(m_progress * 100.0)) + QStringLiteral("%");
        const qreal cell = layers.digitAdvance;
        const qreal ascent = layers.digitAscent;
        const qreal baseline = track.top() + 4.0;
        painter.save();
        painter.setOpacity(painter.opacity() * 0.90);
        for (int i = 0; i < percent.size(); ++i) {
            const QChar ch = percent.at(i);
            const int index = ch == QLatin1Char('%') ? 10 : ch.digitValue();
            if (index < 0)
                continue;
            const QRectF source(index * cell, 0.0, cell, layers.digitAtlas.height() / layers.dpr);
            const QRectF target(track.right() + 14.0 + i * cell, baseline - ascent,
                                cell, layers.digitAtlas.height() / layers.dpr);
            painter.drawImage(target, layers.digitAtlas, source);
        }
        painter.restore();
    }

    // 阶段标签：主线程渲染好的字图，透明度按「基础 0.68 + 闪光 0.32」折算
    if (!layers.stageLabel.isNull()) {
        const qreal flash = std::clamp(1.0 - (t - m_labelChangedAt) * 2.2, 0.0, 1.0);
        painter.save();
        painter.setOpacity(painter.opacity() * (0.68 + 0.32 * flash));
        painter.drawImage(QRectF(track.left(), track.bottom() + 22.0 - layers.stageLabelAscent,
                                 layers.stageLabel.width() / layers.dpr,
                                 layers.stageLabel.height() / layers.dpr),
                          layers.stageLabel);
        painter.restore();
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(withAlpha(kCyan, 0.35 + 0.55 * wave(t * 5.0)));
    painter.drawEllipse(QPointF(track.left() - 11.0, track.center().y()), 2.0, 2.0);
}

void Scene::drawHud(QPainter &painter, const QRectF &frame, qreal) const
{
    QFont hud(QStringLiteral("Consolas"));
    hud.setPixelSize(9);
    painter.setFont(hud);
    painter.setPen(withAlpha(kMuted, 0.55));

    const qreal topLine = frame.top() + 27.0;
    painter.drawText(QPointF(frame.left() + 42.0, topLine), QStringLiteral("NB://BOOT.SEQUENCE"));
    painter.drawText(QRectF(frame.left(), topLine - 9.0, frame.width() - 42.0, 12.0),
                     Qt::AlignRight | Qt::AlignTop, QStringLiteral("v" NEONBOARD_VERSION_STRING));

    painter.setPen(withAlpha(kMuted, 0.36));
    painter.drawText(QPointF(frame.left() + 42.0, frame.bottom() - 24.0),
                     QStringLiteral("PERSONAL WORKBENCH"));
    painter.drawText(QRectF(frame.left(), frame.bottom() - 33.0, frame.width() - 42.0, 12.0),
                     Qt::AlignRight | Qt::AlignTop, QStringLiteral("CTRL+SHIFT+SPACE"));

    // 标题下方的副标题：固定文字，烘焙在这一层里（原来每帧重排一次文字，很贵）
    {
        const qreal ruleY = frame.top() + frame.height() * 0.560 + 21.0;
        QFont sub(QStringLiteral("Segoe UI"));
        sub.setPixelSize(10);
        sub.setLetterSpacing(QFont::AbsoluteSpacing, 3.2);
        painter.setFont(sub);
        painter.setPen(withAlpha(kMuted, 0.72));
        painter.drawText(QRectF(frame.left(), ruleY + 11.0, frame.width(), 16.0),
                         Qt::AlignHCenter | Qt::AlignTop,
                         QStringLiteral("PERSONAL WORKBENCH   //   LOADING"));
    }

    painter.setPen(QPen(withAlpha(kCyan, 0.50), 1.4));
    constexpr qreal inset = 20.0;
    constexpr qreal len = 14.0;
    const qreal left = frame.left() + inset;
    const qreal right = frame.right() - inset;
    const qreal top = frame.top() + inset;
    const qreal bottom = frame.bottom() - inset;
    painter.drawLine(QPointF(left, top), QPointF(left + len, top));
    painter.drawLine(QPointF(left, top), QPointF(left, top + len));
    painter.drawLine(QPointF(right, top), QPointF(right - len, top));
    painter.drawLine(QPointF(right, top), QPointF(right, top + len));
    painter.drawLine(QPointF(left, bottom), QPointF(left + len, bottom));
    painter.drawLine(QPointF(left, bottom), QPointF(left, bottom - len));
    painter.drawLine(QPointF(right, bottom), QPointF(right - len, bottom));
    painter.drawLine(QPointF(right, bottom), QPointF(right, bottom - len));

    painter.setPen(QPen(withAlpha(kCyan, 0.16), 1.0));
    for (int i = 0; i < 14; ++i) {
        const qreal y = frame.top() + 62.0 + i * 15.0;
        const qreal tick = (i % 4 == 0) ? 8.0 : 4.0;
        painter.drawLine(QPointF(frame.left() + 10.0, y),
                         QPointF(frame.left() + 10.0 + tick, y));
        painter.drawLine(QPointF(frame.right() - 10.0, y),
                         QPointF(frame.right() - 10.0 - tick, y));
    }
}

} // namespace Splash
