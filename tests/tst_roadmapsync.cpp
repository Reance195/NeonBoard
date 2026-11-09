// R18-02：RoadmapSync 多项目注册表（项目 ↔ 仓库绑定）的行为。
//
// 覆盖七条：
//   1. 全局单文件向后兼容：不绑定时导入 / 导出与改动前同语义
//   2. 重复导入幂等：重启不产生重复卡
//   3. 绑定项目的文件即「成员清单」：导入的未归属卡自动归该项目
//   4. 已归属别的项目的卡不被抢（归属是用户显式动作）
//   5. 写回隔离：项目文件只有该项目的卡，全局文件其余的卡 —— 每张卡一个「家」
//   6. 解绑：卡保留不删，家文件回落到全局文件
//   7. repo_path 列迁移不升 user_version（侧表纪律）
//
// 全用临时 sqlite 库 + 临时目录快照文件；不调 resolvePath / autoExportEnabled
// （那两者读环境变量与用户 ini，测试不给它们碰）。

#include "Database.h"
#include "ProjectStore.h"
#include "RoadmapSync.h"
#include "TaskModel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QJsonObject makeItem(const QString &id, const QString &title, const QString &status, qint64 updatedAt)
{
    QJsonObject item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("title"), title);
    item.insert(QStringLiteral("status"), status);
    item.insert(QStringLiteral("priority"), 0);
    item.insert(QStringLiteral("tags"), QJsonArray());
    item.insert(QStringLiteral("orderKey"), 1);
    item.insert(QStringLiteral("createdAt"), 1000);
    item.insert(QStringLiteral("updatedAt"), updatedAt);
    item.insert(QStringLiteral("completedAt"), 0);
    return item;
}

QString makeSnapshot(const QList<QJsonObject> &items)
{
    QJsonArray array;
    for (const QJsonObject &item : items)
        array.append(item);
    QJsonObject out;
    out.insert(QStringLiteral("version"), 3);
    out.insert(QStringLiteral("updated"), QStringLiteral("2026-11-06T00:00:00"));
    out.insert(QStringLiteral("items"), array);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Indented));
}

void writeSnapshot(const QString &path, const QString &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        qFatal("无法写入 %s", qPrintable(path));
    file.write(content.toUtf8());
}

QList<QJsonObject> snapshotItems(const QString &path)
{
    QList<QJsonObject> items;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return items;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll())
                                 .object()
                                 .value(QStringLiteral("items"))
                                 .toArray();
    for (const QJsonValue &value : array)
        items.append(value.toObject());
    return items;
}

// 快照里是否存在该 id（deleted 匹配时才算命中）
bool hasItem(const QList<QJsonObject> &items, const QString &id, bool deleted = false)
{
    for (const QJsonObject &item : items)
        if (item.value(QStringLiteral("id")).toString() == id)
            return item.value(QStringLiteral("deleted")).toBool() == deleted;
    return false;
}

QString itemTitle(const QList<QJsonObject> &items, const QString &id)
{
    for (const QJsonObject &item : items)
        if (item.value(QStringLiteral("id")).toString() == id)
            return item.value(QStringLiteral("title")).toString();
    return {};
}

} // namespace

class TestRoadmapSync : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void globalOnlyBackwardCompatible();
    void reimportIsIdempotent();
    void scopedImportAssignsToProject();
    void scopedImportDoesNotStealAssignedTasks();
    void exportRoutingSeparatesFiles();
    void unbindingKeepsTasksAndMovesHomeToGlobal();
    void repoPathColumnDoesNotBumpVersion();
    void tombstoneIgnoresCardsWithHomeElsewhere();
    void realDeleteStillAppliesForHomeFile();
    void sameRepoTwoProjectsDoNotKillEachOther();
    void failedImportDoesNotConsumeMtime();

private:
    QString globalPath() const { return m_dir.filePath(QStringLiteral("global-%1.json").arg(m_seq)); }
    QString repoDir() const { return m_dir.filePath(QStringLiteral("repo-%1").arg(m_seq)); }
    QString repoFile() const { return repoDir() + QStringLiteral("/progress.json"); }

    void writeGlobal(const QList<QJsonObject> &items) { writeSnapshot(globalPath(), makeSnapshot(items)); }
    void writeRepo(const QList<QJsonObject> &items)
    {
        QDir(m_dir.path()).mkpath(QStringLiteral("repo-%1").arg(m_seq));
        writeSnapshot(repoFile(), makeSnapshot(items));
    }

    QTemporaryDir m_dir;
    // 每个用例独立文件名：QtTest 单实例跑完全部用例，QTemporaryDir 跨用例复用，
    // 固定路径会让上一个用例的库 / 卡 / 快照残留（tst_projectstore 同款坑）。
    int m_seq = 0;
    Database *m_db = nullptr;
    TaskModel *m_model = nullptr;
    ProjectStore *m_projects = nullptr;
    RoadmapSync *m_sync = nullptr;
};

void TestRoadmapSync::init()
{
    QString error;
    m_db = new Database(this);
    QVERIFY2(m_db->open(&error, m_dir.filePath(QStringLiteral("board-%1.db").arg(++m_seq))),
             qPrintable(error));
    m_model = new TaskModel(m_db, this);
    m_model->load();
    m_projects = new ProjectStore(m_db, this);
    m_sync = new RoadmapSync(m_db, m_model, m_projects, this);
    m_sync->setPath(globalPath());
}

void TestRoadmapSync::cleanup()
{
    delete m_sync;
    delete m_projects;
    delete m_model;
    delete m_db;
    m_db = nullptr;
}

void TestRoadmapSync::globalOnlyBackwardCompatible()
{
    // 不绑定任何项目：行为与改造前的单文件模式一致
    writeGlobal({makeItem("g1", QStringLiteral("全局卡一"), QStringLiteral("todo"), 1000),
                 makeItem("g2", QStringLiteral("全局卡二"), QStringLiteral("doing"), 2000)});

    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_model->count(), 2);
    QVERIFY(m_projects->projectOfTask(QStringLiteral("g1")).isEmpty());
    QVERIFY(m_projects->projectOfTask(QStringLiteral("g2")).isEmpty());

    m_sync->exportAll();
    const QList<QJsonObject> items = snapshotItems(globalPath());
    QCOMPARE(items.size(), 2);
    QVERIFY(hasItem(items, QStringLiteral("g1")));
    QVERIFY(hasItem(items, QStringLiteral("g2")));
}

void TestRoadmapSync::reimportIsIdempotent()
{
    writeGlobal({makeItem("g1", QStringLiteral("卡一"), QStringLiteral("todo"), 1000)});

    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_model->count(), 1);

    // 重启语义：再来一遍导入，不产生重复卡
    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_model->count(), 1);
}

void TestRoadmapSync::scopedImportAssignsToProject()
{
    writeGlobal({makeItem("g1", QStringLiteral("全局卡"), QStringLiteral("todo"), 1000)});
    writeRepo({makeItem("a1", QStringLiteral("仓库卡一"), QStringLiteral("todo"), 1000),
               makeItem("a2", QStringLiteral("仓库卡二"), QStringLiteral("doing"), 2000)});

    const QString projectId = m_projects->createProject(QStringLiteral("仓库A"), QStringLiteral("#59d6ff"));
    QVERIFY(!projectId.isEmpty());
    QVERIFY(m_projects->setRepoPath(projectId, repoDir()));

    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_model->count(), 3);

    // 绑定项目文件的卡自动归属
    QCOMPARE(m_projects->projectOfTask(QStringLiteral("a1")), projectId);
    QCOMPARE(m_projects->projectOfTask(QStringLiteral("a2")), projectId);
    // 全局文件的卡保持未归属
    QVERIFY(m_projects->projectOfTask(QStringLiteral("g1")).isEmpty());
}

void TestRoadmapSync::scopedImportDoesNotStealAssignedTasks()
{
    // 卡已显式归属 Q，仓库 B 的文件里也出现它（历史双文件遗留）：
    // 导入只补齐不覆盖 —— 归属保持 Q。仓库目录先落好再绑定（目录必须存在）
    const QString q = m_projects->createProject(QStringLiteral("项目Q"));
    const QString b = m_projects->createProject(QStringLiteral("仓库B"));
    const QString taskId = m_model->addTask(QStringLiteral("争议卡"), QStringLiteral("todo"), 1, QString());
    QVERIFY(!taskId.isEmpty());
    QVERIFY(m_projects->assignTask(taskId, q));
    writeGlobal({});
    writeRepo({makeItem(taskId, QStringLiteral("争议卡"), QStringLiteral("todo"), 1000)});
    QVERIFY(m_projects->setRepoPath(b, repoDir()));

    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_projects->projectOfTask(taskId), q);
}

void TestRoadmapSync::exportRoutingSeparatesFiles()
{
    // 两张卡：t1 未归属（家 = 全局文件），t2 归属 P（P 绑定仓库 A，家 = 项目文件）
    const QString p = m_projects->createProject(QStringLiteral("仓库A"));
    writeGlobal({});
    writeRepo({});
    QVERIFY(m_projects->setRepoPath(p, repoDir()));
    const QString t1 = m_model->addTask(QStringLiteral("未归属卡"), QStringLiteral("todo"), 1, QString());
    const QString t2 = m_model->addTask(QStringLiteral("项目卡"), QStringLiteral("doing"), 1, QString());
    QVERIFY(m_projects->assignTask(t2, p));

    m_sync->exportAll();

    const QList<QJsonObject> global = snapshotItems(globalPath());
    const QList<QJsonObject> repo = snapshotItems(repoFile());
    QCOMPARE(global.size(), 1);
    QVERIFY(hasItem(global, t1));
    QVERIFY(!hasItem(global, t2));
    QCOMPARE(repo.size(), 1);
    QVERIFY(hasItem(repo, t2));
    QVERIFY(!hasItem(repo, t1));
}

void TestRoadmapSync::unbindingKeepsTasksAndMovesHomeToGlobal()
{
    writeRepo({makeItem("a1", QStringLiteral("仓库卡"), QStringLiteral("todo"), 1000)});
    const QString p = m_projects->createProject(QStringLiteral("仓库A"));
    QVERIFY(m_projects->setRepoPath(p, repoDir()));
    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_projects->projectOfTask(QStringLiteral("a1")), p);

    // 解绑：卡保留（不删），归属还在，家文件回到全局
    QVERIFY(m_projects->setRepoPath(p, QString()));
    m_sync->exportAll();

    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_projects->projectOfTask(QStringLiteral("a1")), p);
    const QList<QJsonObject> global = snapshotItems(globalPath());
    QVERIFY(hasItem(global, QStringLiteral("a1")));
    // 仓库文件原样冻结：解绑即「停止自动导入与写回」，里面的 a1 不被动
    QVERIFY(hasItem(snapshotItems(repoFile()), QStringLiteral("a1")));
}

void TestRoadmapSync::repoPathColumnDoesNotBumpVersion()
{
    // 侧表纪律：加列不升 user_version，老版本程序读库不受影响
    QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    QSqlQuery version(db);
    QVERIFY(version.exec(QStringLiteral("PRAGMA user_version")));
    if (version.next())
        QCOMPARE(version.value(0).toInt(), 2);

    QSqlQuery column(db);
    QVERIFY(column.exec(QStringLiteral("PRAGMA table_info(project)")));
    bool hasRepoPath = false;
    while (column.next()) {
        if (column.value(1).toString() == QStringLiteral("repo_path"))
            hasRepoPath = true;
    }
    QVERIFY(hasRepoPath);
}

// ---- DX-133：墓碑只对「家在本文件」的卡生效 ----
//
// 缺陷链：导出时 scoped 过滤只控制「写不写」，却把卡也漏出了 seen；
// 墓碑循环把「文件有、seen 没有」一律标 deleted，于是归属迁移 / 双项目
// 绑同路径时，别人家的卡被误标墓碑，下次导入 deleteTask 把看板卡永久删掉。

void TestRoadmapSync::tombstoneIgnoresCardsWithHomeElsewhere()
{
    // 第一段：归属迁移（家从项目文件回落全局文件）不得丢卡
    writeRepo({makeItem("m1", QStringLiteral("迁移卡"), QStringLiteral("todo"), 1000)});
    const QString p = m_projects->createProject(QStringLiteral("仓库A"));
    QVERIFY(m_projects->setRepoPath(p, repoDir()));
    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_projects->projectOfTask(QStringLiteral("m1")), p);

    // 解除归属 → 家回落全局文件
    QVERIFY(m_projects->assignTask(QStringLiteral("m1"), QString()));
    m_sync->exportAll();

    // 全局文件接住了家；项目文件里不得留下墓碑（旧条目要么原样要么被清走）
    QVERIFY(hasItem(snapshotItems(globalPath()), QStringLiteral("m1")));
    QVERIFY(!hasItem(snapshotItems(repoFile()), QStringLiteral("m1"), true));

    // 下一次导入：看板卡必须安然无恙
    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_model->count(), 1);
    QVERIFY(!m_model->taskById(QStringLiteral("m1")).isEmpty());

    // 第二段：存量损坏兜底 —— 文件里已经躺着历史误标的墓碑（updatedAt 更新，
    // 会按「文件较新」进删除分支），导入侧防线必须拦下：不删、按冲突计
    QJsonObject tomb = makeItem("m1", QStringLiteral("迁移卡"), QStringLiteral("todo"),
                                QDateTime::currentSecsSinceEpoch() + 3600);
    tomb.insert(QStringLiteral("deleted"), true);
    writeRepo({tomb});
    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_model->count(), 1);
    QVERIFY(!m_model->taskById(QStringLiteral("m1")).isEmpty());
    QVERIFY(m_sync->conflicts() >= 1);
}

void TestRoadmapSync::realDeleteStillAppliesForHomeFile()
{
    // 真删除链路不回归：看板上删掉的卡，家文件的墓碑照常对账，不误伤他卡
    writeRepo({makeItem("r1", QStringLiteral("待删卡"), QStringLiteral("todo"), 1000),
               makeItem("r2", QStringLiteral("留存卡"), QStringLiteral("doing"), 2000)});
    const QString p = m_projects->createProject(QStringLiteral("仓库A"));
    QVERIFY(m_projects->setRepoPath(p, repoDir()));
    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_model->count(), 2);

    QVERIFY(m_model->deleteTask(QStringLiteral("r1")));
    m_sync->exportAll();
    QVERIFY(hasItem(snapshotItems(repoFile()), QStringLiteral("r1"), true));
    QVERIFY(hasItem(snapshotItems(repoFile()), QStringLiteral("r2"), false));

    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_model->count(), 1);
    QVERIFY(!m_model->taskById(QStringLiteral("r2")).isEmpty());
    QVERIFY(m_model->taskById(QStringLiteral("r1")).isEmpty());
}

void TestRoadmapSync::sameRepoTwoProjectsDoNotKillEachOther()
{
    // 两个项目绑同一路径（手滑配置）：各自的导出不得把对方家的卡标成墓碑，
    // 导入也不得删对方的卡 —— 谁家归谁写，别人的条目原样或清走，唯独不删。
    // setRepoPath 要求目录已存在（绑定校验），先落目录再绑
    QDir(m_dir.path()).mkpath(QStringLiteral("repo-%1").arg(m_seq));
    const QString p1 = m_projects->createProject(QStringLiteral("仓库一"));
    const QString p2 = m_projects->createProject(QStringLiteral("仓库二"));
    QVERIFY(m_projects->setRepoPath(p1, repoDir()));
    QVERIFY(m_projects->setRepoPath(p2, repoDir()));

    const QString t1 = m_model->addTask(QStringLiteral("甲的卡"), QStringLiteral("todo"), 1, QString());
    const QString t2 = m_model->addTask(QStringLiteral("乙的卡"), QStringLiteral("doing"), 1, QString());
    QVERIFY(!t1.isEmpty() && !t2.isEmpty());
    QVERIFY(m_projects->assignTask(t1, p1));
    QVERIFY(m_projects->assignTask(t2, p2));

    m_sync->exportAll();
    QVERIFY(!hasItem(snapshotItems(repoFile()), t1, true));
    QVERIFY(!hasItem(snapshotItems(repoFile()), t2, true));

    QVERIFY(m_sync->importAll(false));
    QCOMPARE(m_model->count(), 2);
    QCOMPARE(m_projects->projectOfTask(t1), p1);
    QCOMPARE(m_projects->projectOfTask(t2), p2);
}

// DX-142：poll 的 mtime 只能在导入成功后记账——先记后导入的话，坏 JSON 触发的
// 导入失败会把这次变更吞掉。用 setFileTime 把修好后的文件按回失败时刻的 mtime：
// 修复前该文件永远不会被再导入（mtime 已被失败消费），修复后必须导入。
void TestRoadmapSync::failedImportDoesNotConsumeMtime()
{
    // mtime 判定是 1 秒粒度：三次写入若落在同一秒，goodMtime == badMtime，
    // 回滚后的 mtime 会与 entry.lastMtime 相同而被跳过。三步全部显式打点，
    // 拉开到不同秒，只考察「失败是否消费 mtime」这一个变量。
    const qint64 base =
        QDateTime::currentSecsSinceEpoch() - 300;
    const auto stamp = [](const QString &path, qint64 secs) {
        QFile stampFile(path);
        QVERIFY(stampFile.open(QIODevice::ReadWrite));
        QVERIFY(stampFile.setFileTime(
            QDateTime::fromSecsSinceEpoch(secs), QFileDevice::FileModificationTime));
        stampFile.close();
        QCOMPARE(QFileInfo(path).lastModified().toSecsSinceEpoch(), secs);
    };

    writeGlobal({makeItem(QStringLiteral("T1"), QStringLiteral("任务一"), QStringLiteral("todo"), 100)});
    stamp(globalPath(), base);
    const qint64 goodMtime = base;
    m_sync->poll();
    QCOMPARE(m_model->count(), 1);

    // 覆盖成坏 JSON（mtime 前进）：poll 导入失败，看板内容不受影响
    writeSnapshot(globalPath(), QStringLiteral("{oops"));
    stamp(globalPath(), base + 60);
    const qint64 badMtime = base + 60;
    m_sync->poll();
    QCOMPARE(m_model->count(), 1);

    // 修好文件（快照是全量语义，T1 保留），再把 mtime 按回坏 JSON 那一刻
    //（模拟「失败后文件未再变化」）
    writeGlobal({makeItem(QStringLiteral("T1"), QStringLiteral("任务一"), QStringLiteral("todo"), 100),
                 makeItem(QStringLiteral("T2"), QStringLiteral("任务二"), QStringLiteral("todo"), 200)});
    stamp(globalPath(), badMtime);

    // 同一个 mtime（坏 JSON 那次的）必须还能触发导入——失败不消费变更
    m_sync->poll();
    QCOMPARE(m_model->count(), 2);
}

QTEST_MAIN(TestRoadmapSync)
#include "tst_roadmapsync.moc"
