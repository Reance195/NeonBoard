#include "TaskInputParser.h"

#include <QDate>
#include <QDateTime>
#include <QTest>
#include <QTime>

namespace {

// 固定基准时间：2026-10-25（周日）10:00。
// 所有相对日期 / 时刻断言都以它为基准，保证跨时区、跨机器可复现。
QDateTime baseNow()
{
    return QDateTime(QDate(2026, 10, 25), QTime(10, 0));
}

// 期望的到期时间：与实现一致地按本地时间构造再取 epoch，避免时区差异导致误判。
qint64 epochOf(const QDate &date, const QTime &time)
{
    return QDateTime(date, time).toSecsSinceEpoch();
}

} // namespace

class TestTaskInputParser : public QObject
{
    Q_OBJECT

private slots:
    void emptyInputReturnsDefaults();
    void parsesTitleTagPriorityAndDateTime();

    void priorityWordMappings_data();
    void priorityWordMappings();

    void relativeDayWords_data();
    void relativeDayWords();

    void bareWeekdayRollsForward();
    void nextWeekWeekday();
    void absoluteDateKeepsGivenDay();
    void monthDayUsesCurrentYear();

    void timeOnlyInPastRollsToTomorrow();
    void timeOnlyInFutureStaysToday();
    void meridiemAfternoonAddsTwelve();
    void meridiemNoonIsTwelve();
    void meridiemMorningTwelveIsMidnight();

    void duplicateTagsAreDeduped();
    void titleIsCleanedOfSeparators();
};

void TestTaskInputParser::emptyInputReturnsDefaults()
{
    const ParsedTaskInput parsed = TaskInputParser::parse(QStringLiteral("   "), baseNow());

    QCOMPARE(parsed.title, QString());
    QVERIFY(parsed.tags.isEmpty());
    QCOMPARE(parsed.priority, -1);
    QCOMPARE(parsed.dueAt, qint64(0));
    QVERIFY(!parsed.hasDate);
    QVERIFY(!parsed.hasTime);
}

void TestTaskInputParser::parsesTitleTagPriorityAndDateTime()
{
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("写周报 明天 15:00 #工作 !3"), baseNow());

    QCOMPARE(parsed.title, QStringLiteral("写周报"));
    QCOMPARE(parsed.tags, QStringList{QStringLiteral("工作")});
    QCOMPARE(parsed.priority, 3);
    QVERIFY(parsed.hasDate);
    QVERIFY(parsed.hasTime);
    // 明天 = 2026-10-26；15:00 原样按 24 小时制
    QCOMPARE(parsed.dueAt, epochOf(QDate(2026, 10, 26), QTime(15, 0)));
}

void TestTaskInputParser::priorityWordMappings_data()
{
    QTest::addColumn<QString>("token");
    QTest::addColumn<int>("expected");

    QTest::newRow("紧急=3") << QStringLiteral("!紧急") << 3;
    QTest::newRow("高=3") << QStringLiteral("!高") << 3;
    QTest::newRow("中=2") << QStringLiteral("！中") << 2;
    QTest::newRow("低=1") << QStringLiteral("!低") << 1;
}

void TestTaskInputParser::priorityWordMappings()
{
    QFETCH(QString, token);
    QFETCH(int, expected);

    const ParsedTaskInput parsed =
        TaskInputParser::parse(token + QStringLiteral(" 修 bug"), baseNow());

    QCOMPARE(parsed.priority, expected);
    // 优先级标记应从标题中剔除
    QCOMPARE(parsed.title, QStringLiteral("修 bug"));
}

void TestTaskInputParser::relativeDayWords_data()
{
    QTest::addColumn<QString>("token");
    QTest::addColumn<int>("delta");

    QTest::newRow("今天") << QStringLiteral("今天") << 0;
    QTest::newRow("今晚") << QStringLiteral("今晚") << 0;
    QTest::newRow("明天") << QStringLiteral("明天") << 1;
    QTest::newRow("明晚") << QStringLiteral("明晚") << 1;
    QTest::newRow("后天") << QStringLiteral("后天") << 2;
    QTest::newRow("大后天") << QStringLiteral("大后天") << 3;
    QTest::newRow("昨天") << QStringLiteral("昨天") << -1;
    QTest::newRow("前天") << QStringLiteral("前天") << -2;
}

void TestTaskInputParser::relativeDayWords()
{
    QFETCH(QString, token);
    QFETCH(int, delta);

    const ParsedTaskInput parsed =
        TaskInputParser::parse(token + QStringLiteral(" 复盘"), baseNow());

    QVERIFY(parsed.hasDate);
    // 只给日期不给时刻：默认落到当天 23:59
    QVERIFY(!parsed.hasTime);
    QCOMPARE(parsed.dueAt, epochOf(baseNow().date().addDays(delta), QTime(23, 59)));
}

void TestTaskInputParser::bareWeekdayRollsForward()
{
    // 基准日是周日，裸「周五」已过去 → 顺延到本周之后的下一个周五
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("周五 交报告"), baseNow());

    QVERIFY(parsed.hasDate);
    QCOMPARE(parsed.dueAt, epochOf(QDate(2026, 10, 30), QTime(23, 59)));
}

void TestTaskInputParser::nextWeekWeekday()
{
    // 基准日所在周周一为 2026-10-19，下周三 = 10-19 + 7 + 2
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("下周三 评审"), baseNow());

    QVERIFY(parsed.hasDate);
    QCOMPARE(parsed.dueAt, epochOf(QDate(2026, 10, 28), QTime(23, 59)));
}

void TestTaskInputParser::absoluteDateKeepsGivenDay()
{
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("2026-11-05 上线"), baseNow());

    QVERIFY(parsed.hasDate);
    QCOMPARE(parsed.dueAt, epochOf(QDate(2026, 11, 5), QTime(23, 59)));
}

void TestTaskInputParser::monthDayUsesCurrentYear()
{
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("12/31 复盘"), baseNow());

    QVERIFY(parsed.hasDate);
    QCOMPARE(parsed.dueAt, epochOf(QDate(2026, 12, 31), QTime(23, 59)));
}

void TestTaskInputParser::timeOnlyInPastRollsToTomorrow()
{
    // 只给时刻且已过（基准 10:00，06:30 已过）→ 顺延到明天同一时刻
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("06:30 晨跑"), baseNow());

    QVERIFY(parsed.hasTime);
    QVERIFY(!parsed.hasDate);
    QCOMPARE(parsed.dueAt, epochOf(QDate(2026, 10, 26), QTime(6, 30)));
}

void TestTaskInputParser::timeOnlyInFutureStaysToday()
{
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("20:00 收尾"), baseNow());

    QVERIFY(parsed.hasTime);
    QVERIFY(!parsed.hasDate);
    QCOMPARE(parsed.dueAt, epochOf(QDate(2026, 10, 25), QTime(20, 0)));
}

void TestTaskInputParser::meridiemAfternoonAddsTwelve()
{
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("下午 3 点 开会"), baseNow());

    QVERIFY(parsed.hasTime);
    QCOMPARE(parsed.dueAt, epochOf(QDate(2026, 10, 25), QTime(15, 0)));
    QCOMPARE(parsed.title, QStringLiteral("开会"));
}

void TestTaskInputParser::meridiemNoonIsTwelve()
{
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("中午 12 点 吃饭"), baseNow());

    QVERIFY(parsed.hasTime);
    QCOMPARE(parsed.dueAt, epochOf(QDate(2026, 10, 25), QTime(12, 0)));
}

void TestTaskInputParser::meridiemMorningTwelveIsMidnight()
{
    // 上午 12 点 = 0 点；0 点已过 → 顺延到明天 0 点
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("上午 12 点 提交"), baseNow());

    QVERIFY(parsed.hasTime);
    QCOMPARE(parsed.dueAt, epochOf(QDate(2026, 10, 26), QTime(0, 0)));
}

void TestTaskInputParser::duplicateTagsAreDeduped()
{
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("#工作 #工作 写周报"), baseNow());

    QCOMPARE(parsed.tags, QStringList{QStringLiteral("工作")});
    QCOMPARE(parsed.title, QStringLiteral("写周报"));
}

void TestTaskInputParser::titleIsCleanedOfSeparators()
{
    // 首尾分隔符与多余空白都要清掉，不能留下孤立标点
    const ParsedTaskInput parsed =
        TaskInputParser::parse(QStringLiteral("  ，， 买牛奶 ，，  "), baseNow());

    QCOMPARE(parsed.title, QStringLiteral("买牛奶"));
}

QTEST_GUILESS_MAIN(TestTaskInputParser)

#include "tst_taskinputparser.moc"
