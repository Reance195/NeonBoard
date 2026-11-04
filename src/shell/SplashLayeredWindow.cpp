#include "SplashLayeredWindow.h"

#include <QDebug>

#include <cstring>

namespace {

// 窗口类名：进程内注册一次即可（注册是进程级的，与建窗口的线程无关）。
const wchar_t kClassName[] = L"NeonBoardSplashLayered";

void ensureClassRegistered()
{
    static bool registered = false;
    if (registered)
        return;
    WNDCLASSEXW wc;
    std::memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);
    registered = true;
}

} // namespace

SplashLayeredWindow::SplashLayeredWindow() = default;

SplashLayeredWindow::~SplashLayeredWindow()
{
    destroy();
}

bool SplashLayeredWindow::create(const QRect &physicalRect)
{
#ifdef Q_OS_WIN
    ensureClassRegistered();
    m_rect = physicalRect;
    // WS_EX_LAYERED    → 分层窗口，逐像素 alpha
    // WS_EX_TRANSPARENT → 鼠标穿透（等价于原来的 Qt::WindowTransparentForInput）
    // WS_EX_TOOLWINDOW  → 不占任务栏
    // WS_EX_NOACTIVATE  → 不抢焦点
    // WS_EX_TOPMOST     → 始终置顶
    m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW
                                 | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                             kClassName, L"NeonBoard", WS_POPUP,
                             physicalRect.left(), physicalRect.top(),
                             physicalRect.width(), physicalRect.height(),
                             nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m_hwnd) {
        qWarning() << "SPLASH layered window create failed" << GetLastError();
        return false;
    }
    return true;
#else
    Q_UNUSED(physicalRect);
    return false;
#endif
}

void SplashLayeredWindow::moveTo(const QRect &physicalRect)
{
#ifdef Q_OS_WIN
    m_rect = physicalRect;
    if (m_hwnd)
        SetWindowPos(m_hwnd, HWND_TOPMOST, physicalRect.left(), physicalRect.top(),
                     physicalRect.width(), physicalRect.height(),
                     SWP_NOACTIVATE | SWP_NOZORDER);
#endif
}

void SplashLayeredWindow::show()
{
#ifdef Q_OS_WIN
    if (m_hwnd)
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
#endif
    m_visible = true;
}

void SplashLayeredWindow::hide()
{
#ifdef Q_OS_WIN
    if (m_hwnd)
        ShowWindow(m_hwnd, SW_HIDE);
#endif
    m_visible = false;
}

void SplashLayeredWindow::blit(const QImage &image)
{
#ifdef Q_OS_WIN
    if (!m_hwnd || image.isNull())
        return;
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0)
        return;

    if (!m_screenDc)
        m_screenDc = GetDC(nullptr);
    if (!m_memDc)
        m_memDc = CreateCompatibleDC(nullptr);
    if (!m_screenDc || !m_memDc)
        return;

    if (!m_bitmap || m_bitmapWidth != width || m_bitmapHeight != height) {
        if (m_bitmap) {
            SelectObject(m_memDc, m_oldBitmap);
            DeleteObject(m_bitmap);
            m_bitmap = nullptr;
        }
        BITMAPINFO info;
        std::memset(&info, 0, sizeof(info));
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; // 负数 = 自上而下，与 QImage 的行序一致
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        m_bitmap = CreateDIBSection(m_memDc, &info, DIB_RGB_COLORS, &m_bits, nullptr, 0);
        if (!m_bitmap) {
            qWarning() << "SPLASH CreateDIBSection failed" << GetLastError();
            return;
        }
        m_oldBitmap = SelectObject(m_memDc, m_bitmap);
        m_bitmapWidth = width;
        m_bitmapHeight = height;
    }

    // QImage::Format_ARGB32_Premultiplied 在内存里就是 BGRA，与 32bpp BI_RGB DIB 同序；
    // 且已是预乘 alpha，UpdateLayeredWindow 的 ULW_ALPHA 直接可用。
    std::memcpy(m_bits, image.constBits(), static_cast<size_t>(width) * height * 4);

    POINT sourceOrigin{0, 0};
    POINT destOrigin{m_rect.left(), m_rect.top()};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(m_hwnd, m_screenDc, &destOrigin, &size, m_memDc, &sourceOrigin,
                             0, &blend, ULW_ALPHA)) {
        qWarning() << "SPLASH UpdateLayeredWindow failed" << GetLastError();
    }
#else
    Q_UNUSED(image);
#endif
}

void SplashLayeredWindow::destroy()
{
#ifdef Q_OS_WIN
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_memDc) {
        if (m_oldBitmap)
            SelectObject(m_memDc, m_oldBitmap);
        if (m_bitmap)
            DeleteObject(m_bitmap);
        DeleteDC(m_memDc);
        m_memDc = nullptr;
        m_oldBitmap = nullptr;
        m_bitmap = nullptr;
        m_bits = nullptr;
        m_bitmapWidth = 0;
        m_bitmapHeight = 0;
    }
    if (m_screenDc) {
        ReleaseDC(nullptr, m_screenDc);
        m_screenDc = nullptr;
    }
#endif
    m_visible = false;
}
