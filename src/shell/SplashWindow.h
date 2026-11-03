#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QPainter;

class SplashWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SplashWindow(QWidget *parent = nullptr);

    void setStage(qreal progress, const QString &label);
    void pump(int milliseconds);
    void finish();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct Star
    {
        qreal x;
        qreal y;
        qreal size;
        qreal phase;
        qreal speed;
    };

    void tick();
    void drawBackdrop(QPainter &painter, const QRectF &frame, qreal t) const;
    void drawGrid(QPainter &painter, const QRectF &frame, qreal t) const;
    void drawStars(QPainter &painter, const QRectF &frame, qreal t) const;
    void drawLogo(QPainter &painter, const QRectF &frame, qreal t) const;
    void drawTitle(QPainter &painter, const QRectF &frame, qreal t) const;
    void drawProgress(QPainter &painter, const QRectF &frame, qreal t) const;
    void drawHud(QPainter &painter, const QRectF &frame, qreal t) const;

    QVector<Star> m_stars;
    QElapsedTimer m_clock;
    QTimer m_frameTimer;
    qreal m_elapsed = 0.0;
    qreal m_progress = 0.02;
    qreal m_targetProgress = 0.02;
    qreal m_fadeIn = 0.0;
    qreal m_labelChangedAt = -10.0;
    QString m_label;
    bool m_finished = false;
};
