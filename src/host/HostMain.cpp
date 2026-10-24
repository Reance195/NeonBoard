#include "AppPaths.h"
#include "SingleInstance.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QTimer>

#include <windows.h>
#include <shellapi.h>

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kIdleTimerId = 1;
constexpr int kHotkeyId = 1;
constexpr int kIdleCheckMs = 2000;
constexpr int kIdleTicksBeforeStop = 30;
constexpr int kLaunchGraceMs = 15000;

NOTIFYICONDATAW g_trayIcon{};
HWND g_hostWindow = nullptr;
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

void showTrayMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"打开 NeonBoard");
    AppendMenuW(menu, MF_STRING | (isAutostart() ? MF_CHECKED : 0), 2, L"开机自启");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"退出");
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(g_hostWindow);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                    pt.x, pt.y, 0, g_hostWindow, nullptr);
    DestroyMenu(menu);
    if (cmd == 1)
        showBoard();
    else if (cmd == 2)
        setAutostart(!isAutostart());
    else if (cmd == 3)
        quitHost();
}

LRESULT CALLBACK hostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
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
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NeonBoardHost"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

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
    g_trayIcon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_trayIcon.szTip, L"NeonBoard");
    Shell_NotifyIconW(NIM_ADD, &g_trayIcon);

    RegisterHotKey(g_hostWindow, kHotkeyId, MOD_CONTROL | MOD_SHIFT, VK_SPACE);
    SetTimer(g_hostWindow, kIdleTimerId, kIdleCheckMs, nullptr);

    QObject::connect(&instance, &SingleInstance::showRequested, qApp,
                     []() { showBoard(); });
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
