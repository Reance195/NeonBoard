#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>

class QLocalSocket;

// 单实例（R4-02 扩展：带动作的二次启动）
//
// 语义：进程启动时探测固定名字的本地 socket。
//   · 探测失败 = 我是主实例 → 监听，把收到的动作以 actionRequested 抛出；
//   · 探测成功 = 已有主实例 → 把「本次启动想做的事」写进去，然后正常退出。
//
// 为什么不让第二实例自己开窗口：任务栏会短暂出现两个图标，且两个实例各写一份
// 数据库，是最典型的双写事故。把意图转发过去是唯一正确解。
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    // relayAction：非主实例时转发给主实例的动作（见 AppActions）。
    // 传空串等价于 "show"（只把窗口亮出来）。
    explicit SingleInstance(const QString &key, const QString &relayAction = QString(),
                            QObject *parent = nullptr);

    bool isPrimary() const { return m_primary; }

signals:
    // 主实例收到一次二次启动请求。action 一定是 AppActions::isKnown 为真的值；
    // 陌生/损坏的消息回退为 "show" —— 「点了没反应」比「多亮一次窗口」更糟。
    void actionRequested(const QString &action);

private slots:
    void acceptConnections();

private:
    void readAvailable(QLocalSocket *socket);
    void dispatch(QLocalSocket *socket);

    QString m_key;
    bool m_primary = false;
    QLocalServer m_server;
};
