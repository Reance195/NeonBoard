#include "Hotkey.h"

#include <QCoreApplication>
#include <QDebug>
#include <QQuickWindow>

#include <windows.h>

Hotkey::Hotkey(QObject *parent)
    : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

Hotkey::~Hotkey()
{
    uninstall();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool Hotkey::install(QObject *window)
{
    auto *quickWindow = qobject_cast<QQuickWindow *>(window);
    if (!quickWindow) {
        m_error = QStringLiteral("无效窗口");
        emit registeredChanged();
        return false;
    }

    uninstall();
    m_hwnd = reinterpret_cast<void *>(quickWindow->winId());
    const BOOL ok = RegisterHotKey(reinterpret_cast<HWND>(m_hwnd),
                                   kHotkeyId,
                                   MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT,
                                   VK_SPACE);
    m_registered = ok != FALSE;
    m_error = m_registered ? QString()
                           : QStringLiteral("Ctrl+Shift+Space 注册失败，可能被其他程序占用");
    emit registeredChanged();
    return m_registered;
}

void Hotkey::uninstall()
{
    if (m_registered && m_hwnd) {
        UnregisterHotKey(reinterpret_cast<HWND>(m_hwnd), kHotkeyId);
        m_registered = false;
        emit registeredChanged();
    }
    m_hwnd = nullptr;
}

bool Hotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result)

    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;

    auto *msg = static_cast<MSG *>(message);
    if (msg && msg->message == WM_HOTKEY && msg->wParam == kHotkeyId) {
        emit activated();
        return true;
    }
    return false;
}
