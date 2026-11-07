// R4-03 批次二：列感知的模型层（TaskModel 的 columnId + TaskFilterModel 按列过滤）。
//
// 用 sqlite 临时库跑真实读写，不 mock —— 这段逻辑的坑都在「写库字段与内存副本是否一致」，
// mock 掉数据库恰好会把这类问题藏起来。

#include "Database.h"
#include "DragFeel.h"
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
    void filteredSearchDropLandsAtVisibleGap();
    void filteredSearchDropTailAnchorsHiddenSuccessor();
    void filteredStageDropWithHiddenBetween();
    void unfilteredDropKeepsLegacySemantics();

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

// ---- R1-8：过滤态拖拽落点 ----
//
// 搜索 / 阶段过滤激活时，霓虹指示线画在「可见卡」之间，落库要把 beforeId 映射回
// 完整列序的 order_key（DragFeel::resolve 按可见序列算插入下标、按完整顺序锚列尾，
// moveTaskBefore 按 order_key 中点落库）。这组用例把「落库顺序与可见顺序一致」钉住：
// 指示线落在哪两张可见卡之间，过滤视图里新卡就必须出现在那两张卡之间；被过滤的卡
// 不参与落点语义，只保持完整序列的相对位置。

namespace {

QList<DragFeel::Slot> makeSlots(const QStringList &ids, qreal slotHeight = 100.0,
                                qreal spacing = 8.0)
{
    // 局部变量不能叫 slots —— Qt 把它定义为空宏
    QList<DragFeel::Slot> out;
    out.reserve(ids.size());
    qreal y = 0;
    for (const QString &id : ids) {
        out.append({id, y, slotHeight});
        y += slotHeight + spacing;
    }
    return out;
}

QString makeTask(TaskModel *model, const QString &title, const QString &tags = QString())
{
    return model->addTask(title, QStringLiteral("todo"), 1, tags, 0,
                          QStringLiteral("col-todo"));
}

} // namespace

void TestTaskColumns::filteredSearchDropLandsAtVisibleGap()
{
    // 完整列序 A, X, B, C（X 被搜索滤掉）：拖到 A 下半区，指示线画在 A 与 B 之间，
    // 新卡在过滤视图里必须紧邻 A 之后 —— 不可见的 X 不参与落点语义。
    const QString a = makeTask(m_model, QStringLiteral("alpha one"));
    const QString x = makeTask(m_model, QStringLiteral("xyz hidden"));
    const QString b = makeTask(m_model, QStringLiteral("beta two"));
    const QString c = makeTask(m_model, QStringLiteral("gamma three"));
    QVERIFY(!a.isEmpty() && !x.isEmpty() && !b.isEmpty() && !c.isEmpty());

    m_filter->setColumnId(QStringLiteral("col-todo"));
    m_filter->setSearch(QStringLiteral("a"));
    QCOMPARE(m_filter->rowCount(), 3);

    // y=70 落在 A（0..100）的下半区、B（108..208）上半区之外
    const DragFeel::Place place = DragFeel::resolve(
        makeSlots({a, b, c}), m_model->taskIdsInColumn(QStringLiteral("col-todo")),
        70.0, 8.0, 0.0);
    QCOMPARE(place.beforeId, b);

    const QString dropped = makeTask(m_model, QStringLiteral("drop anchor"));
    QVERIFY(m_model->moveTaskBefore(dropped, QStringLiteral("todo"), place.beforeId));

    QCOMPARE(m_filter->idAt(0), a);
    QCOMPARE(m_filter->idAt(1), dropped);
    QCOMPARE(m_filter->idAt(2), b);
    QCOMPARE(m_filter->idAt(3), c);
    // 完整序列：X 的相对位置不被打乱（A 在 X 前，X 在新卡前）
    const QStringList full = m_model->taskIdsInColumn(QStringLiteral("col-todo"));
    const int xAt = full.indexOf(x);
    QVERIFY(full.indexOf(a) < xAt);
    QVERIFY(xAt < full.indexOf(dropped));
}

void TestTaskColumns::filteredSearchDropTailAnchorsHiddenSuccessor()
{
    // 完整列序 A, B, X（X 被滤掉）：拖到 B 下半区，指示线在可见列尾。
    // beforeId 取完整顺序里 B 的后一张（被过滤的 X），新卡落在 B 与 X 之间 ——
    // 过滤视图里它就是最后一张，与指示线一致；X 不被挤到新卡之前。
    const QString a = makeTask(m_model, QStringLiteral("alpha one"));
    const QString b = makeTask(m_model, QStringLiteral("beta two"));
    const QString x = makeTask(m_model, QStringLiteral("xyz hidden"));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !x.isEmpty());

    m_filter->setColumnId(QStringLiteral("col-todo"));
    m_filter->setSearch(QStringLiteral("a"));
    QCOMPARE(m_filter->rowCount(), 2);

    // y=170 落在 B（108..208）的下半区：插到完整顺序里 B 的后一张之前
    const DragFeel::Place place = DragFeel::resolve(
        makeSlots({a, b}), m_model->taskIdsInColumn(QStringLiteral("col-todo")),
        170.0, 8.0, 0.0);
    QCOMPARE(place.beforeId, x);

    const QString dropped = makeTask(m_model, QStringLiteral("drop anchor"));
    QVERIFY(m_model->moveTaskBefore(dropped, QStringLiteral("todo"), place.beforeId));

    QCOMPARE(m_filter->idAt(0), a);
    QCOMPARE(m_filter->idAt(1), b);
    QCOMPARE(m_filter->idAt(2), dropped);
    QCOMPARE(m_model->taskIdsInColumn(QStringLiteral("col-todo")),
             (QStringList{a, b, dropped, x}));
}

void TestTaskColumns::filteredStageDropWithHiddenBetween()
{
    // 阶段过滤同口径：完整列序 A, X1, X2, B（X1/X2 分类不符被滤掉），
    // 拖到 A 下半区，新卡在过滤视图里落在 A 与 B 之间。
    const QString a = makeTask(m_model, QStringLiteral("alpha one"),
                               QStringLiteral("[\"工程\"]"));
    const QString x1 = makeTask(m_model, QStringLiteral("hidden one"),
                                QStringLiteral("[\"杂项\"]"));
    const QString x2 = makeTask(m_model, QStringLiteral("hidden two"),
                                QStringLiteral("[\"杂项\"]"));
    const QString b = makeTask(m_model, QStringLiteral("beta two"),
                               QStringLiteral("[\"工程\"]"));
    QVERIFY(!a.isEmpty() && !x1.isEmpty() && !x2.isEmpty() && !b.isEmpty());

    m_filter->setColumnId(QStringLiteral("col-todo"));
    m_filter->setStage(QStringLiteral("工程"));
    QCOMPARE(m_filter->rowCount(), 2);

    const DragFeel::Place place = DragFeel::resolve(
        makeSlots({a, b}), m_model->taskIdsInColumn(QStringLiteral("col-todo")),
        70.0, 8.0, 0.0);
    QCOMPARE(place.beforeId, b);

    const QString dropped = makeTask(m_model, QStringLiteral("drop anchor"),
                                     QStringLiteral("[\"工程\"]"));
    QVERIFY(m_model->moveTaskBefore(dropped, QStringLiteral("todo"), place.beforeId));

    QCOMPARE(m_filter->idAt(0), a);
    QCOMPARE(m_filter->idAt(1), dropped);
    QCOMPARE(m_filter->idAt(2), b);
    // 完整序列：X1、X2 的相对位置不被打乱
    const QStringList full = m_model->taskIdsInColumn(QStringLiteral("col-todo"));
    QCOMPARE(full.indexOf(a) < full.indexOf(x1), true);
    QCOMPARE(full.indexOf(x1) < full.indexOf(x2), true);
    QCOMPARE(full.indexOf(x2) < full.indexOf(dropped), true);
    QCOMPARE(full.indexOf(dropped) < full.indexOf(b), true);
}

void TestTaskColumns::unfilteredDropKeepsLegacySemantics()
{
    // 未过滤回归（R1-1 老语义不动）：拖到目标卡上半区 = 插到它之前；
    // 拖到末卡下半区 = 列尾。
    const QString a = makeTask(m_model, QStringLiteral("alpha one"));
    const QString b = makeTask(m_model, QStringLiteral("beta two"));
    const QString c = makeTask(m_model, QStringLiteral("gamma three"));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());

    m_filter->setColumnId(QStringLiteral("col-todo"));
    QCOMPARE(m_filter->rowCount(), 3);

    // y=115 落在 B（108..208）的上半区 → 插到 B 之前
    const DragFeel::Place upper = DragFeel::resolve(
        makeSlots({a, b, c}), m_model->taskIdsInColumn(QStringLiteral("col-todo")),
        115.0, 8.0, 0.0);
    QCOMPARE(upper.beforeId, b);
    const QString dropped = makeTask(m_model, QStringLiteral("drop anchor"));
    QVERIFY(m_model->moveTaskBefore(dropped, QStringLiteral("todo"), upper.beforeId));
    QCOMPARE(m_model->taskIdsInColumn(QStringLiteral("col-todo")),
             (QStringList{a, dropped, b, c}));

    // 再拖到末卡 C 的下半区（此时可见序列已是 4 张，c 区间 324..424，y=410 在中点之下）→ 列尾
    const DragFeel::Place tail = DragFeel::resolve(
        makeSlots({a, dropped, b, c}), m_model->taskIdsInColumn(QStringLiteral("col-todo")),
        410.0, 8.0, 0.0);
    QVERIFY(tail.beforeId.isEmpty());
    QVERIFY(m_model->moveTaskBefore(dropped, QStringLiteral("todo"), tail.beforeId));
    QCOMPARE(m_model->taskIdsInColumn(QStringLiteral("col-todo")),
             (QStringList{a, b, c, dropped}));
}

QTEST_GUILESS_MAIN(TestTaskColumns)

#include "tst_taskcolumns.moc"
