#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Database;

class PluginLoader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY pluginsChanged)
    Q_PROPERTY(QString pluginDir READ pluginDir CONSTANT)
    Q_PROPERTY(QStringList disabledPlugins READ disabledPlugins NOTIFY pluginsChanged)

public:
    static constexpr int MinimumApiVersion = 1;
    static constexpr int MaximumApiVersion = 2;

    explicit PluginLoader(Database *database = nullptr, QObject *parent = nullptr);

    QVariantList plugins() const { return m_plugins; }
    /** 扫描到的全部插件条目（含被同 id 覆盖掉的），供插件库区分「已装」与「可安装」 */
    QVariantList candidates() const { return m_candidates; }
    QString pluginDir() const { return m_pluginDir; }
    /** 四个插件源目录（用户安装区 / 程序目录 / 官方源 / 示例源），供热插拔监视使用 */
    QStringList sourceDirs() const;
    QStringList disabledPlugins() const;

    Q_INVOKABLE void scan();
    Q_INVOKABLE bool openPluginDir();
    Q_INVOKABLE QVariantMap pluginById(const QString &id) const;
    Q_INVOKABLE bool setPluginEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool isPluginEnabled(const QString &id) const;

signals:
    void pluginsChanged();

private:
    QStringList readDisabledList() const;
    bool writeDisabledList(const QStringList &ids) const;

    Database *m_database = nullptr;
    QVariantList m_plugins;
    QVariantList m_candidates;
    QString m_pluginDir;
};
