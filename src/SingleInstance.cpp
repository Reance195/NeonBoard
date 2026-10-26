#include "SingleInstance.h"

#include "AppActions.h"

#include <QDebug>
#include <QLocalSocket>
#include <QVariant>

namespace {

constexpr int kProbeTimeoutMs = 250;
// 与 AppActions 的帧上限呼应：这里再兜一层，避免对端只写不换行时无限累积
constexpr int kMaxPayloadBytes = 8192;
constexpr auto kPayloadProperty = "neonboardPayload";
constexpr auto kDispatchedProperty = "neonboardDispatched";

} // namespace

SingleInstance::SingleInstance(const QString &key, const QString &relayAction, QObject *parent)
    : QObject(parent)
    , m_key(key)
{
    const QString action =
        relayAction.isEmpty() ? QString::fromLatin1(AppActions::kShow) : relayAction;

    QLocalSocket probe;
    probe.connectToServer(m_key);
    if (probe.waitForConnected(kProbeTimeoutMs)) {
        probe.write(AppActions::encodeFrame(action));
        probe.flush();
        probe.waitForBytesWritten(kProbeTimeoutMs);
        // 主动断开并等对端读到 EOF：进程紧接着就退出，若内核里还压着未读数据，
        // 主实例可能只收到半截帧。断连完成是「对端已处理」的最强保证。
        probe.disconnectFromServer();
        if (probe.state() != QLocalSocket::UnconnectedState)
            probe.waitForDisconnected(kProbeTimeoutMs);
        return;
    }

    // 探测失败可能是「主实例已崩、名字还占着」：先清残留再监听
    QLocalServer::removeServer(m_key);
    m_primary = m_server.listen(m_key);
    if (m_primary) {
        connect(&m_server, &QLocalServer::newConnection,
                this, &SingleInstance::acceptConnections);
    }
}

void SingleInstance::acceptConnections()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        socket->setProperty(kPayloadProperty, QByteArray());
        socket->setProperty(kDispatchedProperty, false);

        connect(socket, &QLocalSocket::readyRead, this,
                [this, socket]() { readAvailable(socket); });
        // 断开时才真正收尾：等对端写完再解析，避免读到半个动作名
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            readAvailable(socket);
            dispatch(socket);
            socket->deleteLater();
        });
        if (socket->bytesAvailable() > 0)
            readAvailable(socket);
    }
}

void SingleInstance::readAvailable(QLocalSocket *socket)
{
    QByteArray payload = socket->property(kPayloadProperty).toByteArray();
    payload.append(socket->readAll());
    if (payload.size() > kMaxPayloadBytes) {
        // 不明来源在灌数据：丢掉这次连接的内容，交给 dispatch 走默认动作
        qWarning() << "SingleInstance: 丢弃超长消息" << payload.size();
        payload.clear();
        socket->setProperty(kPayloadProperty, payload);
        return;
    }
    socket->setProperty(kPayloadProperty, payload);

    // 对端可能不主动断开；收到整行就立即分发，别等到连接超时
    if (payload.contains('\n'))
        dispatch(socket);
}

void SingleInstance::dispatch(QLocalSocket *socket)
{
    if (socket->property(kDispatchedProperty).toBool())
        return; // 一行只分发一次：readyRead 与 disconnected 都会走到这里
    socket->setProperty(kDispatchedProperty, true);

    QString action = AppActions::decodeFrame(socket->property(kPayloadProperty).toByteArray());
    if (action.isEmpty()) {
        // 旧版只发裸 "show"、或管道被别的东西连上：退回「亮窗口」，
        // 保持与历史行为一致，且不会把未知字符串当 QML 方法名执行
        action = QString::fromLatin1(AppActions::kShow);
    }
    emit actionRequested(action);
}
