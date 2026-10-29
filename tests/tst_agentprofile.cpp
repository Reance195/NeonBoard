// R6-03：Agent profile 的门面行为；R9-01 增补三层合并 / 墓碑 / 迁移。
//
// 覆盖七条：
//   1. 内置 opencode / claude / cursor-agent 三条（codex 未实测，不内置）
//   2. argv 拼装：exe + args + 只读参数 + prompt；展示命令行带引号
//   3. promptMode=stdin 时 prompt 不进 argv
//   4. 未知 agent 返回空而不是崩溃
//   5. 保存校验（非法 id / exe / promptMode / output / 超时 / 空参数）与按 id 覆盖
//   6. 删除 / 恢复内置；总开关默认关、默认 agent、cwd 白名单
//   7. meta 读写跨实例可见（不升 user_version、不碰 task 表）
//
// R9-01 增补四条：
//   8. 三层合并优先级：用户覆盖 > 插件声明 > 内置，同 id 就地覆盖、新 id 追加
//   9. 墓碑：删掉下层条目后插件重扫不会让它复活
//  10. 老库整份快照迁移成增量覆盖，且迁移后插件声明的 agent 不被旧快照淹没
//  11. isAvailable：绝对路径查文件、裸命令查 PATH

#include "AgentProfile.h"
#include "Database.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QStringList idsOf(const QVariantList &list)
{
    QStringList ids;
    for (const QVariant &value : list)
        ids.append(value.toMap().value(QStringLiteral("id")).toString());
    return ids;
}

// 落盘契约：这两个键名是跨版本兼容面，测试里硬编码，改名必须改这里。
const QString kLegacyKey = QStringLiteral("exec.agentProfiles");
const QString kOverridesKey = QStringLiteral("exec.agentProfileOverrides");

// 从测试二进制所在目录向上找仓库根：Ninja 单配置是 build/，VS 多配置是 build/Release/，
// 靠 plugins/agent-claude/plugin.json 存不存在判定，不写死层数。
QString sourceRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 4; ++depth) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("plugins/agent-claude/plugin.json"))))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return QString();
}

QStringList jsonToStringList(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &item : value.toArray())
        out.append(item.toString());
    return out;
}

} // namespace

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

    void declaredLayerMerges();
    void tombstoneHidesDeclared();
    void legacySnapshotMigrates();
    void availabilityProbe();
    void availabilityInModel();
    void officialPluginManifests();

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

void TestAgentProfile::declaredLayerMerges()
{
    // 插件声明：一条全新 agent，一条覆盖内置 claude
    QVariantMap declaredClaude = customProfile(QStringLiteral("claude"));
    declaredClaude.insert(QStringLiteral("exe"), QStringLiteral("claude-dev"));

    const QVariantList declared{customProfile(QStringLiteral("plugin-agent")), declaredClaude};
    m_profile->setDeclaredProfiles(declared);

    QCOMPARE(m_profile->profiles().size(), 4); // 3 内置 + 1 新增
    QCOMPARE(m_profile->profileOf(QStringLiteral("plugin-agent"))
                 .value(QStringLiteral("exe"))
                 .toString(),
             QStringLiteral("my-agent"));
    // 同 id 就地覆盖，位置不变；新 id 追加到末尾
    QCOMPARE(idsOf(m_profile->profiles()),
             (QStringList{QStringLiteral("opencode"), QStringLiteral("claude"),
                          QStringLiteral("cursor-agent"), QStringLiteral("plugin-agent")}));
    QCOMPARE(m_profile->profileOf(QStringLiteral("claude"))
                 .value(QStringLiteral("exe"))
                 .toString(),
             QStringLiteral("claude-dev"));

    // 用户层压过插件层
    QVariantMap userEdit = customProfile(QStringLiteral("claude"));
    userEdit.insert(QStringLiteral("exe"), QStringLiteral("claude-user"));
    QVERIFY(m_profile->saveProfile(userEdit));
    QCOMPARE(m_profile->profileOf(QStringLiteral("claude"))
                 .value(QStringLiteral("exe"))
                 .toString(),
             QStringLiteral("claude-user"));

    // 改回与下层一致 = 撤销覆盖，跟随插件声明
    QVERIFY(m_profile->saveProfile(declaredClaude));
    QCOMPARE(m_profile->profileOf(QStringLiteral("claude"))
                 .value(QStringLiteral("exe"))
                 .toString(),
             QStringLiteral("claude-dev"));
    QVERIFY(!m_db->metaValue(kOverridesKey).contains(QStringLiteral("claude")));

    // 引用未知 id 的坏声明不得污染列表
    m_profile->setDeclaredProfiles(
        QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("x")}}});
    QCOMPARE(m_profile->profiles().size(), 3);
}

void TestAgentProfile::tombstoneHidesDeclared()
{
    m_profile->setDeclaredProfiles(QVariantList{customProfile(QStringLiteral("plugin-agent"))});
    QCOMPARE(m_profile->profiles().size(), 4);

    QVERIFY(m_profile->removeProfile(QStringLiteral("plugin-agent")));
    QVERIFY(m_profile->profileOf(QStringLiteral("plugin-agent")).isEmpty());
    QCOMPARE(m_profile->profiles().size(), 3);
    QVERIFY(!m_profile->removeProfile(QStringLiteral("plugin-agent"))); // 幂等

    // 插件重扫后同一 id 仍被墓碑挡住 —— 声明不是用户改动的对手
    m_profile->setDeclaredProfiles(
        QVariantList{customProfile(QStringLiteral("plugin-agent")),
                     customProfile(QStringLiteral("other-agent"))});
    QVERIFY(m_profile->profileOf(QStringLiteral("plugin-agent")).isEmpty());
    QVERIFY(!m_profile->profileOf(QStringLiteral("other-agent")).isEmpty());

    // 恢复默认后墓碑一起清掉
    QVERIFY(m_profile->resetProfiles());
    QVERIFY(!m_profile->profileOf(QStringLiteral("plugin-agent")).isEmpty());
}

void TestAgentProfile::legacySnapshotMigrates()
{
    // 老库形态：整份快照 —— 用户改过 claude、删掉 opencode、加了 custom-agent，
    // cursor-agent 保持内置原样（取真内置条目，别用 customProfile 伪造，
    // 否则它本来就与下层不等价，断言「不留覆盖」会假失败）。
    QVariantMap legacyClaude = customProfile(QStringLiteral("claude"));
    legacyClaude.insert(QStringLiteral("exe"), QStringLiteral("claude-legacy"));

    QVariantMap builtinCursor;
    for (const QVariant &value : AgentProfile::builtinProfiles()) {
        if (value.toMap().value(QStringLiteral("id")).toString()
            == QStringLiteral("cursor-agent")) {
            builtinCursor = value.toMap();
        }
    }
    QVERIFY(!builtinCursor.isEmpty());

    const QVariantList snapshot{legacyClaude, builtinCursor,
                                customProfile(QStringLiteral("custom-agent"))};
    QVERIFY(m_db->setMetaValue(
        kLegacyKey,
        QString::fromUtf8(
            QJsonDocument(QJsonArray::fromVariantList(snapshot)).toJson(QJsonDocument::Compact))));

    AgentProfile legacy(m_db);
    QCOMPARE(idsOf(legacy.profiles()),
             (QStringList{QStringLiteral("claude"), QStringLiteral("cursor-agent"),
                          QStringLiteral("custom-agent")}));
    QCOMPARE(legacy.profileOf(QStringLiteral("claude"))
                 .value(QStringLiteral("exe"))
                 .toString(),
             QStringLiteral("claude-legacy"));

    // 迁移后老键消失，只剩增量覆盖；与内置等价的 cursor-agent 不留覆盖
    QVERIFY(m_db->metaValue(kLegacyKey).isEmpty());
    const QString overrides = m_db->metaValue(kOverridesKey);
    QVERIFY(overrides.contains(QStringLiteral("claude")));
    QVERIFY(overrides.contains(QStringLiteral("custom-agent")));
    QVERIFY(overrides.contains(QStringLiteral("opencode"))); // 墓碑
    QVERIFY(!overrides.contains(QStringLiteral("cursor-agent")));

    // 本卡的要害：迁移之后插件声明的 agent 不能被旧快照淹没
    AgentProfile withPlugins(m_db);
    withPlugins.setDeclaredProfiles(
        QVariantList{customProfile(QStringLiteral("plugin-agent"))});
    QVERIFY(!withPlugins.profileOf(QStringLiteral("plugin-agent")).isEmpty());
    QCOMPARE(idsOf(withPlugins.profiles()).size(), 4); // claude/cursor/custom/plugin
}

void TestAgentProfile::availabilityProbe()
{
    QVERIFY(!m_profile->isAvailable(QStringLiteral("no-such-agent")));

    QVariantMap abs = customProfile(QStringLiteral("abs-agent"));
    abs.insert(QStringLiteral("exe"), QCoreApplication::applicationFilePath());
    QVERIFY(m_profile->saveProfile(abs));
    QVERIFY(m_profile->isAvailable(QStringLiteral("abs-agent")));

    // 绝对路径但文件不存在 —— cursor-agent 那种「声明了却调不动」的形态
    QVariantMap missing = customProfile(QStringLiteral("missing-agent"));
    missing.insert(QStringLiteral("exe"), m_dir.filePath(QStringLiteral("no-such-cli.exe")));
    QVERIFY(m_profile->saveProfile(missing));
    QVERIFY(!m_profile->isAvailable(QStringLiteral("missing-agent")));
}

void TestAgentProfile::availabilityInModel()
{
    // R9-04：profiles() 的每一行都要带 available，且与 isAvailable(id) 同源。
    // 设置页靠这一列把「已声明但不可用」标出来 —— 缺了它，不可用的 agent 与
    // 可用的在列表里长得一模一样，要等到派活失败才发现。
    for (const QVariant &value : m_profile->profiles()) {
        const QVariantMap row = value.toMap();
        const QString id = row.value(QStringLiteral("id")).toString();
        QVERIFY2(row.contains(QStringLiteral("available")), qPrintable(id));
        QCOMPARE(row.value(QStringLiteral("available")).toBool(), m_profile->isAvailable(id));
    }

    // 绝对路径：存在即 true，不存在即 false（cursor-agent 那种「声明了却调不动」的形态）
    QVariantMap real = customProfile(QStringLiteral("real-agent"));
    real.insert(QStringLiteral("exe"), QCoreApplication::applicationFilePath());
    QVERIFY(m_profile->saveProfile(real));

    QVariantMap ghost = customProfile(QStringLiteral("ghost-agent"));
    ghost.insert(QStringLiteral("exe"), m_dir.filePath(QStringLiteral("no-such-cli.exe")));
    QVERIFY(m_profile->saveProfile(ghost));

    QVariantMap byId;
    for (const QVariant &value : m_profile->profiles())
        byId.insert(value.toMap().value(QStringLiteral("id")).toString(), value.toMap());

    const QVariantMap realRow = byId.value(QStringLiteral("real-agent")).toMap();
    const QVariantMap ghostRow = byId.value(QStringLiteral("ghost-agent")).toMap();
    QVERIFY(realRow.value(QStringLiteral("available")).toBool());
    QVERIFY(!ghostRow.value(QStringLiteral("available")).toBool());

    // available 是本机瞬时事实，不许落盘：PATH 会变、盘会拔，存进 meta 就变成谎话。
    QVERIFY(!m_db->metaValue(kOverridesKey).contains(QStringLiteral("available")));
}

void TestAgentProfile::officialPluginManifests()
{
    // 官方 agent 插件声明的 profile 会 upsert 覆盖同 id 的内置条目（L1 > L0）。
    // 于是「插件参数写错」不会报错，只会让派活拉起的命令与内置表 / 文档悄悄分叉 ——
    // 界面照样显示 claude，跑起来却是另一组参数。这里逐字段钉死。
    const QString root = sourceRoot();
    QVERIFY2(!root.isEmpty(),
             "定位不到仓库根（plugins/agent-claude/plugin.json）：测试二进制应在 build/ 下");

    // 每加一个官方 agent 插件就追加一项；漏加等于该插件没有守护。
    const QStringList manifests{
        QStringLiteral("agent-claude"),
        QStringLiteral("agent-opencode"),
        QStringLiteral("agent-cursor"),
    };

    for (const QString &dirName : manifests) {
        const QString path = root + QStringLiteral("/plugins/") + dirName
                             + QStringLiteral("/plugin.json");
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));

        QJsonParseError parseError{};
        const QJsonObject manifest = QJsonDocument::fromJson(file.readAll(), &parseError).object();
        QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));

        // 没有 exec.agentProfiles 权限的声明会被静默忽略：插件看起来装上了，却毫无作用
        const QStringList permissions =
            jsonToStringList(manifest.value(QStringLiteral("permissions")));
        QVERIFY2(permissions.contains(QStringLiteral("exec.agentProfiles")),
                 qPrintable(dirName + QStringLiteral(" 缺 exec.agentProfiles 权限声明")));

        const QJsonObject contributes = manifest.value(QStringLiteral("contributes")).toObject();
        const QJsonArray declaredList = contributes.value(QStringLiteral("agentProfiles")).toArray();
        QCOMPARE(int(declaredList.size()), 1);

        const QJsonObject entry = declaredList.at(0).toObject();
        const QVariantMap declared = entry.toVariantMap();
        const QString id = declared.value(QStringLiteral("id")).toString();

        QVariantMap builtin;
        for (const QVariant &value : AgentProfile::builtinProfiles()) {
            if (value.toMap().value(QStringLiteral("id")).toString() == id)
                builtin = value.toMap();
        }
        QVERIFY2(!builtin.isEmpty(), qPrintable(QStringLiteral("内置表里没有 %1").arg(id)));

        for (const QString &key : {QStringLiteral("displayName"), QStringLiteral("exe"),
                                   QStringLiteral("promptMode"), QStringLiteral("output")}) {
            QCOMPARE(declared.value(key).toString(), builtin.value(key).toString());
        }
        for (const QString &key : {QStringLiteral("args"), QStringLiteral("readonlyArgs")}) {
            QCOMPARE(jsonToStringList(entry.value(key)), builtin.value(key).toStringList());
        }
        QCOMPARE(declared.value(QStringLiteral("defaultTimeoutMin")).toInt(),
                 builtin.value(QStringLiteral("defaultTimeoutMin")).toInt());
        QVERIFY(declared.value(QStringLiteral("env")).toMap()
                == builtin.value(QStringLiteral("env")).toMap());

        // 走一遍真实合并路径：声明覆盖内置之后，argv 必须与覆盖前逐字一致
        AgentProfile merged(m_db);
        merged.setDeclaredProfiles(QVariantList{declared});
        const QString prompt = QStringLiteral("派活验证");
        QCOMPARE(merged.argv(id, true, prompt), m_profile->argv(id, true, prompt));
        QCOMPARE(merged.argv(id, false, prompt), m_profile->argv(id, false, prompt));
    }
}

QTEST_GUILESS_MAIN(TestAgentProfile)
#include "tst_agentprofile.moc"
