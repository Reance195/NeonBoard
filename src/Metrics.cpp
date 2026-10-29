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
        // CPU 时间与墙钟同时取：两者之差就是「等 I/O / 等调度」的部分。
        // 冷启动时这个差值能占到一半以上，说明瓶颈在磁盘而不是代码。
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            auto toMs = [](const FILETIME &ft) {
                ULARGE_INTEGER value{};
                value.LowPart = ft.dwLowDateTime;
                value.HighPart = ft.dwHighDateTime;
                return static_cast<qint64>(value.QuadPart / 10000); // 100ns → ms
            };
            m_startupCpuMs = toMs(kernel) + toMs(user);
        }
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
    // cpu 放在最后：CI 的 check-perf.ps1 用正则从这一行里抓 startup / fps / mem，
    // 老字段保持在原位，加字段不改变它们的可匹配性。
    return QStringLiteral("startup=%1ms fps=%2 mem=%3MB frames=%4 cpu=%5ms")
        .arg(m_startupMs)
        .arg(QString::number(m_fps, 'f', 1))
        .arg(m_workingSetMb)
        .arg(m_frameCount)
        .arg(m_startupCpuMs);
}
