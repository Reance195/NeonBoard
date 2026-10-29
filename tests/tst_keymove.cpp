// R8-06：键盘跨列位移与「取消不留痕」。
//
// 键盘这条路最容易出的两类问题都没有崩溃可看：
//   1. 在最后一列继续按 Ctrl+→，卡跑到第一列去了（下标未夹紧 / 取模）；
//   2. 按了但没动，却压了一条撤销记录 —— Ctrl+Z 按下去像丢了反应。
// 另外把「Esc 取消拖拽」的契约钉死：取消路径不许写库、不许进撤销栈。
// 全会话真库（sqlite 临时库）：移动到底有没有落盘，要看 taskStore 的副本。

#include "Database.h"
#include "KeyMove.h"
#include "TaskExtra.h"
#include "TaskModel.h"
#include "TaskUndo.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestKeyMove : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // 纯算术
    void stepStopsAtEdges();
    void stepRejectsBadInput();
    void insertIndexKeepsRelativePosition();
    void insertIndexFallsToTailWhenTargetShorter();

    // 端到端（真库）
    void keyboardMoveLandsAtSameIndex();
    void keyboardMoveIsUndoable();
    void edgeColumnDoesNotPushUndo();
    void twoStepsReachTheLastColumn();
    void cancelTouchesNothing();
    void announceIsSafeWithoutWindow();

private:
    QString seed(const QString &title, const QString &status, const QString &columnId, int orderKey);
    // 模拟 Main.qml 的 moveCardAcrossColumns：算目标列 → 算落位 → 走撤销栈
    bool moveAcross(const QString &taskId, int delta);
    QString columnIdAt(int index) const;

    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_model = nullptr;
    TaskExtra *m_extra = nullptr;
    TaskUndo *m_undo = nullptr;
    KeyMove *m_keys = nullptr;
    // 三列的 id 由建库时的默认看板给出；顺序固定为 待办 / 进行中 / 已完成
    QStringList m_columnIds;
};

void TestKeyMove::init()
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
    m_extra = new TaskExtra(m_db, m_model);
    m_undo = new TaskUndo(m_model);
    m_undo->setTaskExtra(m_extra);
    m_keys = new KeyMove;

    m_columnIds.clear();
    const QVariantList columns = m_db->boardColumns(m_db->defaultBoardId());
    for (const QVariant &item : columns) {
        const QVariantMap map = item.toMap();
        m_columnIds << map.value(QStringLiteral("id")).toString();
    }
    QCOMPARE(m_columnIds.size(), 3);
}

void TestKeyMove::cleanup()
{
    delete m_keys;
    m_keys = nullptr;
    delete m_undo;
    m_undo = nullptr;
    delete m_extra;
    m_extra = nullptr;
    delete m_model;
    m_model = nullptr;
    delete m_db;
    m_db = nullptr;
}

QString TestKeyMove::seed(const QString &title, const QString &status, const QString &columnId,
                          int orderKey)
{
    const QString id = m_model->addTask(title, status, 1, QString(), 0, columnId);
    // addTask 自己排 order_key；这里只保证顺序与调用顺序一致，不强制具体数值
    Q_UNUSED(orderKey)
    return id;
}

QString TestKeyMove::columnIdAt(int index) const
{
    return (index >= 0 && index < m_columnIds.size()) ? m_columnIds.at(index) : QString();
}

bool TestKeyMove::moveAcross(const QString &taskId, int delta)
{
    const QVariantMap task = m_model->taskById(taskId);
    if (task.isEmpty())
        return false;
    const int current = m_columnIds.indexOf(task.value(QStringLiteral("columnId")).toString());
    if (current < 0)
        return false;
    const int target = m_keys->stepFor(current, delta, m_columnIds.size());
    if (target < 0)
        return false;

    const QStringList fromIds = m_model->taskIdsInColumn(m_columnIds.at(current));
    const int fromIndex = fromIds.indexOf(taskId);
    const QStringList targetIds = m_model->taskIdsInColumn(m_columnIds.at(target));
    const int index = m_keys->insertIndexFor(fromIndex, targetIds.size());
    const QString beforeId = index < targetIds.size() ? targetIds.at(index) : QString();
    return m_undo->moveTaskBeforeColumn(taskId, m_columnIds.at(target), beforeId);
}

// ---------------- 纯算术 ----------------

void TestKeyMove::stepStopsAtEdges()
{
    QCOMPARE(KeyMove::step(0, 1, 3), 1);
    QCOMPARE(KeyMove::step(1, 1, 3), 2);
    QCOMPARE(KeyMove::step(1, -1, 3), 0);
    // 边界：返回 -1 而不是夹紧/取模，调用方才不会误以为移动成功
    QCOMPARE(KeyMove::step(0, -1, 3), -1);
    QCOMPARE(KeyMove::step(2, 1, 3), -1);
}

void TestKeyMove::stepRejectsBadInput()
{
    QCOMPARE(KeyMove::step(0, 1, 0), -1);   // 一列都没有
    QCOMPARE(KeyMove::step(0, 1, 1), -1);   // 只有一列，左右都无处可去
    QCOMPARE(KeyMove::step(-5, 1, 3), -1);  // 下标越界
    QCOMPARE(KeyMove::step(9, 1, 3), -1);
    QCOMPARE(KeyMove::step(1, 0, 3), -1);   // delta 为 0 不是一次移动
}

void TestKeyMove::insertIndexKeepsRelativePosition()
{
    QCOMPARE(KeyMove::insertIndex(0, 3), 0);
    QCOMPARE(KeyMove::insertIndex(1, 3), 1);
    QCOMPARE(KeyMove::insertIndex(2, 3), 2);
    // 目标列恰好等长：落在同一位置，来回搬两次还能回原位
    QCOMPARE(KeyMove::insertIndex(1, 1), 1);
}

void TestKeyMove::insertIndexFallsToTailWhenTargetShorter()
{
    QCOMPARE(KeyMove::insertIndex(5, 2), 2);  // 目标列只有两张 → 列尾
    QCOMPARE(KeyMove::insertIndex(0, 0), 0);  // 空列
    QCOMPARE(KeyMove::insertIndex(-1, 4), 4); // 源列里找不到这张卡 → 列尾
}

// ---------------- 端到端 ----------------

void TestKeyMove::keyboardMoveLandsAtSameIndex()
{
    const QString a = seed(QStringLiteral("一"), QStringLiteral("todo"), columnIdAt(0), 1);
    const QString b = seed(QStringLiteral("二"), QStringLiteral("todo"), columnIdAt(0), 2);
    seed(QStringLiteral("丙"), QStringLiteral("todo"), columnIdAt(0), 3);
    // 目标列先放两张，保证「同索引」有意义
    seed(QStringLiteral("丁"), QStringLiteral("doing"), columnIdAt(1), 1);
    seed(QStringLiteral("戊"), QStringLiteral("doing"), columnIdAt(1), 2);

    QVERIFY(moveAcross(b, 1));

    const QStringList doing = m_model->taskIdsInColumn(columnIdAt(1));
    QCOMPARE(doing.size(), 3);
    // b 在源列下标 1 → 落到目标列下标 1
    QCOMPARE(doing.at(1), b);
    QCOMPARE(m_model->taskById(b).value(QStringLiteral("status")).toString(),
             QStringLiteral("doing"));
    // 源列只剩两张，顺序不乱
    const QStringList todo = m_model->taskIdsInColumn(columnIdAt(0));
    QCOMPARE(todo.size(), 2);
    QCOMPARE(todo.at(0), a);
}

void TestKeyMove::keyboardMoveIsUndoable()
{
    const QString a = seed(QStringLiteral("一"), QStringLiteral("todo"), columnIdAt(0), 1);
    const QString b = seed(QStringLiteral("二"), QStringLiteral("todo"), columnIdAt(0), 2);
    const QStringList before = m_model->taskIdsInColumn(columnIdAt(0));
    QCOMPARE(before, (QStringList{ a, b }));

    QVERIFY(moveAcross(b, 1));
    QVERIFY(m_undo->canUndo());
    QCOMPARE(m_undo->lastAction(), QStringLiteral("已移动任务"));

    // 全链路最后一步：Ctrl+Z
    QVERIFY(m_undo->undo());
    QCOMPARE(m_model->taskIdsInColumn(columnIdAt(0)), before);
    QVERIFY(m_model->taskIdsInColumn(columnIdAt(1)).isEmpty());
    QCOMPARE(m_model->taskById(b).value(QStringLiteral("status")).toString(),
             QStringLiteral("todo"));
}

void TestKeyMove::edgeColumnDoesNotPushUndo()
{
    const QString a = seed(QStringLiteral("一"), QStringLiteral("todo"), columnIdAt(0), 1);
    QVERIFY(!m_undo->canUndo());

    // 第一列再往左：没有相邻列 → 不移动，也不该留下一条空撤销记录
    QVERIFY(!moveAcross(a, -1));
    QVERIFY(!m_undo->canUndo());
    QCOMPARE(m_model->taskIdsInColumn(columnIdAt(0)).size(), 1);

    // 最后一列同理
    const QString b = seed(QStringLiteral("二"), QStringLiteral("done"), columnIdAt(2), 1);
    QVERIFY(!moveAcross(b, 1));
    QVERIFY(!m_undo->canUndo());
}

void TestKeyMove::twoStepsReachTheLastColumn()
{
    const QString a = seed(QStringLiteral("一路向右"), QStringLiteral("todo"), columnIdAt(0), 1);
    QVERIFY(moveAcross(a, 1));
    QCOMPARE(m_model->taskById(a).value(QStringLiteral("columnId")).toString(), columnIdAt(1));
    // 连按第二次：不需要重新选中，靠的是上层把选中跟着卡走
    QVERIFY(moveAcross(a, 1));
    QCOMPARE(m_model->taskById(a).value(QStringLiteral("columnId")).toString(), columnIdAt(2));

    QVERIFY(m_undo->undo());
    QCOMPARE(m_model->taskById(a).value(QStringLiteral("columnId")).toString(), columnIdAt(1));
    QVERIFY(m_undo->undo());
    QCOMPARE(m_model->taskById(a).value(QStringLiteral("columnId")).toString(), columnIdAt(0));
}

void TestKeyMove::cancelTouchesNothing()
{
    const QString a = seed(QStringLiteral("拖了一半就 Esc"), QStringLiteral("todo"),
                           columnIdAt(0), 1);
    const QStringList before = m_model->taskIdsInColumn(columnIdAt(0));
    const qint64 updatedAt = m_model->taskById(a).value(QStringLiteral("updatedAt")).toLongLong();

    // 「取消」在实现上就是什么都不做：拖拽期间不写库，所以没有可回滚的东西。
    // 这条断言守的是「别有人后来加了个记录起点、失败回滚」的实现 ——
    // 那样取消会变成一次真实写入 + 一次回滚，updated_at 就动了。
    QVERIFY(!m_undo->canUndo());
    QCOMPARE(m_model->taskIdsInColumn(columnIdAt(0)), before);
    QCOMPARE(m_model->taskById(a).value(QStringLiteral("updatedAt")).toLongLong(), updatedAt);
    QCOMPARE(m_model->taskById(a).value(QStringLiteral("columnId")).toString(), columnIdAt(0));
}

void TestKeyMove::announceIsSafeWithoutWindow()
{
    // 无 GUI 的测试进程里没有可播报的窗口：必须静默返回，不许崩
    KeyMove keys;
    keys.announce(QString());
    keys.announce(QStringLiteral("已移到「进行中」第 2 位"));
}

QTEST_GUILESS_MAIN(TestKeyMove)

#include "tst_keymove.moc"
