#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>

class Database : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(int taskCount READ taskCount NOTIFY taskCountChanged)

public:
    explicit Database(QObject *parent = nullptr);
    ~Database() override;

    // pathOverride 为空时用 AppPaths::dataDir()/board.db；非空用于自检 / 测试实例，
    // 避免测试数据写进真实看板库。每次 open 都会分配独立连接名，允许同时开多个库。
    bool open(QString *error = nullptr, const QString &pathOverride = QString());
    QString path() const { return m_path; }
    QString connectionName() const { return m_connectionName; }
    int taskCount() const { return m_taskCount; }

    // 上次 open 是否因为文件损坏而重建了空库；备份文件路径与判定原因供界面提示。
    bool recoveredFromCorruption() const { return m_recovered; }
    QString recoveryBackup() const { return m_recoveryBackup; }
    QString recoveryReason() const { return m_recoveryReason; }

    Q_INVOKABLE void refreshStats();

    // ---- 看板与列（schema v2，R4-03）----
    // 一张卡落在哪列由 task.column_id 决定；column_id 为空时按 status 落到默认列。
    // 列只是「同一状态的不同分段」，status 仍是 todo/doing/done 三态，
    // 完成判定 / 撤销 / 提醒等既有逻辑语义不变。
    static constexpr int kSchemaVersion = 2;

    QString defaultBoardId() const;
    QVariantList boards() const;
    QVariantList boardColumns(const QString &boardId) const;
    QString defaultColumnForStatus(const QString &status) const;

    QString pluginValue(const QString &pluginId, const QString &key) const;
    bool setPluginValue(const QString &pluginId, const QString &key, const QString &value);
    bool removePluginValue(const QString &pluginId, const QString &key);

    QString metaValue(const QString &key, const QString &defaultValue = QString()) const;
    bool setMetaValue(const QString &key, const QString &value);
    bool removeMetaValue(const QString &key);

signals:
    void taskCountChanged();

private:
    bool openConnection(QString *error);
    void closeConnection();
    bool integrityOk(QString *reason) const;
    bool recoverCorrupt(const QString &reason, QString *error);
    // schema 版本：0 = 空库，逐级迁移到 kSchemaVersion；高于当前版本直接拒绝打开。
    int userVersion() const;
    bool ensureSchema(QString *error);
    bool createSchemaV1(QString *error);
    bool migrateToV2(QString *error);
    bool exec(const QString &sql, QString *error = nullptr);
    bool execQuery(const QString &sql, QString *error = nullptr);

    QSqlDatabase m_db;
    QString m_path;
    QString m_connectionName;
    int m_taskCount = 0;
    bool m_recovered = false;
    QString m_recoveryBackup;
    QString m_recoveryReason;
};
