// R8-03：详情面板的字段读写收口。实现见 CardDetail.h 的动机说明。

#include "CardDetail.h"

#include "TaskExtra.h"
#include "TaskModel.h"
#include "TaskUndo.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

namespace {

// 标签在库里的形态是 JSON 数组字符串（TaskModel::normalizeTags），但历史数据里
// 也可能留着逗号串。这里统一读成列表再比 —— 只比「集合与顺序」，不比字面量，
// 否则「R8, 详情」和 ["R8","详情"] 会被判成改动，白白写一次库。
QStringList tagListOf(const QString &raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty())
        return {};

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && document.isArray()) {
        QStringList tags;
        const QJsonArray array = document.array();
        tags.reserve(array.size());
        for (const QJsonValue &value : array) {
            const QString tag = value.toString().trimmed();
            if (!tag.isEmpty())
                tags.append(tag);
        }
        return tags;
    }

    QStringList tags;
    const QStringList parts = trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts);
    tags.reserve(parts.size());
    for (const QString &part : parts) {
        const QString tag = part.trimmed();
        if (!tag.isEmpty())
            tags.append(tag);
    }
    return tags;
}

} // namespace

CardDetail::CardDetail(TaskModel *tasks, TaskExtra *extra, TaskUndo *undo, QObject *parent)
    : QObject(parent)
    , m_tasks(tasks)
    , m_extra(extra)
    , m_undo(undo)
{
}

QString CardDetail::normalizeNote(const QString &text)
{
    QString out = text;
    out.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    out.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    // 只去尾部的空白行 / 空格，正文内的换行与缩进原样保留（Markdown 靠缩进认嵌套）
    int end = out.size();
    while (end > 0) {
        const QChar ch = out.at(end - 1);
        if (!ch.isSpace())
            break;
        --end;
    }
    return out.left(end);
}

bool CardDetail::isNoteDirty(const QString &original, const QString &current) const
{
    return normalizeNote(original) != normalizeNote(current);
}

CardDetail::ChangeSet CardDetail::diffOf(const QString &id, const QVariantMap &patch) const
{
    ChangeSet changes;
    if (id.isEmpty() || !m_tasks || !m_undo)
        return changes;

    // 先看卡在不在：不存在就什么都不做，不写库也不写撤销栈
    const QVariantMap task = m_tasks->taskById(id);
    if (task.value(QStringLiteral("id")).toString().isEmpty())
        return changes;

    if (patch.contains(QStringLiteral("title"))) {
        const QString title = patch.value(QStringLiteral("title")).toString().trimmed();
        // 标题为空不是「改动」而是非法输入：整单拒绝，避免用户的其他编辑被静默吞掉一半
        if (title.isEmpty()) {
            changes.invalid = true;
            return changes;
        }
        if (title != task.value(QStringLiteral("title")).toString())
            changes.taskFields.insert(QStringLiteral("title"), title);
    }

    if (patch.contains(QStringLiteral("note"))) {
        const QString note = normalizeNote(patch.value(QStringLiteral("note")).toString());
        if (note != normalizeNote(task.value(QStringLiteral("note")).toString()))
            changes.taskFields.insert(QStringLiteral("note"), note);
    }

    if (patch.contains(QStringLiteral("priority"))) {
        const int priority = qBound(0, patch.value(QStringLiteral("priority")).toInt(), 3);
        if (priority != task.value(QStringLiteral("priority")).toInt())
            changes.taskFields.insert(QStringLiteral("priority"), priority);
    }

    if (patch.contains(QStringLiteral("tags"))) {
        const QStringList tags = tagListOf(patch.value(QStringLiteral("tags")).toString());
        if (tags != tagListOf(task.value(QStringLiteral("tagsFull")).toString()))
            // 交给 TaskModel 序列化：补丁只提供「人写的逗号串」，JSON 形态由模型决定
            changes.taskFields.insert(QStringLiteral("tags"),
                                      tags.join(QStringLiteral(",")));
    }

    if (patch.contains(QStringLiteral("dueAt"))) {
        const qint64 dueAt = patch.value(QStringLiteral("dueAt")).toLongLong();
        // 界面只给到「日」，库里却可能带着具体时刻（导入 / 插件写入）。
        // 按天比而不是按秒比：否则一进编辑态就被判成脏，用户没碰到期也会把时刻改掉。
        const auto dayOf = [](qint64 epoch) -> qint64 {
            return epoch <= 0 ? 0 : QDateTime::fromSecsSinceEpoch(epoch).date().toJulianDay();
        };
        if (dayOf(dueAt) != dayOf(task.value(QStringLiteral("dueAt")).toLongLong()))
            changes.taskFields.insert(QStringLiteral("dueAt"), dueAt);
    }

    if (m_extra && (patch.contains(QStringLiteral("remindAt"))
                    || patch.contains(QStringLiteral("rrule")))) {
        const QVariantMap extra = m_extra->extraOf(id);
        if (patch.contains(QStringLiteral("remindAt"))) {
            const qint64 remindAt = patch.value(QStringLiteral("remindAt")).toLongLong();
            // 同上：界面精确到分钟，库里可能带秒，按分钟比
            if (remindAt / 60 != extra.value(QStringLiteral("remindAt")).toLongLong() / 60) {
                changes.remindChanged = true;
                changes.remindAt = remindAt;
            }
        }
        if (patch.contains(QStringLiteral("rrule"))) {
            const QString rrule = patch.value(QStringLiteral("rrule")).toString().trimmed();
            if (rrule != extra.value(QStringLiteral("rrule")).toString())
                changes.rruleChanged = true;
            // task_extra.rrule 是 NOT NULL：QML 传下来的空串可能是「null QString」
            // （QVariant 里没有值），直接绑上去会踩约束。统一换成非 null 的空串。
            changes.rrule = rrule.isNull() ? QStringLiteral("") : rrule;
        }
    }

    return changes;
}

bool CardDetail::hasChanges(const QString &id, const QVariantMap &patch) const
{
    return !diffOf(id, patch).isEmpty();
}

bool CardDetail::saveFields(const QString &id, const QVariantMap &patch,
                            const QString &description)
{
    const ChangeSet changes = diffOf(id, patch);
    if (changes.isEmpty())
        return false;

    const QString action = description.isEmpty() ? QStringLiteral("修改卡片") : description;

    bool ok = true;
    if (!changes.taskFields.isEmpty())
        ok = m_undo->updateTask(id, changes.taskFields, action);

    // 侧表两项走 TaskExtra：与编辑器表单、插件写入同一条路，不进撤销栈
    if (ok && changes.remindChanged)
        ok = m_extra && m_extra->setReminder(id, changes.remindAt);
    if (ok && changes.rruleChanged)
        ok = m_extra && m_extra->setRrule(id, changes.rrule);
    return ok;
}

bool CardDetail::saveNote(const QString &id, const QString &original, const QString &current)
{
    if (!isNoteDirty(original, current))
        return false;
    QVariantMap patch;
    patch.insert(QStringLiteral("note"), normalizeNote(current));
    return saveFields(id, patch, QStringLiteral("修改备注"));
}
