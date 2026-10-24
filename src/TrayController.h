#pragma once

#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;

class TrayController : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(bool headless = false, QObject *parent = nullptr);
    ~TrayController() override;

    void updateStats(int todo, int doing, int doneToday);
    void showMessage(const QString &title, const QString &body);
    // 实例标签（多预览实例并行时用于区分托盘项）
    void setInstanceLabel(const QString &label);

    Q_INVOKABLE bool isAutostart() const;
    Q_INVOKABLE void setAutostart(bool enabled);

signals:
    void showRequested();
    void hideRequested();
    void newTaskRequested();
    void settingsRequested();
    void quitRequested();

private:
    void refreshAutostartAction();

    bool m_headless = false;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_hideAction = nullptr;
    QAction *m_statsAction = nullptr;
    QAction *m_autostartAction = nullptr;
    QString m_instanceLabel;
};
