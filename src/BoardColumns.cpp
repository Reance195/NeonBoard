#include "BoardColumns.h"

#include "Database.h"
#include "ProjectStore.h"
#include "TaskFilterModel.h"
#include "TaskModel.h"
#include "TaskTree.h"

#include <QDebug>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

BoardColumns::BoardColumns(Database *database, TaskModel *tasks, QObject *parent)
    : QAbstractListModel(parent)
    , m_database(database)
    , m_tasks(tasks)
{
}

void BoardColumns::setTaskTree(TaskTree *tree)
{
    m_tree = tree;
    for (Column &column : m_columns) {
        if (column.model)
            column.model->setTaskTree(tree);
    }
}

int BoardColumns::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_columns.size());
}

QVariant BoardColumns::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_columns.size())
        return {};

    const Column &column = m_columns.at(index.row());
    switch (role) {
    case IdRole:
        return column.id;
    case TitleRole:
        return column.title;
    case StatusRole:
        return column.status;
    case ModelRole:
        return QVariant::fromValue(static_cast<QObject *>(column.model));
    case IndexRole:
        return index.row();
    default:
        return {};
    }
}

QHash<int, QByteArray> BoardColumns::roleNames() const
{
    // 角色名带 boardColumn 前缀：QML 里 BoardColumn 组件自身就有 title / status /
    // taskModel 属性，同名角色和组件属性撞名会让 required property 直接编译报错。
    return {
        {IdRole, QByteArrayLiteral("boardColumnId")},
        {TitleRole, QByteArrayLiteral("boardColumnTitle")},
        {StatusRole, QByteArrayLiteral("boardColumnStatus")},
        {ModelRole, QByteArrayLiteral("boardColumnModel")},
        {IndexRole, QByteArrayLiteral("boardColumnIndex")},
    };
}

void BoardColumns::setSearch(const QString &search)
{
    if (m_search == search)
        return;
    m_search = search;
    for (Column &column : m_columns) {
        if (column.model)
            column.model->setSearch(search);
    }
    emit searchChanged();
}

void BoardColumns::setStage(const QString &stage)
{
    if (m_stage == stage)
        return;
    m_stage = stage;
    for (Column &column : m_columns) {
        if (column.model)
            column.model->setStage(stage);
    }
    emit stageChanged();
}

void BoardColumns::setProject(const QString &project)
{
    if (m_project == project)
        return;
    m_project = project;
    applyProjectScope();
    emit projectChanged();
    // 列的卡片数变了（各列模型的 countChanged 会转发到 countsChanged），
    // 顶栏统计片要跟着重算，否则切到项目视图后数字还是全局的。
    emit countsChanged();
}

void BoardColumns::setProjectStore(ProjectStore *store)
{
    if (m_projects == store)
        return;
    if (m_projects)
        m_projects->disconnect(this);
    m_projects = store;
    if (m_projects) {
        // 归类脚本跑完、详情抽屉里改了归属 —— 范围名没变但集合变了，
        // 不重算的话项目视图会一直显示旧的那批卡。
        connect(m_projects, &ProjectStore::changed, this, [this]() {
            applyProjectScope();
            emit countsChanged();
        });
    }
    applyProjectScope();
}

void BoardColumns::applyProjectScope()
{
    // 集合查在这里而不是过滤模型里：过滤模型只管行过滤，不碰数据库。
    // 一次查询喂给所有列，别让每列各查一遍。
    QSet<QString> ids;
    if (m_projects && !m_project.isEmpty()) {
        const QStringList list = m_projects->taskIdsOf(m_project);
        ids = QSet<QString>(list.begin(), list.end());
    }
    for (Column &column : m_columns) {
        if (column.model)
            column.model->setProjectScope(m_project, ids);
    }
}

TaskFilterModel *BoardColumns::createColumnModel(const QString &columnId, const QString &status)
{
    auto *filter = new TaskFilterModel(this);
    filter->setSourceModel(m_tasks);
    filter->setColumnId(columnId);
    filter->setStatus(status);
    filter->setTaskTree(m_tree);
    // 新建的列模型必须继承当前的搜索 / 阶段过滤，
    // 否则列管理新增一列后，它会把被过滤掉的任务也显示出来。
    // 项目范围的集合由 reload() 末尾的 applyProjectScope() 统一喂（这里查库会每列重复查一次）。
    filter->setSearch(m_search);
    filter->setStage(m_stage);
    connect(filter, &TaskFilterModel::countChanged, this, &BoardColumns::countsChanged);
    return filter;
}

void BoardColumns::clearColumns()
{
    for (Column &column : m_columns) {
        if (column.model) {
            column.model->disconnect(this);
            column.model->deleteLater();
        }
    }
    m_columns.clear();
}

void BoardColumns::reload()
{
    if (!m_database || !m_tasks)
        return;

    QVariantList rows = m_database->boardColumns(m_database->defaultBoardId());
    if (rows.isEmpty()) {
        // 看板表读不出来（老库迁移异常 / 被外部工具清空）：退化成「一状态一列」，
        // 让界面还能用；columnId 为空时过滤器回退按 status 过滤。
        qWarning() << "BoardColumns: board_column 为空，退化为按状态的默认三列";
        const QStringList statuses{QStringLiteral("todo"), QStringLiteral("doing"),
                                   QStringLiteral("done")};
        const QStringList titles{QStringLiteral("待办"), QStringLiteral("进行中"),
                                 QStringLiteral("已完成")};
        for (int i = 0; i < statuses.size(); ++i) {
            QVariantMap synthetic;
            synthetic.insert(QStringLiteral("id"),
                             m_database->defaultColumnForStatus(statuses.at(i)));
            synthetic.insert(QStringLiteral("name"), titles.at(i));
            synthetic.insert(QStringLiteral("status"), statuses.at(i));
            rows.append(synthetic);
        }
    }

    QStringList newIds;
    newIds.reserve(rows.size());
    for (const QVariant &row : rows)
        newIds.append(row.toMap().value(QStringLiteral("id")).toString());

    QStringList oldIds;
    oldIds.reserve(m_columns.size());
    for (const Column &column : m_columns)
        oldIds.append(column.id);

    if (newIds == oldIds) {
        // 列集合没变：只刷新标题/状态，不重建模型 ——
        // 重建会让已经实例化的列丢掉选中卡与滚动位置。
        bool changed = false;
        for (int i = 0; i < m_columns.size(); ++i) {
            const QVariantMap row = rows.at(i).toMap();
            Column &column = m_columns[i];
            const QString title = row.value(QStringLiteral("name")).toString();
            const QString status = row.value(QStringLiteral("status")).toString();
            if (column.title != title) {
                column.title = title;
                changed = true;
            }
            if (column.status != status) {
                column.status = status;
                if (column.model)
                    column.model->setStatus(status);
                changed = true;
            }
        }
        if (changed && !m_columns.isEmpty())
            emit dataChanged(index(0), index(static_cast<int>(m_columns.size()) - 1));
        emit columnsChanged();
        return;
    }

    beginResetModel();
    clearColumns();
    for (const QVariant &row : rows) {
        const QVariantMap map = row.toMap();
        Column column;
        column.id = map.value(QStringLiteral("id")).toString();
        column.title = map.value(QStringLiteral("name")).toString();
        column.status = map.value(QStringLiteral("status")).toString();
        if (column.title.isEmpty())
            column.title = column.status;
        column.model = createColumnModel(column.id, column.status);
        m_columns.append(column);
    }
    endResetModel();

    // 新列建出来时还没有范围集合（createColumnModel 只知道范围名，不查库），
    // 在这里统一喂一次，否则重建列之后项目视图会短暂显示全部卡片。
    applyProjectScope();

    emit columnsChanged();
    emit countsChanged();
}

int BoardColumns::indexOfColumn(const QString &columnId) const
{
    for (int i = 0; i < m_columns.size(); ++i) {
        if (m_columns.at(i).id == columnId)
            return i;
    }
    return -1;
}

QString BoardColumns::columnIdForStatus(const QString &status) const
{
    for (const Column &column : m_columns) {
        if (column.status == status)
            return column.id;
    }
    return {};
}

void BoardColumns::setLastError(const QString &error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

QString BoardColumns::addColumn(const QString &name, const QString &status)
{
    if (!m_database)
        return {};
    QString error;
    const QString id = m_database->addColumn(name, status, &error);
    if (id.isEmpty()) {
        setLastError(error);
        return {};
    }
    setLastError(QString());
    reload();
    return id;
}

bool BoardColumns::renameColumn(const QString &columnId, const QString &name)
{
    if (!m_database)
        return false;
    QString error;
    if (!m_database->renameColumn(columnId, name, &error)) {
        setLastError(error);
        return false;
    }
    setLastError(QString());
    reload();
    return true;
}

bool BoardColumns::moveColumn(const QString &columnId, int delta)
{
    if (!m_database)
        return false;
    QString error;
    if (!m_database->moveColumn(columnId, delta, &error)) {
        // 越界不是错误，是「到头了」：不打扰用户，也不留错误提示
        setLastError(error.isEmpty() ? m_lastError : error);
        return false;
    }
    setLastError(QString());
    reload();
    return true;
}

bool BoardColumns::removeColumn(const QString &columnId)
{
    if (!m_database)
        return false;
    QString error;
    if (!m_database->deleteColumn(columnId, &error)) {
        setLastError(error);
        return false;
    }
    setLastError(QString());
    // 删列会把卡片迁到同状态的另一列（库里的 column_id 变了），
    // 内存里的任务副本还是旧列 id —— 不重读任务，被迁走的卡片会从所有列里消失。
    if (m_tasks)
        m_tasks->load();
    reload();
    return true;
}

int BoardColumns::columnCountForStatus(const QString &status) const
{
    int total = 0;
    for (const Column &column : m_columns) {
        if (column.status == status)
            ++total;
    }
    return total;
}

QString BoardColumns::columnStatusOf(const QString &columnId) const
{
    for (const Column &column : m_columns) {
        if (column.id == columnId)
            return column.status;
    }
    return {};
}

QString BoardColumns::titleOf(const QString &columnId) const
{
    if (columnId.isEmpty())
        return {};
    for (const Column &column : m_columns) {
        if (column.id == columnId)
            return column.title;
    }
    return {};
}

int BoardColumns::countForStatus(const QString &status) const
{
    int total = 0;
    for (const Column &column : m_columns) {
        if (column.status == status && column.model)
            total += column.model->count();
    }
    return total;
}
