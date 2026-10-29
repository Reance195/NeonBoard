// R8-04：卡片事件审计（侧表 task_event）。
//
// 这张表有两个必须守住的性质：
//   1. **老库开得起来**：不含 task_event 的库打开后自动补表，历史任务一条不丢，
//      user_version 不动 —— 动了就等于宣布「这个库只有新版本能开」。
//   2. **一个动作一条事件**：搬到已完成会连发 taskMoved + taskCompleted，
//      两条都记会让时间线看起来像点了两下。completed 优先，moved 让路。

#include "Database.h"
#include "TaskAudit.h"
#include "TaskModel.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

class TestTaskEvent : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void oldDatabaseGetsTableWithoutVersionBump();
    void createRecordsOneEvent();
    void moveRecordsFromTo();
    void completeRecordsOnlyCompleted();
    void restoreAfterDoneIsRestored();
    void removeKeepsHistory();
    void eventsAreNewestFirstAndLimited();
    void countMatchesEvents();
    void unknownKindFallsBackToRaw();
    void emptyTaskIdIsRejected();
    void oldEventsAreTrimmed();

private:
    QString pathFor(const QString &suffix) const;
    int userVersion() const;

    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_model = nullptr;
    TaskAudit *m_audit = nullptr;
};

QString TestTaskEvent::pathFor(const QString &suffix) const
{
    return m_dir.filePath(QString::fromLatin1(QTest::currentTestFunction()) + suffix
                          + QStringLiteral(".db"));
}

int TestTaskEvent::userVersion() const
{
    const QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery query(db);
    query.exec(QStringLiteral("PRAGMA user_version"));
    return query.next() ? query.value(0).toInt() : -1;
}

void TestTaskEvent::init()
{
    const QString path = pathFor(QString());
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));

    m_db = new Database;
    QString error;
    QVERIFY2(m_db->open(&error, path), qPrintable(error));
    m_model = new TaskModel(m_db);
    m_model->load();
    m_audit = new TaskAudit(m_db, m_model);
}

void TestTaskEvent::cleanup()
{
    delete m_audit;
    m_audit = nullptr;
    delete m_model;
    m_model = nullptr;
    delete m_db;
    m_db = nullptr;
}

void TestTaskEvent::oldDatabaseGetsTableWithoutVersionBump()
{
    const QString keep = m_model->addTask(QStringLiteral("老库里的卡"), QStringLiteral("todo"), 1,
                                          QString(), 0);
    QVERIFY(!keep.isEmpty());
    const int version = userVersion();
    const QString path = m_db->path();

    // 模拟「R8-04 之前的库」：把侧表整个删掉，用户版本保持不变
    {
        const QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("DROP TABLE task_event")));
        QVERIFY(query.exec(QStringLiteral("DELETE FROM sqlite_sequence WHERE name='task_event'"))
                || true); // 没有 sqlite_sequence 也不算错
    }
    delete m_audit;
    m_audit = nullptr;
    delete m_model;
    m_model = nullptr;
    delete m_db;
    m_db = nullptr;

    // 用新实例重开同一个库：自动补表、版本不变、历史任务还在
    Database reopened;
    QString error;
    QVERIFY2(reopened.open(&error, path), qPrintable(error));
    TaskModel model(&reopened);
    model.load();
    QCOMPARE(model.count(), 1);
    QVERIFY(!model.taskById(keep).isEmpty());

    const QSqlDatabase db = QSqlDatabase::database(reopened.connectionName());
    QSqlQuery check(db);
    QVERIFY(check.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='task_event'")));
    QVERIFY2(check.next(), "老库重开后没有自动补出 task_event 表");

    QSqlQuery versionQuery(db);
    QVERIFY(versionQuery.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(versionQuery.next());
    QCOMPARE(versionQuery.value(0).toInt(), version);

    // 补表之后立刻可用
    TaskAudit audit(&reopened, &model);
    QVERIFY(audit.record(keep, QStringLiteral("created"), QStringLiteral("补表后写入")));
    QCOMPARE(audit.countOf(keep), 1);
}

void TestTaskEvent::createRecordsOneEvent()
{
    const QString id = m_model->addTask(QStringLiteral("新建就有一条"), QStringLiteral("todo"), 1,
                                        QString(), 0);
    QVERIFY(!id.isEmpty());
    const QVariantList events = m_audit->eventsOf(id);
    QCOMPARE(events.size(), 1);
    const QVariantMap first = events.first().toMap();
    QCOMPARE(first.value(QStringLiteral("kind")).toString(), QStringLiteral("created"));
    QCOMPARE(first.value(QStringLiteral("label")).toString(), QStringLiteral("创建"));
    // detail 存标题：卡删了之后还能从时间线认出「这是哪张」
    QCOMPARE(first.value(QStringLiteral("detail")).toString(), QStringLiteral("新建就有一条"));
}

void TestTaskEvent::moveRecordsFromTo()
{
    const QString id = m_model->addTask(QStringLiteral("搬到进行中"), QStringLiteral("todo"), 1,
                                        QString(), 0);
    // 走界面真正用的那条路径（按钮 / 拖拽都经 moveTask → moveTaskBefore）：
    // moveTaskToColumn 是老接口，不发 taskMoved，别拿它验时间线
    QVERIFY(m_model->moveTask(id, QStringLiteral("doing"), -1));
    const QVariantList events = m_audit->eventsOf(id);
    QCOMPARE(events.size(), 2);   // created + moved
    const QVariantMap latest = events.first().toMap();
    QCOMPARE(latest.value(QStringLiteral("kind")).toString(), QStringLiteral("moved"));
    QCOMPARE(latest.value(QStringLiteral("detail")).toString(), QStringLiteral("todo → doing"));
}

void TestTaskEvent::completeRecordsOnlyCompleted()
{
    const QString id = m_model->addTask(QStringLiteral("直接完成"), QStringLiteral("todo"), 1,
                                        QString(), 0);
    QVERIFY(m_model->moveTask(id, QStringLiteral("done"), -1));

    const QVariantList events = m_audit->eventsOf(id);
    // 一个动作一条事件：taskMoved(todo→done) 让位给 taskCompleted
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.first().toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("completed"));
    QCOMPARE(events.at(1).toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("created"));
}

void TestTaskEvent::restoreAfterDoneIsRestored()
{
    const QString id = m_model->addTask(QStringLiteral("完成又后悔"), QStringLiteral("todo"), 1,
                                        QString(), 0);
    QVERIFY(m_model->moveTask(id, QStringLiteral("done"), -1));
    QVERIFY(m_model->moveTask(id, QStringLiteral("todo"), -1));

    const QVariantList events = m_audit->eventsOf(id);
    QCOMPARE(events.size(), 3);
    QCOMPARE(events.first().toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("restored"));
    QCOMPARE(events.first().toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("恢复"));
    QCOMPARE(events.first().toMap().value(QStringLiteral("detail")).toString(),
             QStringLiteral("done → todo"));
}

void TestTaskEvent::removeKeepsHistory()
{
    const QString id = m_model->addTask(QStringLiteral("删了也要留档"), QStringLiteral("todo"), 1,
                                        QString(), 0);
    QVERIFY(!m_model->removeTask(id).isEmpty());
    // 卡没了，事件还在：审计的意义就是证明「它存在过」
    const QVariantList events = m_audit->eventsOf(id);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.first().toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("removed"));
}

void TestTaskEvent::eventsAreNewestFirstAndLimited()
{
    const QString id = m_model->addTask(QStringLiteral("来回搬三次"), QStringLiteral("todo"), 1,
                                        QString(), 0);
    QVERIFY(m_model->moveTask(id, QStringLiteral("doing"), -1));
    QVERIFY(m_model->moveTask(id, QStringLiteral("todo"), -1));

    const QVariantList all = m_audit->eventsOf(id, 0);
    QCOMPARE(all.size(), 3);
    // 倒序：最新的在最前。同一秒内也稳定 —— 排序按自增 id 而不是 at
    for (int i = 0; i + 1 < all.size(); ++i) {
        const qint64 newer = all.at(i).toMap().value(QStringLiteral("id")).toLongLong();
        const qint64 older = all.at(i + 1).toMap().value(QStringLiteral("id")).toLongLong();
        QVERIFY2(newer > older, "时间线不是按最新在前排的");
    }
    QCOMPARE(m_audit->eventsOf(id, 2).size(), 2);
    QCOMPARE(m_audit->eventsOf(id, 2).first().toMap().value(QStringLiteral("id")).toLongLong(),
             all.first().toMap().value(QStringLiteral("id")).toLongLong());
}

void TestTaskEvent::countMatchesEvents()
{
    const QString id = m_model->addTask(QStringLiteral("数一数"), QStringLiteral("todo"), 1,
                                        QString(), 0);
    QVERIFY(m_model->moveTaskToColumn(id, m_db->defaultColumnForStatus(QStringLiteral("doing"))));
    QCOMPARE(m_audit->countOf(id), m_audit->eventsOf(id, 0).size());
    QCOMPARE(m_audit->countOf(QStringLiteral("不存在")), 0);
}

void TestTaskEvent::unknownKindFallsBackToRaw()
{
    // 将来加种类不必同时改界面：认不出的原样返回
    QCOMPARE(m_audit->labelOf(QStringLiteral("created")), QStringLiteral("创建"));
    QCOMPARE(m_audit->labelOf(QStringLiteral("archived")), QStringLiteral("archived"));
}

void TestTaskEvent::emptyTaskIdIsRejected()
{
    QVERIFY(!m_audit->record(QString(), QStringLiteral("created")));
    QVERIFY(m_audit->eventsOf(QString()).isEmpty());
    QCOMPARE(m_audit->countOf(QString()), 0);

    // 库没开 / 句柄已失效时读侧返回空、写侧返回 false，不许崩
    Database unopened;
    TaskModel idle(&unopened);
    TaskAudit idleAudit(&unopened, &idle);
    QVERIFY(idleAudit.eventsOf(QStringLiteral("any")).isEmpty());
    QCOMPARE(idleAudit.countOf(QStringLiteral("any")), 0);
    QVERIFY(!idleAudit.record(QStringLiteral("any"), QStringLiteral("created")));

    // 没有 TaskModel 也要活得下去（只读场景 / 测试里直接 new 一个）
    TaskAudit detached(m_db, nullptr);
    QVERIFY(detached.eventsOf(QStringLiteral("any")).isEmpty());
}

void TestTaskEvent::oldEventsAreTrimmed()
{
    const QString id = m_model->addTask(QStringLiteral("拖来拖去一千次"), QStringLiteral("todo"), 1,
                                        QString(), 0);
    const int extra = TaskAudit::kMaxEventsPerTask + 5;
    for (int i = 0; i < extra; ++i)
        QVERIFY(m_audit->record(id, QStringLiteral("moved"), QStringLiteral("nudge")));
    // 单卡封顶：只留最近 N 条，否则一张常年反复拖的卡会把库撑大
    QCOMPARE(m_audit->countOf(id), TaskAudit::kMaxEventsPerTask);
}

QTEST_GUILESS_MAIN(TestTaskEvent)

#include "tst_taskevent.moc"
