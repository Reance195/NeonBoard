#include "AgentProfile.h"

#include "Database.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {

// R9-01 之前的用户层：整份快照。只在迁移时读一次，之后删除。
const QString kLegacyProfilesKey = QStringLiteral("exec.agentProfiles");
// 用户层现在的形态：id → profile，null 表示墓碑（隐藏下层同 id 的条目）。
const QString kOverridesKey = QStringLiteral("exec.agentProfileOverrides");
const QString kEnabledKey = QStringLiteral("exec.enabled");
const QString kDefaultAgentKey = QStringLiteral("exec.defaultAgent");
const QString kTrustedKey = QStringLiteral("exec.trustedCwds");
const QString kRoleAgentsKey = QStringLiteral("exec.roleAgents");

const QStringList kPromptModes = {
    QStringLiteral("arg"), QStringLiteral("stdin"),
};
const QStringList kOutputModes = {
    QStringLiteral("stream-json"), QStringLiteral("json"), QStringLiteral("text"),
};

QVariantMap makeBuiltin(const QString &id, const QString &displayName, const QString &exe,
                        const QStringList &args, const QStringList &readonlyArgs,
                        const QString &output)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("displayName"), displayName},
        {QStringLiteral("exe"), exe},
        {QStringLiteral("args"), args},
        {QStringLiteral("promptMode"), QStringLiteral("arg")},
        {QStringLiteral("readonlyArgs"), readonlyArgs},
        {QStringLiteral("output"), output},
        {QStringLiteral("env"), QVariantMap{{QStringLiteral("NO_COLOR"), QStringLiteral("1")}}},
        {QStringLiteral("defaultTimeoutMin"), 30},
    };
}

// args / readonlyArgs：必须是字符串数组，条目去空白后不得为空。
bool parseStringList(const QVariant &value, QStringList *out)
{
    if (!value.isValid() || value.isNull()) {
        out->clear();
        return true;
    }
    if (!value.canConvert<QVariantList>())
        return false;
    QStringList list;
    for (const QVariant &item : value.toList()) {
        const QString text = item.toString().trimmed();
        if (text.isEmpty())
            return false;
        list.append(text);
    }
    *out = list;
    return true;
}

// 校验并规范化一条 profile；非法返回空 map。
QVariantMap normalizeProfile(const QVariantMap &raw)
{
    static const QRegularExpression idPattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));

    const QString id = raw.value(QStringLiteral("id")).toString().trimmed();
    if (!idPattern.match(id).hasMatch())
        return {};

    const QString exe = raw.value(QStringLiteral("exe")).toString().trimmed();
    if (exe.isEmpty())
        return {};

    QStringList args;
    QStringList readonlyArgs;
    if (!parseStringList(raw.value(QStringLiteral("args")), &args))
        return {};
    if (!parseStringList(raw.value(QStringLiteral("readonlyArgs")), &readonlyArgs))
        return {};

    QString promptMode = raw.value(QStringLiteral("promptMode")).toString().trimmed().toLower();
    if (promptMode.isEmpty())
        promptMode = QStringLiteral("arg");
    if (!kPromptModes.contains(promptMode))
        return {};

    QString output = raw.value(QStringLiteral("output")).toString().trimmed().toLower();
    if (output.isEmpty())
        output = QStringLiteral("text");
    if (!kOutputModes.contains(output))
        return {};

    QVariantMap env;
    const QVariant rawEnv = raw.value(QStringLiteral("env"));
    if (rawEnv.isValid() && !rawEnv.isNull()) {
        if (!rawEnv.canConvert<QVariantMap>())
            return {};
        const QVariantMap map = rawEnv.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const QString key = it.key().trimmed();
            if (key.isEmpty())
                return {};
            env.insert(key, it.value().toString());
        }
    }

    int timeoutMin = 30;
    const QVariant rawTimeout = raw.value(QStringLiteral("defaultTimeoutMin"));
    if (rawTimeout.isValid() && !rawTimeout.isNull()) {
        timeoutMin = rawTimeout.toInt();
        if (timeoutMin < 1 || timeoutMin > 480)
            return {};
    }

    QString displayName = raw.value(QStringLiteral("displayName")).toString().trimmed();
    if (displayName.isEmpty())
        displayName = id;

    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("displayName"), displayName},
        {QStringLiteral("exe"), exe},
        {QStringLiteral("args"), args},
        {QStringLiteral("promptMode"), promptMode},
        {QStringLiteral("readonlyArgs"), readonlyArgs},
        {QStringLiteral("output"), output},
        {QStringLiteral("env"), env},
        {QStringLiteral("defaultTimeoutMin"), timeoutMin},
    };
}

// 规范化后的 profile 逐字段等价比较。走 JSON 而不是 QVariant::toString()：
// args / readonlyArgs 是字符串数组，toString() 一律给空串，会把不同参数判成相等。
QByteArray canonicalProfile(const QVariantMap &profile)
{
    return QJsonDocument(QJsonObject::fromVariantMap(profile)).toJson(QJsonDocument::Compact);
}

bool sameProfile(const QVariantMap &a, const QVariantMap &b)
{
    if (a.isEmpty() || b.isEmpty())
        return a.isEmpty() && b.isEmpty();
    return canonicalProfile(a) == canonicalProfile(b);
}

bool sameProfileList(const QVariantList &a, const QVariantList &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        if (!sameProfile(a.at(i).toMap(), b.at(i).toMap()))
            return false;
    }
    return true;
}

// 同 id 后者覆盖前者，其余保持前者的顺序；新 id 追加到末尾。
void upsertProfile(QVariantList *list, const QVariantMap &profile)
{
    const QString id = profile.value(QStringLiteral("id")).toString();
    for (int i = 0; i < list->size(); ++i) {
        if (list->at(i).toMap().value(QStringLiteral("id")).toString() == id) {
            (*list)[i] = profile;
            return;
        }
    }
    list->append(profile);
}

void removeProfileById(QVariantList *list, const QString &id)
{
    for (int i = list->size() - 1; i >= 0; --i) {
        if (list->at(i).toMap().value(QStringLiteral("id")).toString() == id)
            list->removeAt(i);
    }
}

// 只接受 JSON 数组；解析失败或不是数组时把 ok 置 false。
QJsonArray decodeArray(const QString &text, bool *ok)
{
    *ok = false;
    if (text.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isArray())
        return {};
    *ok = true;
    return doc.array();
}

bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

QString quoteArg(const QString &arg)
{
    const bool needsQuotes = arg.isEmpty() || arg.contains(QLatin1Char(' '))
        || arg.contains(QLatin1Char('\t')) || arg.contains(QLatin1Char('"'));
    if (!needsQuotes)
        return arg;
    QString escaped = arg;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

} // namespace

AgentProfile::AgentProfile(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
{
}

int AgentProfile::revision() const
{
    return m_revision;
}

void AgentProfile::bump()
{
    ++m_revision;
    emit changed();
}

QVariantList AgentProfile::builtinProfiles()
{
    return {
        makeBuiltin(QStringLiteral("opencode"), QStringLiteral("opencode"),
                    QStringLiteral("opencode"),
                    {QStringLiteral("run"), QStringLiteral("--format"), QStringLiteral("json")},
                    {QStringLiteral("--agent"), QStringLiteral("plan")},
                    QStringLiteral("json")),
        makeBuiltin(QStringLiteral("claude"), QStringLiteral("Claude Code"),
                    QStringLiteral("claude"),
                    {QStringLiteral("-p"), QStringLiteral("--output-format"),
                     QStringLiteral("stream-json")},
                    {QStringLiteral("--permission-mode"), QStringLiteral("plan")},
                    QStringLiteral("stream-json")),
        makeBuiltin(QStringLiteral("cursor-agent"), QStringLiteral("Cursor Agent"),
                    QStringLiteral("cursor-agent"),
                    {QStringLiteral("-p"), QStringLiteral("--output-format"),
                     QStringLiteral("stream-json")},
                    {QStringLiteral("--plan")},
                    QStringLiteral("stream-json")),
    };
}

QVariantList AgentProfile::baseProfiles() const
{
    QVariantList out = builtinProfiles();
    for (const QVariant &value : m_declared)
        upsertProfile(&out, value.toMap());
    return out;
}

QVariantMap AgentProfile::baseProfile(const QString &id) const
{
    for (const QVariant &value : baseProfiles()) {
        const QVariantMap profile = value.toMap();
        if (profile.value(QStringLiteral("id")).toString() == id)
            return profile;
    }
    return {};
}

QJsonObject AgentProfile::readOverrides() const
{
    if (!m_database)
        return {};
    const QString text = m_database->metaValue(kOverridesKey);
    if (text.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject();
}

bool AgentProfile::writeOverride(const QString &id, const QJsonValue &value)
{
    if (!m_database || id.isEmpty())
        return false;
    QJsonObject overrides = readOverrides();
    overrides.insert(id, value);
    if (!m_database->setMetaValue(
            kOverridesKey,
            QString::fromUtf8(QJsonDocument(overrides).toJson(QJsonDocument::Compact)))) {
        return false;
    }
    bump();
    return true;
}

bool AgentProfile::clearOverride(const QString &id)
{
    if (!m_database)
        return false;
    QJsonObject overrides = readOverrides();
    if (!overrides.contains(id))
        return true; // 本来就没有：幂等成功，不必惊动监听者
    overrides.remove(id);
    const bool ok =
        overrides.isEmpty()
            ? m_database->removeMetaValue(kOverridesKey)
            : m_database->setMetaValue(
                  kOverridesKey,
                  QString::fromUtf8(QJsonDocument(overrides).toJson(QJsonDocument::Compact)));
    if (!ok)
        return false;
    bump();
    return true;
}

// 老库的 exec.agentProfiles 是「用户改完后的整份快照」。整份快照与插件声明互斥：
// 快照一写，之后插件声明的 agent 就永远看不见了。所以首次读到时翻译成增量覆盖。
void AgentProfile::migrateLegacyProfiles() const
{
    if (m_legacyChecked || !m_database)
        return;
    m_legacyChecked = true;

    const QString legacy = m_database->metaValue(kLegacyProfilesKey);
    if (legacy.isEmpty())
        return;

    bool ok = false;
    const QJsonArray snapshot = decodeArray(legacy, &ok);
    if (!ok) {
        // 坏数据：删掉回落 L0 ∪ L1。留着只会每次解析失败，还挡住后续迁移。
        m_database->removeMetaValue(kLegacyProfilesKey);
        return;
    }

    QJsonObject overrides = readOverrides();
    QSet<QString> seen;
    for (const QJsonValue &value : snapshot) {
        if (!value.isObject())
            continue;
        const QVariantMap normalized = normalizeProfile(value.toObject().toVariantMap());
        if (normalized.isEmpty())
            continue;
        const QString id = normalized.value(QStringLiteral("id")).toString();
        seen.insert(id);
        // 与下层完全等价的条目不必留成覆盖（读的时候也会被忽略）。
        if (!sameProfile(baseProfile(id), normalized))
            overrides.insert(id, QJsonObject::fromVariantMap(normalized));
    }

    // 快照里没有、内置却有的 id：用户当年删掉了它，写墓碑保住这个决定。
    // 只对 L0 内置立墓碑 —— 插件声明的 agent 在快照成形时还不存在，
    // 拿它当「用户删过」就是凭空隐藏插件，正是 R9-01 要修的那个 bug 的镜像。
    for (const QVariant &value : builtinProfiles()) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString();
        if (id.isEmpty() || seen.contains(id) || overrides.contains(id))
            continue;
        overrides.insert(id, QJsonValue(QJsonValue::Null));
    }

    if (!overrides.isEmpty()) {
        m_database->setMetaValue(
            kOverridesKey,
            QString::fromUtf8(QJsonDocument(overrides).toJson(QJsonDocument::Compact)));
    }
    // 迁移在 const 读路径里发生，不 bump：调用方拿到的是迁移后的结果，
    // 从 getter 里发 changed() 只会让 QML 绑定当场重入。
    m_database->removeMetaValue(kLegacyProfilesKey);
}

QVariantList AgentProfile::profiles() const
{
    migrateLegacyProfiles();

    QVariantList out = baseProfiles();
    if (!m_database)
        return out;

    const QJsonObject overrides = readOverrides();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        const QString id = it.key();
        const QJsonValue value = it.value();
        if (value.isNull()) { // 墓碑
            removeProfileById(&out, id);
            continue;
        }
        if (!value.isObject())
            continue;
        const QVariantMap normalized = normalizeProfile(value.toObject().toVariantMap());
        // 键与 id 不一致 = 坏数据，单条忽略而不是整份丢弃。
        if (normalized.isEmpty() || normalized.value(QStringLiteral("id")).toString() != id)
            continue;
        // 与下层等价的覆盖没有信息量：迁移可能留下这种条目，读时忽略，
        // 插件后来改了声明也能自动跟上。
        if (sameProfile(baseProfile(id), normalized))
            continue;
        upsertProfile(&out, normalized);
    }
    return out;
}

void AgentProfile::setDeclaredProfiles(const QVariantList &profiles)
{
    QVariantList normalized;
    for (const QVariant &value : profiles) {
        if (!value.canConvert<QVariantMap>())
            continue;
        const QVariantMap profile = normalizeProfile(value.toMap());
        if (profile.isEmpty())
            continue; // 插件写错了不影响其它声明
        upsertProfile(&normalized, profile);
    }

    if (sameProfileList(normalized, m_declared))
        return;
    m_declared = normalized;
    bump();
}

bool AgentProfile::isAvailable(const QString &id) const
{
    const QVariantMap profile = profileOf(id);
    if (profile.isEmpty())
        return false;
    const QString exe = profile.value(QStringLiteral("exe")).toString().trimmed();
    if (exe.isEmpty())
        return false;
    const QFileInfo info(exe);
    if (info.isAbsolute())
        return info.isFile();
    return !QStandardPaths::findExecutable(exe).isEmpty();
}

QVariantMap AgentProfile::profileOf(const QString &id) const
{
    const QString wanted = id.trimmed();
    if (wanted.isEmpty())
        return {};
    for (const QVariant &value : profiles()) {
        const QVariantMap profile = value.toMap();
        if (profile.value(QStringLiteral("id")).toString() == wanted)
            return profile;
    }
    return {};
}

bool AgentProfile::saveProfile(const QVariantMap &profile)
{
    if (!m_database)
        return false;
    const QVariantMap normalized = normalizeProfile(profile);
    if (normalized.isEmpty())
        return false;

    const QString id = normalized.value(QStringLiteral("id")).toString();
    // 存的是「与下层的差异」而不是整份快照：与下层等价就删掉覆盖，等于恢复默认，
    // 插件后来升级参数时用户没改过的部分也能跟着走。
    if (sameProfile(baseProfile(id), normalized))
        return clearOverride(id);

    return writeOverride(id, QJsonObject::fromVariantMap(normalized));
}

bool AgentProfile::removeProfile(const QString &id)
{
    if (!m_database)
        return false;
    const QString wanted = id.trimmed();
    if (wanted.isEmpty())
        return false;

    const bool inBase = !baseProfile(wanted).isEmpty();
    const QJsonObject overrides = readOverrides();
    const bool hasOverride = overrides.contains(wanted);

    if (!inBase && !hasOverride)
        return false; // 本来就没有这个 id
    if (inBase && hasOverride && overrides.value(wanted).isNull())
        return false; // 已经隐藏过了

    // 只存在于用户层的直接删条目；下层有的必须写墓碑，否则它会原样复活。
    if (!inBase)
        return clearOverride(wanted);
    return writeOverride(wanted, QJsonValue(QJsonValue::Null));
}

bool AgentProfile::resetProfiles()
{
    if (!m_database)
        return false;
    m_database->removeMetaValue(kOverridesKey);
    m_database->removeMetaValue(kLegacyProfilesKey);
    m_legacyChecked = true;
    bump();
    return true;
}

QStringList AgentProfile::argv(const QString &id, bool readonly, const QString &prompt) const
{
    const QVariantMap profile = profileOf(id);
    if (profile.isEmpty())
        return {};

    QStringList out;
    out.append(profile.value(QStringLiteral("exe")).toString());
    out.append(profile.value(QStringLiteral("args")).toStringList());
    if (readonly)
        out.append(profile.value(QStringLiteral("readonlyArgs")).toStringList());
    if (profile.value(QStringLiteral("promptMode")).toString() != QStringLiteral("stdin")
        && !prompt.trimmed().isEmpty()) {
        out.append(prompt);
    }
    return out;
}

QString AgentProfile::commandLine(const QString &id, bool readonly, const QString &prompt) const
{
    QStringList parts;
    for (const QString &arg : argv(id, readonly, prompt))
        parts.append(quoteArg(arg));
    return parts.join(QLatin1Char(' '));
}

bool AgentProfile::executorEnabled() const
{
    if (!m_database)
        return false;
    return m_database->metaValue(kEnabledKey) == QStringLiteral("1");
}

bool AgentProfile::setExecutorEnabled(bool enabled)
{
    if (!m_database)
        return false;
    if (executorEnabled() == enabled)
        return true;
    const bool ok = enabled ? m_database->setMetaValue(kEnabledKey, QStringLiteral("1"))
                            : m_database->removeMetaValue(kEnabledKey);
    if (ok)
        bump();
    return ok;
}

QString AgentProfile::defaultAgent() const
{
    if (!m_database)
        return {};
    return m_database->metaValue(kDefaultAgentKey).trimmed();
}

bool AgentProfile::setDefaultAgent(const QString &id)
{
    if (!m_database)
        return false;
    const QString wanted = id.trimmed();
    if (wanted.isEmpty()) {
        if (defaultAgent().isEmpty())
            return true;
        const bool ok = m_database->removeMetaValue(kDefaultAgentKey);
        if (ok)
            bump();
        return ok;
    }
    if (profileOf(wanted).isEmpty())
        return false;
    if (defaultAgent() == wanted)
        return true;
    const bool ok = m_database->setMetaValue(kDefaultAgentKey, wanted);
    if (ok)
        bump();
    return ok;
}

// ---- 角色 → agent 映射（R7-04）----

QVariantMap AgentProfile::roleAgents() const
{
    if (!m_database)
        return {};
    // 存的是 JSON 对象（role → agentId），不是数组；单独解析，别复用 decodeArray
    const QString text = m_database->metaValue(kRoleAgentsKey);
    if (text.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject())
        return {};
    return doc.object().toVariantMap();
}

QString AgentProfile::agentForRole(const QString &role) const
{
    const QString wanted = role.trimmed().toLower();
    if (!wanted.isEmpty()) {
        const QString mapped = roleAgents().value(wanted).toString().trimmed();
        if (!mapped.isEmpty() && !profileOf(mapped).isEmpty())
            return mapped;
    }
    return defaultAgent();
}

bool AgentProfile::setRoleAgent(const QString &role, const QString &agentId)
{
    if (!m_database)
        return false;
    const QString wanted = role.trimmed().toLower();
    if (wanted.isEmpty())
        return false;

    QVariantMap map = roleAgents();
    const QString agent = agentId.trimmed();
    if (agent.isEmpty()) {
        if (!map.contains(wanted))
            return true;
        map.remove(wanted);
    } else {
        if (profileOf(agent).isEmpty())
            return false; // 不许把角色指到不存在的 profile
        map.insert(wanted, agent);
    }

    if (map.isEmpty())
        return clearRoleAgents();
    const QJsonDocument doc(QJsonObject::fromVariantMap(map));
    if (!m_database->setMetaValue(kRoleAgentsKey,
                                  QString::fromUtf8(doc.toJson(QJsonDocument::Compact))))
        return false;
    bump();
    return true;
}

bool AgentProfile::clearRoleAgents()
{
    if (!m_database)
        return false;
    m_database->removeMetaValue(kRoleAgentsKey);
    bump();
    return true;
}

QStringList AgentProfile::trustedCwds() const
{
    if (!m_database)
        return {};
    bool ok = false;
    const QJsonArray array = decodeArray(m_database->metaValue(kTrustedKey), &ok);
    if (!ok)
        return {};
    QStringList out;
    for (const QJsonValue &value : array) {
        const QString path = value.toString();
        if (!path.isEmpty() && !out.contains(path))
            out.append(path);
    }
    return out;
}

bool AgentProfile::isTrustedCwd(const QString &cwd) const
{
    const QString cleaned = QDir::cleanPath(cwd.trimmed());
    if (cleaned.isEmpty())
        return false;
    for (const QString &trusted : trustedCwds()) {
        if (samePath(trusted, cleaned))
            return true;
    }
    return false;
}

bool AgentProfile::trustCwd(const QString &cwd)
{
    if (!m_database)
        return false;
    const QString cleaned = QDir::cleanPath(cwd.trimmed());
    const QStringList parts = cleaned.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!QDir::isAbsolutePath(cleaned) || parts.contains(QStringLiteral(".."))
        || !QFileInfo(cleaned).isDir()) {
        return false;
    }
    if (isTrustedCwd(cleaned))
        return true;

    QStringList list = trustedCwds();
    list.append(cleaned);
    QJsonArray array;
    for (const QString &path : list)
        array.append(path);
    const QString text = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    if (!m_database->setMetaValue(kTrustedKey, text))
        return false;
    bump();
    return true;
}

bool AgentProfile::forgetCwd(const QString &cwd)
{
    if (!m_database)
        return false;
    const QString cleaned = QDir::cleanPath(cwd.trimmed());
    QStringList remaining;
    bool removed = false;
    for (const QString &trusted : trustedCwds()) {
        if (samePath(trusted, cleaned))
            removed = true;
        else
            remaining.append(trusted);
    }
    if (!removed)
        return false;

    QJsonArray array;
    for (const QString &path : remaining)
        array.append(path);
    const QString text = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    if (!m_database->setMetaValue(kTrustedKey, text))
        return false;
    bump();
    return true;
}
