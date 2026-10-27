// R6-03：Agent profile 的门面行为。
//
// 覆盖七条：
//   1. 内置 opencode / claude / cursor-agent 三条（codex 未实测，不内置）
//   2. argv 拼装：exe + args + 只读参数 + prompt；展示命令行带引号
//   3. promptMode=stdin 时 prompt 不进 argv
//   4. 未知 agent 返回空而不是崩溃
//   5. 保存校验（非法 id / exe / promptMode / output / 超时 / 空参数）与按 id 覆盖
//   6. 删除 / 恢复内置；总开关默认关、默认 agent、cwd 白名单
//   7. meta 读写跨实例可见（不升 user_version、不碰 task 表）

#include "AgentProfile.h"
#include "Database.h"

#include <QDir>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class TestAgentProfile : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void builtins();
    void argvAssembly();
    void stdinPromptNotInArgv();
    void unknownAgent();
    void saveValidation();
    void saveIsUpsert();
    void removeAndReset();
    void executorSwitch();
    void defaultAgentRules();
    void trustedCwdRules();
    void persistsAcrossInstances();

private:
    QVariantMap customProfile(const QString &id = QStringLiteral("custom-agent")) const;
    QTemporaryDir m_dir;
    int m_seq = 0;
    Database *m_db = nullptr;
    AgentProfile *m_profile = nullptr;
};

void TestAgentProfile::init()
{
    // 每个用例用独立库文件：QTemporaryDir 在整个测试类生命周期内复用，
    // 共用一个 board.db 会让上一个用例的 profile / 白名单渗进下一个断言。
    const QString path = m_dir.filePath(QStringLiteral("board-%1.db").arg(++m_seq));
    m_db = new Database(this);
    QString error;
    QVERIFY2(m_db->open(&error, path), qPrintable(error));
    m_profile = new AgentProfile(m_db, this);
}

void TestAgentProfile::cleanup()
{
    delete m_profile;
    delete m_db;
    m_profile = nullptr;
    m_db = nullptr;
}

QVariantMap TestAgentProfile::customProfile(const QString &id) const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("displayName"), QStringLiteral("测试 agent")},
        {QStringLiteral("exe"), QStringLiteral("my-agent")},
        {QStringLiteral("args"), QStringList{QStringLiteral("--json"), QStringLiteral("run")}},
        {QStringLiteral("promptMode"), QStringLiteral("arg")},
        {QStringLiteral("readonlyArgs"), QStringList{QStringLiteral("--readonly")}},
        {QStringLiteral("output"), QStringLiteral("json")},
        {QStringLiteral("env"), QVariantMap{{QStringLiteral("NO_COLOR"), QStringLiteral("1")}}},
        {QStringLiteral("defaultTimeoutMin"), 15},
    };
}

void TestAgentProfile::builtins()
{
    const QVariantList list = m_profile->profiles();
    QCOMPARE(list.size(), 3);

    QStringList ids;
    for (const QVariant &value : list)
        ids.append(value.toMap().value(QStringLiteral("id")).toString());
    QCOMPARE(ids, (QStringList{QStringLiteral("opencode"), QStringLiteral("claude"),
                               QStringLiteral("cursor-agent")}));
    QVERIFY(!ids.contains(QStringLiteral("codex")));

    const QVariantMap opencode = m_profile->profileOf(QStringLiteral("opencode"));
    QCOMPARE(opencode.value(QStringLiteral("exe")).toString(), QStringLiteral("opencode"));
    QCOMPARE(opencode.value(QStringLiteral("promptMode")).toString(), QStringLiteral("arg"));
    QCOMPARE(opencode.value(QStringLiteral("output")).toString(), QStringLiteral("json"));
    QCOMPARE(opencode.value(QStringLiteral("defaultTimeoutMin")).toInt(), 30);
    QCOMPARE(opencode.value(QStringLiteral("env")).toMap().value(QStringLiteral("NO_COLOR")).toString(),
             QStringLiteral("1"));
}

void TestAgentProfile::argvAssembly()
{
    const QString prompt = QStringLiteral("修复 cwd 校验");

    QCOMPARE(m_profile->argv(QStringLiteral("opencode"), false, prompt),
             (QStringList{QStringLiteral("opencode"), QStringLiteral("run"),
                          QStringLiteral("--format"), QStringLiteral("json"), prompt}));
    QCOMPARE(m_profile->argv(QStringLiteral("opencode"), true, prompt),
             (QStringList{QStringLiteral("opencode"), QStringLiteral("run"),
                          QStringLiteral("--format"), QStringLiteral("json"),
                          QStringLiteral("--agent"), QStringLiteral("plan"), prompt}));

    QCOMPARE(m_profile->argv(QStringLiteral("claude"), true, prompt),
             (QStringList{QStringLiteral("claude"), QStringLiteral("-p"),
                          QStringLiteral("--output-format"), QStringLiteral("stream-json"),
                          QStringLiteral("--permission-mode"), QStringLiteral("plan"), prompt}));
    QCOMPARE(m_profile->argv(QStringLiteral("cursor-agent"), true, prompt),
             (QStringList{QStringLiteral("cursor-agent"), QStringLiteral("-p"),
                          QStringLiteral("--output-format"), QStringLiteral("stream-json"),
                          QStringLiteral("--plan"), prompt}));

    // 展示用命令行只做引号处理，不代表执行路径
    QCOMPARE(m_profile->commandLine(QStringLiteral("opencode"), true, prompt),
             QStringLiteral("opencode run --format json --agent plan \"修复 cwd 校验\""));
    // 空 prompt 不下发（执行器可能改用 stdin）
    QCOMPARE(m_profile->argv(QStringLiteral("opencode"), false, QString()),
             (QStringList{QStringLiteral("opencode"), QStringLiteral("run"),
                          QStringLiteral("--format"), QStringLiteral("json")}));
}

void TestAgentProfile::stdinPromptNotInArgv()
{
    QVariantMap stdinProfile = customProfile(QStringLiteral("stdin-agent"));
    stdinProfile.insert(QStringLiteral("promptMode"), QStringLiteral("stdin"));
    QVERIFY(m_profile->saveProfile(stdinProfile));

    const QString prompt = QStringLiteral("这段不该出现在命令行");
    const QStringList args = m_profile->argv(QStringLiteral("stdin-agent"), false, prompt);
    QCOMPARE(args, (QStringList{QStringLiteral("my-agent"), QStringLiteral("--json"),
                                QStringLiteral("run")}));
    QVERIFY(!m_profile->commandLine(QStringLiteral("stdin-agent"), false, prompt).contains(prompt));
}

void TestAgentProfile::unknownAgent()
{
    QVERIFY(m_profile->profileOf(QStringLiteral("no-such-agent")).isEmpty());
    QVERIFY(m_profile->argv(QStringLiteral("no-such-agent"), false, QStringLiteral("x")).isEmpty());
    QVERIFY(m_profile->commandLine(QStringLiteral("no-such-agent")).isEmpty());
}

void TestAgentProfile::saveValidation()
{
    QVariantMap blankId = customProfile();
    blankId.insert(QStringLiteral("id"), QStringLiteral(""));
    QVERIFY(!m_profile->saveProfile(blankId));

    QVariantMap badId = customProfile();
    badId.insert(QStringLiteral("id"), QStringLiteral("bad id"));
    QVERIFY(!m_profile->saveProfile(badId));

    QVariantMap blankExe = customProfile();
    blankExe.insert(QStringLiteral("exe"), QStringLiteral("  "));
    QVERIFY(!m_profile->saveProfile(blankExe));

    QVariantMap badPrompt = customProfile();
    badPrompt.insert(QStringLiteral("promptMode"), QStringLiteral("pipe"));
    QVERIFY(!m_profile->saveProfile(badPrompt));

    QVariantMap badOutput = customProfile();
    badOutput.insert(QStringLiteral("output"), QStringLiteral("xml"));
    QVERIFY(!m_profile->saveProfile(badOutput));

    QVariantMap badTimeout = customProfile();
    badTimeout.insert(QStringLiteral("defaultTimeoutMin"), 0);
    QVERIFY(!m_profile->saveProfile(badTimeout));
    badTimeout.insert(QStringLiteral("defaultTimeoutMin"), 999);
    QVERIFY(!m_profile->saveProfile(badTimeout));

    QVariantMap emptyArg = customProfile();
    emptyArg.insert(QStringLiteral("args"), QStringList{QStringLiteral("--json"), QString()});
    QVERIFY(!m_profile->saveProfile(emptyArg));

    // 校验失败不得污染存储
    QCOMPARE(m_profile->profiles().size(), 3);
}

void TestAgentProfile::saveIsUpsert()
{
    QVERIFY(m_profile->saveProfile(customProfile()));
    QCOMPARE(m_profile->profiles().size(), 4);
    QCOMPARE(m_profile->profileOf(QStringLiteral("custom-agent"))
                 .value(QStringLiteral("defaultTimeoutMin")).toInt(), 15);

    QVariantMap edited = customProfile();
    edited.insert(QStringLiteral("args"), QStringList{QStringLiteral("--stream")});
    edited.insert(QStringLiteral("defaultTimeoutMin"), 45);
    QVERIFY(m_profile->saveProfile(edited));

    const QVariantList list = m_profile->profiles();
    QCOMPARE(list.size(), 4);
    int hits = 0;
    for (const QVariant &value : list) {
        if (value.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("custom-agent"))
            ++hits;
    }
    QCOMPARE(hits, 1);
    QCOMPARE(m_profile->profileOf(QStringLiteral("custom-agent"))
                 .value(QStringLiteral("defaultTimeoutMin")).toInt(), 45);
}

void TestAgentProfile::removeAndReset()
{
    QVERIFY(m_profile->removeProfile(QStringLiteral("opencode")));
    QCOMPARE(m_profile->profiles().size(), 2);
    QVERIFY(m_profile->profileOf(QStringLiteral("opencode")).isEmpty());
    QVERIFY(!m_profile->removeProfile(QStringLiteral("opencode")));

    QVERIFY(m_profile->resetProfiles());
    QCOMPARE(m_profile->profiles().size(), 3);
    QVERIFY(!m_profile->profileOf(QStringLiteral("opencode")).isEmpty());

    // 自定义 profile 也随「恢复内置」一起清掉
    QVERIFY(m_profile->saveProfile(customProfile()));
    QCOMPARE(m_profile->profiles().size(), 4);
    QVERIFY(m_profile->resetProfiles());
    QCOMPARE(m_profile->profiles().size(), 3);
}

void TestAgentProfile::executorSwitch()
{
    QVERIFY(!m_profile->executorEnabled());
    QVERIFY(m_profile->setExecutorEnabled(true));
    QVERIFY(m_profile->executorEnabled());

    AgentProfile second(m_db);
    QVERIFY(second.executorEnabled());

    QVERIFY(m_profile->setExecutorEnabled(false));
    QVERIFY(!m_profile->executorEnabled());
    QVERIFY(!second.executorEnabled());
}

void TestAgentProfile::defaultAgentRules()
{
    QCOMPARE(m_profile->defaultAgent(), QString());
    QVERIFY(m_profile->setDefaultAgent(QStringLiteral("claude")));
    QCOMPARE(m_profile->defaultAgent(), QStringLiteral("claude"));

    // 未知 agent 不得写进默认值
    QVERIFY(!m_profile->setDefaultAgent(QStringLiteral("no-such-agent")));
    QCOMPARE(m_profile->defaultAgent(), QStringLiteral("claude"));

    // 空串表示清空
    QVERIFY(m_profile->setDefaultAgent(QString()));
    QCOMPARE(m_profile->defaultAgent(), QString());
}

void TestAgentProfile::trustedCwdRules()
{
    QVERIFY(m_profile->trustedCwds().isEmpty());
    QVERIFY(!m_profile->isTrustedCwd(m_dir.path()));
    QVERIFY(!m_profile->isTrustedCwd(QString()));

    QVERIFY(m_profile->trustCwd(m_dir.path()));
    QVERIFY(m_profile->isTrustedCwd(m_dir.path()));
    QCOMPARE(m_profile->trustedCwds().size(), 1);

    // 重复信任幂等
    QVERIFY(m_profile->trustCwd(m_dir.path()));
    QCOMPARE(m_profile->trustedCwds().size(), 1);

    // 相对路径 / 不存在的目录不得进白名单
    QVERIFY(!m_profile->trustCwd(QStringLiteral("relative/dir")));
    QVERIFY(!m_profile->trustCwd(m_dir.filePath(QStringLiteral("no-such-dir"))));
    QCOMPARE(m_profile->trustedCwds().size(), 1);

#ifdef Q_OS_WIN
    QVERIFY(m_profile->isTrustedCwd(m_dir.path().toUpper()));
#endif

    QVERIFY(m_profile->forgetCwd(m_dir.path()));
    QVERIFY(!m_profile->isTrustedCwd(m_dir.path()));
    QVERIFY(!m_profile->forgetCwd(m_dir.path()));
}

void TestAgentProfile::persistsAcrossInstances()
{
    QVERIFY(m_profile->saveProfile(customProfile(QStringLiteral("persisted"))));

    AgentProfile second(m_db);
    const QVariantMap loaded = second.profileOf(QStringLiteral("persisted"));
    QCOMPARE(loaded.value(QStringLiteral("exe")).toString(), QStringLiteral("my-agent"));
    QCOMPARE(loaded.value(QStringLiteral("promptMode")).toString(), QStringLiteral("arg"));
    QCOMPARE(second.profiles().size(), 4);

    // 侧表实现，schema 版本不变
    QSqlQuery query(QSqlDatabase::database(m_db->connectionName()));
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), Database::kSchemaVersion);
}

QTEST_GUILESS_MAIN(TestAgentProfile)
#include "tst_agentprofile.moc"
