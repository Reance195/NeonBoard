#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>

class Database : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(int taskCount READ taskCount NOTIFY taskCountChanged)

public:
    explicit Database(QObject *parent = nullptr);
    ~Database() override;

    bool open(QString *error = nullptr);
    QString path() const { return m_path; }
    QString connectionName() const { return m_connectionName; }
    int taskCount() const { return m_taskCount; }

    Q_INVOKABLE void refreshStats();

    QString pluginValue(const QString &pluginId, const QString &key) const;
    bool setPluginValue(const QString &pluginId, const QString &key, const QString &value);
    bool removePluginValue(const QString &pluginId, const QString &key);

signals:
    void taskCountChanged();

private:
    bool exec(const QString &sql, QString *error = nullptr);
    bool execQuery(const QString &sql, QString *error = nullptr);

    QSqlDatabase m_db;
    QString m_path;
    QString m_connectionName;
    int m_taskCount = 0;
};
