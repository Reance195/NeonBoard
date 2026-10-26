// R4-03 批次三：看板列清单（BoardColumns）与列内精确插位。
//
// 与 tst_taskcolumns 同一条思路：跑真实 sqlite 临时库，不 mock ——
// 这段代码的坑都在「列清单、过滤模型、内存副本、库里的列字段」四者是否同步，
// mock 掉数据库恰好会把这类问题藏起来。

#include "BoardColumns.h"
#include "Database.h"
#include "TaskFilterModel.h"
#include "TaskModel.h"
#include "TaskTree.h"
#include "TaskUndo.h"

#include <QFile>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>

class TestBoardColumns : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void listsDatabaseColumns();
    void searchReachesEveryColumn();
    void addTaskToColumnLandsInThatColumn();
    void moveTaskBeforeColumnOrdersWithinColumn();
    void statusMoveKeepsColumnInSync();
    void reloadReusesModelsWhenColumnsUnchanged();
    void undoRestoresColumn();

    // 列管理（增 / 改名 / 调序 / 删）
    void addColumnAppendsToBoard();
    void renameColumnKeepsCardsAndModels();
    void moveColumnReordersAndStopsAtEdges();
    void removeColumnMigratesCards();

private:
    TaskFilterModel *columnModel(int row) const;
    QStringList columnIds() const;

    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_model = nullptr;
    TaskTree *m_tree = nullptr;
    BoardColumns *m_columns = nullptr;
};

void TestBoardColumns::init()
{
    QVERIFY(m_dir.isValid());
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
    m_tree = new TaskTree(m_db, m_model);
    m_columns = new BoardColumns(m_db, m_model);
    m_columns->setTaskTree(m_tree);
    m_columns->reload();
}

void TestBoardColumns::cleanup()
{
    delete m_columns;
    m_columns = nullptr;
    delete m_tree;
    m_tree = nullptr;
    delete m_model;
    m_model = nullptr;
    delete m_db;
    m_db = nullptr;
}

TaskFilterModel *TestBoardColumns::columnModel(int row) const
{
    const QModelIndex index = m_columns->index(row, 0);
    if (!index.isValid())
        return nullptr;
    return qobject_cast<TaskFilterModel *>(
        index.data(BoardColumns::ModelRole).value<QObject *>());
}

QStringList TestBoardColumns::columnIds() const
{
    QStringList ids;
    for (int i = 0; i < m_columns->rowCount(); ++i)
        ids.append(m_columns->index(i, 0).data(BoardColumns::IdRole).toString());
    return ids;
}

void TestBoardColumns::listsDatabaseColumns()
{
    QCOMPARE(m_columns->rowCount(), 3);

    const QModelIndex first = m_columns->index(0, 0);
    QCOMPARE(first.data(BoardColumns::IdRole).toString(), QStringLiteral("col-todo"));
    QCOMPARE(first.data(BoardColumns::TitleRole).toString(), QStringLiteral("待办"));
    QCOMPARE(first.data(BoardColumns::StatusRole).toString(), QStringLiteral("todo"));
    QCOMPARE(first.data(BoardColumns::IndexRole).toInt(), 0);

    // 列顺序必须按 order_key：界面列序、键盘左右、拖拽落点都依赖它
    QStringList ids;
    for (int i = 0; i < m_columns->rowCount(); ++i)
        ids.append(m_columns->index(i, 0).data(BoardColumns::IdRole).toString());
    QCOMPARE(ids, QStringList({QStringLiteral("col-todo"), QStringLiteral("col-doing"),
                               QStringLiteral("col-done")}));

    // 每列都挂着一个可用的过滤模型
    for (int i = 0; i < m_columns->rowCount(); ++i)
        QVERIFY(columnModel(i) != nullptr);

    // 统计片用的状态汇总：一加一减要对得上
    const QString a = m_model->addTask(QStringLiteral("统计卡"));
    QVERIFY(!a.isEmpty());
    QCOMPARE(m_columns->todoCount(), 1);
    QCOMPARE(m_columns->doingCount(), 0);
    QCOMPARE(m_columns->columnIdForStatus(QStringLiteral("doing")), QStringLiteral("col-doing"));
}

void TestBoardColumns::searchReachesEveryColumn()
{
    const QString a = m_model->addTask(QStringLiteral("买牛奶"));
    const QString b = m_model->addTask(QStringLiteral("写周报"), QStringLiteral("doing"));
    QVERIFY(!a.isEmpty() && !b.isEmpty());

    QCOMPARE(columnModel(0)->count(), 1);
    QCOMPARE(columnModel(1)->count(), 1);

    // 搜索必须落到每一列：漏掉的列会把被过滤的卡片照旧显示出来
    m_columns->setSearch(QStringLiteral("牛奶"));
    QCOMPARE(columnModel(0)->count(), 1);
    QCOMPARE(columnModel(1)->count(), 0);
    QCOMPARE(m_columns->todoCount(), 1);
    QCOMPARE(m_columns->doingCount(), 0);

    m_columns->setSearch(QString());
    QCOMPARE(columnModel(1)->count(), 1);
}

void TestBoardColumns::addTaskToColumnLandsInThatColumn()
{
    // 通过「进行中」列的代理模型录入：列优先，状态必须跟着列走
    const QString id = columnModel(1)->addTask(QStringLiteral("从列录入"), QStringLiteral("todo"),
                                              1, QString(), 0, QStringLiteral("col-doing"));
    QVERIFY(!id.isEmpty());
    const QVariantMap task = m_model->taskById(id);
    QCOMPARE(task.value(QStringLiteral("columnId")).toString(), QStringLiteral("col-doing"));
    QCOMPARE(task.value(QStringLiteral("status")).toString(), QStringLiteral("doing"));
    QCOMPARE(columnModel(1)->count(), 1);

    // 未知列不该把卡片塞进不存在的列，退回按状态落默认列
    const QString fallback = columnModel(0)->addTask(QStringLiteral("未知列"), QStringLiteral("todo"),
                                                     1, QString(), 0,
                                                     QStringLiteral("col-not-exist"));
    QVERIFY(!fallback.isEmpty());
    QCOMPARE(m_model->taskById(fallback).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-todo"));
}

void TestBoardColumns::moveTaskBeforeColumnOrdersWithinColumn()
{
    const QString a = m_model->addTask(QStringLiteral("甲"));
    const QString b = m_model->addTask(QStringLiteral("乙"));
    const QString c = m_model->addTask(QStringLiteral("丙"));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());
    QCOMPARE(m_model->taskIdsInColumn(QStringLiteral("col-todo")),
             QStringList({a, b, c}));

    // 把「丙」拖到「甲」之前：列内顺序要精确，且状态/列都不变
    QVERIFY(m_model->moveTaskBeforeColumn(c, QStringLiteral("col-todo"), a));
    QCOMPARE(m_model->taskIdsInColumn(QStringLiteral("col-todo")),
             QStringList({c, a, b}));
    QCOMPARE(m_model->taskById(c).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-todo"));

    // 落点卡不在本列（跨列 id）时退化为列尾，不能跨列插位
    QVERIFY(m_model->moveTaskBeforeColumn(c, QStringLiteral("col-todo"),
                                          QStringLiteral("col-does-not-exist")));
    QCOMPARE(m_model->taskIdsInColumn(QStringLiteral("col-todo")),
             QStringList({a, b, c}));

    // 未知列直接拒绝，不改任何字段
    QVERIFY(!m_model->moveTaskBeforeColumn(a, QStringLiteral("col-not-exist"), QString()));
    QCOMPARE(m_model->taskById(a).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-todo"));
}

void TestBoardColumns::statusMoveKeepsColumnInSync()
{
    const QString id = m_model->addTask(QStringLiteral("状态与列"));
    QVERIFY(!id.isEmpty());
    QCOMPARE(m_model->taskById(id).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-todo"));

    // 老路径（按状态移动）也必须把列一起搬走，否则卡片留在待办列、
    // 统计却算成进行中，界面与数据各说各话
    QVERIFY(m_model->moveTask(id, QStringLiteral("doing"), 0));
    QCOMPARE(m_model->taskById(id).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-doing"));

    // 改状态字段同样要同步列，并且会落到 done 列
    QVERIFY(m_model->updateTask(id, {{QStringLiteral("status"), QStringLiteral("done")}}));
    const QVariantMap done = m_model->taskById(id);
    QCOMPARE(done.value(QStringLiteral("columnId")).toString(), QStringLiteral("col-done"));
    QVERIFY(done.value(QStringLiteral("completedAt")).toLongLong() > 0);

    // 落盘一致：重开模型读到的列与内存一致
    TaskModel reloaded(m_db);
    reloaded.load();
    QCOMPARE(reloaded.taskById(id).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-done"));
}

void TestBoardColumns::reloadReusesModelsWhenColumnsUnchanged()
{
    QPointer<TaskFilterModel> before = columnModel(0);
    QVERIFY(before);

    // 列集合没变时 reload 不该重建模型：重建会让界面丢掉选中卡与滚动位置
    m_columns->reload();
    QPointer<TaskFilterModel> after = columnModel(0);
    QCOMPARE(before.data(), after.data());
    QCOMPARE(m_columns->rowCount(), 3);
}

void TestBoardColumns::undoRestoresColumn()
{
    TaskUndo undo(m_model);
    const QString id = m_model->addTask(QStringLiteral("撤销搬列"));
    QVERIFY(!id.isEmpty());

    QVERIFY(undo.moveTaskToColumn(id, QStringLiteral("col-doing")));
    QCOMPARE(m_model->taskById(id).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-doing"));

    // 撤销必须把列一起还原：只还原 status 会留下卡在错误列的数据
    QVERIFY(undo.undo());
    const QVariantMap restored = m_model->taskById(id);
    QCOMPARE(restored.value(QStringLiteral("columnId")).toString(), QStringLiteral("col-todo"));
    QCOMPARE(restored.value(QStringLiteral("status")).toString(), QStringLiteral("todo"));
}

void TestBoardColumns::addColumnAppendsToBoard()
{
    // 新列排在末尾（order_key = MAX + 1024），名字两端空格要去掉
    const QString id = m_columns->addColumn(QStringLiteral("  进行中·后端  "),
                                            QStringLiteral("doing"));
    QVERIFY(!id.isEmpty());
    QVERIFY(m_columns->lastError().isEmpty());
    QCOMPARE(m_columns->rowCount(), 4);
    QCOMPARE(columnIds().last(), id);
    QCOMPARE(m_columns->index(3, 0).data(BoardColumns::TitleRole).toString(),
             QStringLiteral("进行中·后端"));
    QCOMPARE(m_columns->index(3, 0).data(BoardColumns::StatusRole).toString(),
             QStringLiteral("doing"));
    QCOMPARE(m_columns->columnStatusOf(id), QStringLiteral("doing"));
    QCOMPARE(m_columns->columnCountForStatus(QStringLiteral("doing")), 2);
    // 新列自带可用模型，且继承当前搜索词（这里没搜，应为空）
    QVERIFY(columnModel(3) != nullptr);
    QCOMPARE(columnModel(3)->count(), 0);

    // 顺带把「新列能接卡」验证掉：按列录入后统计片与列都要对
    const QString card = m_model->addTask(QStringLiteral("后端卡"), QStringLiteral("doing"),
                                          1, QString(), 0, id);
    QVERIFY(!card.isEmpty());
    QCOMPARE(columnModel(3)->count(), 1);
    QCOMPARE(m_columns->doingCount(), 1);

    // 空名 / 未知状态一律拒绝，并且把原因交给界面
    QVERIFY(m_columns->addColumn(QStringLiteral("   "), QStringLiteral("todo")).isEmpty());
    QVERIFY(!m_columns->lastError().isEmpty());
    QCOMPARE(m_columns->rowCount(), 4);

    QVERIFY(m_columns->addColumn(QStringLiteral("野列"), QStringLiteral("archived")).isEmpty());
    QVERIFY(!m_columns->lastError().isEmpty());
    QCOMPARE(m_columns->rowCount(), 4);
}

void TestBoardColumns::renameColumnKeepsCardsAndModels()
{
    const QString card = m_model->addTask(QStringLiteral("改名不动卡"));
    QVERIFY(!card.isEmpty());
    TaskFilterModel *before = columnModel(0);
    QVERIFY(before);

    QVERIFY(m_columns->renameColumn(QStringLiteral("col-todo"), QStringLiteral("本周待办")));
    QVERIFY(m_columns->lastError().isEmpty());
    QCOMPARE(m_columns->index(0, 0).data(BoardColumns::TitleRole).toString(),
             QStringLiteral("本周待办"));
    // 改名不重建模型、不动卡片：否则界面会丢选中态与滚动位置
    QCOMPARE(columnModel(0), before);
    QCOMPARE(columnModel(0)->count(), 1);

    // 空名拒绝，旧名保留（不能让列变成空白标题）
    QVERIFY(!m_columns->renameColumn(QStringLiteral("col-todo"), QStringLiteral("  ")));
    QVERIFY(!m_columns->lastError().isEmpty());
    QCOMPARE(m_columns->index(0, 0).data(BoardColumns::TitleRole).toString(),
             QStringLiteral("本周待办"));

    // 列不存在要报错，而不是静默成功
    QVERIFY(!m_columns->renameColumn(QStringLiteral("col-not-exist"), QStringLiteral("幽灵")));
    QVERIFY(!m_columns->lastError().isEmpty());
}

void TestBoardColumns::moveColumnReordersAndStopsAtEdges()
{
    // 右移 = 与右邻居换位（不是把列扔到末尾）
    QVERIFY(m_columns->moveColumn(QStringLiteral("col-todo"), 1));
    QCOMPARE(columnIds(), QStringList({QStringLiteral("col-doing"), QStringLiteral("col-todo"),
                                       QStringLiteral("col-done")}));
    // 角色里的行号也要跟着变：键盘左右移动、拖拽落点都读它
    QCOMPARE(m_columns->index(1, 0).data(BoardColumns::IndexRole).toInt(), 1);

    // 左移回原位：来回移动必须可逆（order_key 整体重排，不会越移越乱）
    QVERIFY(m_columns->moveColumn(QStringLiteral("col-todo"), -1));
    QCOMPARE(columnIds(), QStringList({QStringLiteral("col-todo"), QStringLiteral("col-doing"),
                                       QStringLiteral("col-done")}));

    // 已经到头：返回 false、不改数据、也不留错误提示（这不是用户操作失误）
    QVERIFY(!m_columns->moveColumn(QStringLiteral("col-todo"), -1));
    QCOMPARE(columnIds(), QStringList({QStringLiteral("col-todo"), QStringLiteral("col-doing"),
                                       QStringLiteral("col-done")}));
    QVERIFY(m_columns->lastError().isEmpty());
    QVERIFY(!m_columns->moveColumn(QStringLiteral("col-done"), 1));
    QCOMPARE(columnIds(), QStringList({QStringLiteral("col-todo"), QStringLiteral("col-doing"),
                                       QStringLiteral("col-done")}));

    // 未知列不动任何数据
    QVERIFY(!m_columns->moveColumn(QStringLiteral("col-not-exist"), 1));
    QCOMPARE(columnIds(), QStringList({QStringLiteral("col-todo"), QStringLiteral("col-doing"),
                                       QStringLiteral("col-done")}));
}

void TestBoardColumns::removeColumnMigratesCards()
{
    // 同状态多加一列，把卡放进去，再删这一列
    const QString extra = m_columns->addColumn(QStringLiteral("待办·杂项"),
                                               QStringLiteral("todo"));
    QVERIFY(!extra.isEmpty());
    QCOMPARE(m_columns->columnCountForStatus(QStringLiteral("todo")), 2);

    const QString card = m_model->addTask(QStringLiteral("被迁走的卡"), QStringLiteral("todo"),
                                          1, QString(), 0, extra);
    QVERIFY(!card.isEmpty());
    // 新列追加在末尾，所以它在第 4 行（索引 3）
    QCOMPARE(m_columns->indexOfColumn(extra), 3);
    QCOMPARE(columnModel(3)->count(), 1);

    QVERIFY(m_columns->removeColumn(extra));
    QVERIFY(m_columns->lastError().isEmpty());
    QCOMPARE(m_columns->rowCount(), 3);
    QCOMPARE(m_columns->columnCountForStatus(QStringLiteral("todo")), 1);

    // 卡片不能消失：库里 column_id 被迁到同状态剩下的列，内存副本也必须跟着重读
    QCOMPARE(m_model->taskById(card).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-todo"));
    QCOMPARE(m_model->taskById(card).value(QStringLiteral("status")).toString(),
             QStringLiteral("todo"));
    QCOMPARE(columnModel(0)->count(), 1);

    // 某状态只剩一列时拒绝删除：删了这种状态的卡片就没有列可放
    QVERIFY(!m_columns->removeColumn(QStringLiteral("col-todo")));
    QVERIFY(!m_columns->lastError().isEmpty());
    QCOMPARE(m_columns->rowCount(), 3);
    QCOMPARE(columnModel(0)->count(), 1);

    // 不存在的列同样报错，而不是「删了但没删掉」的假成功
    QVERIFY(!m_columns->removeColumn(QStringLiteral("col-not-exist")));
    QVERIFY(!m_columns->lastError().isEmpty());
}

QTEST_GUILESS_MAIN(TestBoardColumns)

#include "tst_boardcolumns.moc"
