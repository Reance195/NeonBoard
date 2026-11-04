#include "SplashWindow.h"
#include "SplashLayeredWindow.h"
#include "SplashScene.h"

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QScreen>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {

constexpr int kWidth = 700;
constexpr int kHeight = 430;
// 30fps：闪屏的动效都是慢的（辉光脉动 3.7s 一轮、轨道环 156°/s、扫光 105px/s），
// 30 帧足够顺；而帧率翻倍就是 CPU 翻倍 —— 渲染线程一旦占满一个核，
// 就会被主线程的同步阻塞挤掉，画面照样冻住（DX-39 实测：60fps 下每帧 12~20ms，
// 渲染线程满负荷 → 出现 200ms+ 停顿）。
constexpr int kFrameIntervalMs = 33;
constexpr int kFinishHoldMs = 320;
constexpr int kFadeMs = 220;

} // namespace

/**
 * 渲染线程。
 *
 * 画面在这里画、在这里贴屏（Win32 分层窗口，见 SplashLayeredWindow），
 * **与主线程完全无关**：主线程堵在数据库打开 / engine.load() 里时，
 * 动画照走不误 —— 这正是 DX-39 要修的那件事。
 *
 * 主线程 → 渲染线程只走「阶段值 / 显示 / 收尾」三类命令（互斥量保护），
 * **不走排队信号**：主线程在启动期经常是阻塞的，排队信号要等主线程的事件循环
 * 才有空取，而这里要的是「主线程堵着也能立刻看到新阶段」。
 *
 * 渲染线程 → 主线程只回一个「已关闭」信号（条件变量），供 finish() 收尾等。
 */
class SplashWindow::RenderThread : public QThread
{
public:
    RenderThread();
    ~RenderThread() override;

    void setStage(qreal progress, const QString &label, const QImage &labelImage, qreal labelAscent);
    void setDigitAtlas(const QImage &atlas, qreal advance, qreal ascent);
    void showWindow(const QRect &logicalRect, qreal devicePixelRatio);
    void requestClose();
    void requestFinish();

    bool waitForFirstFrame(int timeoutMs);
    bool waitForClosed(int timeoutMs);
    bool isClosed() const;
    bool isVisible() const;

protected:
    void run() override;

private:
    enum class Phase
    {
        Running,
        Holding,
        Fading,
        Done,
    };

    void tick();
    void renderFrame(qreal alpha);
    void shutdown();

    // 诊断（NEONBOARD_SPLASH_DIAG=1 时才记账）：用来回答两个问题 ——
    //  「画面是真冻住了，还是探针采样太稀？」、「渲染线程到底烧了多少 CPU？」
    void recordDiag(qint64 paintUs, qint64 blitUs, quint64 contentHash);
    void dumpDiag();

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;

    // 主线程 → 渲染线程
    bool m_hasStage = false;
    qreal m_pendingProgress = 0.0;
    QString m_pendingLabel;
    QImage m_pendingLabelImage;
    qreal m_pendingLabelAscent = 0.0;
    bool m_hasLabelImage = false;
    bool m_hasAtlas = false;
    QImage m_pendingAtlas;
    qreal m_pendingAtlasAdvance = 0.0;
    qreal m_pendingAtlasAscent = 0.0;
    bool m_showRequested = false;
    QRect m_pendingLogicalRect;
    qreal m_pendingDpr = 1.0;
    bool m_closeRequested = false;
    bool m_finishRequested = false;

    // 渲染线程内部
    SplashLayeredWindow m_window;
    Splash::Scene m_scene;
    QImage m_frame;
    QElapsedTimer m_clock;
    qreal m_lastNow = 0.0;
    qreal m_dpr = 1.0;
    Phase m_phase = Phase::Running;
    qint64 m_phaseStartedMs = 0;
    int m_ticks = 0;
    int m_frames = 0;

    // 状态回报
    bool m_firstFrame = false;
    bool m_closed = false;
    bool m_visible = false;

    // 诊断统计（仅 NEONBOARD_SPLASH_DIAG=1 时使用）
    bool m_diag = false;
    qint64 m_diagPaintUs = 0;
    qint64 m_diagBlitUs = 0;
    qint64 m_diagPaintMaxUs = 0;
    qint64 m_diagBlitMaxUs = 0;
    qint64 m_diagMaxGapMs = 0;
    qint64 m_diagMaxGapAtMs = -1;
    qint64 m_diagLastTickMs = -1;
    quint64 m_diagHash = 0;
    bool m_diagHasHash = false;
    qint64 m_diagSameSinceMs = -1;    // 「画面与上一帧完全相同」的连续段起点
    qint64 m_diagSameTotalMs = 0;
    qint64 m_diagSameMaxMs = 0;
    qint64 m_diagSameMaxAtMs = -1;
    QStringList m_diagSameRuns;       // "起点ms:时长ms" 列表
};

SplashWindow::RenderThread::RenderThread()
{
    m_diag = !qEnvironmentVariableIsEmpty("NEONBOARD_SPLASH_DIAG");
    Splash::Scene::setProfiling(m_diag);
    start();
}

SplashWindow::RenderThread::~RenderThread()
{
    requestClose();
    quit(); // 兜底：即使 tick 没跑到也让事件循环退出
    wait(3000);
    // 线程已 join，这里读统计是安全的。
    dumpDiag();
}

void SplashWindow::RenderThread::setStage(qreal progress, const QString &label,
                                         const QImage &labelImage, qreal labelAscent)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingProgress = progress;
    m_pendingLabel = label;
    m_pendingLabelImage = labelImage;
    m_pendingLabelAscent = labelAscent;
    m_hasLabelImage = true;
    m_hasStage = true;
}

void SplashWindow::RenderThread::setDigitAtlas(const QImage &atlas, qreal advance, qreal ascent)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingAtlas = atlas;
    m_pendingAtlasAdvance = advance;
    m_pendingAtlasAscent = ascent;
    m_hasAtlas = true;
}

void SplashWindow::RenderThread::showWindow(const QRect &logicalRect, qreal devicePixelRatio)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingLogicalRect = logicalRect;
        m_pendingDpr = devicePixelRatio;
        m_showRequested = true;
    }
    m_cv.notify_all();
}

void SplashWindow::RenderThread::requestClose()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closeRequested = true;
    }
    m_cv.notify_all();
}

void SplashWindow::RenderThread::requestFinish()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finishRequested = true;
    }
    m_cv.notify_all();
}

bool SplashWindow::RenderThread::waitForFirstFrame(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_firstFrame || m_closed)
                return true;
            m_cv.wait_for(lock, std::chrono::milliseconds(4),
                          [this] { return m_firstFrame || m_closed; });
            if (m_firstFrame || m_closed)
                return true;
        }
        QApplication::processEvents(QEventLoop::AllEvents, 4);
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_firstFrame;
}

bool SplashWindow::RenderThread::waitForClosed(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_closed)
                return true;
            m_cv.wait_for(lock, std::chrono::milliseconds(4),
                          [this] { return m_closed; });
            if (m_closed)
                return true;
        }
        QApplication::processEvents(QEventLoop::AllEvents, 4);
    }
    return isClosed();
}

bool SplashWindow::RenderThread::isClosed() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_closed;
}

bool SplashWindow::RenderThread::isVisible() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_visible;
}

void SplashWindow::RenderThread::run()
{
    // 帧定时器必须住在渲染线程（QTimer 的线程归属看它在哪创建），
    // 所以它是 run() 的局部变量，不能做成 RenderThread 的成员。
    QTimer frameTimer;
    frameTimer.setInterval(kFrameIntervalMs);
    frameTimer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&frameTimer, &QTimer::timeout, [this] { tick(); });
    frameTimer.start();

    m_clock.start();

    exec(); // 渲染线程自己的事件循环

    m_window.destroy();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
        m_visible = false;
    }
    m_cv.notify_all();
}

void SplashWindow::RenderThread::tick()
{
    // 1) 取主线程推来的命令
    bool hasStage = false;
    qreal progress = 0.0;
    QString label;
    QImage labelImage;
    qreal labelAscent = 0.0;
    bool hasLabelImage = false;
    bool hasAtlas = false;
    QImage atlas;
    qreal atlasAdvance = 0.0;
    qreal atlasAscent = 0.0;
    bool show = false;
    QRect logicalRect;
    qreal dpr = 1.0;
    bool close = false;
    bool finish = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_hasStage) {
            hasStage = true;
            progress = m_pendingProgress;
            label = m_pendingLabel;
            m_hasStage = false;
        }
        if (m_hasLabelImage) {
            hasLabelImage = true;
            labelImage = m_pendingLabelImage;
            labelAscent = m_pendingLabelAscent;
            m_hasLabelImage = false;
        }
        if (m_hasAtlas) {
            hasAtlas = true;
            atlas = m_pendingAtlas;
            atlasAdvance = m_pendingAtlasAdvance;
            atlasAscent = m_pendingAtlasAscent;
            m_hasAtlas = false;
        }
        if (m_showRequested) {
            show = true;
            logicalRect = m_pendingLogicalRect;
            dpr = m_pendingDpr;
            m_showRequested = false;
        }
        close = m_closeRequested;
        m_closeRequested = false;
        finish = m_finishRequested;
        m_finishRequested = false;
    }

    if (hasStage)
        m_scene.setStage(progress, label);
    if (hasLabelImage)
        m_scene.setStageLabelImage(labelImage, labelAscent);
    if (hasAtlas)
        m_scene.setDigitAtlas(atlas, atlasAdvance, atlasAscent);

    if (show) {
        m_dpr = dpr > 0.0 ? dpr : 1.0;
        // Windows 的窗口坐标一律物理像素：逻辑矩形按设备像素比换算过去。
        const QRect physical(
            qRound(logicalRect.left() * m_dpr), qRound(logicalRect.top() * m_dpr),
            qRound(logicalRect.width() * m_dpr), qRound(logicalRect.height() * m_dpr));
        if (m_window.create(physical))
            m_window.show();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_visible = true;
    }

    if (finish && m_phase == Phase::Running) {
        m_scene.setStage(1.0, QStringLiteral("READY"));
        m_phase = Phase::Holding;
        m_phaseStartedMs = m_clock.elapsed();
    }

    if (close) {
        shutdown();
        return;
    }

    // 2) 推进动画（这一段与主线程无关：主线程堵住时照走）
    const qreal now = m_clock.elapsed() / 1000.0;
    m_scene.advance(now - m_lastNow);
    m_lastNow = now;

    // 3) 收尾序列：READY 停留 → 淡出 → 关闭
    qreal alpha = 1.0;
    if (m_phase == Phase::Holding) {
        if (m_clock.elapsed() - m_phaseStartedMs >= kFinishHoldMs) {
            m_phase = Phase::Fading;
            m_phaseStartedMs = m_clock.elapsed();
        }
    }
    if (m_phase == Phase::Fading) {
        const qint64 since = m_clock.elapsed() - m_phaseStartedMs;
        const qreal k = std::clamp(qreal(since) / qreal(kFadeMs), 0.0, 1.0);
        alpha = 1.0 - k;
        if (k >= 1.0) {
            shutdown();
            return;
        }
    }

    // 4) 画一帧并贴到屏幕上
    renderFrame(alpha);

    // 每秒一行渲染状态：出问题时先看这行（帧数不动 = 渲染线程被卡住）
    if (++m_ticks % 60 == 0) {
        qInfo() << "SPLASH tick" << m_ticks << "frames" << m_frames;
    }
}

void SplashWindow::RenderThread::renderFrame(qreal alpha)
{
    if (!m_window.isVisible())
        return;
    const int pixelWidth = qMax(1, qRound(kWidth * m_dpr));
    const int pixelHeight = qMax(1, qRound(kHeight * m_dpr));

    QElapsedTimer frameTimer;
    frameTimer.start();

    // 复用同一块缓冲：每帧新建 2MB+ 的 QImage 是纯浪费。
    if (m_frame.isNull() || m_frame.width() != pixelWidth || m_frame.height() != pixelHeight) {
        m_frame = QImage(pixelWidth, pixelHeight, QImage::Format_ARGB32_Premultiplied);
    }
    QImage &frame = m_frame;
    frame.fill(Qt::transparent);
    {
        QPainter painter(&frame);
        // 自己按设备像素比缩放，不依赖 QImage 的 devicePixelRatio 约定。
        painter.scale(m_dpr, m_dpr);
        m_scene.paint(painter, QSizeF(kWidth, kHeight), alpha);
    }
    const qint64 paintUs = frameTimer.nsecsElapsed() / 1000;

    frameTimer.restart();
    m_window.blit(frame);
    const qint64 blitUs = frameTimer.nsecsElapsed() / 1000;
    ++m_frames;

    if (m_diag) {
        // 画面内容指纹：隔行隔列抽样做 FNV-1a，够便宜又不至于漏掉整幅画面的变化。
        quint64 hash = 1469598103934665603ULL;
        const uchar *bits = frame.constBits();
        const int stride = frame.bytesPerLine();
        for (int y = 0; y < pixelHeight; y += 4) {
            const uchar *row = bits + static_cast<size_t>(y) * stride;
            for (int x = 0; x < pixelWidth; x += 4) {
                hash ^= static_cast<quint64>(row[x * 4]);
                hash *= 1099511628211ULL;
                hash ^= static_cast<quint64>(row[x * 4 + 1]);
                hash *= 1099511628211ULL;
            }
        }
        recordDiag(paintUs, blitUs, hash);
    }

    // 首帧贴上屏后回报一次：show() 要等它，保证「画面已上屏」再往下走启动流程。
    if (!m_firstFrame) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_firstFrame = true;
        m_cv.notify_all();
    }
}

void SplashWindow::RenderThread::recordDiag(qint64 paintUs, qint64 blitUs, quint64 contentHash)
{
    const qint64 nowMs = m_clock.elapsed();
    m_diagPaintUs += paintUs;
    m_diagBlitUs += blitUs;
    m_diagPaintMaxUs = qMax(m_diagPaintMaxUs, paintUs);
    m_diagBlitMaxUs = qMax(m_diagBlitMaxUs, blitUs);

    if (m_diagLastTickMs >= 0) {
        const qint64 gap = nowMs - m_diagLastTickMs;
        if (gap > m_diagMaxGapMs) {
            m_diagMaxGapMs = gap;
            m_diagMaxGapAtMs = m_diagLastTickMs;
        }
    }
    m_diagLastTickMs = nowMs;

    const bool same = m_diagHasHash && contentHash == m_diagHash;
    if (same) {
        if (m_diagSameSinceMs < 0)
            m_diagSameSinceMs = nowMs;
    } else {
        if (m_diagSameSinceMs >= 0) {
            const qint64 len = nowMs - m_diagSameSinceMs;
            m_diagSameTotalMs += len;
            if (len > m_diagSameMaxMs) {
                m_diagSameMaxMs = len;
                m_diagSameMaxAtMs = m_diagSameSinceMs;
            }
            m_diagSameRuns.append(QStringLiteral("%1:%2").arg(m_diagSameSinceMs).arg(len));
        }
        m_diagSameSinceMs = -1;
    }
    m_diagHash = contentHash;
    m_diagHasHash = true;
}

void SplashWindow::RenderThread::dumpDiag()
{
    if (!m_diag)
        return;
    // 收尾时把还在累积的「同画面段」结账
    if (m_diagSameSinceMs >= 0) {
        const qint64 len = m_diagLastTickMs - m_diagSameSinceMs;
        m_diagSameTotalMs += len;
        if (len > m_diagSameMaxMs) {
            m_diagSameMaxMs = len;
            m_diagSameMaxAtMs = m_diagSameSinceMs;
        }
        m_diagSameRuns.append(QStringLiteral("%1:%2").arg(m_diagSameSinceMs).arg(len));
        m_diagSameSinceMs = -1;
    }
    const int frames = qMax(1, m_frames);
    const Splash::Scene::Profile &p = m_scene.profile();
    qInfo() << "SPLASH diag frames" << m_frames
            << "paintAvgUs" << (m_diagPaintUs / frames)
            << "paintMaxUs" << m_diagPaintMaxUs
            << "blitAvgUs" << (m_diagBlitUs / frames)
            << "blitMaxUs" << m_diagBlitMaxUs
            << "maxGapMs" << m_diagMaxGapMs << "atMs" << m_diagMaxGapAtMs
            << "sameTotalMs" << m_diagSameTotalMs
            << "sameMaxMs" << m_diagSameMaxMs
            << "sameMaxAtMs" << m_diagSameMaxAtMs
            << "runs" << m_diagSameRuns.join(QLatin1Char(','));
    const double perSample = p.samples > 0 ? 1000000.0 / p.samples : 0.0;
    qInfo() << "SPLASH diag avgMs frame" << (p.frameNs * perSample / 1000.0)
            << "prologue" << (p.prologueNs * perSample / 1000.0)
            << "refreshBg" << (p.refreshBgNs * perSample / 1000.0)
            << "refreshScroll" << (p.refreshScrollNs * perSample / 1000.0)
            << "refreshLogo" << (p.refreshLogoNs * perSample / 1000.0)
            << "blit" << (p.blitNs * perSample / 1000.0)
            << "logo" << (p.logoNs * perSample / 1000.0)
            << "live" << (p.liveNs * perSample / 1000.0)
            << "tail" << (p.tailNs * perSample / 1000.0)
            << "bgGlow" << (p.glowNs * perSample / 1000.0)
            << "bgBackdrop" << (p.backdropNs * perSample / 1000.0)
            << "bgGrid" << (p.gridNs * perSample / 1000.0)
            << "samples" << p.samples;
}

void SplashWindow::RenderThread::shutdown()
{
    m_phase = Phase::Done;
    m_window.hide();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_visible = false;
    }
    quit();
}

// ---------------------------------------------------------------- 门面

SplashWindow::SplashWindow()
    : m_thread(std::make_unique<RenderThread>())
{
    // 设备像素比在这里定下来：主线程要按它渲染文字字图。
    QScreen *screen = QGuiApplication::primaryScreen();
    m_dpr = screen ? screen->devicePixelRatio() : 1.0;

    // 数字图集：主线程渲染一次，渲染线程只负责按格贴图。
    // 为什么不在渲染线程画字：QML 引擎启动期间主线程会长时间持有字体库锁，
    // 渲染线程这时画字会被整段阻塞 —— 实测一次画字 = 250ms 的画面冻结（DX-39）。
    qreal advance = 0.0;
    qreal ascent = 0.0;
    const QFontMetricsF metrics(Splash::Scene::percentFont());
    ascent = metrics.ascent();
    const QImage atlas = Splash::Scene::renderDigitAtlas(m_dpr, &advance);
    m_thread->setDigitAtlas(atlas, advance, ascent);
}

SplashWindow::~SplashWindow()
{
    // 析构必须把渲染线程收干净：否则线程还在跑就析构函数体，Qt 会报
    // "QThread: Destroyed while thread is still running"。
    m_thread->requestClose();
    m_thread->wait(3000);
}

void SplashWindow::setStage(qreal progress, const QString &label)
{
    // 标签文字在**主线程**渲染：字体库锁在 QML 加载期被主线程长时间持有，
    // 渲染线程这时去画字会被整段阻塞（DX-39 实测 250ms 停顿的元凶）。
    qreal ascent = 0.0;
    const QImage labelImage = Splash::Scene::renderStageLabel(label, m_dpr, &ascent);
    m_thread->setStage(progress, label, labelImage, ascent);
}

void SplashWindow::show()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    const QRect geometry(available.center() - QPoint(kWidth / 2, kHeight / 2),
                         QSize(kWidth, kHeight));
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
    qInfo() << "SPLASH geometry" << geometry << "available" << available << "dpr" << dpr;
    m_thread->showWindow(geometry, dpr);
    if (!m_thread->waitForFirstFrame(2000))
        qWarning() << "SPLASH first frame timeout";
}

void SplashWindow::close()
{
    if (m_thread->isClosed())
        return;
    m_thread->requestClose();
    m_thread->waitForClosed(1000);
}

void SplashWindow::pump(int milliseconds)
{
    if (!isVisible())
        return;
    // 画面由渲染线程自己推进，这里只负责「主线程按剧本停留 + 顺带处理事件」。
    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents(QEventLoop::AllEvents, 4);
        QThread::msleep(4);
    } while (timer.elapsed() < milliseconds);
}

void SplashWindow::finish()
{
    if (m_thread->isClosed())
        return;
    m_thread->requestFinish();
    m_thread->waitForClosed(4000);
}

bool SplashWindow::isVisible() const
{
    return m_thread->isVisible();
}
