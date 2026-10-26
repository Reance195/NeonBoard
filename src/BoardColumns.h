#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

class Database;
class TaskFilterModel;
class TaskModel;
class TaskTree;

// 看板列清单（R4-03 批次三）：把 board_column 表映射成 QML 可以直接 Repeater 的列表模型。
//
// 设计取舍：
//   1. 每列自带一个 TaskFilterModel（按 columnId 过滤），QML 只负责画，不再自己维护
//      「哪三列、叫什么、过滤什么状态」—— 列结构的唯一事实源是数据库。
//   2. 搜索 / 里程碑过滤统一在这里落到底下列模型，界面只设一次，避免逐列同步漏改。
//   3. 列的增删才 reset 模型；仅标题/顺序变化走 dataChanged，保住界面里的选中态与滚动位置。
class BoardColumns : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString stage READ stage WRITE setStage NOTIFY stageChanged)
    Q_PROPERTY(int count READ count NOTIFY columnsChanged)
    // 顶栏统计片用：按「状态」汇总（同状态的多个列相加）
    Q_PROPERTY(int todoCount READ todoCount NOTIFY countsChanged)
    Q_PROPERTY(int doingCount READ doingCount NOTIFY countsChanged)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY countsChanged)
    // 最近一次列管理操作的失败原因（成功时清空），供界面直接提示
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        StatusRole,
        ModelRole,
        IndexRole
    };
    Q_ENUM(Role)

    explicit BoardColumns(Database *database, TaskModel *tasks, QObject *parent = nullptr);

    void setTaskTree(TaskTree *tree);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString search() const { return m_search; }
    void setSearch(const QString &search);

    QString stage() const { return m_stage; }
    void setStage(const QString &stage);

    int count() const { return static_cast<int>(m_columns.size()); }
    int todoCount() const { return countForStatus(QStringLiteral("todo")); }
    int doingCount() const { return countForStatus(QStringLiteral("doing")); }
    int doneCount() const { return countForStatus(QStringLiteral("done")); }

    // 从 board_column 重读列（启动时与列管理操作后调用）
    Q_INVOKABLE void reload();
    Q_INVOKABLE int indexOfColumn(const QString &columnId) const;
    // 该状态的默认列 id（快速录入「新建到某状态」用）；没有则返回空串
    Q_INVOKABLE QString columnIdForStatus(const QString &status) const;

    // ---- 列管理：写库后 reload，界面不需要自己拼状态 ----
    Q_INVOKABLE QString addColumn(const QString &name, const QString &status);
    Q_INVOKABLE bool renameColumn(const QString &columnId, const QString &name);
    Q_INVOKABLE bool moveColumn(const QString &columnId, int delta);
    Q_INVOKABLE bool removeColumn(const QString &columnId);
    // 某状态还剩几列：用来判断「删除」是否可用（最后一列不许删）
    Q_INVOKABLE int columnCountForStatus(const QString &status) const;
    Q_INVOKABLE QString columnStatusOf(const QString &columnId) const;

    QString lastError() const { return m_lastError; }

signals:
    void columnsChanged();
    void searchChanged();
    void stageChanged();
    void countsChanged();
    void lastErrorChanged();

private:
    struct Column {
        QString id;
        QString title;
        QString status;
        TaskFilterModel *model = nullptr;
    };

    TaskFilterModel *createColumnModel(const QString &columnId, const QString &status);
    void clearColumns();
    int countForStatus(const QString &status) const;
    void setLastError(const QString &error);

    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    TaskTree *m_tree = nullptr;
    QVector<Column> m_columns;
    QString m_search;
    QString m_stage;
    QString m_lastError;
};
