#include "TaskFilterModel.h"

#include "TaskModel.h"

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

QString TaskFilterModel::idAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};
    return mapToSource(index(row, 0)).data(TaskModel::IdRole).toString();
}

bool TaskFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (index.data(TaskModel::StatusRole).toString() != m_status)
        return false;
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
    const qint64 leftKey = left.data(TaskModel::OrderKeyRole).toLongLong();
    const qint64 rightKey = right.data(TaskModel::OrderKeyRole).toLongLong();
    if (leftKey != rightKey)
        return leftKey < rightKey;
    return left.data(TaskModel::IdRole).toString() < right.data(TaskModel::IdRole).toString();
}
