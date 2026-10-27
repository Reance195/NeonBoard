// M0：任务执行定义侧表（task_exec）的门面行为。
//
// 覆盖四条：
//   1. 无行时返回默认值（不崩、不返回空指针）
//   2. setExec / execOf 往返一致（含 JSON 数组字段）
//   3. 校验拒绝：未知 workflow、空列表项、超时/maxAttempts 越界、不存在的任务
//   4. 任务删除时侧表行同步清理（订阅 TaskModel::taskRemoved）

#include "Database.h"
#include "TaskExec.h"
#include "TaskModel.h"

#include <QTemporaryDir>
#include <QtTest>

class TestTaskExec : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void defaultsWhenNoRow();
    void roundtripFields();
    void rejectsInvalid();
    void clearResets();
    void removesRowWhenTaskDeleted();

private:
    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_model = nullptr;
    TaskExec *m_exec = nullptr;
};

void TestTaskExec::init()
{
    const QString path = m_dir.filePath(QStringLiteral("board.db"));
    m_db = new Database(this);
    QString error;
    QVERIFY2(m_db->open(&error, path), qPrintable(error));
    m_model = new TaskModel(m_db, this);
    m_model->load();
    m_exec = new TaskExec(m_db, m_model, this);
}

void TestTaskExec::cleanup()
{
    delete m_exec;
    delete m_model;
    delete m_db;
    m_exec = nullptr;
    m_model = nullptr;
    m_db = nullptr;
}

void TestTaskExec::defaultsWhenNoRow()
{
    const QString id = m_model->addTask(QStringLiteral("默认值"));
    QVERIFY(!id.isEmpty());

    const QVariantMap map = m_exec->execOf(id);
    QCOMPARE(map.value(QStringLiteral("goal")).toString(), QString());
    QVERIFY(map.value(QStringLiteral("acceptance")).toList().isEmpty());
    QVERIFY(map.value(QStringLiteral("scope")).toList().isEmpty());
    QCOMPARE(map.value(QStringLiteral("timeoutMin")).toInt(), 30);
    QCOMPARE(map.value(QStringLiteral("maxAttempts")).toInt(), 1);
}

void TestTaskExec::roundtripFields()
{
    const QString id = m_model->addTask(QStringLiteral("往返"));
    QVERIFY(!id.isEmpty());

    QVariantMap fields;
    fields.insert(QStringLiteral("goal"), QStringLiteral("删除后统计立即刷新"));
    fields.insert(QStringLiteral("acceptance"),
                  QVariantList{QStringLiteral("pwsh -File scripts/verify.ps1")});
    fields.insert(QStringLiteral("scope"),
                  QVariantList{QStringLiteral("src/TaskModel.cpp"), QStringLiteral("tests/**")});
    fields.insert(QStringLiteral("agent"), QStringLiteral("opencode"));
    fields.insert(QStringLiteral("cwd"), QStringLiteral("E:/projects/NeonBoard"));
    fields.insert(QStringLiteral("workflow"), QStringLiteral("fix"));
    fields.insert(QStringLiteral("timeoutMin"), 45);
    fields.insert(QStringLiteral("maxAttempts"), 2);
    QVERIFY(m_exec->setExec(id, fields));

    const QVariantMap map = m_exec->execOf(id);
    QCOMPARE(map.value(QStringLiteral("goal")).toString(), QStringLiteral("删除后统计立即刷新"));
    QCOMPARE(map.value(QStringLiteral("acceptance")).toList().size(), 1);
    QCOMPARE(map.value(QStringLiteral("scope")).toList().size(), 2);
    QCOMPARE(map.value(QStringLiteral("agent")).toString(), QStringLiteral("opencode"));
    QCOMPARE(map.value(QStringLiteral("cwd")).toString(), QStringLiteral("E:/projects/NeonBoard"));
    QCOMPARE(map.value(QStringLiteral("workflow")).toString(), QStringLiteral("fix"));
    QCOMPARE(map.value(QStringLiteral("timeoutMin")).toInt(), 45);
    QCOMPARE(map.value(QStringLiteral("maxAttempts")).toInt(), 2);

    // 局部更新：只改 agent，其他字段保持不变
    QVariantMap patch;
    patch.insert(QStringLiteral("agent"), QStringLiteral("claude"));
    QVERIFY(m_exec->setExec(id, patch));
    const QVariantMap after = m_exec->execOf(id);
    QCOMPARE(after.value(QStringLiteral("agent")).toString(), QStringLiteral("claude"));
    QCOMPARE(after.value(QStringLiteral("goal")).toString(), QStringLiteral("删除后统计立即刷新"));
}

void TestTaskExec::rejectsInvalid()
{
    const QString id = m_model->addTask(QStringLiteral("校验"));
    QVERIFY(!id.isEmpty());

    QVariantMap badWorkflow;
    badWorkflow.insert(QStringLiteral("workflow"), QStringLiteral("随便写的"));
    QVERIFY(!m_exec->setExec(id, badWorkflow));

    QVariantMap emptyAcceptance;
    emptyAcceptance.insert(QStringLiteral("acceptance"), QVariantList{QString()});
    QVERIFY(!m_exec->setExec(id, emptyAcceptance));

    QVariantMap badTimeout;
    badTimeout.insert(QStringLiteral("timeoutMin"), 0);
    QVERIFY(!m_exec->setExec(id, badTimeout));

    QVariantMap badAttempts;
    badAttempts.insert(QStringLiteral("maxAttempts"), 99);
    QVERIFY(!m_exec->setExec(id, badAttempts));

    QVariantMap goalOnly;
    goalOnly.insert(QStringLiteral("goal"), QStringLiteral("不存在的任务"));
    QVERIFY(!m_exec->setExec(QStringLiteral("no-such-task"), goalOnly));
}

void TestTaskExec::clearResets()
{
    const QString id = m_model->addTask(QStringLiteral("清空"));
    QVERIFY(!id.isEmpty());

    QVariantMap fields;
    fields.insert(QStringLiteral("goal"), QStringLiteral("临时目标"));
    QVERIFY(m_exec->setExec(id, fields));
    QVERIFY(!m_exec->execOf(id).value(QStringLiteral("goal")).toString().isEmpty());

    QVERIFY(m_exec->clearExec(id));
    QCOMPARE(m_exec->execOf(id).value(QStringLiteral("goal")).toString(), QString());
}

void TestTaskExec::removesRowWhenTaskDeleted()
{
    const QString id = m_model->addTask(QStringLiteral("删除同步"));
    QVERIFY(!id.isEmpty());

    QVariantMap fields;
    fields.insert(QStringLiteral("goal"), QStringLiteral("会被一起删掉"));
    QVERIFY(m_exec->setExec(id, fields));
    QCOMPARE(m_exec->execOf(id).value(QStringLiteral("goal")).toString(),
             QStringLiteral("会被一起删掉"));

    QVERIFY(m_model->deleteTask(id));
    QCOMPARE(m_exec->execOf(id).value(QStringLiteral("goal")).toString(), QString());
}

QTEST_GUILESS_MAIN(TestTaskExec)
#include "tst_taskexec.moc"
