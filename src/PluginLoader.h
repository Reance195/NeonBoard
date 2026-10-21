#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class PluginLoader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QString pluginDir READ pluginDir CONSTANT)

public:
    explicit PluginLoader(QObject *parent = nullptr);

    QVariantList plugins() const { return m_plugins; }
    QString pluginDir() const { return m_pluginDir; }

    Q_INVOKABLE void scan();
    Q_INVOKABLE bool openPluginDir();
    QVariantMap pluginById(const QString &id) const;

signals:
    void pluginsChanged();

private:
    QVariantList m_plugins;
    QString m_pluginDir;
};
