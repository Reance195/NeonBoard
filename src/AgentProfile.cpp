#include "AgentProfile.h"

#include "Database.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace {

const QString kProfilesKey = QStringLiteral("exec.agentProfiles");
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

QString encodeProfiles(const QVariantList &profiles)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonArray::fromVariantList(profiles)).toJson(QJsonDocument::Compact));
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

QVariantList AgentProfile::profiles() const
{
    if (!m_database)
        return builtinProfiles();

    const QString text = m_database->metaValue(kProfilesKey);
    if (text.isEmpty())
        return builtinProfiles();

    bool ok = false;
    const QJsonArray array = decodeArray(text, &ok);
    if (!ok)
        return builtinProfiles();

    QVariantList out;
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QVariantMap normalized = normalizeProfile(value.toObject().toVariantMap());
        if (!normalized.isEmpty())
            out.append(normalized);
    }
    return out;
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
    QVariantList list = profiles();
    bool replaced = false;
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
            list[i] = normalized;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        list.append(normalized);
    return writeProfiles(list);
}

bool AgentProfile::removeProfile(const QString &id)
{
    if (!m_database)
        return false;
    const QString wanted = id.trimmed();
    if (wanted.isEmpty())
        return false;

    QVariantList list = profiles();
    bool removed = false;
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).toMap().value(QStringLiteral("id")).toString() == wanted) {
            list.removeAt(i);
            removed = true;
        }
    }
    if (!removed)
        return false;
    return writeProfiles(list);
}

bool AgentProfile::resetProfiles()
{
    if (!m_database)
        return false;
    m_database->removeMetaValue(kProfilesKey);
    bump();
    return true;
}

bool AgentProfile::writeProfiles(const QVariantList &profiles)
{
    if (!m_database)
        return false;
    if (!m_database->setMetaValue(kProfilesKey, encodeProfiles(profiles)))
        return false;
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
