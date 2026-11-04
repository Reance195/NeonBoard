#include "TrayMenu.h"
#include "MenuSkin.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QRectF>
#include <QtGlobal>

#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdio>
#include <memory>

namespace {

constexpr wchar_t kWindowClass[] = L"NeonBoardTrayMenuWnd";
constexpr UINT_PTR kFadeTimerId = 1;
constexpr int kFadeIntervalMs = 16;
constexpr int kFadeDurationMs = 130;

// GDI+ 进程级生命周期：只启一次，退出时关。
struct GdiplusRuntime
{
    ULONG_PTR token = 0;
    GdiplusRuntime()
    {
        Gdiplus::GdiplusStartupInput input;
        input.GdiplusVersion = 1;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
    }
    ~GdiplusRuntime()
    {
        if (token)
            Gdiplus::GdiplusShutdown(token);
    }
};

bool ensureGdiplus()
{
    static GdiplusRuntime runtime;
    return runtime.token != 0;
}

double systemScale()
{
    UINT dpi = GetDpiForSystem();
    if (dpi == 0)
        dpi = 96;
    return static_cast<double>(dpi) / 96.0;
}

int scaled(int value, double scale)
{
    return qRound(value * scale);
}

// 主题色 + 透明度；fade 用来做入场淡入（所有元素一起变淡）。
Gdiplus::Color themedColor(const QString &hex, int alpha, double fade)
{
    const int value = qBound(0, qRound(alpha * fade), 255);
    return Gdiplus::Color(MenuSkin::argb(hex, value));
}

void addRoundedPath(Gdiplus::GraphicsPath &path, const Gdiplus::RectF &rect, Gdiplus::REAL radius)
{
    const Gdiplus::REAL d = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

// 把 GDI+ 位图搬进一块 32 位 DIB：UpdateLayeredWindow 要的是 DIB 位图，
// 而 Bitmap::GetHBITMAP 会把 alpha 通道丢掉（拿到的 GDI 位图没有逐像素 alpha）。
// 直接 LockBits 取预乘 BGRA  memcpy 进 CreateDIBSection 的位图，alpha 才留得住。
HBITMAP createLayeredBitmap(Gdiplus::Bitmap &source)
{
    const UINT width = source.GetWidth();
    const UINT height = source.GetHeight();
    if (width == 0 || height == 0)
        return nullptr;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height); // 负高度 = 自上而下
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dib || !bits)
        return nullptr;

    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData data{};
    if (source.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data)
        != Gdiplus::Ok) {
        DeleteObject(dib);
        return nullptr;
    }
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    for (UINT y = 0; y < height; ++y) {
        memcpy(static_cast<BYTE *>(bits) + y * rowBytes,
               static_cast<BYTE *>(data.Scan0) + static_cast<INT>(y) * data.Stride,
               rowBytes);
    }
    source.UnlockBits(&data);
    return dib;
}

} // namespace

// ---- 极简 PNG 写出 --------------------------------------------------------
//
// 为什么不用现成的：系统自带的 GDI+ PNG 编码器在部分 Windows 镜像上缺失
// （gdiplus.dll 在，但 GdiPlusimaging.dll 与编码器 CLSID 不在，Save 返回
// FileNotFound），而宿主为了常驻内存刻意只链 QtCore，不引 QtGui 的 QImage。
// 于是自己拼一个 PNG：压缩用 QtCore 的 qCompress（内部就是 zlib），
// 只支持一种格式 —— 8 位 RGBA、无隔行。
namespace {

quint32 crc32Of(const QByteArray &data)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int bit = 0; bit < 8; ++bit)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        ready = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (char ch : data)
        crc = table[(crc ^ static_cast<quint8>(ch)) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void appendBigEndian32(QByteArray &out, quint32 value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.append(static_cast<char>((value >> shift) & 0xFFu));
}

void appendChunk(QByteArray &png, const char *type, const QByteArray &payload)
{
    appendBigEndian32(png, static_cast<quint32>(payload.size()));
    QByteArray chunk(type, 4);
    chunk.append(payload);
    png.append(chunk);
    appendBigEndian32(png, crc32Of(chunk));
}

// rgba：每像素 4 字节，顺序 R,G,B,A（直通 alpha，不是预乘）。
bool writePng(const QString &path, const QByteArray &rgba, int width, int height)
{
    if (width <= 0 || height <= 0 || rgba.size() < width * height * 4)
        return false;

    QByteArray raw;
    raw.reserve(height * (1 + width * 4));
    for (int y = 0; y < height; ++y) {
        raw.append(char(0)); // 每行的 filter 字节：0 = None
        raw.append(rgba.constData() + static_cast<qsizetype>(y) * width * 4, width * 4);
    }
    const QByteArray compressed = qCompress(raw, 9);
    // Qt 的 qCompress 输出前面带 4 字节「压缩前长度」，zlib 流本体从 0x78 开始；
    // PNG 的 IDAT 要的是纯 zlib 流，把这段前缀去掉（判断首字节，兼容两种形态）。
    QByteArray zlibData = compressed;
    if (zlibData.size() > 4 && static_cast<quint8>(zlibData.at(0)) != 0x78)
        zlibData = zlibData.mid(4);

    QByteArray png;
    png.append(QByteArray::fromHex("89504e470d0a1a0a"));
    QByteArray ihdr;
    appendBigEndian32(ihdr, static_cast<quint32>(width));
    appendBigEndian32(ihdr, static_cast<quint32>(height));
    ihdr.append(char(8)); // 位深
    ihdr.append(char(6)); // 颜色类型 6 = RGBA
    ihdr.append(char(0)); // 压缩方式
    ihdr.append(char(0)); // 过滤方式
    ihdr.append(char(0)); // 隔行
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", zlibData);
    appendChunk(png, "IEND", QByteArray());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(png) == png.size();
}

} // namespace

// ---------------------------------------------------------------------------

struct TrayMenu::Impl
{
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    struct Row
    {
        QRectF rect;     // 位图坐标
        bool separator = false;
    };

    QList<TrayMenu::Item> items;
    MenuSkin::Palette palette = MenuSkin::paletteForPreset(QStringLiteral("neon"));

    HWND window = nullptr;
    HWND previousFocus = nullptr;
    int hoverIndex = -1;
    int pressedIndex = -1;
    int windowX = 0;
    int windowY = 0;
    double fade = 1.0;
    bool visible = false;

    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
    int panelX = 0;
    int panelY = 0;
    int panelWidth = 0;
    int panelHeight = 0;
    QList<Row> rows;

    bool ensureWindow();
    void computeLayout(double scale);
    bool renderBitmap(int hover, double fadeFactor);
    bool applyBitmap();
    void hide();
    void trigger(int index);
    void moveHover(int delta);
    int indexAt(int x, int y) const;
};

bool TrayMenu::Impl::ensureWindow()
{
    if (window)
        return true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TrayMenu::Impl::windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc); // 已注册时返回失败，无所谓

    window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
                              kWindowClass, L"NeonBoard Tray Menu",
                              WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window)
        return false;
    SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return true;
}

void TrayMenu::Impl::computeLayout(double scale)
{
    const MenuSkin::Metrics m = MenuSkin::metrics();
    const int padX = scaled(m.padX, scale);
    const int padY = scaled(m.padY, scale);
    const int itemHeight = scaled(m.itemHeight, scale);
    const int sepHeight = scaled(1 + 2 * m.sepGap, scale);
    const int checkWidth = scaled(m.checkWidth, scale);
    const int glowMargin = scaled(m.glowLayers + 2, scale);

    bool anyCheck = false;
    for (const TrayMenu::Item &item : items) {
        if (item.kind == TrayMenu::Item::Action && item.checkable) {
            anyCheck = true;
            break;
        }
    }

    // 文字宽度量一次：菜单宽度按最长条目定，窄菜单比固定宽度耐看。
    int textWidth = 0;
    {
        Gdiplus::Bitmap scratch(1, 1, PixelFormat32bppPARGB);
        Gdiplus::Graphics measure(&scratch);
        measure.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        Gdiplus::Font font(L"Microsoft YaHei UI",
                           static_cast<Gdiplus::REAL>(scaled(m.fontSize, scale)),
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentNear);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);
        Gdiplus::RectF bounds(0, 0, 4096, 512);
        for (const TrayMenu::Item &item : items) {
            if (item.kind == TrayMenu::Item::Separator)
                continue;
            Gdiplus::RectF fitted;
            measure.MeasureString(reinterpret_cast<LPCWSTR>(item.text.utf16()), -1, &font,
                                  bounds, &format, &fitted);
            // GDI+ 的 RectF 把 Width / Height 做成**公开成员变量**（不是方法），
            // 写成 fitted.Width() 会被当成「拿浮点数当函数调用」而编译失败。
            textWidth = qMax(textWidth, qRound(fitted.Width));
        }
    }

    panelWidth = padX * 2 + (anyCheck ? checkWidth : 0) + textWidth;

    rows.clear();
    int y = padY;
    for (const TrayMenu::Item &item : items) {
        Row row;
        if (item.kind == TrayMenu::Item::Separator) {
            row.separator = true;
            row.rect = QRectF(0, y, panelWidth, sepHeight);
            y += sepHeight;
        } else {
            row.rect = QRectF(0, y, panelWidth, itemHeight);
            y += itemHeight;
        }
        rows.append(row);
    }
    panelHeight = y + padY;

    panelX = glowMargin;
    panelY = glowMargin;
    bitmapWidth = panelWidth + glowMargin * 2;
    bitmapHeight = panelHeight + glowMargin * 2;

    // 行坐标挪到位图坐标（辉光留白在外圈）
    for (Row &row : rows)
        row.rect.translate(panelX, panelY);
}

bool TrayMenu::Impl::renderBitmap(int hover, double fadeFactor)
{
    const double scale = systemScale();
    computeLayout(scale);
    if (panelWidth <= 0 || panelHeight <= 0)
        return false;

    const MenuSkin::Metrics m = MenuSkin::metrics();
    const int padX = scaled(m.padX, scale);
    const int itemRadius = scaled(m.itemRadius, scale);
    const int checkWidth = scaled(m.checkWidth, scale);
    const Gdiplus::REAL radius = static_cast<Gdiplus::REAL>(scaled(m.radius, scale));

    // 位图画成直通 alpha（32bppARGB）：分层窗口那路读的时候由 GDI+ 转成预乘，
    // 出图那路直接就是 PNG 要的直通 alpha，两边都不用手工反预乘。
    bitmap = std::make_unique<Gdiplus::Bitmap>(bitmapWidth, bitmapHeight, PixelFormat32bppARGB);
    Gdiplus::Graphics g(bitmap.get());
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));

    const Gdiplus::RectF panel(static_cast<Gdiplus::REAL>(panelX), static_cast<Gdiplus::REAL>(panelY),
                               static_cast<Gdiplus::REAL>(panelWidth), static_cast<Gdiplus::REAL>(panelHeight));

    // 1) 外辉光：一圈圈同心描边，越靠内越亮、越靠外越淡 ——
    // 霓虹感就来自这里。等 alpha 的一圈会变成实心青框，所以要按层数递减。
    const int step = qMax(1, scaled(1, scale));
    for (int i = m.glowLayers; i >= 1; --i) {
        const Gdiplus::REAL expand = static_cast<Gdiplus::REAL>(i * step);
        Gdiplus::GraphicsPath path;
        addRoundedPath(path,
                       Gdiplus::RectF(panel.X - expand, panel.Y - expand,
                                      panel.Width + expand * 2.0f, panel.Height + expand * 2.0f),
                       radius + expand);
        const int alpha = 3 + (m.glowLayers - i + 1);
        Gdiplus::Pen pen(themedColor(palette.accent, alpha, fadeFactor),
                         static_cast<Gdiplus::REAL>(step));
        g.DrawPath(&pen, &path);
    }

    // 2) 面板底
    {
        Gdiplus::GraphicsPath path;
        addRoundedPath(path, panel, radius);
        Gdiplus::SolidBrush brush(themedColor(palette.bg, 246, fadeFactor));
        g.FillPath(&brush, &path);
    }

    // 3) 顶部内高光：玻璃质感的一点点来源
    {
        Gdiplus::GraphicsPath path;
        addRoundedPath(path,
                       Gdiplus::RectF(panel.X + 1.0f, panel.Y + 1.0f, panel.Width - 2.0f, panel.Height - 2.0f),
                       qMax<Gdiplus::REAL>(1.0f, radius - 1.0f));
        Gdiplus::Pen pen(themedColor(QStringLiteral("#FFFFFF"), 20, fadeFactor), 1.0f);
        g.DrawPath(&pen, &path);
    }

    // 4) 描边
    {
        Gdiplus::GraphicsPath path;
        addRoundedPath(path, panel, radius);
        Gdiplus::Pen pen(themedColor(palette.accent, 120, fadeFactor),
                         static_cast<Gdiplus::REAL>(scaled(m.borderWidth, scale)));
        g.DrawPath(&pen, &path);
    }

    // 5) 条目
    Gdiplus::Font font(L"Microsoft YaHei UI",
                       static_cast<Gdiplus::REAL>(scaled(m.fontSize, scale)),
                       Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);

    bool anyCheck = false;
    for (const TrayMenu::Item &item : items) {
        if (item.kind == TrayMenu::Item::Action && item.checkable) {
            anyCheck = true;
            break;
        }
    }

    const int count = static_cast<int>(qMin(rows.size(), items.size()));
    for (int i = 0; i < count; ++i) {
        const Row &row = rows.at(i);
        const TrayMenu::Item &item = items.at(i);
        if (row.separator) {
            const Gdiplus::REAL lineY = row.rect.top() + row.rect.height() / 2.0;
            Gdiplus::Pen pen(themedColor(palette.text, 30, fadeFactor), 1.0f);
            g.DrawLine(&pen, panel.X + padX, lineY, panel.X + panelWidth - padX, lineY);
            continue;
        }

        const bool isHover = (i == hover);
        const QString hoverColor = item.danger ? palette.danger : palette.accent;
        if (isHover) {
            Gdiplus::GraphicsPath path;
            addRoundedPath(path,
                           Gdiplus::RectF(row.rect.x() + 4.0f, row.rect.y() + 2.0f,
                                          row.rect.width() - 8.0f, row.rect.height() - 4.0f),
                           static_cast<Gdiplus::REAL>(itemRadius));
            Gdiplus::SolidBrush brush(themedColor(hoverColor, 40, fadeFactor));
            g.FillPath(&brush, &path);

            // 左侧霓虹条：一眼能看出「选中哪一条」
            Gdiplus::GraphicsPath bar;
            addRoundedPath(bar,
                           Gdiplus::RectF(row.rect.x() + 6.0f, row.rect.y() + 9.0f, 3.0f,
                                          row.rect.height() - 18.0f),
                           1.5f);
            Gdiplus::SolidBrush barBrush(themedColor(hoverColor, 235, fadeFactor));
            g.FillPath(&barBrush, &bar);
        }

        const Gdiplus::REAL textX = row.rect.x() + padX + (anyCheck ? checkWidth : 0);
        const Gdiplus::RectF textRect(textX, row.rect.y(), row.rect.width(), row.rect.height());
        const QString textColor = isHover ? QStringLiteral("#FFFFFF") : palette.text;
        Gdiplus::SolidBrush textBrush(themedColor(textColor, 255, fadeFactor));
        g.DrawString(reinterpret_cast<LPCWSTR>(item.text.utf16()), -1, &font, textRect, &format,
                     &textBrush);

        if (item.checkable && item.checked) {
            const Gdiplus::REAL cx = row.rect.x() + padX + checkWidth / 2.0;
            const Gdiplus::REAL cy = row.rect.y() + row.rect.height() / 2.0;
            const Gdiplus::REAL s = static_cast<Gdiplus::REAL>(scaled(7, scale));
            Gdiplus::GraphicsPath check;
            check.AddLine(Gdiplus::PointF(cx - s * 0.9f, cy + s * 0.05f),
                          Gdiplus::PointF(cx - s * 0.25f, cy + s * 0.7f));
            check.AddLine(Gdiplus::PointF(cx - s * 0.25f, cy + s * 0.7f),
                          Gdiplus::PointF(cx + s * 0.95f, cy - s * 0.65f));
            Gdiplus::Pen pen(themedColor(palette.accent, 240, fadeFactor),
                             static_cast<Gdiplus::REAL>(scaled(2, scale)));
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            g.DrawPath(&pen, &check);
        }
    }
    return true;
}

bool TrayMenu::Impl::applyBitmap()
{
    if (!window || !bitmap)
        return false;
    HBITMAP dib = createLayeredBitmap(*bitmap);
    if (!dib)
        return false;

    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HGDIOBJ old = SelectObject(memDc, dib);
    SIZE size{ bitmapWidth, bitmapHeight };
    POINT dst{ windowX, windowY };
    POINT src{ 0, 0 };
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    const BOOL ok = UpdateLayeredWindow(window, screenDc, &dst, &size, memDc, &src, 0, &blend, ULW_ALPHA);
    SelectObject(memDc, old);
    DeleteDC(memDc);
    DeleteObject(dib);
    ReleaseDC(nullptr, screenDc);
    return ok != FALSE;
}

void TrayMenu::Impl::hide()
{
    if (!visible)
        return;
    visible = false;
    if (window)
        KillTimer(window, kFadeTimerId);
    ShowWindow(window, SW_HIDE);
    if (previousFocus && IsWindow(previousFocus))
        SetFocus(previousFocus);
    previousFocus = nullptr;
}

void TrayMenu::Impl::trigger(int index)
{
    if (index < 0 || index >= items.size()) {
        hide();
        return;
    }
    const TrayMenu::Item item = items.at(index);
    hide();
    if (item.onTriggered)
        item.onTriggered();
}

void TrayMenu::Impl::moveHover(int delta)
{
    if (items.isEmpty())
        return;
    int index = hoverIndex;
    for (int step = 0; step < items.size(); ++step) {
        index += delta;
        if (index < 0)
            index = items.size() - 1;
        if (index >= items.size())
            index = 0;
        if (items.at(index).kind != TrayMenu::Item::Separator)
            break;
    }
    if (index == hoverIndex)
        return;
    hoverIndex = index;
    renderBitmap(hoverIndex, fade);
    applyBitmap();
}

int TrayMenu::Impl::indexAt(int x, int y) const
{
    for (int i = 0; i < rows.size() && i < items.size(); ++i) {
        const Row &row = rows.at(i);
        if (row.separator)
            continue;
        if (row.rect.contains(QPointF(x, y)))
            return i;
    }
    return -1;
}

LRESULT CALLBACK TrayMenu::Impl::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Impl *impl = reinterpret_cast<Impl *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_MOUSEMOVE: {
        if (!impl)
            break;
        const int index = impl->indexAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (index != impl->hoverIndex) {
            impl->hoverIndex = index;
            impl->renderBitmap(index, impl->fade);
            impl->applyBitmap();
        }
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE: {
        if (impl && impl->hoverIndex != -1) {
            impl->hoverIndex = -1;
            impl->renderBitmap(-1, impl->fade);
            impl->applyBitmap();
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!impl)
            break;
        impl->pressedIndex = impl->indexAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        SetCapture(hwnd);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!impl)
            break;
        const int index = impl->indexAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        const int pressed = impl->pressedIndex;
        impl->pressedIndex = -1;
        ReleaseCapture();
        if (index >= 0 && index == pressed)
            impl->trigger(index);
        else
            impl->hide(); // 点在菜单外：按系统菜单的习惯收起
        return 0;
    }
    case WM_CAPTURECHANGED: {
        if (impl)
            impl->pressedIndex = -1;
        return 0;
    }
    case WM_ACTIVATE: {
        if (impl && impl->visible && LOWORD(wParam) == WA_INACTIVE)
            impl->hide();
        return 0;
    }
    case WM_KEYDOWN: {
        if (!impl)
            break;
        if (wParam == VK_ESCAPE) {
            impl->hide();
            return 0;
        }
        if (wParam == VK_UP) {
            impl->moveHover(-1);
            return 0;
        }
        if (wParam == VK_DOWN) {
            impl->moveHover(1);
            return 0;
        }
        if (wParam == VK_RETURN || wParam == VK_SPACE) {
            impl->trigger(impl->hoverIndex);
            return 0;
        }
        break;
    }
    case WM_TIMER: {
        if (!impl || wParam != kFadeTimerId)
            break;
        impl->fade += static_cast<double>(kFadeIntervalMs) / kFadeDurationMs;
        if (impl->fade >= 1.0) {
            impl->fade = 1.0;
            KillTimer(hwnd, kFadeTimerId);
        }
        impl->renderBitmap(impl->hoverIndex, impl->fade);
        impl->applyBitmap();
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// ---------------------------------------------------------------------------

TrayMenu::TrayMenu()
    : m_impl(new Impl)
{
}

TrayMenu::~TrayMenu()
{
    if (m_impl && m_impl->window) {
        m_impl->hide();
        DestroyWindow(m_impl->window);
        m_impl->window = nullptr;
    }
    delete m_impl;
    m_impl = nullptr;
}

void TrayMenu::setItems(const QList<Item> &items)
{
    if (m_impl)
        m_impl->items = items;
}

void TrayMenu::setPalette(const MenuSkin::Palette &palette)
{
    if (m_impl)
        m_impl->palette = palette;
}

void TrayMenu::popupAtCursor()
{
    if (!m_impl || m_impl->items.isEmpty())
        return;
    if (!ensureGdiplus() || !m_impl->ensureWindow()) {
        showNativeFallback();
        return;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    if (!m_impl->renderBitmap(-1, 0.0)) {
        showNativeFallback();
        return;
    }

    // 贴边翻转：下面放不下就往上弹，右边放不下就往左收。
    const int width = m_impl->bitmapWidth;
    const int height = m_impl->bitmapHeight;
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(monitor, &info);

    int x = cursor.x;
    int y = cursor.y;
    if (x + width > info.rcWork.right)
        x = info.rcWork.right - width;
    if (y + height > info.rcWork.bottom)
        y = cursor.y - height;
    if (y < info.rcWork.top)
        y = info.rcWork.top;
    if (x < info.rcWork.left)
        x = info.rcWork.left;

    m_impl->windowX = x;
    m_impl->windowY = y;
    m_impl->hoverIndex = -1;
    m_impl->pressedIndex = -1;
    m_impl->fade = 0.0;
    m_impl->previousFocus = GetFocus();
    m_impl->applyBitmap();
    ShowWindow(m_impl->window, SW_SHOWNOACTIVATE);
    SetForegroundWindow(m_impl->window);
    SetFocus(m_impl->window);
    m_impl->visible = true;
    SetTimer(m_impl->window, kFadeTimerId, kFadeIntervalMs, nullptr);
}

bool TrayMenu::renderPreview(const QString &pngPath, int hoverIndex) const
{
    if (!m_impl || m_impl->items.isEmpty())
        return false;
    if (!ensureGdiplus())
        return false;
    if (!m_impl->renderBitmap(hoverIndex, 1.0))
        return false;

    // 位图转 RGBA 再由自带的极简 PNG 写出器落盘：
    // 内存格式一致（直通 alpha），且不依赖系统 GDI+ 的 PNG 编码器
    // （部分 Windows 镜像缺该组件，Save 会返回 FileNotFound）。
    const UINT width = m_impl->bitmap->GetWidth();
    const UINT height = m_impl->bitmap->GetHeight();
    QByteArray rgba;
    rgba.resize(static_cast<qsizetype>(width) * height * 4);
    {
        Gdiplus::Rect rect(0, 0, width, height);
        Gdiplus::BitmapData data{};
        if (m_impl->bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data)
            != Gdiplus::Ok)
            return false;
        for (UINT y = 0; y < height; ++y) {
            const BYTE *src = static_cast<const BYTE *>(data.Scan0) + static_cast<INT>(y) * data.Stride;
            char *dst = rgba.data() + static_cast<qsizetype>(y) * width * 4;
            for (UINT x = 0; x < width; ++x) {
                dst[x * 4 + 0] = static_cast<char>(src[x * 4 + 2]); // R
                dst[x * 4 + 1] = static_cast<char>(src[x * 4 + 1]); // G
                dst[x * 4 + 2] = static_cast<char>(src[x * 4 + 0]); // B
                dst[x * 4 + 3] = static_cast<char>(src[x * 4 + 3]); // A
            }
        }
        m_impl->bitmap->UnlockBits(&data);
    }
    return writePng(pngPath, rgba, static_cast<int>(width), static_cast<int>(height));
}

void TrayMenu::showNativeFallback() const
{
    // 兜底：GDI+ 或窗口创建失败时退回系统菜单 —— 「点得到」优先于「好看」。
    if (!m_impl || m_impl->items.isEmpty())
        return;
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    for (int i = 0; i < m_impl->items.size(); ++i) {
        const Item &item = m_impl->items.at(i);
        if (item.kind == Item::Separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        UINT flags = MF_STRING;
        if (item.checkable && item.checked)
            flags |= MF_CHECKED;
        AppendMenuW(menu, flags, static_cast<UINT>(i + 1),
                   reinterpret_cast<LPCWSTR>(item.text.utf16()));
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0,
                                        nullptr, nullptr);
    DestroyMenu(menu);
    if (command >= 1 && command <= static_cast<UINT>(m_impl->items.size())) {
        const Item item = m_impl->items.at(static_cast<int>(command) - 1);
        if (item.onTriggered)
            item.onTriggered();
    }
}
