#pragma once

#include <QElapsedTimer>
#include <QObject>

class QQuickWindow;
class QTimer;

class Metrics : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 startupMs READ startupMs NOTIFY startupMeasured)
    Q_PROPERTY(qreal fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(qint64 workingSetMb READ workingSetMb NOTIFY statsChanged)
    Q_PROPERTY(qint64 frameCount READ frameCount NOTIFY statsChanged)

public:
    explicit Metrics(QObject *parent = nullptr);

    static void startStartupTimer();

    qint64 startupMs() const { return m_startupMs; }
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
    qint64 m_workingSetMb = 0;
};
