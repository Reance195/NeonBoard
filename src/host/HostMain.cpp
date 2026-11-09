#include "AppPaths.h"
#include "MenuSkin.h"
#include "SingleInstance.h"
#include "TrayMenu.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QDebug>
#include <QList>
#include <QProcess>
#include <QSettings>
#include <QTimer>

#include <windows.h>
#include <shellapi.h>

#include <cstdio>

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kIdleTimerId = 1;

// DX-143：Explorer 重启会广播 TaskbarCreated，托盘图标必须跟着重建，否则永久消失
UINT taskbarCreatedMessage()
{
    static const UINT msg = RegisterWindowMessageW(L"TaskbarCreated");
    return msg;
}
constexpr int kHotkeyId = 1;
constexpr int kIdleCheckMs = 2000;
constexpr int kIdleTicksBeforeStop = 30;
constexpr int kLaunchGraceMs = 15000;

NOTIFYICONDATAW g_trayIcon{};
HWND g_hostWindow = nullptr;
TrayMenu *g_trayMenu = nullptr;
QProcess *g_renderer = nullptr;
QElapsedTimer g_launchGuard;
int g_hiddenTicks = 0;

struct WindowSearch
{
    DWORD pid = 0;
    HWND found = nullptr;
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto *search = reinterpret_cast<WindowSearch *>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search->pid)
        return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr)
        return TRUE;
    if ((GetWindowLongW(hwnd, GWL_STYLE) & WS_CHILD) != 0)
        return TRUE;
    search->found = hwnd;
    return FALSE;
}

HWND findRendererWindow()
{
    if (!g_renderer || g_renderer->state() == QProcess::NotRunning)
        return nullptr;
    WindowSearch search;
    search.pid = static_cast<DWORD>(g_renderer->processId());
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

void startRenderer();

bool isAutostart()
{
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    return run.contains(QStringLiteral("NeonBoard"));
}

void setAutostart(bool enabled)
{
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    if (enabled) {
        const QString command = QStringLiteral("\"%1\"")
                                    .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        run.setValue(QStringLiteral("NeonBoard"), command);
    } else {
        run.remove(QStringLiteral("NeonBoard"));
    }
}

void showBoard()
{
    if (!g_renderer || g_renderer->state() == QProcess::NotRunning) {
        startRenderer();
        return;
    }
    HWND hwnd = findRendererWindow();
    if (!hwnd) {
        startRenderer();
        return;
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

void toggleBoard()
{
    if (!g_renderer || g_renderer->state() == QProcess::NotRunning) {
        startRenderer();
        return;
    }
    HWND hwnd = findRendererWindow();
    if (!hwnd) {
        startRenderer();
        return;
    }
    if (IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
    }
}

void startRenderer()
{
    if (g_renderer && g_renderer->state() != QProcess::NotRunning) {
        HWND hwnd = findRendererWindow();
        if (hwnd) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        return;
    }

    const QString exe = QCoreApplication::applicationDirPath()
                        + QStringLiteral("/NeonBoard.exe");
    if (!QFile::exists(exe)) {
        g_trayIcon.uFlags = NIF_INFO;
        wcscpy_s(g_trayIcon.szInfoTitle, L"NeonBoard");
        wcscpy_s(g_trayIcon.szInfo, L"找不到 NeonBoard.exe");
        Shell_NotifyIconW(NIM_MODIFY, &g_trayIcon);
        g_trayIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        return;
    }

    if (!g_renderer) {
        g_renderer = new QProcess(qApp);
        g_renderer->setProcessChannelMode(QProcess::ForwardedChannels);
        QObject::connect(g_renderer, &QProcess::finished, qApp,
                         [](int code, QProcess::ExitStatus) {
                             g_hiddenTicks = 0;
                             g_launchGuard.invalidate();
                         });
    }

    g_launchGuard.start();
    g_hiddenTicks = 0;
    g_renderer->start(exe, QStringList{QStringLiteral("--hosted")});
}

void checkIdle()
{
    if (!g_renderer || g_renderer->state() == QProcess::NotRunning)
        return;
    if (g_launchGuard.isValid() && g_launchGuard.elapsed() < kLaunchGraceMs)
        return;
    HWND hwnd = findRendererWindow();
    if (!hwnd)
        return;
    if (IsWindowVisible(hwnd)) {
        g_hiddenTicks = 0;
        return;
    }
    if (++g_hiddenTicks >= kIdleTicksBeforeStop) {
        g_hiddenTicks = 0;
        g_renderer->kill();
    }
}

void quitHost()
{
    if (g_renderer && g_renderer->state() != QProcess::NotRunning) {
        g_renderer->kill();
        g_renderer->waitForFinished(3000);
    }
    if (g_trayIcon.cbSize)
        Shell_NotifyIconW(NIM_DELETE, &g_trayIcon);
    PostQuitMessage(0);
}

// 托盘菜单的条目：真实弹窗与预览出图共用一份，避免两边画得不一样。
QList<TrayMenu::Item> buildTrayMenuItems(bool forceAutostartChecked)
{
    QList<TrayMenu::Item> items;

    TrayMenu::Item open;
    open.kind = TrayMenu::Item::Action;
    open.text = QStringLiteral("打开 NeonBoard");
    open.onTriggered = [] { showBoard(); };
    items.append(open);

    TrayMenu::Item autostart;
    autostart.kind = TrayMenu::Item::Action;
    autostart.text = QStringLiteral("开机自启");
    autostart.checkable = true;
    autostart.checked = forceAutostartChecked || isAutostart();
    autostart.onTriggered = [] { setAutostart(!isAutostart()); };
    items.append(autostart);

    TrayMenu::Item separator;
    separator.kind = TrayMenu::Item::Separator;
    items.append(separator);

    TrayMenu::Item quit;
    quit.kind = TrayMenu::Item::Action;
    quit.text = QStringLiteral("退出");
    quit.danger = true;
    quit.onTriggered = [] { quitHost(); };
    items.append(quit);

    return items;
}

// 看板当前主题：与主程序读同一份界面设置（ini 在 com.lin/NeonBoard.ini），
// 所以看板换主题时，托盘菜单的霓虹主色也跟着换。
QString currentThemePreset()
{
    QSettings theme(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("com.lin"), QStringLiteral("NeonBoard"));
    return theme.value(QStringLiteral("appearance/themePreset"), QStringLiteral("neon")).toString();
}

void showTrayMenu()
{
    // 托盘右键菜单（R17-01）：自绘的霓虹弹窗。
    // 原来这里用 CreatePopupMenu/TrackPopupMenu，画出来是 Windows 白底方框，
    // 字号字体全由系统决定，跟 NeonBoard 的赛博霓虹风完全不搭；系统菜单也没有
    // 「让主题系统接管」的接口。现在换成自己画的一层窗口，条目与行为保持不变。
    if (!g_trayMenu)
        g_trayMenu = new TrayMenu();
    g_trayMenu->setPalette(MenuSkin::paletteForPreset(currentThemePreset()));
    g_trayMenu->setItems(buildTrayMenuItems(false));
    g_trayMenu->popupAtCursor();
}

LRESULT CALLBACK hostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == taskbarCreatedMessage()) {
        // DX-143：Explorer 重启后重建托盘图标（g_trayIcon 的配置原样复用）
        Shell_NotifyIconW(NIM_ADD, &g_trayIcon);
        return 0;
    }
    switch (msg) {
    case WM_HOTKEY:
        if (wParam == kHotkeyId)
            toggleBoard();
        return 0;
    case kTrayMessage:
        if (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_LBUTTONDBLCLK)
            toggleBoard();
        else if (LOWORD(lParam) == WM_RBUTTONUP)
            showTrayMenu();
        return 0;
    case WM_TIMER:
        if (wParam == kIdleTimerId)
            checkIdle();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int main(int argc, char **argv)
{
    // 托盘菜单是自绘位图（R17-01），必须先知道屏幕是几 DPI，
    // 否则高缩放屏上菜单会被系统拉伸糊掉。必须在建任何窗口之前声明。
    // SetProcessDpiAwarenessContext 只有 Win10 1703+ 有，动态取，老系统不至于起不来。
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetDpiContextFn = BOOL(WINAPI *)(HANDLE);
        const auto setContext = reinterpret_cast<SetDpiContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext)
            setContext(reinterpret_cast<HANDLE>(-4)); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    }

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NeonBoardHost"));
    QCoreApplication::setApplicationVersion(QStringLiteral(NEONBOARD_VERSION_STRING));
    // 界面设置与主程序同源（ini 在 com.lin/NeonBoard.ini）：沙箱下也指向同一个数据目录。
    QCoreApplication::setOrganizationName(QStringLiteral("com.lin"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("com.lin.neonboard"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    if (AppPaths::isolated())
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, AppPaths::dataDir());

    // 预览出图（验收用）：离屏把菜单画成 PNG，不弹窗、不抢单实例锁。
    const QStringList arguments = QCoreApplication::arguments();
    const int previewIndex = arguments.indexOf(QStringLiteral("--menu-preview"));
    if (previewIndex >= 0) {
        const QString output = previewIndex + 1 < arguments.size() ? arguments.at(previewIndex + 1)
                                                                 : QString();
        if (output.isEmpty()) {
            fprintf(stderr, "--menu-preview needs an output path\n");
            return 2;
        }
        TrayMenu preview;
        preview.setPalette(MenuSkin::paletteForPreset(currentThemePreset()));
        preview.setItems(buildTrayMenuItems(true));
        const bool ok = preview.renderPreview(output, 0);
        fprintf(stderr, "MENU-PREVIEW %s %s\n", qPrintable(output), ok ? "OK" : "FAIL");
        return ok ? 0 : 2;
    }

    SingleInstance instance(AppPaths::singleInstanceKey() + QStringLiteral("-host"));
    if (!instance.isPrimary())
        return 0;

    WNDCLASSW wc{};
    wc.lpfnWndProc = hostWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NeonBoardHostWnd";
    RegisterClassW(&wc);
    g_hostWindow = CreateWindowExW(0, wc.lpszClassName, L"NeonBoardHost", 0, 0, 0, 0, 0,
                                   HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!g_hostWindow)
        return 1;

    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.hWnd = g_hostWindow;
    g_trayIcon.uID = 1;
    g_trayIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_trayIcon.uCallbackMessage = kTrayMessage;
    // 托盘图标用内嵌在 exe 里的应用图标（resources/neonboard.rc，IDI_NEONBOARD=101），
    // 不是系统通用图标；ID 必须与 .rc 里的定义保持同步。
    g_trayIcon.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    wcscpy_s(g_trayIcon.szTip, L"NeonBoard");
    Shell_NotifyIconW(NIM_ADD, &g_trayIcon);

    if (!RegisterHotKey(g_hostWindow, kHotkeyId, MOD_CONTROL | MOD_SHIFT, VK_SPACE)) {
        // DX-143：注册失败必须可见——HUD 仍在宣传这个快捷键，静默失败等于骗人；
        // 托盘与二次启动唤起不依赖热键，降级可用
        qWarning() << "HostMain: 全局热键 Ctrl+Shift+Space 注册失败（可能被其他程序占用），"
                      "托盘与二次启动唤起仍可用";
    }
    SetTimer(g_hostWindow, kIdleTimerId, kIdleCheckMs, nullptr);

    // 宿主只负责「常驻 + 唤起」，不区分动作：收到任何二次启动请求都把看板亮出来
    QObject::connect(&instance, &SingleInstance::actionRequested, qApp,
                     [](const QString &) { showBoard(); });
    if (QCoreApplication::arguments().contains(QStringLiteral("--open")))
        QTimer::singleShot(0, qApp, []() { showBoard(); });
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, []() {
        if (g_renderer && g_renderer->state() != QProcess::NotRunning)
            g_renderer->kill();
        if (g_trayIcon.cbSize)
            Shell_NotifyIconW(NIM_DELETE, &g_trayIcon);
        if (g_hostWindow)
            KillTimer(g_hostWindow, kIdleTimerId);
        UnregisterHotKey(g_hostWindow, kHotkeyId);
    });

    return app.exec();
}
