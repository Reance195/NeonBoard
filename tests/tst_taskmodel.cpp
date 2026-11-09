#include "Database.h"
#include "TaskModel.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

// R10-03：标签语义修正。
//
// tags 一个数组里混着三种语义（阶段 / 分类 / 角色），而 R10-03 之前只读 tags[0]。
// 这些用例把「顺序无关」钉住，避免以后再退回拿数组下标当模型：
// 一旦有人把 stageOf 改回 firstTag，stageFromAnyPosition 立刻红。
//
// DX-140：撤销删除丢列、renumber 失败内存漂移、重复 order_key 的 tiebreak 口径。
// 这三类坑全在「写库字段与内存副本是否一致」，mock 掉数据库恰好会把问题藏起来，
// 所以与 tst_taskcolumns 同一方式：sqlite 临时库 + 真实 TaskModel。
class TestTaskModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void stageFromAnyPosition();
    void stageIgnoresRoleTag();
    void stageFallsBackToOther();
    void stageHandlesBareString();
    void categorySkipsStageAndRole();
    void categoryEmptyWhenNoCategory();
    void roleTagIsCaseInsensitive();
    void firstTagKeepsLegacyMeaning();

    void undoRestoreKeepsCustomColumn();
    void renumberColumnRollsBackMemoryOnCommitFailure();
    void renumberStatusRollsBackMemoryOnCommitFailure();
    void renumberStatusUsesIdTiebreakMatchingDisplayOrder();

private:
    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_model = nullptr;
};

void TestTaskModel::init()
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
}

void TestTaskModel::cleanup()
{
    delete m_model;
    m_model = nullptr;
    delete m_db;
    m_db = nullptr;
}

void TestTaskModel::stageFromAnyPosition()
{
    // 阶段排在第二位也必须认出来 —— 这正是 R10-03 之前丢卡的主因。
    QCOMPARE(TaskModel::stageOf(QStringLiteral(R"(["主题","R0"])")), QStringLiteral("R0"));
    QCOMPARE(TaskModel::stageOf(QStringLiteral(R"(["R4","工程"])")), QStringLiteral("R4"));
    QCOMPARE(TaskModel::stageOf(QStringLiteral(R"(["可靠性","工程","DX"])")), QStringLiteral("DX"));
}

void TestTaskModel::stageIgnoresRoleTag()
{
    // 实测库里 33 张卡就是这一种：只有角色词，没有阶段。
    QCOMPARE(TaskModel::stageOf(QStringLiteral(R"(["agent"])")), QStringLiteral("OTHER"));
    QCOMPARE(TaskModel::stageOf(QStringLiteral(R"(["agent","R6"])")), QStringLiteral("R6"));
}

void TestTaskModel::stageFallsBackToOther()
{
    QCOMPARE(TaskModel::stageOf(QStringLiteral("[]")), QStringLiteral("OTHER"));
    QCOMPARE(TaskModel::stageOf(QString()), QStringLiteral("OTHER"));
}

void TestTaskModel::stageHandlesBareString()
{
    // 非 JSON 的裸标签（历史行）整体当一个标签处理，不丢内容。
    QCOMPARE(TaskModel::stageOf(QStringLiteral("r3")), QStringLiteral("R3"));
}

void TestTaskModel::categorySkipsStageAndRole()
{
    QCOMPARE(TaskModel::categoryOf(QStringLiteral(R"(["R4","工程","agent"])")),
             QStringLiteral("工程"));
    QCOMPARE(TaskModel::categoryOf(QStringLiteral(R"(["主题","R0"])")), QStringLiteral("主题"));
}

void TestTaskModel::categoryEmptyWhenNoCategory()
{
    // 只有阶段或只有角色词时不编造分类 —— 空串由界面呈现为「未分类」，
    // 比把 agent / R5 当成分类贴在卡面上诚实。
    QVERIFY(TaskModel::categoryOf(QStringLiteral(R"(["agent"])")).isEmpty());
    QVERIFY(TaskModel::categoryOf(QStringLiteral(R"(["R5"])")).isEmpty());
    QVERIFY(TaskModel::categoryOf(QStringLiteral("[]")).isEmpty());
}

void TestTaskModel::roleTagIsCaseInsensitive()
{
    QVERIFY(TaskModel::isRoleTag(QStringLiteral("agent")));
    QVERIFY(TaskModel::isRoleTag(QStringLiteral("AGENT")));
    QVERIFY(!TaskModel::isRoleTag(QStringLiteral("R4")));
    QVERIFY(!TaskModel::isRoleTag(QStringLiteral("工程")));
}

void TestTaskModel::firstTagKeepsLegacyMeaning()
{
    // firstTag 仍是「第一个标签」：导出（DataPort）与插件快照还依赖这个契约，
    // R10-03 只改卡面显示（TagRole），不动它。
    QCOMPARE(TaskModel::firstTag(QStringLiteral(R"(["主题","R0"])")), QStringLiteral("主题"));
}

// DX-140：撤销删除的快照必须带列归属——自定义列删卡再撤销，卡回到原列而不是
// 该状态的默认列；默认列卡撤销保持原行为（仍落默认列）。
void TestTaskModel::undoRestoreKeepsCustomColumn()
{
    QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery insert(db);
    QVERIFY(insert.exec(QStringLiteral(
        "INSERT INTO board_column (id, board_id, name, status, order_key) VALUES "
        "('col-custom', 'board-default', '自定义', 'todo', 4096)")));

    const QString id = m_model->addTask(QStringLiteral("自定义列卡"), QStringLiteral("todo"),
                                        1, QString(), 0, QStringLiteral("col-custom"));
    QVERIFY(!id.isEmpty());
    QCOMPARE(m_model->taskById(id).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-custom"));

    const QVariantMap snapshot = m_model->removeTask(id);
    QVERIFY(!snapshot.isEmpty());
    QCOMPARE(snapshot.value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-custom"));
    QVERIFY(m_model->taskById(id).isEmpty());

    QVERIFY2(!m_model->restoreTask(snapshot).isEmpty(),
             "restoreTask 应还原快照并返回任务 id");
    QCOMPARE(m_model->taskById(id).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-custom"));
    QCOMPARE(m_model->taskById(id).value(QStringLiteral("status")).toString(),
             QStringLiteral("todo"));

    // 默认列卡撤销不回归：快照列 = 默认列，撤销后仍落默认列
    const QString defaultId = m_model->addTask(QStringLiteral("默认列卡"));
    const QVariantMap defaultSnapshot = m_model->removeTask(defaultId);
    QCOMPARE(defaultSnapshot.value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-todo"));
    QVERIFY(!m_model->restoreTask(defaultSnapshot).isEmpty());
    QCOMPARE(m_model->taskById(defaultId).value(QStringLiteral("columnId")).toString(),
             QStringLiteral("col-todo"));
}

// DX-140：renumber 的 UPDATE 在事务里失败时（触发器 RAISE 注入），内存 orderKey
// 不得先行改写——commit 成功前内存与库必须一致，失败即整体回滚。
// renumberColumn 是私有函数，经公开入口 moveTaskBeforeColumn（gap≤1 时触发）驱动。
void TestTaskModel::renumberColumnRollsBackMemoryOnCommitFailure()
{
    const QString a = m_model->addTask(QStringLiteral("甲"));
    const QString b = m_model->addTask(QStringLiteral("乙"));
    const QString c = m_model->addTask(QStringLiteral("丙"));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());

    // 把 a、b 的 order_key 压到只差 1，制造「插入必须触发 renumber」的间隙
    QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery squeeze(db);
    QVERIFY(squeeze.exec(QStringLiteral(
        "UPDATE task SET order_key = 1 WHERE id = '%1'").arg(a)));
    QVERIFY(squeeze.exec(QStringLiteral(
        "UPDATE task SET order_key = 2 WHERE id = '%1'").arg(b)));
    m_model->load();
    QCOMPARE(m_model->taskById(a).value(QStringLiteral("orderKey")).toLongLong(), 1);

    // 注入：a 是 renumber 的第一个 UPDATE 目标（(order_key,id) 最小），一改就 abort
    QSqlQuery trigger(db);
    QVERIFY(trigger.exec(QStringLiteral(
        "CREATE TRIGGER fail_renumber BEFORE UPDATE OF order_key ON task "
        "WHEN NEW.id = '%1' AND NEW.order_key <> OLD.order_key "
        "BEGIN SELECT RAISE(ABORT, 'injected'); END").arg(a)));

    // 把 c 插到 b 之前：gap=1 触发 renumberColumn，第一条 UPDATE 就被拦下。
    // moveTaskBeforeColumn 对 renumber 失败是软处理（仍按旧间隙落位），
    // 这里只钉 DX-140 的判据：失败的 renumber 不得触碰内存。
    QVERIFY(m_model->moveTaskBeforeColumn(c, QStringLiteral("col-todo"), b));
    QCOMPARE(m_model->taskById(a).value(QStringLiteral("orderKey")).toLongLong(), 1);
    QCOMPARE(m_model->taskById(b).value(QStringLiteral("orderKey")).toLongLong(), 2);
    QSqlQuery probe(db);
    QVERIFY(probe.exec(QStringLiteral("SELECT order_key FROM task WHERE id = '%1'").arg(a)));
    QVERIFY(probe.next());
    QCOMPARE(probe.value(0).toLongLong(), 1);
    QVERIFY(probe.exec(QStringLiteral("SELECT order_key FROM task WHERE id = '%1'").arg(b)));
    QVERIFY(probe.next());
    QCOMPARE(probe.value(0).toLongLong(), 2);

    // 注入解除后同样的落点：renumber 正常收敛，列内顺序恢复且键严格递增
    trigger.exec(QStringLiteral("DROP TRIGGER fail_renumber"));
    QVERIFY(m_model->moveTaskBeforeColumn(c, QStringLiteral("col-todo"), b));
    const QStringList order = m_model->taskIdsInColumn(QStringLiteral("col-todo"));
    QCOMPARE(order, (QStringList{a, c, b}));
    qint64 previousKey = 0;
    for (const QString &taskId : order) {
        const qint64 key = m_model->taskById(taskId)
                               .value(QStringLiteral("orderKey")).toLongLong();
        QVERIFY2(key > previousKey, "renumber 收敛后列内 order_key 必须严格递增");
        previousKey = key;
    }
}

// DX-140：renumberStatus 与 renumberColumn 同构——commit 失败时内存 orderKey
// 必须保持原值。经公开入口 moveTask（gap≤1 时触发）驱动。
void TestTaskModel::renumberStatusRollsBackMemoryOnCommitFailure()
{
    const QString a = m_model->addTask(QStringLiteral("甲"));
    const QString b = m_model->addTask(QStringLiteral("乙"));
    const QString c = m_model->addTask(QStringLiteral("丙"));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());

    QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery squeeze(db);
    QVERIFY(squeeze.exec(QStringLiteral(
        "UPDATE task SET order_key = 1 WHERE id = '%1'").arg(a)));
    QVERIFY(squeeze.exec(QStringLiteral(
        "UPDATE task SET order_key = 2 WHERE id = '%1'").arg(b)));
    m_model->load();

    // 注入：a 是 renumber 的第一个 UPDATE 目标（(order_key,id) 最小），一改就 abort
    QSqlQuery trigger(db);
    QVERIFY(trigger.exec(QStringLiteral(
        "CREATE TRIGGER fail_renumber BEFORE UPDATE OF order_key ON task "
        "WHEN NEW.id = '%1' AND NEW.order_key <> OLD.order_key "
        "BEGIN SELECT RAISE(ABORT, 'injected'); END").arg(a)));

    // 把 c 移到下标 1：gap=1 触发 renumberStatus，第一条 UPDATE 就被拦下
    QVERIFY(m_model->moveTask(c, QStringLiteral("todo"), 1));
    QCOMPARE(m_model->taskById(a).value(QStringLiteral("orderKey")).toLongLong(), 1);
    QCOMPARE(m_model->taskById(b).value(QStringLiteral("orderKey")).toLongLong(), 2);
    QSqlQuery probe(db);
    QVERIFY(probe.exec(QStringLiteral("SELECT order_key FROM task WHERE id = '%1'").arg(a)));
    QVERIFY(probe.next());
    QCOMPARE(probe.value(0).toLongLong(), 1);
    QVERIFY(probe.exec(QStringLiteral("SELECT order_key FROM task WHERE id = '%1'").arg(b)));
    QVERIFY(probe.next());
    QCOMPARE(probe.value(0).toLongLong(), 2);

    // 注入解除后同样的落点：renumber 正常收敛，展示顺序恢复且键严格递增
    trigger.exec(QStringLiteral("DROP TRIGGER fail_renumber"));
    QVERIFY(m_model->moveTask(c, QStringLiteral("todo"), 1));
    const QStringList order = m_model->taskIdsInStatus(QStringLiteral("todo"));
    QCOMPARE(order, (QStringList{a, c, b}));
    qint64 previousKey = 0;
    for (const QString &taskId : order) {
        const qint64 key = m_model->taskById(taskId)
                               .value(QStringLiteral("orderKey")).toLongLong();
        QVERIFY2(key > previousKey, "renumber 收敛后状态内 order_key 必须严格递增");
        previousKey = key;
    }
}

// DX-140：同状态出现重复 order_key 时，renumber 的赋值次序必须与展示顺序一致
// （同样的 (order_key, id) tiebreak）——否则重排会把「并列」的卡片次序洗乱。
void TestTaskModel::renumberStatusUsesIdTiebreakMatchingDisplayOrder()
{
    const QString a = m_model->addTask(QStringLiteral("甲"));
    const QString b = m_model->addTask(QStringLiteral("乙"));
    const QString c = m_model->addTask(QStringLiteral("丙"));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());

    // 把 b 压成与 a 同键：展示顺序（order_key 升序、id 兜底）里 a、b 的相对次序
    // 完全由 id 决定
    QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery squeeze(db);
    QVERIFY(squeeze.exec(QStringLiteral(
        "UPDATE task SET order_key = 1024 WHERE id = '%1'").arg(b)));
    m_model->load();

    const QString first = a < b ? a : b;
    const QString second = a < b ? b : a;
    QCOMPARE(m_model->taskIdsInStatus(QStringLiteral("todo")),
             (QStringList{first, second, c}));

    // 把 c 移到下标 1：与前卡间隙 0 触发 renumberStatus，并列键被收敛
    QVERIFY(m_model->moveTask(c, QStringLiteral("todo"), 1));

    // renumber 后（order_key, id) 的先后保持不变：first 仍拿最小键，second 次之；
    // 若 renumberStatus 不带 id tiebreak，并列行的赋值次序不定，这里就会洗牌
    QCOMPARE(m_model->taskById(first).value(QStringLiteral("orderKey")).toLongLong(), 1024);
    QCOMPARE(m_model->taskById(second).value(QStringLiteral("orderKey")).toLongLong(), 2048);
    QCOMPARE(m_model->taskIdsInStatus(QStringLiteral("todo")),
             (QStringList{first, c, second}));
}

QTEST_MAIN(TestTaskModel)
#include "tst_taskmodel.moc"
