#include "SplashWindow.h"

#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QScreen>
#include <QThread>
#include <QtMath>

#include <algorithm>
#include <cmath>

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

} // namespace

SplashWindow::SplashWindow(QWidget *parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                         | Qt::WindowTransparentForInput)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFixedSize(700, 430);
    setWindowOpacity(1.0);

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

    m_frameTimer.setInterval(16);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_frameTimer, &QTimer::timeout, this, &SplashWindow::tick);
}

void SplashWindow::setStage(qreal progress, const QString &label)
{
    m_targetProgress = std::clamp(progress, 0.0, 1.0);
    if (!label.isEmpty() && label != m_label) {
        m_label = label;
        m_labelChangedAt = m_elapsed;
    }
}

void SplashWindow::pump(int milliseconds)
{
    if (!isVisible())
        return;

    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents(QEventLoop::AllEvents, 4);
        tick();
        repaint();
        QThread::msleep(4);
    } while (timer.elapsed() < milliseconds);
}

void SplashWindow::finish()
{
    if (m_finished)
        return;
    m_finished = true;
    m_label = QStringLiteral("READY");
    m_labelChangedAt = m_elapsed;
    m_targetProgress = 1.0;

    pump(320);

    constexpr int fadeMs = 220;
    QElapsedTimer fade;
    fade.start();
    while (fade.elapsed() < fadeMs) {
        const qreal k = std::clamp(fade.elapsed() / qreal(fadeMs), 0.0, 1.0);
        setWindowOpacity(1.0 - k);
        QApplication::processEvents(QEventLoop::AllEvents, 4);
        repaint();
        QThread::msleep(4);
    }

    m_frameTimer.stop();
    close();
}

void SplashWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (QScreen *scr = screen()) {
        const QRect available = scr->availableGeometry();
        move(available.center() - QPoint(width() / 2, height() / 2));
        qInfo() << "SPLASH geometry" << geometry() << "available" << available
                << "dpr" << scr->devicePixelRatio();
    }
    if (!m_clock.isValid())
        m_clock.start();
    m_frameTimer.start();
    tick();
    repaint();
}

void SplashWindow::tick()
{
    if (!m_clock.isValid())
        m_clock.start();

    const qreal now = m_clock.elapsed() / 1000.0;
    const qreal delta = std::clamp(now - m_elapsed, 0.0, 0.12);
    m_elapsed = now;
    m_fadeIn = std::min(1.0, m_fadeIn + delta * 5.0);
    m_progress += (m_targetProgress - m_progress) * std::clamp(delta * 6.0, 0.0, 1.0);
    m_progress = std::clamp(m_progress, 0.0, 1.0);
    update();
}

void SplashWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                           | QPainter::SmoothPixmapTransform);
    painter.setOpacity(m_fadeIn);

    constexpr qreal margin = 14.0;
    constexpr qreal radius = 24.0;
    const QRectF frame(margin, margin, width() - margin * 2.0, height() - margin * 2.0);

    painter.setBrush(Qt::NoBrush);
    for (int i = 8; i >= 1; --i) {
        const qreal spread = i * 3.0;
        painter.setPen(QPen(withAlpha(kCyan, 0.055 - i * 0.005), 1.2));
        painter.drawRoundedRect(frame.adjusted(-spread, -spread, spread, spread),
                                radius + spread, radius + spread);
    }

    QPainterPath clip;
    clip.addRoundedRect(frame, radius, radius);

    painter.save();
    painter.setClipPath(clip);
    drawBackdrop(painter, frame, m_elapsed);
    drawGrid(painter, frame, m_elapsed);
    drawStars(painter, frame, m_elapsed);
    drawLogo(painter, frame, m_elapsed);
    drawTitle(painter, frame, m_elapsed);
    drawProgress(painter, frame, m_elapsed);
    drawHud(painter, frame, m_elapsed);
    painter.restore();

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(withAlpha(kCyan, 0.28 + 0.12 * wave(m_elapsed * 1.4)), 1.0));
    painter.drawRoundedRect(frame, radius, radius);
    painter.setPen(QPen(withAlpha(kCyan, 0.08), 1.0));
    painter.drawRoundedRect(frame.adjusted(3.0, 3.0, -3.0, -3.0), radius - 3.0, radius - 3.0);
}

void SplashWindow::drawBackdrop(QPainter &painter, const QRectF &frame, qreal t) const
{
    QLinearGradient background(frame.topLeft(), frame.bottomRight());
    background.setColorAt(0.0, QColor(8, 13, 28));
    background.setColorAt(0.42, QColor(4, 6, 13));
    background.setColorAt(1.0, QColor(19, 7, 31));
    painter.fillRect(frame, background);

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

    painter.save();
    painter.setPen(QPen(QColor(255, 255, 255, 5), 1.0));
    for (qreal y = frame.top(); y < frame.bottom(); y += 3.0)
        painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
    painter.restore();

    const qreal bandY = frame.top() + std::fmod(t * 0.085, 1.0) * frame.height();
    constexpr qreal bandH = 72.0;
    QLinearGradient band(0.0, bandY - bandH, 0.0, bandY + bandH);
    band.setColorAt(0.0, QColor(34, 211, 238, 0));
    band.setColorAt(0.5, QColor(34, 211, 238, 15));
    band.setColorAt(1.0, QColor(34, 211, 238, 0));
    painter.fillRect(QRectF(frame.left(), bandY - bandH, frame.width(), bandH * 2.0), band);
}

void SplashWindow::drawGrid(QPainter &painter, const QRectF &frame, qreal t) const
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

    const qreal scroll = std::fmod(t * 0.30, 1.0);
    for (int i = 0; i < 16; ++i) {
        const qreal k = (i + scroll) / 16.0;
        const qreal y = horizon + std::pow(k, 2.1) * (frame.bottom() - horizon);
        painter.setPen(QPen(withAlpha(kCyan, 0.03 + 0.16 * k * k), 1.0));
        painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
    }
    painter.restore();

    QLinearGradient horizonLine(frame.left(), 0.0, frame.right(), 0.0);
    horizonLine.setColorAt(0.0, QColor(34, 211, 238, 0));
    horizonLine.setColorAt(0.5, QColor(34, 211, 238, 140));
    horizonLine.setColorAt(1.0, QColor(34, 211, 238, 0));
    painter.setPen(QPen(QBrush(horizonLine), 1.4));
    painter.drawLine(QPointF(frame.left(), horizon), QPointF(frame.right(), horizon));
}

void SplashWindow::drawStars(QPainter &painter, const QRectF &frame, qreal t) const
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

void SplashWindow::drawLogo(QPainter &painter, const QRectF &frame, qreal t) const
{
    const QPointF center(frame.center().x(), frame.top() + frame.height() * 0.335);
    const qreal pulse = 0.82 + 0.18 * wave(t * 2.6);

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

    QFont logoFont(QStringLiteral("Segoe UI"));
    logoFont.setPixelSize(128);
    logoFont.setWeight(QFont::Black);
    QPainterPath glyph;
    glyph.addText(QPointF(0.0, 0.0), logoFont, QStringLiteral("N"));
    const QRectF bounds = glyph.boundingRect();

    painter.save();
    painter.translate(center.x() - bounds.center().x(), center.y() - bounds.center().y());

    for (int i = 10; i >= 1; --i) {
        const qreal alpha = 0.045 * pulse * (1.0 - (i - 1) / 10.0 * 0.7);
        painter.strokePath(glyph, QPen(withAlpha(kCyan, alpha), i * 2.4,
                                       Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }

    QLinearGradient fill(bounds.topLeft(), bounds.bottomRight());
    fill.setColorAt(0.00, QColor(236, 254, 255));
    fill.setColorAt(0.38, QColor(103, 232, 249));
    fill.setColorAt(0.72, QColor(34, 211, 238));
    fill.setColorAt(1.00, QColor(168, 85, 247));
    painter.fillPath(glyph, fill);
    painter.strokePath(glyph, QPen(withAlpha(QColor(255, 255, 255), 0.35), 1.2));

    painter.save();
    painter.setClipPath(glyph);
    const qreal sweepY = bounds.top() - 40.0
                         + std::fmod(t * 105.0, bounds.height() + 140.0);
    QLinearGradient sweep(0.0, sweepY - 28.0, 0.0, sweepY + 28.0);
    sweep.setColorAt(0.0, QColor(255, 255, 255, 0));
    sweep.setColorAt(0.5, QColor(255, 255, 255, 135));
    sweep.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(bounds.adjusted(-30.0, -60.0, 30.0, 60.0), sweep);
    painter.restore();

    const qreal glitchPhase = std::fmod(t, 3.7);
    if (glitchPhase < 0.05) {
        painter.save();
        painter.translate(3.0, 0.0);
        painter.fillPath(glyph, withAlpha(kPink, 0.30));
        painter.restore();
        painter.save();
        painter.translate(-3.0, 0.0);
        painter.fillPath(glyph, withAlpha(kCyan, 0.34));
        painter.restore();
    }

    painter.restore();
}

void SplashWindow::drawTitle(QPainter &painter, const QRectF &frame, qreal t) const
{
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(29);
    font.setWeight(QFont::DemiBold);
    const QFontMetricsF metrics(font);

    const QString title = QStringLiteral("NEONBOARD");
    constexpr qreal spacing = 9.0;

    qreal total = 0.0;
    for (const QChar ch : title)
        total += metrics.horizontalAdvance(ch) + spacing;
    total -= spacing;

    const qreal baseline = frame.top() + frame.height() * 0.560;
    const bool glitch = std::fmod(t, 4.3) < 0.05;

    QLinearGradient fill(0.0, baseline - 26.0, 0.0, baseline);
    fill.setColorAt(0.0, QColor(246, 254, 255));
    fill.setColorAt(0.48, QColor(178, 246, 253));
    fill.setColorAt(1.0, QColor(34, 211, 238));

    painter.setFont(font);

    qreal x = frame.center().x() - total / 2.0;
    for (int i = 0; i < title.size(); ++i) {
        const QChar ch = title.at(i);
        const qreal advance = metrics.horizontalAdvance(ch);
        const qreal appear = std::clamp((t - 0.24 - i * 0.05) * 3.4, 0.0, 1.0);
        const qreal rise = (1.0 - appear) * 7.0;

        if (appear > 0.0) {
            QPainterPath path;
            path.addText(QPointF(x, baseline + rise), font, QString(ch));

            painter.save();
            painter.setOpacity(m_fadeIn * appear);

            if (glitch) {
                painter.save();
                painter.translate(2.5, 0.0);
                painter.fillPath(path, withAlpha(kPink, 0.30));
                painter.restore();
                painter.save();
                painter.translate(-2.5, 0.0);
                painter.fillPath(path, withAlpha(kCyan, 0.38));
                painter.restore();
            }

            painter.strokePath(path, QPen(withAlpha(kCyan, 0.10), 7.0));
            painter.strokePath(path, QPen(withAlpha(kCyan, 0.20), 2.6));
            painter.fillPath(path, fill);
            painter.restore();
        }

        x += advance + spacing;
    }

    const qreal ruleY = baseline + 21.0;
    const qreal half = 16.0 + 150.0 * std::clamp(m_progress * 1.35, 0.0, 1.0);
    QLinearGradient rule(frame.center().x() - half, 0.0, frame.center().x() + half, 0.0);
    rule.setColorAt(0.0, withAlpha(kCyan, 0.0));
    rule.setColorAt(0.5, withAlpha(kCyan, 0.60));
    rule.setColorAt(1.0, withAlpha(kCyan, 0.0));
    painter.setPen(QPen(QBrush(rule), 1.0));
    painter.drawLine(QPointF(frame.center().x() - half, ruleY),
                     QPointF(frame.center().x() + half, ruleY));

    QFont sub(QStringLiteral("Segoe UI"));
    sub.setPixelSize(10);
    sub.setLetterSpacing(QFont::AbsoluteSpacing, 3.2);
    painter.setFont(sub);
    painter.setPen(withAlpha(kMuted, 0.40 + 0.32 * std::clamp(t * 1.3, 0.0, 1.0)));
    painter.drawText(QRectF(frame.left(), ruleY + 11.0, frame.width(), 16.0),
                     Qt::AlignHCenter | Qt::AlignTop,
                     QStringLiteral("PERSONAL WORKBENCH   //   LOADING"));
}

void SplashWindow::drawProgress(QPainter &painter, const QRectF &frame, qreal t) const
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

    QFont mono(QStringLiteral("Consolas"));
    mono.setPixelSize(10);
    painter.setFont(mono);
    painter.setPen(withAlpha(kCyan, 0.90));
    const QString percent = QString::number(qRound(m_progress * 100.0))
                            + QStringLiteral("%");
    painter.drawText(QPointF(track.right() + 14.0, track.top() + 4.0), percent);

    QFont labelFont(QStringLiteral("Microsoft YaHei UI"));
    labelFont.setPixelSize(10);
    labelFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.6);
    painter.setFont(labelFont);

    const qreal flash = std::clamp(1.0 - (t - m_labelChangedAt) * 2.2, 0.0, 1.0);
    painter.setPen(withAlpha(kMuted, 0.68 + 0.32 * flash));
    painter.drawText(QPointF(track.left(), track.bottom() + 22.0), m_label);

    painter.setPen(Qt::NoPen);
    painter.setBrush(withAlpha(kCyan, 0.35 + 0.55 * wave(t * 5.0)));
    painter.drawEllipse(QPointF(track.left() - 11.0, track.center().y()), 2.0, 2.0);
}

void SplashWindow::drawHud(QPainter &painter, const QRectF &frame, qreal) const
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
