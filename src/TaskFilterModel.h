#pragma once

#include <QSortFilterProxyModel>
#include <QSet>
#include <QVariantMap>

class TaskModel;

class TaskTree;

class TaskFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status WRITE setStatus NOTIFY statusChanged)
    // 列过滤（R4-03）：为空时按 status 过滤（旧行为），
    // 非空时只显示落在该列的卡片 —— 同状态的多个自定义列靠它区分。
    Q_PROPERTY(QString columnId READ columnId WRITE setColumnId NOTIFY columnIdChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString stage READ stage WRITE setStage NOTIFY stageChanged)
    // 项目范围过滤（R10-02）：空串 = 全局（不过滤），非空 = 只留该项目的卡。
    // 本模型只读「允许的卡片 id 集合」，不自己查库 —— 查归属是 BoardColumns 的事，
    // 这样过滤模型不必依赖 ProjectStore，任何只用到列的测试也不用连带编项目门面。
    Q_PROPERTY(QString project READ project NOTIFY projectChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit TaskFilterModel(QObject *parent = nullptr);

    QString status() const;
    void setStatus(const QString &status);

    QString columnId() const;
    void setColumnId(const QString &columnId);

    QString search() const;
    void setSearch(const QString &search);

    // 里程碑过滤（R0/R1/R2/R3，留空表示不过滤）
    QString stage() const;
    void setStage(const QString &stage);

    // 项目范围过滤（留空表示全局）
    QString project() const;
    // 原子设置：范围名 + 该范围允许的卡片 id 集合。
    // 拆成两个 setter 的话，中间会有一瞬「新范围 + 旧集合」，列里会闪一下错的卡。
    // 集合为空且 project 非空 = 该项目一张卡都没有（不是「不过滤」）。
    void setProjectScope(const QString &project, const QSet<QString> &taskIds);

    int count() const;

    // 注入主/子任务关联表：子任务按父卡分组，紧跟父卡排列
    void setTaskTree(TaskTree *tree);

    Q_INVOKABLE QString idAt(int row) const;
    // 代理模型不自带写入能力，统一转发到源 TaskModel。
    // columnId 非空时落在该列（动态列快速录入用），否则按 status 落默认列。
    Q_INVOKABLE QString addTask(const QString &title,
                                const QString &status = QStringLiteral("todo"),
                                int priority = 1,
                                const QString &tags = QString(),
                                qint64 dueAt = 0,
                                const QString &columnId = QString()) const;
    // 快速录入解析预览（转发到源 TaskModel）
    Q_INVOKABLE QVariantMap previewInput(const QString &text) const;

signals:
    void statusChanged();
    void columnIdChanged();
    void searchChanged();
    void stageChanged();
    void projectChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    TaskModel *sourceTaskModel() const;

    QString m_status;
    QString m_columnId;
    QString m_search;
    QString m_stage;
    QString m_project;
    QSet<QString> m_projectTaskIds;
    TaskTree *m_tree = nullptr;
};
