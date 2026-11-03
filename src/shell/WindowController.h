#pragma once

#include <QObject>
#include <QPointer>

class QQuickWindow;
class QTimer;

class WindowController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool quitting READ quitting NOTIFY quittingChanged)
    Q_PROPERTY(bool maximized READ maximized NOTIFY maximizedChanged)

public:
    explicit WindowController(QObject *parent = nullptr);

    void attach(QQuickWindow *window);
    void saveGeometry();

    bool quitting() const { return m_quitting; }
    bool maximized() const;

    Q_INVOKABLE void toggleVisibility();
    Q_INVOKABLE void showAndActivate();
    Q_INVOKABLE void hideToTray();
    Q_INVOKABLE void minimize();
    Q_INVOKABLE void toggleMaximize();
    Q_INVOKABLE void closeToTray();
    Q_INVOKABLE void quit();

signals:
    void quittingChanged();
    void maximizedChanged();

private:
    void loadGeometry();
    void scheduleSave();
    void applyNativeStyle();

    QPointer<QQuickWindow> m_window;
    QTimer *m_saveTimer = nullptr;
    bool m_quitting = false;
};
