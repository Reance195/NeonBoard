#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class PluginContext;

class PluginEventBus : public QObject
{
    Q_OBJECT

public:
    explicit PluginEventBus(QObject *parent = nullptr);

    void subscribe(PluginContext *context, const QStringList &events);
    void unsubscribe(PluginContext *context);
    QStringList subscriptions(PluginContext *context) const;
    QStringList subscriptions(const PluginContext *context) const;
    int publish(const QString &name, const QVariantMap &payload);

private:
    QHash<PluginContext *, QStringList> m_subscriptions;
};
