#pragma once

#include <QObject>

class TaskExtra;
class TrayController;
class QTimer;

// 提醒调度器：定时轮询到期提醒，触发托盘通知 + 窗口辉光，并写 notified_at 防重复。
// 启动补发漏提醒：同一条查询天然覆盖「应用没跑期间到期」的提醒。
class ReminderScheduler : public QObject
{
    Q_OBJECT
public:
    ReminderScheduler(TaskExtra *extra, TrayController *tray, QObject *parent = nullptr);

    Q_INVOKABLE void start(int intervalMs = 30000);

signals:
    // 提醒触发（QML 侧据此播放窗口边缘辉光动画）
    void fired(const QString &taskId, const QString &title);

private:
    void tick();

    TaskExtra *m_extra = nullptr;
    TrayController *m_tray = nullptr;
    QTimer *m_timer = nullptr;
};
