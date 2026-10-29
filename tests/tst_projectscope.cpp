// R10-02：两级看板（全局 ⇄ 项目范围）的视图语义。
//
// 对应卡片验收：
//   2. 切到某项目后列内只剩该项目卡片；切回全局，卡片数与切之前一致
//   3. 未归属卡不被任何项目视图收走（它们只在全局视图可见）
//   4. 切换范围不写库 —— task 表的 updated_at 不变
//
// 全程走真实 sqlite 临时库 + 真实 board_column 三列，不 mock：
// 「列里还剩哪些卡」正是 BoardColumns → TaskFilterModel → task_project 这条链的结果，
// 把任一段换成假对象，测出来的就只是假对象自己的行为。
//
// 另有一层是 QML 侧的（切换器长什么样、卡片角标显不显示），那部分由
// `NeonBoard.exe --smoke` + 日志无 QML 告警兜着 —— C++ 测试测不到画面。

#include "BoardColumns.h"
#include "Database.h"
#include "ProjectStore.h"
#include "TaskModel.h"
#include "TaskTree.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class TestProjectScope : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void globalViewShowsEveryCard();
    void projectViewKeepsOnlyItsCards();
    void unassignedCardsHiddenFromEveryProjectView();
    void switchingBackRestoresGlobalCounts();
    void scopeSwitchDoesNotWriteDatabase();

private:
    // 建一张 todo 卡，返回 id
    QString addTodo(const QString &title);
    // 库里每个任务行的 updated_at，用于「切换不写库」的比对
    QList<QPair<QString, qint64>> taskStamps() const;

    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_tasks = nullptr;
    TaskTree *m_tree = nullptr;
    ProjectStore *m_projects = nullptr;
    BoardColumns *m_columns = nullptr;
};

void TestProjectScope::init()
{
    QVERIFY(m_dir.isValid());
    // 每个用例一个库文件：复用同一路径会让上一个用例的卡与归属留下来，
    // 「全局有多少张」这种断言就会靠脏数据假通过。
    const QString path = m_dir.filePath(QString::fromLatin1(QTest::currentTestFunction())
                                        + QStringLiteral(".db"));
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));

    m_db = new Database;
    QString error;
    QVERIFY2(m_db->open(&error, path), qPrintable(error));
    m_tasks = new TaskModel(m_db);
    m_tasks->load();
    m_tree = new TaskTree(m_db, m_tasks);
    m_projects = new ProjectStore(m_db);
    m_columns = new BoardColumns(m_db, m_tasks);
    m_columns->setTaskTree(m_tree);
    // 注入在 reload 之前：列模型建出来时就带着项目门面，不必等下一次设置。
    m_columns->setProjectStore(m_projects);
    m_columns->reload();
}

void TestProjectScope::cleanup()
{
    delete m_columns;
    m_columns = nullptr;
    delete m_projects;
    m_projects = nullptr;
    delete m_tree;
    m_tree = nullptr;
    delete m_tasks;
    m_tasks = nullptr;
    delete m_db;
    m_db = nullptr;
}

QString TestProjectScope::addTodo(const QString &title)
{
    return m_tasks->addTask(title, QStringLiteral("todo"), 1, QString(), 0);
}

QList<QPair<QString, qint64>> TestProjectScope::taskStamps() const
{
    QList<QPair<QString, qint64>> stamps;
    QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT id, updated_at FROM task ORDER BY id")))
        return stamps;
    while (query.next())
        stamps.append({query.value(0).toString(), query.value(1).toLongLong()});
    return stamps;
}

void TestProjectScope::globalViewShowsEveryCard()
{
    const QString projectA = m_projects->createProject(QStringLiteral("NeonBoard"));
    QVERIFY(!projectA.isEmpty());

    const QString a1 = addTodo(QStringLiteral("A 的卡 1"));
    const QString a2 = addTodo(QStringLiteral("A 的卡 2"));
    const QString loose = addTodo(QStringLiteral("没归属的卡"));
    QVERIFY(!a1.isEmpty() && !a2.isEmpty() && !loose.isEmpty());
    QVERIFY(m_projects->assignTask(a1, projectA));
    QVERIFY(m_projects->assignTask(a2, projectA));

    // 默认是全局（project 为空串），三张卡都该在列里
    QCOMPARE(m_columns->project(), QString());
    QCOMPARE(m_columns->todoCount(), 3);
}

void TestProjectScope::projectViewKeepsOnlyItsCards()
{
    const QString projectA = m_projects->createProject(QStringLiteral("NeonBoard"));
    const QString projectB = m_projects->createProject(QStringLiteral("抢号系统"));
    QVERIFY(!projectA.isEmpty() && !projectB.isEmpty());

    const QString a1 = addTodo(QStringLiteral("A 的卡 1"));
    const QString a2 = addTodo(QStringLiteral("A 的卡 2"));
    const QString b1 = addTodo(QStringLiteral("B 的卡 1"));
    QVERIFY(m_projects->assignTask(a1, projectA));
    QVERIFY(m_projects->assignTask(a2, projectA));
    QVERIFY(m_projects->assignTask(b1, projectB));

    m_columns->setProject(projectA);
    QCOMPARE(m_columns->project(), projectA);
    QCOMPARE(m_columns->todoCount(), 2);

    m_columns->setProject(projectB);
    QCOMPARE(m_columns->todoCount(), 1);
}

void TestProjectScope::unassignedCardsHiddenFromEveryProjectView()
{
    const QString projectA = m_projects->createProject(QStringLiteral("NeonBoard"));
    const QString projectB = m_projects->createProject(QStringLiteral("启动窗改造"));
    QVERIFY(!projectA.isEmpty() && !projectB.isEmpty());

    const QString a1 = addTodo(QStringLiteral("A 的卡"));
    const QString loose = addTodo(QStringLiteral("没归属的卡"));
    QVERIFY(!a1.isEmpty() && !loose.isEmpty());
    QVERIFY(m_projects->assignTask(a1, projectA));
    QCOMPARE(m_projects->unassignedCount(), 1);

    // 未归属的卡不属于任何项目：每个项目视图里都只剩自己那张
    m_columns->setProject(projectA);
    QCOMPARE(m_columns->todoCount(), 1);
    m_columns->setProject(projectB);
    QCOMPARE(m_columns->todoCount(), 0);

    // 回到全局它又出现 —— 「收不走」不等于「藏没了」
    m_columns->setProject(QString());
    QCOMPARE(m_columns->todoCount(), 2);
}

void TestProjectScope::switchingBackRestoresGlobalCounts()
{
    const QString projectA = m_projects->createProject(QStringLiteral("NeonBoard"));
    QVERIFY(!projectA.isEmpty());
    const QString a1 = addTodo(QStringLiteral("A 的卡"));
    const QString a2 = addTodo(QStringLiteral("A 的卡 2"));
    const QString loose = addTodo(QStringLiteral("没归属的卡"));
    QVERIFY(m_projects->assignTask(a1, projectA));
    QVERIFY(m_projects->assignTask(a2, projectA));

    const int before = m_columns->todoCount();
    QCOMPARE(before, 3);

    m_columns->setProject(projectA);
    QCOMPARE(m_columns->todoCount(), 2);

    m_columns->setProject(QString());
    QCOMPARE(m_columns->todoCount(), before);
}

void TestProjectScope::scopeSwitchDoesNotWriteDatabase()
{
    const QString projectA = m_projects->createProject(QStringLiteral("NeonBoard"));
    QVERIFY(!projectA.isEmpty());
    const QString a1 = addTodo(QStringLiteral("A 的卡"));
    const QString loose = addTodo(QStringLiteral("没归属的卡"));
    QVERIFY(m_projects->assignTask(a1, projectA));

    const auto before = taskStamps();
    QCOMPARE(before.size(), 2);

    m_columns->setProject(projectA);
    m_columns->setProject(QString());
    m_columns->setProject(projectA);

    // 范围是视图状态：来回切三次，task 行一个字节都不该动
    QCOMPARE(taskStamps(), before);
}

QTEST_MAIN(TestProjectScope)
#include "tst_projectscope.moc"
