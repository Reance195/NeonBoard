#pragma once

#include <QAbstractListModel>
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
    Q_INVOKABLE bool clearDone();
    Q_INVOKABLE QVariantMap taskById(const QString &id) const;
    Q_INVOKABLE QVariantList allTasks() const;

signals:
    void countsChanged();

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
    static QString firstTag(const QString &tags);
    static QString dueText(qint64 dueAt);

    Database *m_database = nullptr;
    QVector<Task> m_tasks;
};
