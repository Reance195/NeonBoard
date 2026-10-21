#pragma once

#include <QSortFilterProxyModel>

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

    Q_INVOKABLE QString idAt(int row) const;

signals:
    void statusChanged();
    void searchChanged();
    void stageChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_status;
    QString m_search;
    QString m_stage;
};
