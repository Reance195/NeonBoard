#include "WindowController.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QWindow>

#include <windows.h>
#include <dwmapi.h>

WindowController::WindowController(QObject *parent)
    : QObject(parent)
{
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, &WindowController::saveGeometry);
}

void WindowController::attach(QQuickWindow *window)
{
    if (!window)
        return;

    m_window = window;
    connect(window, &QWindow::xChanged, this, &WindowController::scheduleSave);
    connect(window, &QWindow::yChanged, this, &WindowController::scheduleSave);
    connect(window, &QWindow::widthChanged, this, &WindowController::scheduleSave);
    connect(window, &QWindow::heightChanged, this, &WindowController::scheduleSave);
    connect(window, &QWindow::visibilityChanged, this, &WindowController::maximizedChanged);

    loadGeometry();
    QTimer::singleShot(0, this, &WindowController::applyNativeStyle);
}

void WindowController::loadGeometry()
{
    if (!m_window)
        return;

    QSettings settings;
    const QRect saved = settings.value(QStringLiteral("window/geometry")).toRect();
    if (!saved.isValid()) {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;
        const QRect available = screen->availableGeometry();
        qInfo() << "SCREEN geometry" << screen->geometry()
                << "available" << available
                << "dpr" << screen->devicePixelRatio()
                << "window" << m_window->size()
                << "windowDpr" << m_window->devicePixelRatio();
        int width = qMin(m_window->width(), available.width() - 32);
        int height = qMin(m_window->height(), available.height() - 32);
        width = qMax(width, qMin(900, available.width()));
        height = qMax(height, qMin(600, available.height()));
        width = qMin(width, available.width());
        height = qMin(height, available.height());
        m_window->resize(width, height);
        m_window->setPosition(available.center() - QPoint(width / 2, height / 2));
        return;
    }

    QRect geometry = saved;
    QScreen *screen = QGuiApplication::screenAt(geometry.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        qInfo() << "SCREEN(saved) geometry" << screen->geometry()
                << "available" << available
                << "dpr" << screen->devicePixelRatio()
                << "saved" << geometry
                << "windowDpr" << m_window->devicePixelRatio();
        geometry.setWidth(qMin(geometry.width(), available.width()));
        geometry.setHeight(qMin(geometry.height(), available.height()));
        if (geometry.right() > available.right())
            geometry.moveRight(available.right());
        if (geometry.bottom() > available.bottom())
            geometry.moveBottom(available.bottom());
        if (geometry.left() < available.left())
            geometry.moveLeft(available.left());
        if (geometry.top() < available.top())
            geometry.moveTop(available.top());
    }

    m_window->setGeometry(geometry);
    if (settings.value(QStringLiteral("window/maximized"), false).toBool())
        m_window->showMaximized();
}

void WindowController::saveGeometry()
{
    if (!m_window)
        return;

    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), m_window->geometry());
    settings.setValue(QStringLiteral("window/maximized"),
                      m_window->visibility() == QWindow::Maximized);
}

void WindowController::scheduleSave()
{
    if (m_saveTimer)
        m_saveTimer->start();
}

void WindowController::applyNativeStyle()
{
    if (!m_window)
        return;

    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (!hwnd)
        return;

    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    const int rounded = 2;
    DwmSetWindowAttribute(hwnd, 33, &rounded, sizeof(rounded));
}

bool WindowController::maximized() const
{
    return m_window && m_window->visibility() == QWindow::Maximized;
}

void WindowController::toggleVisibility()
{
    if (!m_window)
        return;
    if (m_window->isVisible())
        hideToTray();
    else
        showAndActivate();
}

void WindowController::showAndActivate()
{
    if (!m_window)
        return;

    m_window->show();
    m_window->raise();
    m_window->requestActivate();

    QTimer::singleShot(0, this, [this]() {
        if (!m_window)
            return;
        const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
        if (hwnd)
            SetForegroundWindow(hwnd);
    });
}

void WindowController::hideToTray()
{
    if (m_window)
        m_window->hide();
}

void WindowController::minimize()
{
    if (m_window)
        m_window->showMinimized();
}

void WindowController::toggleMaximize()
{
    if (!m_window)
        return;
    if (m_window->visibility() == QWindow::Maximized)
        m_window->showNormal();
    else
        m_window->showMaximized();
    emit maximizedChanged();
}

void WindowController::closeToTray()
{
    hideToTray();
}

void WindowController::quit()
{
    if (m_quitting)
        return;
    m_quitting = true;
    emit quittingChanged();
    saveGeometry();
    QCoreApplication::quit();
}
