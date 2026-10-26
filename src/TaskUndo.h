#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class TaskModel;
class TaskExtra;

// 通用撤销门面：把「删除 / 移动 / 完成 / 编辑」四类用户操作收进同一条撤销栈。
// 插件写入（PluginContext）与路线图快照同步（RoadmapSync）直连 TaskModel，不进本栈，
// 避免同步动作污染用户撤销历史。
class TaskUndo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(QString lastAction READ lastAction NOTIFY changed)

public:
    explicit TaskUndo(TaskModel *tasks, QObject *parent = nullptr);

    // 注入任务附加属性门面：撤销「完成」时连带删除重复任务派生的下一期卡
    void setTaskExtra(TaskExtra *extra);

    // 以下方法执行操作并记录一条可撤销记录
    Q_INVOKABLE bool deleteTask(const QString &id);
    Q_INVOKABLE bool moveTask(const QString &id,
                              const QString &status,
                              int newIndex,
                              const QString &description = QString());
    Q_INVOKABLE bool moveTaskBefore(const QString &id,
                                    const QString &status,
                                    const QString &beforeId,
                                    const QString &description = QString());
    // 按列移动（动态列用）：status 由列推导，撤销时连列一起回滚
    Q_INVOKABLE bool moveTaskToColumn(const QString &id,
                                      const QString &columnId,
                                      const QString &description = QString());
    Q_INVOKABLE bool moveTaskBeforeColumn(const QString &id,
                                          const QString &columnId,
                                          const QString &beforeId,
                                          const QString &description = QString());
    Q_INVOKABLE bool updateTask(const QString &id,
                                const QVariantMap &fields,
                                const QString &description = QString());

    Q_INVOKABLE bool undo();
    Q_INVOKABLE void clear();

    bool canUndo() const;
    QString lastAction() const;

signals:
    void changed();
    // 新增一条可撤销操作时发出，界面据此弹出提示条
    void actionPerformed(const QString &description);
    void undone(const QString &description);

private:
    enum class Kind {
        Deleted,
        Moved,
        Edited
    };

    struct Entry {
        Kind kind = Kind::Edited;
        QString id;
        QVariantMap payload;
    };

    void push(Kind kind, const QString &id, const QVariantMap &payload, const QString &description);
    bool revert(const Entry &entry);

    TaskModel *m_tasks = nullptr;
    TaskExtra *m_extra = nullptr;
    QVector<Entry> m_stack;
    QString m_lastAction;
};
