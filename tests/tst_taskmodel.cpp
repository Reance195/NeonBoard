#include "TaskModel.h"

#include <QTest>

// R10-03：标签语义修正。
//
// tags 一个数组里混着三种语义（阶段 / 分类 / 角色），而 R10-03 之前只读 tags[0]。
// 这些用例把「顺序无关」钉住，避免以后再退回拿数组下标当模型：
// 一旦有人把 stageOf 改回 firstTag，stageFromAnyPosition 立刻红。
class TestTaskModel : public QObject
{
    Q_OBJECT

private slots:
    void stageFromAnyPosition();
    void stageIgnoresRoleTag();
    void stageFallsBackToOther();
    void stageHandlesBareString();
    void categorySkipsStageAndRole();
    void categoryEmptyWhenNoCategory();
    void roleTagIsCaseInsensitive();
    void firstTagKeepsLegacyMeaning();
};

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

QTEST_MAIN(TestTaskModel)
#include "tst_taskmodel.moc"
