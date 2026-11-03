#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

// 快速录入解析结果：把「明天 15:00 #工作 !3」这类自然输入拆成结构化字段。
// 解析器只做纯文本处理，不依赖数据库，便于独立验证。
struct ParsedTaskInput
{
    QString title;       // 剔除标记后的纯标题
    QStringList tags;    // 文本中解析到的标签（不含 #）
    int priority = -1;   // 优先级 1/2/3，-1 表示文本未指定
    qint64 dueAt = 0;    // 到期时间（epoch 秒），0 表示未指定
    bool hasDate = false; // 是否解析到日期部分
    bool hasTime = false; // 是否解析到时间部分
};

namespace TaskInputParser
{

// 解析一段输入文本；now 用于计算「今天/明天/本周五」等相对日期基准。
ParsedTaskInput parse(const QString &text, const QDateTime &now);

} // namespace TaskInputParser
