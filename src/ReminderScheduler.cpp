#include "ReminderScheduler.h"

#include "TaskExtra.h"
#include "TrayController.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

ReminderScheduler::ReminderScheduler(TaskExtra *extra, TrayController *tray, QObject *parent)
    : QObject(parent)
    , m_extra(extra)
    , m_tray(tray)
{
}

void ReminderScheduler::start(int intervalMs)
{
    if (m_timer)
        return;
    m_timer = new QTimer(this);
    m_timer->setInterval(intervalMs);
    connect(m_timer, &QTimer::timeout, this, &ReminderScheduler::tick);
    m_timer->start();
    // 启动即查一轮：补发应用没跑期间到期的提醒
    tick();
}

void ReminderScheduler::tick()
{
    if (!m_extra)
        return;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QVariantList due = m_extra->dueReminders(now);
    for (const QVariant &value : due) {
        const QVariantMap task = value.toMap();
        const QString id = task.value(QStringLiteral("id")).toString();
        const QString title = task.value(QStringLiteral("title")).toString();
        const qint64 effective = task.value(QStringLiteral("effectiveRemindAt")).toLongLong();

        m_extra->markNotified(id);
        if (m_tray)
            m_tray->showMessage(QStringLiteral("NeonBoard 提醒"), title);
        qInfo().noquote() << "Reminder: fired" << id << title << "effective" << effective;
        emit fired(id, title);
    }
}
