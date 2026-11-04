#pragma once

#include <QString>

#include <memory>

/**
 * 启动闪屏（DX-39）。
 *
 * 结构：门面 + 渲染线程。
 *  · 画面与状态机在 Splash::Scene（纯逻辑，可单测）；
 *  · 渲染在专属线程上完成，载体是 Win32 分层窗口（见 SplashLayeredWindow）——
 *    Qt 窗口不能在非 GUI 线程创建（本机实测卡 3.2s），所以走原生窗口 + DIB 贴图；
 *  · 主线程只负责「推阶段值」与「按剧本停留」。
 *
 * 为什么不再是一个挂在主线程上的 QWidget：
 * 启动路径上有三段主线程同步阻塞（模型层构造 + 卡片 load、engine.load()、窗口 attach），
 * 主线程一阻塞，挂在主线程上的定时器就不触发 —— 动画与进度条会一起冻成静止图。
 * 实测（2026-11-05，同一探针同一天）：改动前闪屏存活约 2.3s，其中 **1120ms 逐像素
 * 完全静止**；渲染搬到专属线程后是 **0ms**（连续三轮全部为 0）。
 *
 * 渲染搬走之后语义才对：**进度条该停就停**（真实工作本来就没进展），
 * **动画不再跟着停**（它跟主线程无关了）。
 */
class SplashWindow
{
public:
    explicit SplashWindow();
    ~SplashWindow();

    SplashWindow(const SplashWindow &) = delete;
    SplashWindow &operator=(const SplashWindow &) = delete;

    /** 推进到某个阶段：进度目标 + 阶段标签。空标签表示不改标签。 */
    void setStage(qreal progress, const QString &label);

    /** 显示闪屏。返回时第一帧已经画到屏幕上。 */
    void show();

    /** 立即关闭（不走淡出）。异常路径用。 */
    void close();

    /** 主线程按剧本停留若干毫秒（期间仍处理事件）。 */
    void pump(int milliseconds);

    /** 收尾：READY → 停留 → 淡出 → 关闭。返回时窗口已经关掉。 */
    void finish();

    /** 闪屏当前是否还开着（关掉后 pump 不再停留）。 */
    bool isVisible() const;

private:
    class RenderThread;
    std::unique_ptr<RenderThread> m_thread;
    qreal m_dpr = 1.0;
};
