#include "TaskInputParser.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSet>
#include <QTime>
#include <QVector>

#include <algorithm>
#include <limits>

namespace {

// 含中文的模式必须让引擎走 UTF 模式，否则字节序列对不上
constexpr QRegularExpression::PatternOptions kPatternOptions =
    QRegularExpression::UseUnicodePropertiesOption;

struct Removal
{
    qsizetype start = 0;
    qsizetype length = 0;
};

// 收集所有匹配区间
void collect(const QString &text, const QRegularExpression &re, QVector<Removal> &out)
{
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedLength() <= 0)
            continue;
        out.append({m.capturedStart(), m.capturedLength()});
    }
}

// 按位置从后往前删除，并折叠空白与首尾连接符
QString stripAll(const QString &text, const QVector<Removal> &removals)
{
    QVector<Removal> sorted = removals;
    std::sort(sorted.begin(), sorted.end(), [](const Removal &a, const Removal &b) {
        return a.start > b.start;
    });

    QString result = text;
    // guard 记录已删除区间的最小起点；从后往前删时若当前区间右端越过它，说明区间交叠
    qsizetype guard = std::numeric_limits<qsizetype>::max();
    for (const Removal &r : sorted) {
        if (r.start + r.length > guard) // 与已删除区间重叠，跳过
            continue;
        result.remove(r.start, r.length);
        guard = r.start;
    }

    static const QRegularExpression space(QStringLiteral("\\s+"), kPatternOptions);
    result = result.replace(space, QStringLiteral(" ")).trimmed();
    static const QRegularExpression edges(
        QStringLiteral("^[\\s,，。、;；:：~～\\-—_·]+|[\\s,，。、;；:：~～\\-—_·]+$"),
        kPatternOptions);
    result = result.replace(edges, QStringLiteral("")).trimmed();
    return result;
}

int weekdayOf(const QString &token)
{
    static const QStringList cn = {QStringLiteral("一"), QStringLiteral("二"), QStringLiteral("三"),
                                   QStringLiteral("四"), QStringLiteral("五"), QStringLiteral("六"),
                                   QStringLiteral("日")};
    const int index = cn.indexOf(token);
    if (index >= 0)
        return index + 1;
    const int number = token.toInt();
    if (number >= 1 && number <= 7)
        return number;
    return 0;
}

// 把上午/下午等修饰语换算成 24 小时制
void applyMeridiem(int &hour, const QString &meridiem)
{
    if (meridiem.isEmpty())
        return;
    if (meridiem == QStringLiteral("下午") || meridiem == QStringLiteral("傍晚")
        || meridiem == QStringLiteral("晚上") || meridiem == QStringLiteral("夜里")) {
        if (hour < 12)
            hour += 12;
        return;
    }
    if (meridiem == QStringLiteral("中午")) {
        hour = 12;
        return;
    }
    // 上午 / 早上 / 早晨 / 凌晨：12 点按 0 点处理
    if (hour == 12)
        hour = 0;
}

} // namespace

namespace TaskInputParser
{

ParsedTaskInput parse(const QString &text, const QDateTime &now)
{
    ParsedTaskInput result;
    if (text.trimmed().isEmpty())
        return result;

    QVector<Removal> removals;

    // ---- 优先级标记：!3 / ！3 / !高 / !紧急 ----
    static const QRegularExpression priorityNumber(
        QStringLiteral("(?:!|！)\\s*([1-3])(?![0-9])"), kPatternOptions);
    static const QRegularExpression priorityWord(
        QStringLiteral("(?:!|！)\\s*(紧急|高|中|低)"), kPatternOptions);
    {
        QRegularExpressionMatchIterator it = priorityNumber.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const int value = m.captured(1).toInt();
            if (value >= 1 && value <= 3 && result.priority < 0) {
                result.priority = value;
                removals.append({m.capturedStart(), m.capturedLength()});
            }
        }
        it = priorityWord.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString word = m.captured(1);
            int value = 2;
            if (word == QStringLiteral("紧急") || word == QStringLiteral("高"))
                value = 3;
            else if (word == QStringLiteral("低"))
                value = 1;
            if (result.priority < 0) {
                result.priority = value;
                removals.append({m.capturedStart(), m.capturedLength()});
            }
        }
    }

    // ---- 标签标记：#工作 / ＃工作 ----
    static const QRegularExpression tagPattern(
        QStringLiteral("[#＃]([^\\s#＃,，。、;；:：!！?？()（）\\[\\]【】\"'\"']+)"), kPatternOptions);
    {
        QRegularExpressionMatchIterator it = tagPattern.globalMatch(text);
        QSet<QString> seen;
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString value = m.captured(1).trimmed();
            if (value.isEmpty())
                continue;
            removals.append({m.capturedStart(), m.capturedLength()});
            if (!seen.contains(value)) {
                seen.insert(value);
                result.tags.append(value);
            }
        }
    }

    // ---- 日期 ----
    QDate date;
    bool hasDate = false;

    // 绝对日期：2026-10-25 / 2026/10/25 / 2026年10月25日
    static const QRegularExpression absoluteDate(
        QStringLiteral("(?<!\\d)(\\d{4})[-/年](\\d{1,2})[-/月](\\d{1,2})日?"), kPatternOptions);
    // 月-日：10-25 / 10/25（前面不允许字母，避免误伤 R1-2）
    static const QRegularExpression monthDay(
        QStringLiteral("(?<![A-Za-z0-9])(\\d{1,2})[-/](\\d{1,2})(?![0-9])"), kPatternOptions);

    {
        const QRegularExpressionMatch m = absoluteDate.match(text);
        if (m.hasMatch()) {
            const QDate candidate(m.captured(1).toInt(), m.captured(2).toInt(),
                                  m.captured(3).toInt());
            if (candidate.isValid()) {
                date = candidate;
                hasDate = true;
                removals.append({m.capturedStart(), m.capturedLength()});
            }
        }
    }
    if (!hasDate) {
        const QRegularExpressionMatch m = monthDay.match(text);
        if (m.hasMatch()) {
            const int month = m.captured(1).toInt();
            const int day = m.captured(2).toInt();
            const QDate candidate(now.date().year(), month, day);
            if (candidate.isValid()) {
                date = candidate;
                hasDate = true;
                removals.append({m.capturedStart(), m.capturedLength()});
            }
        }
    }
    if (!hasDate) {
        // 相对日：今天 / 明天 / 后天 / 大后天 / 昨天 / 前天
        static const QRegularExpression relativeDay(
            QStringLiteral("(今天|今日|今晚|明天|明日|明晚|后天|大后天|昨天|昨日|前天)"),
            kPatternOptions);
        const QRegularExpressionMatch m = relativeDay.match(text);
        if (m.hasMatch()) {
            const QString word = m.captured(1);
            int delta = 0;
            if (word == QStringLiteral("今天") || word == QStringLiteral("今日")
                || word == QStringLiteral("今晚"))
                delta = 0;
            else if (word == QStringLiteral("明天") || word == QStringLiteral("明日")
                     || word == QStringLiteral("明晚"))
                delta = 1;
            else if (word == QStringLiteral("后天"))
                delta = 2;
            else if (word == QStringLiteral("大后天"))
                delta = 3;
            else if (word == QStringLiteral("昨天") || word == QStringLiteral("昨日"))
                delta = -1;
            else if (word == QStringLiteral("前天"))
                delta = -2;
            date = now.date().addDays(delta);
            hasDate = true;
            removals.append({m.capturedStart(), m.capturedLength()});
        }
    }
    if (!hasDate) {
        // 星期：周五 / 星期五 / 下周三 / 下下周一
        static const QRegularExpression weekPattern(
            QStringLiteral("(下下|下|本|这|上)?(?:周|星期|礼拜)([一二三四五六日天1-7])"),
            kPatternOptions);
        const QRegularExpressionMatch m = weekPattern.match(text);
        if (m.hasMatch()) {
            const QString prefix = m.captured(1);
            const int weekday = weekdayOf(m.captured(2));
            if (weekday > 0 && prefix != QStringLiteral("上")) {
                const QDate today = now.date();
                const QDate monday = today.addDays(-(today.dayOfWeek() - 1));
                int weeks = 0;
                if (prefix == QStringLiteral("下"))
                    weeks = 1;
                else if (prefix == QStringLiteral("下下"))
                    weeks = 2;
                QDate target = monday.addDays(weeks * 7 + (weekday - 1));
                if (prefix.isEmpty() && target < today)
                    target = target.addDays(7); // 裸周几：已过则顺延到下周
                date = target;
                hasDate = true;
                removals.append({m.capturedStart(), m.capturedLength()});
            }
        }
    }

    // ---- 时间 ----
    int hour = -1;
    int minute = 0;
    bool hasTime = false;

    static const QRegularExpression colonTime(
        QStringLiteral("(上午|早上|早晨|凌晨|中午|下午|傍晚|晚上|夜里)?\\s*(\\d{1,2})[:：](\\d{1,2})(?::(\\d{2}))?"),
        kPatternOptions);
    static const QRegularExpression chineseTime(
        QStringLiteral("(上午|早上|早晨|凌晨|中午|下午|傍晚|晚上|夜里)?\\s*(\\d{1,2})\\s*[点时](半|\\d{1,2}\\s*分?)?"),
        kPatternOptions);

    {
        // 两种时间写法各取第一个匹配，谁更靠前用谁
        QRegularExpressionMatch m = colonTime.match(text);
        const QRegularExpressionMatch cn = chineseTime.match(text);
        if (cn.hasMatch() && (!m.hasMatch() || cn.capturedStart() < m.capturedStart()))
            m = cn;
        if (m.hasMatch()) {
            const QString meridiem = m.captured(1);
            const int capturedHour = m.captured(2).toInt();
            const QString capturedMinute = m.captured(3);
            int parsedHour = capturedHour;
            int parsedMinute = 0;
            if (capturedMinute == QStringLiteral("半"))
                parsedMinute = 30;
            else if (!capturedMinute.isEmpty())
                parsedMinute = capturedMinute.trimmed().toInt();
            if (capturedHour >= 0 && capturedHour <= 23 && parsedMinute >= 0 && parsedMinute <= 59) {
                applyMeridiem(parsedHour, meridiem);
                hour = parsedHour;
                minute = parsedMinute;
                hasTime = true;
                removals.append({m.capturedStart(), m.capturedLength()});
            }
        }
    }

    // ---- 组装到期时间 ----
    if (hasDate || hasTime) {
        QDateTime due;
        if (hasDate && hasTime) {
            due = QDateTime(date, QTime(hour, minute));
        } else if (hasDate) {
            due = QDateTime(date, QTime(23, 59));
        } else {
            due = QDateTime(now.date(), QTime(hour, minute));
            if (due <= now)
                due = due.addDays(1); // 只给了时刻且已过：顺延到明天
        }
        result.dueAt = due.toSecsSinceEpoch();
        result.hasDate = hasDate;
        result.hasTime = hasTime;
    }

    result.title = stripAll(text, removals);
    return result;
}

} // namespace TaskInputParser
