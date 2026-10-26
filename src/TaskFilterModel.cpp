#include "TaskFilterModel.h"

#include "TaskModel.h"
#include "TaskTree.h"

TaskFilterModel::TaskFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortRole(TaskModel::OrderKeyRole);
    sort(0, Qt::AscendingOrder);

    connect(this, &QAbstractItemModel::rowsInserted, this, [this]() { emit countChanged(); });
    connect(this, &QAbstractItemModel::rowsRemoved, this, [this]() { emit countChanged(); });
    connect(this, &QAbstractItemModel::rowsMoved, this, [this]() { emit countChanged(); });
    connect(this, &QAbstractItemModel::modelReset, this, [this]() { emit countChanged(); });
    connect(this, &QAbstractItemModel::layoutChanged, this, [this]() { emit countChanged(); });
}

QString TaskFilterModel::status() const
{
    return m_status;
}

void TaskFilterModel::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    invalidateFilter();
    emit statusChanged();
    emit countChanged();
}

QString TaskFilterModel::columnId() const
{
    return m_columnId;
}

void TaskFilterModel::setColumnId(const QString &columnId)
{
    if (m_columnId == columnId)
        return;
    m_columnId = columnId;
    invalidateFilter();
    emit columnIdChanged();
    emit countChanged();
}

int TaskFilterModel::count() const
{
    return rowCount();
}

QString TaskFilterModel::search() const
{
    return m_search;
}

void TaskFilterModel::setSearch(const QString &search)
{
    const QString clean = search.trimmed();
    if (m_search == clean)
        return;
    m_search = clean;
    invalidateFilter();
    emit searchChanged();
    emit countChanged();
}

QString TaskFilterModel::stage() const
{
    return m_stage;
}

void TaskFilterModel::setStage(const QString &stage)
{
    const QString clean = stage.trimmed().toUpper();
    if (m_stage == clean)
        return;
    m_stage = clean;
    invalidateFilter();
    emit stageChanged();
    emit countChanged();
}

void TaskFilterModel::setTaskTree(TaskTree *tree)
{
    if (m_tree == tree)
        return;
    if (m_tree)
        m_tree->disconnect(this);
    m_tree = tree;
    if (m_tree) {
        connect(m_tree, &TaskTree::changed, this, [this]() {
            invalidate();
            emit countChanged();
        });
    }
    invalidate();
}

QString TaskFilterModel::idAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};
    return mapToSource(index(row, 0)).data(TaskModel::IdRole).toString();
}

TaskModel *TaskFilterModel::sourceTaskModel() const
{
    return qobject_cast<TaskModel *>(sourceModel());
}

QString TaskFilterModel::addTask(const QString &title, const QString &status, int priority,
                                 const QString &tags, qint64 dueAt, const QString &columnId) const
{
    TaskModel *model = sourceTaskModel();
    if (!model)
        return {};
    return model->addTask(title, status, priority, tags, dueAt, columnId);
}

QVariantMap TaskFilterModel::previewInput(const QString &text) const
{
    TaskModel *model = sourceTaskModel();
    if (!model)
        return {};
    return model->previewInput(text);
}

bool TaskFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!m_columnId.isEmpty()) {
        // 列优先：同状态可能有多列，status 过滤会让它们串味
        if (index.data(TaskModel::ColumnIdRole).toString() != m_columnId)
            return false;
    } else if (index.data(TaskModel::StatusRole).toString() != m_status) {
        return false;
    }
    if (!m_stage.isEmpty()) {
        const QString tag = index.data(TaskModel::TagRole).toString().trimmed().toUpper();
        if (tag != m_stage)
            return false;
    }
    if (m_search.isEmpty())
        return true;
    const QString needle = m_search.toCaseFolded();
    const QString title = index.data(TaskModel::TitleRole).toString();
    const QString note = index.data(TaskModel::NoteRole).toString();
    const QString tags = index.data(TaskModel::TagsRole).toString();
    return title.toCaseFolded().contains(needle)
        || note.toCaseFolded().contains(needle)
        || tags.toCaseFolded().contains(needle);
}

bool TaskFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const QString leftId = left.data(TaskModel::IdRole).toString();
    const QString rightId = right.data(TaskModel::IdRole).toString();
    const qint64 leftKey = left.data(TaskModel::OrderKeyRole).toLongLong();
    const qint64 rightKey = right.data(TaskModel::OrderKeyRole).toLongLong();

    // 有主/子关联时按「父卡分组」排：同一父卡的子任务紧跟父卡，
    // 分组之间仍按父卡的 order_key 先后。
    if (m_tree) {
        const qint64 leftGroup = m_tree->groupOrderKey(leftId);
        const qint64 rightGroup = m_tree->groupOrderKey(rightId);
        if (leftGroup != rightGroup)
            return leftGroup < rightGroup;
        const bool leftChild = m_tree->isChild(leftId);
        const bool rightChild = m_tree->isChild(rightId);
        if (leftChild != rightChild)
            return !leftChild; // 父卡排在自己的子任务前面
    }

    if (leftKey != rightKey)
        return leftKey < rightKey;
    return leftId < rightId;
}
