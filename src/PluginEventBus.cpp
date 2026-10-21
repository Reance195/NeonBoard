#include "PluginEventBus.h"

#include "PluginContext.h"

PluginEventBus::PluginEventBus(QObject *parent)
    : QObject(parent)
{
}

void PluginEventBus::subscribe(PluginContext *context, const QStringList &events)
{
    if (!context)
        return;

    QStringList &current = m_subscriptions[context];
    for (const QString &event : events) {
        const QString name = event.trimmed();
        if (name.isEmpty() || current.contains(name))
            continue;
        current.append(name);
    }
}

void PluginEventBus::unsubscribe(PluginContext *context)
{
    if (!context)
        return;
    m_subscriptions.remove(context);
}

QStringList PluginEventBus::subscriptions(PluginContext *context) const
{
    if (!context)
        return {};
    return m_subscriptions.value(context);
}

QStringList PluginEventBus::subscriptions(const PluginContext *context) const
{
    if (!context)
        return {};
    return m_subscriptions.value(const_cast<PluginContext *>(context));
}

int PluginEventBus::publish(const QString &name, const QVariantMap &payload)
{
    if (name.isEmpty())
        return 0;

    int delivered = 0;
    const auto subscribers = m_subscriptions;
    for (auto it = subscribers.cbegin(); it != subscribers.cend(); ++it) {
        if (!it.value().contains(name))
            continue;
        it.key()->deliverEvent(name, payload);
        ++delivered;
    }
    return delivered;
}
