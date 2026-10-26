// R4-03 批次二：列感知的模型层（TaskModel 的 columnId + TaskFilterModel 按列过滤）。
//
// 用 sqlite 临时库跑真实读写，不 mock —— 这段逻辑的坑都在「写库字段与内存副本是否一致」，
// mock 掉数据库恰好会把这类问题藏起来。

#include "Database.h"
#include "TaskFilterModel.h"
#include "TaskModel.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestTaskColumns : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void addTaskLandsInDefaultColumn();
    void moveTaskToColumnUpdatesStatusAndColumn();
    void filterByColumnSeparatesSameStatus();
    void filterFallsBackToStatusWithoutColumn();
    void unknownColumnIsRejected();

private:
    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_model = nullptr;
    TaskFilterModel *m_filter = nullptr;
};

void TestTaskColumns::init()
{
    QVERIFY(m_dir.isValid());
    // 每个用例一个独立库文件：init/cleanup 是同一个对象复用，共用文件会让计数互相污染
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
    m_filter = new TaskFilterModel;
    m_filter->setSourceModel(m_model);
}

void TestTaskColumns::cleanup()
{
    delete m_filter;
    m_filter = nullptr;
    delete m_model;
    m_model = nullptr;
    delete m_db;
    m_db = nullptr;
}

void TestTaskColumns::addTaskLandsInDefaultColumn()
{
    const QString todoId = m_model->addTask(QStringLiteral("待办卡"));
    const QString doingId = m_model->addTask(QStringLiteral("进行中卡"), QStringLiteral("doing"));
    const QString doneId = m_model->addTask(QStringLiteral("完成卡"), QStringLiteral("done"));
    QVERIFY(!todoId.isEmpty() && !doingId.isEmpty() && !doneId.isEmpty());

    QCOMPARE(m_model->taskById(todoId).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-todo"));
    QCOMPARE(m_model->taskById(doingId).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-doing"));
    QCOMPARE(m_model->taskById(doneId).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-done"));
    // 角色也要对得上：QML 只看 columnId 角色
    QCOMPARE(m_model->index(0, 0).data(TaskModel::ColumnIdRole).toString().isEmpty(), false);
}

void TestTaskColumns::moveTaskToColumnUpdatesStatusAndColumn()
{
    const QString id = m_model->addTask(QStringLiteral("搬列卡"));
    QVERIFY(!id.isEmpty());
    const qint64 beforeCompleted = m_model->taskById(id).value(QStringLiteral("completedAt")).toLongLong();

    QVERIFY(m_model->moveTaskToColumn(id, QStringLiteral("col-doing")));
    QVariantMap doing = m_model->taskById(id);
    QCOMPARE(doing.value(QStringLiteral("status")).toString(), QStringLiteral("doing"));
    QCOMPARE(doing.value(QStringLiteral("columnId")).toString(), QStringLiteral("col-doing"));
    QCOMPARE(doing.value(QStringLiteral("completedAt")).toLongLong(), qint64(0));
    QCOMPARE(m_model->doingCount(), 1);
    QCOMPARE(m_model->todoCount(), 0);

    // 搬到已完成列：状态与完成时间必须一起变（结卡闭环依赖 completedAt）
    QVERIFY(m_model->moveTaskToColumn(id, QStringLiteral("col-done")));
    QVariantMap done = m_model->taskById(id);
    QCOMPARE(done.value(QStringLiteral("status")).toString(), QStringLiteral("done"));
    QCOMPARE(done.value(QStringLiteral("columnId")).toString(), QStringLiteral("col-done"));
    QVERIFY(done.value(QStringLiteral("completedAt")).toLongLong() > beforeCompleted);
    QCOMPARE(m_model->doneCount(), 1);

    // 落盘一致：重开模型仍读得到同一列（防止只改内存副本）
    TaskModel reloaded(m_db);
    reloaded.load();
    QCOMPARE(reloaded.taskById(id).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-done"));
}

void TestTaskColumns::filterByColumnSeparatesSameStatus()
{
    const QString a = m_model->addTask(QStringLiteral("待办甲"));
    const QString b = m_model->addTask(QStringLiteral("待办乙"));
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    // 把乙搬到「进行中」列，但它的 status 也随之变 doing —— 这是列驱动状态的设计
    QVERIFY(m_model->moveTaskToColumn(b, QStringLiteral("col-doing")));

    m_filter->setColumnId(QStringLiteral("col-todo"));
    QCOMPARE(m_filter->rowCount(), 1);
    QCOMPARE(m_filter->idAt(0), a);

    m_filter->setColumnId(QStringLiteral("col-doing"));
    QCOMPARE(m_filter->rowCount(), 1);
    QCOMPARE(m_filter->idAt(0), b);

    // 搜索与列过滤是叠加关系，不是互斥
    m_filter->setSearch(QStringLiteral("乙"));
    m_filter->setColumnId(QStringLiteral("col-todo"));
    QCOMPARE(m_filter->rowCount(), 0);
}

void TestTaskColumns::filterFallsBackToStatusWithoutColumn()
{
    const QString todoId = m_model->addTask(QStringLiteral("旧行为卡"));
    QVERIFY(!todoId.isEmpty());

    // 列过滤为空 → 回到按 status 过滤（三列 QML 仍走这条老路径）
    QCOMPARE(m_filter->columnId(), QString());
    m_filter->setStatus(QStringLiteral("todo"));
    QCOMPARE(m_filter->rowCount(), 1);
    QCOMPARE(m_filter->idAt(0), todoId);

    m_filter->setStatus(QStringLiteral("done"));
    QCOMPARE(m_filter->rowCount(), 0);
}

void TestTaskColumns::unknownColumnIsRejected()
{
    const QString id = m_model->addTask(QStringLiteral("拒绝卡"));
    QVERIFY(!id.isEmpty());
    const QVariantMap before = m_model->taskById(id);

    QVERIFY(!m_model->moveTaskToColumn(id, QStringLiteral("col-does-not-exist")));
    const QVariantMap after = m_model->taskById(id);
    QCOMPARE(after.value(QStringLiteral("columnId")).toString(),
             before.value(QStringLiteral("columnId")).toString());
    QCOMPARE(after.value(QStringLiteral("status")).toString(),
             before.value(QStringLiteral("status")).toString());
}

QTEST_GUILESS_MAIN(TestTaskColumns)

#include "tst_taskcolumns.moc"
