#include "SingleInstance.h"

#include <QLocalSocket>

SingleInstance::SingleInstance(const QString &key, QObject *parent)
    : QObject(parent)
    , m_key(key)
{
    QLocalSocket probe;
    probe.connectToServer(m_key);
    if (probe.waitForConnected(250)) {
        probe.write("show");
        probe.flush();
        probe.waitForBytesWritten(250);
        return;
    }

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
        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        emit showRequested();
        socket->disconnectFromServer();
    }
}
