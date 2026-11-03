#include "WorkflowPresets.h"

#include "Database.h"
#include "TaskExec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QVariant>

namespace {

const QString kPresetsKey = QStringLiteral("exec.presets");
const QString kRoutingKey = QStringLiteral("exec.presets.routing");

// 六条内置预设的 key（顺序即面板顺序）。判定「这条是不是内置」也用它。
QStringList builtinKeys()
{
    return {QStringLiteral("fix"),      QStringLiteral("feature"), QStringLiteral("hotfix"),
            QStringLiteral("docs"),     QStringLiteral("review"),  QStringLiteral("delivery")};
}

QVariantMap makePreset(const QString &key, const QString &label, const QString &desc,
                       const QStringList &steps, const QStringList &skippable)
{
    QVariantList stepList;
    for (const QString &role : steps)
        stepList.append(role);
    QVariantList skipList;
    for (const QString &role : skippable)
        skipList.append(role);
    return {
        {QStringLiteral("key"), key},
        {QStringLiteral("label"), label},
        {QStringLiteral("desc"), desc},
        {QStringLiteral("steps"), stepList},
        {QStringLiteral("skippable"), skipList},
        {QStringLiteral("builtin"), true},
    };
}

QStringList toStringList(const QVariant &value, bool *ok)
{
    QStringList out;
    *ok = true;
    const QVariantList list = value.toList();
    for (const QVariant &item : list) {
        const QString text = item.toString().trimmed().toLower();
        if (text.isEmpty()) {
            *ok = false;
            return {};
        }
        out.append(text);
    }
    return out;
}

QString encodePresets(const QVariantList &presets)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonArray::fromVariantList(presets)).toJson(QJsonDocument::Compact));
}

} // namespace

WorkflowPresets::WorkflowPresets(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
{
}

int WorkflowPresets::revision() const
{
    return m_revision;
}

void WorkflowPresets::bump()
{
    ++m_revision;
    emit changed();
}

bool WorkflowPresets::isProtectedRole(const QString &role)
{
    // 受保护 = 不能出现在任意预设的 skippable 里。
    //   qa        —— 验收是最后防线，谁都不许跳（product-design §11 开放问题 #8）；
    //   engineer  —— 唯一的代码产出方，跳了就没东西可验；
    //   writer    —— docs 流程的产出方；
    //   orchestrator —— 收口角色，跳过等于没结论。
    static const QStringList protectedRoles = {
        QStringLiteral("qa"),  QStringLiteral("engineer"),
        QStringLiteral("writer"), QStringLiteral("orchestrator"),
    };
    return protectedRoles.contains(role.trimmed().toLower());
}

QString WorkflowPresets::normalizeSize(const QString &size)
{
    const QString s = size.trimmed().toUpper();
    if (s == QLatin1String("S") || s == QLatin1String("1"))
        return QStringLiteral("S");
    if (s == QLatin1String("M") || s == QLatin1String("2"))
        return QStringLiteral("M");
    if (s == QLatin1String("L") || s == QLatin1String("3"))
        return QStringLiteral("L");
    if (s == QLatin1String("XL") || s == QLatin1String("4"))
        return QStringLiteral("XL");
    return {};
}

bool WorkflowPresets::isSmallSize(const QString &size)
{
    return normalizeSize(size) == QLatin1String("S");
}

QVariantList WorkflowPresets::builtinPresets()
{
    // 与 TaskExec::presetSteps 的角色序严格一致（两处表达同一份事实，
    // 集成测试会断言 presetSteps(key) 与 presetOf(key).steps 的 role 序列相等）。
    // desc 取自 product-design §7 的「适用」列。
    return {
        makePreset(QStringLiteral("fix"), QStringLiteral("快速修复"),
                   QStringLiteral("一行修复、小 bug"), {QStringLiteral("engineer"),
                                                        QStringLiteral("qa")},
                   {}),
        makePreset(QStringLiteral("feature"), QStringLiteral("新功能"),
                   QStringLiteral("需要拆解与设计的新功能"),
                   {QStringLiteral("pm"), QStringLiteral("architect"),
                    QStringLiteral("engineer"), QStringLiteral("qa")},
                   {QStringLiteral("pm"), QStringLiteral("architect")}),
        makePreset(QStringLiteral("hotfix"), QStringLiteral("紧急修复"),
                   QStringLiteral("紧急，跳过评审（qa 仍保留）"),
                   {QStringLiteral("engineer"), QStringLiteral("qa")}, {}),
        makePreset(QStringLiteral("docs"), QStringLiteral("纯文档"),
                   QStringLiteral("只改文档"), {QStringLiteral("writer")}, {}),
        makePreset(QStringLiteral("review"), QStringLiteral("只读审查"),
                   QStringLiteral("只看不改"), {QStringLiteral("orchestrator")}, {}),
        makePreset(QStringLiteral("delivery"), QStringLiteral("0→1 交付"),
                   QStringLiteral("从需求到收口的大活"),
                   {QStringLiteral("pm"), QStringLiteral("architect"), QStringLiteral("engineer"),
                    QStringLiteral("qa"), QStringLiteral("orchestrator")},
                   {QStringLiteral("pm"), QStringLiteral("architect")}),
    };
}

QVariantMap WorkflowPresets::builtinRouting()
{
    return {
        {QStringLiteral("S"), QStringLiteral("fix")},
        {QStringLiteral("M"), QStringLiteral("feature")},
        {QStringLiteral("L"), QStringLiteral("delivery")},
        {QStringLiteral("XL"), QStringLiteral("delivery")},
    };
}

QVariantMap WorkflowPresets::normalizePreset(const QVariantMap &raw)
{
    static const QRegularExpression keyPattern(
        QStringLiteral("^[a-z0-9][a-z0-9._-]{0,31}$"));

    const QString key = raw.value(QStringLiteral("key")).toString().trimmed().toLower();
    if (!keyPattern.match(key).hasMatch())
        return {};

    QString label = raw.value(QStringLiteral("label")).toString().trimmed();
    if (label.isEmpty())
        label = key;
    if (label.size() > 40)
        return {};

    const QString desc = raw.value(QStringLiteral("desc")).toString().trimmed();
    if (desc.size() > 200)
        return {};

    bool ok = false;
    const QStringList steps = toStringList(raw.value(QStringLiteral("steps")), &ok);
    if (!ok || steps.isEmpty())
        return {};
    for (const QString &role : steps) {
        if (!TaskExec::isKnownRole(role))
            return {};
    }
    // 必须有产出/收口方：engineer（代码）/ writer（文档）/ orchestrator（收口 summary）。
    // 只由 pm / architect / qa 组成的预设没有交付物，无意义（review 是合法的单 orchestrator）。
    if (!steps.contains(QStringLiteral("engineer")) && !steps.contains(QStringLiteral("writer"))
        && !steps.contains(QStringLiteral("orchestrator")))
        return {};

    const QStringList skippable = toStringList(raw.value(QStringLiteral("skippable")), &ok);
    if (!ok)
        return {};
    for (const QString &role : skippable) {
        if (!TaskExec::isKnownRole(role))
            return {};
        // 硬规则：受保护角色永不入列；允许跳的只能是 pm / architect。
        if (isProtectedRole(role))
            return {};
        if (!steps.contains(role))
            return {};
    }

    QVariantList stepList;
    for (const QString &role : steps)
        stepList.append(role);
    QVariantList skipList;
    for (const QString &role : skippable)
        skipList.append(role);

    return {
        {QStringLiteral("key"), key},
        {QStringLiteral("label"), label},
        {QStringLiteral("desc"), desc},
        {QStringLiteral("steps"), stepList},
        {QStringLiteral("skippable"), skipList},
    };
}

QVariantList WorkflowPresets::presets() const
{
    if (!m_database)
        return builtinPresets();

    const QString text = m_database->metaValue(kPresetsKey);
    if (text.isEmpty())
        return builtinPresets();

    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isArray())
        return builtinPresets();

    const QStringList builtins = builtinKeys();
    QVariantList out;
    for (const QJsonValue &value : doc.array()) {
        if (!value.isObject())
            continue;
        QVariantMap normalized = normalizePreset(value.toObject().toVariantMap());
        if (normalized.isEmpty())
            continue;
        normalized.insert(QStringLiteral("builtin"),
                          builtins.contains(normalized.value(QStringLiteral("key")).toString()));
        out.append(normalized);
    }
    // meta 存在但全是非法项：宁可回退内置，也不要给出一张空预设表
    return out.isEmpty() ? builtinPresets() : out;
}

QVariantMap WorkflowPresets::presetOf(const QString &key) const
{
    const QString wanted = key.trimmed().toLower();
    if (wanted.isEmpty())
        return {};
    for (const QVariant &value : presets()) {
        const QVariantMap preset = value.toMap();
        if (preset.value(QStringLiteral("key")).toString() == wanted)
            return preset;
    }
    return {};
}

bool WorkflowPresets::writePresets(const QVariantList &presets)
{
    if (!m_database)
        return false;
    if (!m_database->setMetaValue(kPresetsKey, encodePresets(presets)))
        return false;
    bump();
    return true;
}

bool WorkflowPresets::savePreset(const QVariantMap &preset)
{
    if (!m_database)
        return false;
    const QVariantMap normalized = normalizePreset(preset);
    if (normalized.isEmpty())
        return false;

    const QString key = normalized.value(QStringLiteral("key")).toString();
    QVariantList list = presets();
    bool replaced = false;
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).toMap().value(QStringLiteral("key")).toString() == key) {
            list[i] = normalized;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        list.append(normalized);
    return writePresets(list);
}

bool WorkflowPresets::removePreset(const QString &key)
{
    if (!m_database)
        return false;
    const QString wanted = key.trimmed().toLower();
    if (wanted.isEmpty())
        return false;

    QVariantList list = presets();
    bool removed = false;
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).toMap().value(QStringLiteral("key")).toString() == wanted) {
            list.removeAt(i);
            removed = true;
        }
    }
    if (!removed)
        return false;
    // 删掉最后一条会让预设表为空 —— 空表没有意义，退回内置更安全
    if (list.isEmpty())
        return resetPresets();
    return writePresets(list);
}

bool WorkflowPresets::resetPresets()
{
    if (!m_database)
        return false;
    m_database->removeMetaValue(kPresetsKey);
    bump();
    return true;
}

QVariantMap WorkflowPresets::routing() const
{
    if (!m_database)
        return builtinRouting();
    const QString text = m_database->metaValue(kRoutingKey);
    if (text.isEmpty())
        return builtinRouting();
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject())
        return builtinRouting();

    const QVariantMap raw = doc.object().toVariantMap();
    QVariantMap out;
    const QStringList sizes = {QStringLiteral("S"), QStringLiteral("M"), QStringLiteral("L"),
                               QStringLiteral("XL")};
    for (const QString &size : sizes) {
        const QString mapped = raw.value(size).toString().trimmed().toLower();
        if (mapped.isEmpty())
            continue;
        // 只接受已登记的预设 key；指向未知 key 的项丢弃（不猜）
        if (!presetOf(mapped).isEmpty())
            out.insert(size, mapped);
    }
    // 缺项用内置补齐，保证四条齐全
    const QVariantMap fallback = builtinRouting();
    for (const QString &size : sizes) {
        if (!out.contains(size))
            out.insert(size, fallback.value(size));
    }
    return out;
}

bool WorkflowPresets::saveRouting(const QVariantMap &map)
{
    if (!m_database)
        return false;
    const QStringList sizes = {QStringLiteral("S"), QStringLiteral("M"), QStringLiteral("L"),
                               QStringLiteral("XL")};
    QVariantMap normalized;
    for (const QString &size : sizes) {
        if (!map.contains(size))
            continue;
        const QString mapped = map.value(size).toString().trimmed().toLower();
        if (mapped.isEmpty())
            return false;
        if (presetOf(mapped).isEmpty())
            return false; // 指向未知预设：拒绝，而不是静默丢弃
        normalized.insert(size, mapped);
    }
    if (normalized.isEmpty())
        return false;

    QJsonObject obj;
    for (auto it = normalized.constBegin(); it != normalized.constEnd(); ++it)
        obj.insert(it.key(), it.value().toString());
    if (!m_database->setMetaValue(
            kRoutingKey, QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))))
        return false;
    bump();
    return true;
}

QString WorkflowPresets::routeFor(const QString &size, const QVariantMap &card) const
{
    const QString normalized = normalizeSize(size);
    if (!normalized.isEmpty()) {
        const QString mapped = routing().value(normalized).toString();
        if (!mapped.isEmpty())
            return mapped;
    }

    // 规模未知：按卡片规模兜底（优先级越高越可能是大活；没有规模信息时按标题长度估）
    if (!card.isEmpty()) {
        const int priority = card.value(QStringLiteral("priority")).toInt();
        const int titleLen = card.value(QStringLiteral("title")).toString().trimmed().size();
        if (priority >= 3)
            return QStringLiteral("delivery");
        if (titleLen > 60)
            return QStringLiteral("delivery");
        if (titleLen > 24)
            return QStringLiteral("feature");
    }
    return QStringLiteral("fix");
}

QVariantList WorkflowPresets::skippableSteps(const QString &key, const QString &size) const
{
    // 只有小任务允许裁剪；且裁剪范围仅限预设声明的 skippable（永远是 pm / architect）。
    if (!isSmallSize(size))
        return {};
    const QVariantMap preset = presetOf(key);
    if (preset.isEmpty())
        return {};
    QVariantList out;
    for (const QVariant &role : preset.value(QStringLiteral("skippable")).toList()) {
        if (!isProtectedRole(role.toString()))
            out.append(role);
    }
    return out;
}

QVariantList WorkflowPresets::stepsFor(const QString &key, const QString &size) const
{
    const QVariantMap preset = presetOf(key);
    if (preset.isEmpty())
        return {};

    const QVariantList skips = skippableSteps(key, size);
    QStringList skipRoles;
    for (const QVariant &role : skips)
        skipRoles.append(role.toString());

    // 返回「步骤 map」而不是角色字符串：与 TaskExec::presetSteps 同形状，可直接喂
    // TaskExec::normalizeSteps / WorkflowRunner。agent / prompt / acceptance / scope
    // 留空，由卡片与角色默认值在装配时补。
    QVariantList out;
    for (const QVariant &role : preset.value(QStringLiteral("steps")).toList()) {
        const QString name = role.toString();
        if (skipRoles.contains(name))
            continue;
        out.append(QVariantMap{
            {QStringLiteral("role"), name},
            {QStringLiteral("agent"), QString()},
            {QStringLiteral("prompt"), QString()},
            {QStringLiteral("acceptance"), QVariantList()},
            {QStringLiteral("scope"), QVariantList()},
        });
    }
    return out;
}

QString WorkflowPresets::buildSummary(const QString &taskId, const QString &workflow,
                                      const QVariantList &steps, const QString &cardTitle)
{
    QStringList lines;
    lines << QStringLiteral("# 角色链收口摘要");
    lines << QString();
    if (!cardTitle.trimmed().isEmpty())
        lines << QStringLiteral("- 任务：%1（%2）").arg(cardTitle.trimmed(), taskId);
    else
        lines << QStringLiteral("- 任务：%1").arg(taskId);
    lines << QStringLiteral("- workflow：%1").arg(workflow);
    lines << QStringLiteral("- 步骤数：%1").arg(steps.size());
    lines << QString();
    lines << QStringLiteral("## 逐步结果");

    bool anyFailed = false;
    QString failedRole;
    qint64 totalCostMs = 0;
    int index = 0;
    for (const QVariant &value : steps) {
        const QVariantMap step = value.toMap();
        const QString role = step.value(QStringLiteral("role")).toString();
        const QString status = step.value(QStringLiteral("status")).toString();
        const qint64 costMs = step.value(QStringLiteral("costMs")).toLongLong();
        totalCostMs += costMs;
        ++index;
        QString line = QStringLiteral("%1. %2 — %3").arg(index).arg(role, status);
        if (costMs > 0)
            line += QStringLiteral("（耗时 %1s）").arg(costMs / 1000);
        const QString handoff = step.value(QStringLiteral("handoffPath")).toString();
        if (!handoff.isEmpty())
            line += QStringLiteral(" → %1").arg(handoff);
        lines << line;
        if (status != QLatin1String("passed") && status != QLatin1String("pending")
            && !anyFailed) {
            anyFailed = true;
            failedRole = role;
        }
    }

    lines << QString();
    lines << QStringLiteral("## 结论与建议");
    lines << QStringLiteral("- 累计耗时：%1s").arg(totalCostMs / 1000);
    if (!anyFailed) {
        lines << QStringLiteral("- 结论：所有步骤通过，可交付。");
        lines << QStringLiteral("- 建议下一步：把看板卡标为已办结；如需推送由人决定（本链不自动 push）。");
    } else {
        lines << QStringLiteral("- 结论：第 %1 步（%2）未通过，链已就地收口（fail-fast）。")
                     .arg(index)
                     .arg(failedRole);
        lines << QStringLiteral("- 建议下一步：回到 %1 修复后重跑，或在看板上补一条卡再执行；"
                                "重试轮数受 maxAttempts 约束。")
                     .arg(QStringLiteral("engineer"));
    }
    return lines.join(QLatin1Char('\n'));
}
