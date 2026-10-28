#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class TaskModel;
class TaskExtra;
class TaskUndo;

// R8-03：卡片详情抽屉的后端契约 —— 一次保存的脏检查与分发。
//
// 为什么单独一层而不是全写在 QML 里：
// 详情面板唯一值得担心的数据风险是「什么都没改，却写了一次库」——
// 关抽屉按 Esc、点开的瞬间、来回切阅读/编辑，都不该产生写入（也不会污染撤销栈）。
// 把「脏不脏」和「要不要写」收在一个可被单测盯住的地方（tests/tst_carddetail.cpp），
// QML 就只剩渲染，契约也就有了唯一实现。
//
// 分发口径与「编辑任务」对话框保持一致：
//   · 标题 / 备注 / 优先级 / 标签 / 到期 → task 表，经 TaskUndo（可 Ctrl+Z）
//   · 提醒 / 重复规则 → 侧表 task_extra，经 TaskExtra（不进撤销栈，与插件写入同口径）
class CardDetail : public QObject
{
    Q_OBJECT

public:
    explicit CardDetail(TaskModel *tasks, TaskExtra *extra, TaskUndo *undo,
                        QObject *parent = nullptr);

    // 规范化后再比较：统一 CRLF→LF、去掉尾部空白。
    // 目的只有一个 ——「按了一下回车」不算改动，否则每次点开详情都会莫名变脏。
    static QString normalizeNote(const QString &text);

    // 备注是否真的改了
    Q_INVOKABLE bool isNoteDirty(const QString &original, const QString &current) const;

    // 补丁里有没有**实际**改动（逐字段与库中现值比对）。
    // QML 用它决定「保存」是否可点：界面不该自造比较逻辑，否则改完又改回去仍显示成脏的。
    Q_INVOKABLE bool hasChanges(const QString &id, const QVariantMap &patch) const;

    // 保存补丁：只写真正变化的字段，没变化返回 false 且**零写入**（不动 task / task_extra / 撤销栈）。
    // id 不存在、标题为空这类非法输入同样返回 false，绝不落半条脏数据。
    Q_INVOKABLE bool saveFields(const QString &id, const QVariantMap &patch,
                                const QString &description = QString());

    // 只改备注的便捷入口（保持既有调用与单测的语义）
    Q_INVOKABLE bool saveNote(const QString &id, const QString &original,
                              const QString &current);

private:
    // 一次补丁算出「要写什么」。空集表示没有改动。
    struct ChangeSet
    {
        QVariantMap taskFields;   // 交给 TaskUndo::updateTask
        bool remindChanged = false;
        qint64 remindAt = 0;
        bool rruleChanged = false;
        QString rrule;
        bool invalid = false;     // 补丁非法（如标题被清空）：整单拒绝

        // 非法补丁一律按「没什么可写」处理：hasChanges 与 saveFields 都不该放行
        bool isEmpty() const
        {
            return invalid || (taskFields.isEmpty() && !remindChanged && !rruleChanged);
        }
    };

    ChangeSet diffOf(const QString &id, const QVariantMap &patch) const;

    TaskModel *m_tasks = nullptr;
    TaskExtra *m_extra = nullptr;
    TaskUndo *m_undo = nullptr;
};
