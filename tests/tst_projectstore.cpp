// R10-01：项目维度门面（project / task_project 侧表）的行为。
//
// 覆盖八条：
//   1. 老库打开即建表，且 user_version 仍是 2（侧表纪律：不升版本）
//   2. 归属写入后能读回
//   3. 改归属不留旧行（一卡一项目）
//   4. 归属到不存在的项目被拒
//   5. 解除归属 = 删行，查不到即未归属
//   6. 删项目不级联删卡，只清归属
//   7. 计数（项目卡片数 / 未归属数）与实际一致
//   8. ensureProject 幂等

#include "Database.h"
#include "ProjectStore.h"
#include "TaskModel.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class TestProjectStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void schemaCreatedWithoutBumpingVersion();
    void assignAndReadBack();
    void reassignLeavesNoStaleRow();
    void assignToUnknownProjectRejected();
    void clearAssignment();
    void removeProjectKeepsTasks();
    void countsReflectAssignments();
    void ensureProjectIsIdempotent();

private:
    int assignmentRows(const QString &taskId) const;

    QTemporaryDir m_dir;
    // 每个用例一个独立库文件：同一个临时目录复用同一路径会让上一个用例的项目
    // 与卡片残留下来，ensureProject 那类用例就会靠脏数据「假通过」。
    int m_seq = 0;
    Database *m_db = nullptr;
    TaskModel *m_model = nullptr;
    ProjectStore *m_projects = nullptr;
};

void TestProjectStore::init()
{
    const QString path = m_dir.filePath(QStringLiteral("board-%1.db").arg(++m_seq));
    m_db = new Database(this);
    QString error;
    QVERIFY2(m_db->open(&error, path), qPrintable(error));
    m_model = new TaskModel(m_db, this);
    m_model->load();
    m_projects = new ProjectStore(m_db, this);
}

void TestProjectStore::cleanup()
{
    delete m_projects;
    delete m_model;
    delete m_db;
    m_projects = nullptr;
    m_model = nullptr;
    m_db = nullptr;
}

int TestProjectStore::assignmentRows(const QString &taskId) const
{
    QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM task_project WHERE task_id = ?"));
    query.addBindValue(taskId);
    if (query.exec() && query.next())
        return query.value(0).toInt();
    return -1;
}

void TestProjectStore::schemaCreatedWithoutBumpingVersion()
{
    QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());

    QSqlQuery version(db);
    QVERIFY(version.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(version.next());
    QCOMPARE(version.value(0).toInt(), 2);
    QCOMPARE(Database::kSchemaVersion, 2);

    for (const QString &table : {QStringLiteral("project"), QStringLiteral("task_project")}) {
        QSqlQuery exists(db);
        exists.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name = ?"));
        exists.addBindValue(table);
        QVERIFY(exists.exec());
        QVERIFY(exists.next());
        QCOMPARE(exists.value(0).toInt(), 1);
    }
}

void TestProjectStore::assignAndReadBack()
{
    const QString projectId = m_projects->createProject(QStringLiteral("NeonBoard"),
                                                        QStringLiteral("#59d6ff"));
    QVERIFY(!projectId.isEmpty());

    const QString taskId = m_model->addTask(QStringLiteral("一张卡"), QStringLiteral("todo"), 1,
                                            QStringLiteral(R"(["R10"])"));
    QVERIFY(!taskId.isEmpty());

    QVERIFY(m_projects->assignTask(taskId, projectId));
    QCOMPARE(m_projects->projectOfTask(taskId), projectId);
}

void TestProjectStore::reassignLeavesNoStaleRow()
{
    const QString first = m_projects->createProject(QStringLiteral("NeonBoard"));
    const QString second = m_projects->createProject(QStringLiteral("抢号系统"));
    const QString taskId = m_model->addTask(QStringLiteral("换项目"), QStringLiteral("todo"), 1, QString());

    QVERIFY(m_projects->assignTask(taskId, first));
    QVERIFY(m_projects->assignTask(taskId, second));

    // 一卡一项目：主键 REPLACE 之后表里只该剩一行，指向第二个项目。
    QCOMPARE(assignmentRows(taskId), 1);
    QCOMPARE(m_projects->projectOfTask(taskId), second);
    QCOMPARE(m_projects->taskCount(first), 0);
    QCOMPARE(m_projects->taskCount(second), 1);
}

void TestProjectStore::assignToUnknownProjectRejected()
{
    const QString taskId = m_model->addTask(QStringLiteral("幽灵项目"), QStringLiteral("todo"), 1, QString());
    QVERIFY(!m_projects->assignTask(taskId, QStringLiteral("no-such-project")));
    QCOMPARE(assignmentRows(taskId), 0);
    QVERIFY(m_projects->projectOfTask(taskId).isEmpty());
}

void TestProjectStore::clearAssignment()
{
    const QString projectId = m_projects->createProject(QStringLiteral("临时项目"));
    const QString taskId = m_model->addTask(QStringLiteral("待解除"), QStringLiteral("todo"), 1, QString());

    QVERIFY(m_projects->assignTask(taskId, projectId));
    QCOMPARE(assignmentRows(taskId), 1);

    // 空项目 id = 解除归属；删行而不是写空串，保持「查不到即未归属」。
    QVERIFY(m_projects->assignTask(taskId, QString()));
    QCOMPARE(assignmentRows(taskId), 0);
    QVERIFY(m_projects->projectOfTask(taskId).isEmpty());
}

void TestProjectStore::removeProjectKeepsTasks()
{
    const QString projectId = m_projects->createProject(QStringLiteral("要删的项目"));
    const QString taskId = m_model->addTask(QStringLiteral("不该被删"), QStringLiteral("todo"), 1, QString());
    QVERIFY(m_projects->assignTask(taskId, projectId));

    QVERIFY(m_projects->removeProject(projectId));

    // 卡片是人的数据，项目只是分类：删项目只清归属，卡必须还在。
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(assignmentRows(taskId), 0);
    QVERIFY(m_projects->projectOfTask(taskId).isEmpty());
    QCOMPARE(m_projects->projects().size(), 0);
}

void TestProjectStore::countsReflectAssignments()
{
    const QString a = m_projects->createProject(QStringLiteral("A"));
    const QString b = m_projects->createProject(QStringLiteral("B"));

    const QString t1 = m_model->addTask(QStringLiteral("卡一"), QStringLiteral("todo"), 1, QString());
    const QString t2 = m_model->addTask(QStringLiteral("卡二"), QStringLiteral("todo"), 1, QString());
    m_model->addTask(QStringLiteral("未归属卡"), QStringLiteral("todo"), 1, QString());

    QVERIFY(m_projects->assignTask(t1, a));
    QVERIFY(m_projects->assignTask(t2, b));

    QCOMPARE(m_projects->taskCount(a), 1);
    QCOMPARE(m_projects->taskCount(b), 1);
    QCOMPARE(m_projects->unassignedCount(), 1);
}

void TestProjectStore::ensureProjectIsIdempotent()
{
    // 批量归类脚本与首启种子都会重复调，必须按名字幂等，否则每跑一次多一个项目。
    const QString first = m_projects->ensureProject(QStringLiteral("NeonBoard"));
    const QString second = m_projects->ensureProject(QStringLiteral("NeonBoard"));
    QVERIFY(!first.isEmpty());
    QCOMPARE(second, first);
    QCOMPARE(m_projects->projects().size(), 1);

    // 首尾空白不该造出第二个同名项目
    QCOMPARE(m_projects->ensureProject(QStringLiteral("  NeonBoard  ")), first);
    QCOMPARE(m_projects->projects().size(), 1);
}

QTEST_MAIN(TestProjectStore)
#include "tst_projectstore.moc"
