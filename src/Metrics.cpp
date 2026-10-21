#include "Metrics.h"

#include <QDebug>
#include <QQuickWindow>
#include <QTimer>

#include <windows.h>
#include <psapi.h>

QElapsedTimer Metrics::s_startup;

Metrics::Metrics(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &Metrics::tick);
    m_timer->start();
}

void Metrics::startStartupTimer()
{
    s_startup.start();
}

void Metrics::markWindow(QObject *window)
{
    auto *quickWindow = qobject_cast<QQuickWindow *>(window);
    if (!quickWindow)
        return;

    m_fpsTimer.start();
    connect(quickWindow, &QQuickWindow::frameSwapped, this, &Metrics::onFrameSwapped,
            Qt::UniqueConnection);
}

void Metrics::onFrameSwapped()
{
    if (m_startupMs < 0) {
        m_startupMs = s_startup.elapsed();
        emit startupMeasured();
    }

    ++m_frameCount;
    ++m_framesInInterval;

    const qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed >= 500) {
        m_fps = (m_framesInInterval * 1000.0) / static_cast<double>(elapsed);
        m_framesInInterval = 0;
        m_fpsTimer.restart();
        emit statsChanged();
    }
}

void Metrics::tick()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters))) {
        m_workingSetMb = static_cast<qint64>(counters.WorkingSetSize / (1024 * 1024));
    }
    emit statsChanged();
}

QString Metrics::report() const
{
    return QStringLiteral("startup=%1ms fps=%2 mem=%3MB frames=%4")
        .arg(m_startupMs)
        .arg(QString::number(m_fps, 'f', 1))
        .arg(m_workingSetMb)
        .arg(m_frameCount);
}
