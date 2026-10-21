#include "PluginHost.h"

#include "PluginContext.h"
#include "PluginRuntime.h"

#include <QDebug>

PluginHost::PluginHost(PluginRuntime *runtime, QObject *parent)
    : QObject(parent)
    , m_runtime(runtime)
{
}

PluginContext *PluginHost::active() const
{
    return m_runtime ? m_runtime->activeContext() : nullptr;
}

QString PluginHost::pluginId() const
{
    PluginContext *context = active();
    return context ? context->pluginId() : QString();
}

QStringList PluginHost::permissions() const
{
    PluginContext *context = active();
    return context ? context->permissions() : QStringList();
}

bool PluginHost::isActive() const
{
    return active() != nullptr;
}

bool PluginHost::activate(const QString &pluginId)
{
    if (pluginId.isEmpty()) {
        deactivate();
        return true;
    }

    if (!m_runtime)
        return false;

    PluginContext *context = m_runtime->contextFor(pluginId);
    if (!context) {
        qWarning().noquote() << "PluginHost: activate failed, no context for" << pluginId;
        return false;
    }

    m_runtime->setActiveContext(context);
    qInfo().noquote() << "PluginHost: activated" << pluginId;
    emit pluginIdChanged();
    return true;
}

void PluginHost::deactivate()
{
    if (!m_runtime)
        return;
    if (!m_runtime->activeContext())
        return;
    m_runtime->setActiveContext(nullptr);
    emit pluginIdChanged();
}

bool PluginHost::hasPermission(const QString &permission) const
{
    PluginContext *context = active();
    return context && context->hasPermission(permission);
}

QString PluginHost::storageGet(const QString &key) const
{
    PluginContext *context = active();
    return context ? context->storageGet(key) : QString();
}

bool PluginHost::storageSet(const QString &key, const QString &value)
{
    PluginContext *context = active();
    return context ? context->storageSet(key, value) : false;
}

bool PluginHost::storageRemove(const QString &key)
{
    PluginContext *context = active();
    return context ? context->storageRemove(key) : false;
}

QVariantList PluginHost::tasks() const
{
    PluginContext *context = active();
    return context ? context->tasks() : QVariantList();
}

QString PluginHost::addTask(const QString &title, const QString &status)
{
    PluginContext *context = active();
    return context ? context->addTask(title, status) : QString();
}

bool PluginHost::updateTask(const QString &id, const QVariantMap &fields)
{
    PluginContext *context = active();
    return context ? context->updateTask(id, fields) : false;
}

bool PluginHost::moveTask(const QString &id, const QString &status)
{
    PluginContext *context = active();
    return context ? context->moveTask(id, status) : false;
}

bool PluginHost::moveTaskToEnd(const QString &id, const QString &status)
{
    PluginContext *context = active();
    return context ? context->moveTaskToEnd(id, status) : false;
}

bool PluginHost::deleteTask(const QString &id)
{
    PluginContext *context = active();
    return context ? context->deleteTask(id) : false;
}

void PluginHost::notify(const QString &title, const QString &body)
{
    PluginContext *context = active();
    if (context)
        context->notify(title, body);
}

void PluginHost::log(const QString &message)
{
    PluginContext *context = active();
    if (context) {
        context->log(message);
        return;
    }
    qInfo().noquote() << "[plugin]" << message;
}

bool PluginHost::openUrl(const QString &url)
{
    PluginContext *context = active();
    return context ? context->openUrl(url) : false;
}

QString PluginHost::metaValue(const QString &key, const QString &defaultValue) const
{
    PluginContext *context = active();
    return context ? context->metaValue(key, defaultValue) : defaultValue;
}

bool PluginHost::setMetaValue(const QString &key, const QString &value)
{
    PluginContext *context = active();
    return context ? context->setMetaValue(key, value) : false;
}

bool PluginHost::removeMetaValue(const QString &key)
{
    PluginContext *context = active();
    return context ? context->removeMetaValue(key) : false;
}

bool PluginHost::subscribe(const QStringList &events)
{
    PluginContext *context = active();
    return context ? context->subscribe(events) : false;
}

QStringList PluginHost::subscriptions() const
{
    PluginContext *context = active();
    return context ? context->subscriptions() : QStringList();
}
