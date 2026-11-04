#pragma once

#include <QImage>
#include <QRect>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * 分层窗口（Win32 WS_EX_LAYERED + UpdateLayeredWindow）。
 *
 * 为什么不用 QWindow / QWidget 承载闪屏（DX-39 的实测结论）：
 *  1) Qt 的窗口**在非 GUI 线程里创建时，本机实测 create() 要卡 3 秒多**
 *     （GUI 线程不派发事件就永远卡住；主线程同一动作只要 7 ms）；
 *  2) 更根本的是，Qt 窗口的绘制事件只能由 GUI 线程派发 —— 主线程一忙，
 *     画面就冻住，正是这张卡要修的那件事。
 *
 * UpdateLayeredWindow 不挑线程：渲染线程画完直接贴屏，主线程堵着也不影响，
 * 窗口也归渲染线程所有（Win32 允许在任意线程建窗口并自绘）。
 */
class SplashLayeredWindow
{
public:
    SplashLayeredWindow();
    ~SplashLayeredWindow();

    SplashLayeredWindow(const SplashLayeredWindow &) = delete;
    SplashLayeredWindow &operator=(const SplashLayeredWindow &) = delete;

    /** 创建分层窗口。physicalRect 是**物理像素**坐标（Windows 窗口坐标一律物理像素）。 */
    bool create(const QRect &physicalRect);
    /** 移动窗口（物理像素坐标）。 */
    void moveTo(const QRect &physicalRect);
    void show();
    void hide();
    bool isVisible() const { return m_visible; }

    /** 把一张 ARGB32_Premultiplied 的图整块贴到窗口上（按图的物理像素尺寸）。 */
    void blit(const QImage &image);

    void destroy();

private:
#ifdef Q_OS_WIN
    HWND m_hwnd = nullptr;
    HDC m_screenDc = nullptr;
    HDC m_memDc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_oldBitmap = nullptr;
    void *m_bits = nullptr;
    int m_bitmapWidth = 0;
    int m_bitmapHeight = 0;
#endif
    QRect m_rect;
    bool m_visible = false;
};
