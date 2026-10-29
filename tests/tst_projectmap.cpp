#include <QtTest>

#include <QFileInfo>
#include <QVariantList>
#include <QVariantMap>

#include "ProjectMap.h"

// 项目全景的护栏测试。
//
// 这组断言的目的不是「测函数」，而是「锁住全景图和仓库的一致性」：
// 有人改了 docs/iteration-roadmap.md 的表格列、挪走 scripts/ 下的脚本、
// 或者把派生规则退回「只认 git 标记」，这里必须先红，而不是等用户打开面板
// 看到一张撒谎的地图。
class TestProjectMap : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void findsRepository();
    void roadmapTableDrivesMilestones();
    void stageOrderFollowsRoadmapRows();
    void chainNodesAreVerifyable();
    void skeletonScansRealDirectories();
    void noStructuralDrift();
    void gitMarkerBeatsStaleBaseline();
    void ledgerBaselineCountsAsDone();

private:
    // 派生要跑 3 次 git + 扫 ~180 个文件，所有用例共用一份快照，避免重复耗时
    ProjectMap *m_map = nullptr;

    static QVariantMap milestoneOf(const QVariantList &list, const QString &stage)
    {
        for (const QVariant &v : list) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("stage")).toString() == stage)
                return m;
        }
        return {};
    }
};

void TestProjectMap::initTestCase()
{
    m_map = new ProjectMap(this);
    m_map->refresh();
    // 派生在后台线程跑，等它发布快照（超时给足，CI 上 git 也慢）
    QTRY_VERIFY_WITH_TIMEOUT(!m_map->busy(), 120000);
    QVERIFY(m_map->revision() >= 1);
}

void TestProjectMap::findsRepository()
{
    QVERIFY2(!m_map->sourcePath().isEmpty(),
             "没有从可执行文件目录向上找到 docs/board-ledger.json");
    QVERIFY(QFileInfo::exists(m_map->sourcePath() + QStringLiteral("/docs/board-ledger.json")));
    QVERIFY(!m_map->refreshedAt().isEmpty());
}

void TestProjectMap::roadmapTableDrivesMilestones()
{
    const QVariantList ms = m_map->milestones();
    QVERIFY2(ms.size() >= 8, "路线图 §9 至少应有 R0–R7 八个里程碑");

    QStringList stages;
    for (const QVariant &v : ms)
        stages.append(v.toMap().value(QStringLiteral("stage")).toString());

    // 顺序来自表格行序，不是代码里写死的列表
    QVERIFY(stages.indexOf(QStringLiteral("R0")) == 0);
    QVERIFY(stages.indexOf(QStringLiteral("R0")) < stages.indexOf(QStringLiteral("R7")));

    for (const QVariant &v : ms) {
        const QVariantMap m = v.toMap();
        const QString stage = m.value(QStringLiteral("stage")).toString();
        const QString topic = m.value(QStringLiteral("topic")).toString();
        QVERIFY2(!topic.isEmpty(), qPrintable(stage + " 没有主题"));
        // 主题必须来自表格第二列；退化成阶段号说明没解析到表
        QVERIFY2(topic != stage, qPrintable(stage + " 的主题退化成了阶段号"));
        QVERIFY(m.value(QStringLiteral("inRoadmap")).toBool());
    }
}

// DX-04：阶段条的阶段集合与顺序来自 §9 行序。这条钉住「§9 加一行，阶段条自动多一个
// chip」—— 而且顺序必须与面板同源，不能出现「面板一个顺序、阶段条另一个顺序」。
void TestProjectMap::stageOrderFollowsRoadmapRows()
{
    const QStringList order = m_map->stageOrder();
    QVERIFY2(order.size() >= 8, "路线图 §9 至少应有 R0–R7 八个阶段");
    QCOMPARE(order.first(), QStringLiteral("R0"));
    QVERIFY(order.indexOf(QStringLiteral("R0")) < order.indexOf(QStringLiteral("R7")));

    // 与面板同源：只取面板里标记为 inRoadmap 的那些，顺序应逐项一致
    QStringList inRoadmapStages;
    for (const QVariant &v : m_map->milestones()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("inRoadmap")).toBool())
            inRoadmapStages.append(m.value(QStringLiteral("stage")).toString());
    }
    QCOMPARE(order, inRoadmapStages);
}

void TestProjectMap::chainNodesAreVerifyable()
{
    const QVariantList chain = m_map->chain();
    QVERIFY2(chain.size() >= 7, "链路节点太少，docs/project-map.json 可能没被读到");

    for (const QVariant &v : chain) {
        const QVariantMap node = v.toMap();
        const QString title = node.value(QStringLiteral("title")).toString();
        QVERIFY(!title.isEmpty());

        // 每个节点都必须挂真实文件，否则「实证」就成了摆设
        const QVariantList evidence = node.value(QStringLiteral("evidence")).toList();
        QVERIFY2(evidence.size() >= 2, qPrintable(title + " 的证据不足两个文件"));
        QVERIFY2(node.value(QStringLiteral("missing")).toInt() == 0,
                 qPrintable(title + " 有证据文件不存在（断链）"));
        QVERIFY(node.value(QStringLiteral("ok")).toBool());
    }
}

void TestProjectMap::skeletonScansRealDirectories()
{
    const QVariantList groups = m_map->skeleton();
    QVERIFY2(groups.size() >= 5, "骨架分组太少");
    for (const QVariant &v : groups) {
        const QVariantMap g = v.toMap();
        const QString name = g.value(QStringLiteral("group")).toString();
        QVERIFY2(g.value(QStringLiteral("files")).toInt() > 0,
                 qPrintable(name + " 没扫到文件，目录路径可能写错了"));
        QVERIFY2(g.value(QStringLiteral("loc")).toInt() > 0, qPrintable(name + " 行数为 0"));
    }
}

void TestProjectMap::noStructuralDrift()
{
    QCOMPARE(m_map->errorCount(), 0);

    for (const QVariant &v : m_map->drift()) {
        const QVariantMap item = v.toMap();
        const QString level = item.value(QStringLiteral("level")).toString();
        QVERIFY2(level == QStringLiteral("info") || level == QStringLiteral("warn"),
                 qPrintable(item.value(QStringLiteral("title")).toString()
                            + "：" + item.value(QStringLiteral("detail")).toString()));
    }
}

void TestProjectMap::gitMarkerBeatsStaleBaseline()
{
    // R4 的台账 baseline 仍是 todo，但十张卡都有 #R4-0x=done 标记：
    // 派生必须认 git，否则 R4 会被读成 0/10
    const QVariantMap r4 = milestoneOf(m_map->milestones(), QStringLiteral("R4"));
    QVERIFY(!r4.isEmpty());
    QVERIFY2(r4.value(QStringLiteral("done")).toInt() == r4.value(QStringLiteral("total")).toInt(),
             "R4 的已结卡数不等于总卡数：git 结卡标记没被优先采用");

    const QVariantMap card = m_map->card(QStringLiteral("R4-01"));
    QCOMPARE(card.value(QStringLiteral("status")).toString(), QStringLiteral("done"));
    QCOMPARE(card.value(QStringLiteral("statusSource")).toString(), QStringLiteral("git"));
}

void TestProjectMap::ledgerBaselineCountsAsDone()
{
    // 反过来：#短号=done 是 R4 才立的规矩，R0/R3 这些历史卡只留在 baseline 里。
    // 只认 git 会让它们集体「未完成」，所以这里锁住 baseline=done 也算完成。
    const QVariantMap r0 = milestoneOf(m_map->milestones(), QStringLiteral("R0"));
    QVERIFY(!r0.isEmpty());
    QVERIFY(r0.value(QStringLiteral("total")).toInt() > 0);
    // R0 历史十卡（R0-01 ~ R0-10）全部只靠 baseline=done 计成完成；
    // 不等于总数 —— R0 后来还会进新卡（如 R0-11 首启性能），它们未交付时是正常的进行中/待办，
    // 断言写成 done == total 会把「R0 阶段永远封板」当成不变量，立一张新 R0 卡就红。
    QVERIFY2(r0.value(QStringLiteral("done")).toInt() >= 10,
             "R0 历史卡的 baseline=done 没被计入完成数");

    const QVariantMap card = m_map->card(QStringLiteral("R0-01"));
    QCOMPARE(card.value(QStringLiteral("status")).toString(), QStringLiteral("done"));
    QVERIFY(card.value(QStringLiteral("statusSource")).toString() != QStringLiteral("todo"));
}

QTEST_GUILESS_MAIN(TestProjectMap)

#include "tst_projectmap.moc"
