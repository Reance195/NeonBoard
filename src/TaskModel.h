#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class Database;

class TaskModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countsChanged)
    Q_PROPERTY(int todoCount READ todoCount NOTIFY countsChanged)
    Q_PROPERTY(int doingCount READ doingCount NOTIFY countsChanged)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY countsChanged)
    Q_PROPERTY(int doneTodayCount READ doneTodayCount NOTIFY countsChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        NoteRole,
        StatusRole,
        PriorityRole,
        TagsRole,
        OrderKeyRole,
        DueAtRole,
        CreatedAtRole,
        UpdatedAtRole,
        CompletedAtRole,
        TagRole,
        DueRole
    };
    Q_ENUM(Role)

    explicit TaskModel(Database *database, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int todoCount() const;
    int doingCount() const;
    int doneCount() const;
    int doneTodayCount() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE QString addTask(const QString &title,
                                const QString &status = QStringLiteral("todo"),
                                int priority = 1,
                                const QString &tags = QString(),
                                qint64 dueAt = 0);
    Q_INVOKABLE bool updateTask(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool deleteTask(const QString &id);
    Q_INVOKABLE QVariantMap removeTask(const QString &id);
    Q_INVOKABLE QString restoreTask(const QVariantMap &task);
    Q_INVOKABLE bool moveTask(const QString &id, const QString &status, int newIndex);
    // 精确插位：把任务挪到目标状态列中 beforeId 这张卡片之前；beforeId 为空表示追加到列尾
    Q_INVOKABLE bool moveTaskBefore(const QString &id, const QString &status, const QString &beforeId);
    // 指定状态下按 order_key 升序的任务 id 列表，供界面计算插入落点（不受搜索过滤影响）
    Q_INVOKABLE QStringList taskIdsInStatus(const QString &status) const;
    Q_INVOKABLE bool moveTaskToEnd(const QString &id, const QString &status);
    Q_INVOKABLE bool clearDone();
    Q_INVOKABLE QVariantMap taskById(const QString &id) const;
    Q_INVOKABLE QVariantList allTasks() const;
    // 按指定 id 幂等写入（不存在则插入，存在则按字段更新），用于路线图快照同步
    Q_INVOKABLE QString upsertTask(const QString &id, const QVariantMap &fields);
    // 快速录入预览：把输入文本拆成标题/标签/优先级/到期时间，供界面实时提示
    Q_INVOKABLE QVariantMap previewInput(const QString &text) const;
    // 按里程碑标签（首个标签形如 R0/R1/R2/R3）聚合各阶段的任务完成度
    Q_INVOKABLE QVariantMap stageStats() const;

signals:
    void countsChanged();
    void taskCreated(const QString &id, const QString &title);
    void taskUpdated(const QString &id);
    void taskMoved(const QString &id, const QString &fromStatus, const QString &toStatus);
    void taskCompleted(const QString &id);
    void taskRemoved(const QString &id);

private:
    struct Task {
        QString id;
        QString title;
        QString note;
        QString status;
        int priority = 0;
        QString tags;
        qint64 orderKey = 0;
        qint64 dueAt = 0;
        qint64 createdAt = 0;
        qint64 updatedAt = 0;
        qint64 completedAt = 0;
    };

    int findRow(const QString &id) const;
    qint64 nextOrderKey(const QString &status) const;
    QVector<qint64> orderKeys(const QString &status, const QString &excludeId) const;
    bool renumberStatus(const QString &status);
    static QString normalizeTags(const QString &tags);
    static QString mergeTags(const QString &baseTags, const QStringList &extraTags);
    static QString firstTag(const QString &tags);
    static QString dueText(qint64 dueAt);
    // 里程碑标签：首个标签形如 R0/R1/R2/R3 时返回该阶段，否则归入「其他」
    static QString stageOf(const QString &tags);

    Database *m_database = nullptr;
    QVector<Task> m_tasks;
};
