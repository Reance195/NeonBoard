// R11-03：统计门面的逾期计数。
//
// 判定口径：未完成（status != done）+ 设了到期时间（due_at > 0）+ 已经过期（due_at < now）。
// 三者缺一不可 —— 没设到期的不算、还没到点的不算、已完成的也不再算。
//
// 涉及 task 表，用 sqlite 临时库跑真实读写（与 tst_taskcolumns 同一方式）。

#include "Database.h"
#include "TaskModel.h"
#include "TaskStats.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestTaskStats : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void emptyBoardHasNoOverdue();
    void countsOnlyUnfinishedPastDue();
    void completingTaskClearsOverdue();
    void doneTaskCountsIntoTotalDone();

private:
    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_model = nullptr;
    TaskStats *m_stats = nullptr;

    static qint64 secondsFromNow(qint64 delta)
    {
        return QDateTime::currentSecsSinceEpoch() + delta;
    }
};

void TestTaskStats::init()
{
    QVERIFY(m_dir.isValid());
    // 每个用例一个独立库文件：init/cleanup 复用同一个对象，共用文件会让计数互相污染
    const QString path = m_dir.filePath(QString::fromLatin1(QTest::currentTestFunction())
                                        + QStringLiteral(".db"));
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));

    m_db = new Database;
    QString error;
    QVERIFY2(m_db->open(&error, path), qPrintable(error));
    m_model = new TaskModel(m_db);
    m_model->load();
    m_stats = new TaskStats(m_db, m_model);
}

void TestTaskStats::cleanup()
{
    delete m_stats;
    m_stats = nullptr;
    delete m_model;
    m_model = nullptr;
    delete m_db;
    m_db = nullptr;
}

void TestTaskStats::emptyBoardHasNoOverdue()
{
    QCOMPARE(m_stats->overdueCount(), 0);
    QCOMPARE(m_stats->totalDone(), 0);
}

void TestTaskStats::countsOnlyUnfinishedPastDue()
{
    const qint64 past = secondsFromNow(-3600);
    const qint64 future = secondsFromNow(3600);

    QVERIFY(!m_model->addTask(QStringLiteral("逾期待办"), QStringLiteral("todo"), 1, QString(), past)
                 .isEmpty());
    QVERIFY(!m_model->addTask(QStringLiteral("逾期进行中"), QStringLiteral("doing"), 1, QString(), past)
                 .isEmpty());
    // 这三张都不该计入逾期
    QVERIFY(!m_model->addTask(QStringLiteral("还没到期"), QStringLiteral("todo"), 1, QString(), future)
                 .isEmpty());
    QVERIFY(!m_model->addTask(QStringLiteral("没有到期时间"), QStringLiteral("todo"), 1, QString(), 0)
                 .isEmpty());
    QVERIFY(!m_model->addTask(QStringLiteral("已经完成"), QStringLiteral("done"), 1, QString(), past)
                 .isEmpty());

    m_stats->refresh();
    QCOMPARE(m_stats->overdueCount(), 2);
}

void TestTaskStats::completingTaskClearsOverdue()
{
    const qint64 past = secondsFromNow(-3600);
    const QString id = m_model->addTask(QStringLiteral("要完成的卡"), QStringLiteral("todo"), 1,
                                         QString(), past);
    QVERIFY(!id.isEmpty());

    m_stats->refresh();
    QCOMPARE(m_stats->overdueCount(), 1);

    QVERIFY(m_model->moveTask(id, QStringLiteral("done"), -1));
    m_stats->refresh();

    // 完成即不再逾期
    QCOMPARE(m_stats->overdueCount(), 0);
}

void TestTaskStats::doneTaskCountsIntoTotalDone()
{
    const QString id = m_model->addTask(QStringLiteral("完成的卡"), QStringLiteral("todo"), 1);
    QVERIFY(!id.isEmpty());
    QVERIFY(m_model->moveTask(id, QStringLiteral("done"), -1));

    m_stats->refresh();
    QCOMPARE(m_stats->totalDone(), 1);

    // 热力图里应有一个格子记着这一次完成
    int maxCount = 0;
    const QVariantList cells = m_stats->heatmap();
    for (const QVariant &v : cells)
        maxCount = qMax(maxCount, v.toMap().value(QStringLiteral("count")).toInt());
    QCOMPARE(maxCount, 1);
}

QTEST_MAIN(TestTaskStats)
#include "tst_taskstats.moc"
