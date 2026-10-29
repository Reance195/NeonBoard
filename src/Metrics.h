#pragma once

#include <QElapsedTimer>
#include <QObject>

class QQuickWindow;
class QTimer;

class Metrics : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 startupMs READ startupMs NOTIFY startupMeasured)
    // 首帧时刻的进程 CPU 时间（内核 + 用户）。
    // 为什么不只看墙钟：同一份二进制在本机不同负载下测出来能从 0.9s 飘到 2.1s，
    // 墙钟里混着磁盘等待、杀软扫描、其他 agent 抢占，拿来判断「这次改动有没有用」
    // 只会得出随机结论。CPU 时间只统计真正烧掉的处理时间，是这台机器上唯一稳定的口径。
    Q_PROPERTY(qint64 startupCpuMs READ startupCpuMs NOTIFY startupMeasured)
    Q_PROPERTY(qreal fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(qint64 workingSetMb READ workingSetMb NOTIFY statsChanged)
    Q_PROPERTY(qint64 frameCount READ frameCount NOTIFY statsChanged)

public:
    explicit Metrics(QObject *parent = nullptr);

    static void startStartupTimer();

    qint64 startupMs() const { return m_startupMs; }
    qint64 startupCpuMs() const { return m_startupCpuMs; }
    qreal fps() const { return m_fps; }
    qint64 workingSetMb() const { return m_workingSetMb; }
    qint64 frameCount() const { return m_frameCount; }

    Q_INVOKABLE void markWindow(QObject *window);
    Q_INVOKABLE QString report() const;

signals:
    void startupMeasured();
    void statsChanged();

private slots:
    void onFrameSwapped();
    void tick();

private:
    static QElapsedTimer s_startup;

    QElapsedTimer m_fpsTimer;
    QTimer *m_timer = nullptr;
    int m_framesInInterval = 0;
    qint64 m_frameCount = 0;
    qreal m_fps = 0.0;
    qint64 m_startupMs = -1;
    qint64 m_startupCpuMs = -1;
    qint64 m_workingSetMb = 0;
};
