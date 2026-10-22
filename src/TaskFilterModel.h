#pragma once

#include <QSortFilterProxyModel>
#include <QVariantMap>

class TaskModel;

class TaskTree;

class TaskFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status WRITE setStatus NOTIFY statusChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString stage READ stage WRITE setStage NOTIFY stageChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit TaskFilterModel(QObject *parent = nullptr);

    QString status() const;
    void setStatus(const QString &status);

    QString search() const;
    void setSearch(const QString &search);

    // 里程碑过滤（R0/R1/R2/R3，留空表示不过滤）
    QString stage() const;
    void setStage(const QString &stage);

    int count() const;

    // 注入主/子任务关联表：子任务按父卡分组，紧跟父卡排列
    void setTaskTree(TaskTree *tree);

    Q_INVOKABLE QString idAt(int row) const;
    // 代理模型不自带写入能力，统一转发到源 TaskModel
    Q_INVOKABLE QString addTask(const QString &title,
                                const QString &status = QStringLiteral("todo"),
                                int priority = 1,
                                const QString &tags = QString(),
                                qint64 dueAt = 0) const;
    // 快速录入解析预览（转发到源 TaskModel）
    Q_INVOKABLE QVariantMap previewInput(const QString &text) const;

signals:
    void statusChanged();
    void searchChanged();
    void stageChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    TaskModel *sourceTaskModel() const;

    QString m_status;
    QString m_search;
    QString m_stage;
    TaskTree *m_tree = nullptr;
};
