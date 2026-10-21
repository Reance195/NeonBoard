#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>

class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(const QString &key, QObject *parent = nullptr);

    bool isPrimary() const { return m_primary; }

signals:
    void showRequested();

private slots:
    void acceptConnections();

private:
    QString m_key;
    bool m_primary = false;
    QLocalServer m_server;
};
