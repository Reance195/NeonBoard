#include "LedgerImport.h"

#include "Database.h"
#include "TaskExec.h"
#include "TaskModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

bool sameList(const QVariantList &a, const QVariantList &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (a.at(i).toString() != b.at(i).toString())
            return false;
    return true;
}

QStringList jsonToStringList(const QJsonArray &array)
{
    QStringList out;
    for (const QJsonValue &v : array) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty())
            out.append(s);
    }
    return out;
}

} // namespace

LedgerImport::LedgerImport(Database *database, TaskModel *tasks, TaskExec *exec, QObject *parent)
    : QObject(parent),
      m_database(database),
      m_tasks(tasks),
      m_exec(exec)
{}

int LedgerImport::revision() const
{
    return m_revision;
}

QString LedgerImport::resolveLedgerPath()
{
    // 从 exe 目录向上探测仓库根（与 ProjectMap / RoadmapSync 同一策略）
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("docs/board-ledger.json"))))
            return dir.filePath(QStringLiteral("docs/board-ledger.json"));
        if (!dir.cdUp())
            break;
    }
    return {};
}

QVariantMap LedgerImport::parseLedgerFile(const QString &path)
{
    QVariantMap out;
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QStringLiteral("台账文件不存在或不可读：%1").arg(path));
        return out;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"),
                   QStringLiteral("台账 JSON 解析失败：%1").arg(parseError.errorString()));
        return out;
    }

    const QJsonObject cards = doc.object().value(QStringLiteral("cards")).toObject();
    QVariantList list;
    for (auto it = cards.constBegin(); it != cards.constEnd(); ++it) {
        const QJsonObject card = it.value().toObject();
        QVariantMap entry;
        entry.insert(QStringLiteral("code"), it.key());
        entry.insert(QStringLiteral("id"), card.value(QStringLiteral("id")).toString());
        entry.insert(QStringLiteral("title"), card.value(QStringLiteral("title")).toString());
        entry.insert(QStringLiteral("note"), card.value(QStringLiteral("note")).toString());
        QVariantList acceptance;
        for (const QString &s :
             jsonToStringList(card.value(QStringLiteral("acceptance")).toArray()))
            acceptance.append(s);
        QVariantList scope;
        for (const QString &s : jsonToStringList(card.value(QStringLiteral("scope")).toArray()))
            scope.append(s);
        entry.insert(QStringLiteral("acceptance"), acceptance);
        entry.insert(QStringLiteral("scope"), scope);
        list.append(entry);
    }
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("cards"), list);
    return out;
}

QString LedgerImport::goalFromNote(const QString &note)
{
    // note 形如「起因：…。目标：…交付（…）：…」——取「目标：」段（没有则首段），
    // 去前缀、限 300 字符，避免长篇背景灌进 goal。
    QString text = note.trimmed();
    if (text.isEmpty())
        return {};
    const QRegularExpression goalRx(QStringLiteral("目标：([^。]*。)"));
    const QRegularExpressionMatch m = goalRx.match(text);
    if (m.hasMatch())
        text = m.captured(1).trimmed();
    else {
        // 首段：按中文句号切（注意 '。' 是多字节字符，不能用 QLatin1Char）
        const int firstStop = text.indexOf(QChar(0x3002));
        if (firstStop > 0 && firstStop < 300)
            text = text.left(firstStop + 1);
    }
    text.remove(QStringLiteral("目标："));
    if (text.size() > 300)
        text = text.left(300);
    return text.trimmed();
}

QString LedgerImport::findTaskId(const QString &code, const QVariantMap &card) const
{
    if (!m_tasks)
        return {};
    // 优先按 id 精确匹配（路线图导入的卡与台账同 id）
    const QString id = card.value(QStringLiteral("id")).toString();
    if (!id.isEmpty() && !m_tasks->taskById(id).isEmpty())
        return id;
    // 兜底：标题内嵌短号（#R6-09 这类，计划钩子同款整词匹配）
    const QRegularExpression rx(QStringLiteral("#") + QRegularExpression::escape(code)
                                    + QStringLiteral("\\b"),
                                QRegularExpression::CaseInsensitiveOption);
    const QVariantList all = m_tasks->allTasks();
    for (const QVariant &v : all) {
        const QVariantMap task = v.toMap();
        if (rx.match(task.value(QStringLiteral("title")).toString()).hasMatch())
            return task.value(QStringLiteral("id")).toString();
    }
    return {};
}

QVariantMap LedgerImport::importOne(const QString &code, const QVariantMap &card)
{
    QVariantMap out;
    out.insert(QStringLiteral("code"), code);

    const QString taskId = findTaskId(code, card);
    if (taskId.isEmpty()) {
        out.insert(QStringLiteral("result"), QStringLiteral("missing")); // 未建卡：只报告
        return out;
    }

    const QVariantMap current = m_exec->execOf(taskId);
    QVariantMap patch;
    QVariantList conflicts;
    int filled = 0;

    // goal：台账 note 首段 → task_exec.goal（只补空）
    const QString goal = goalFromNote(card.value(QStringLiteral("note")).toString());
    const QString currentGoal = current.value(QStringLiteral("goal")).toString().trimmed();
    if (!goal.isEmpty()) {
        if (currentGoal.isEmpty()) {
            patch.insert(QStringLiteral("goal"), goal);
            ++filled;
        } else if (currentGoal != goal) {
            conflicts.append(QVariantMap{{QStringLiteral("field"), QStringLiteral("goal")},
                                         {QStringLiteral("ledger"), goal},
                                         {QStringLiteral("board"), currentGoal}});
        }
    }

    // acceptance / scope：只补空列表；两边都有且不同 → 报冲突不覆盖
    for (const QString &field : {QStringLiteral("acceptance"), QStringLiteral("scope")}) {
        const QVariantList ledgerList = card.value(field).toList();
        const QVariantList boardList = current.value(field).toList();
        if (ledgerList.isEmpty())
            continue;
        if (boardList.isEmpty()) {
            patch.insert(field, ledgerList);
            ++filled;
        } else if (!sameList(boardList, ledgerList)) {
            conflicts.append(
                QVariantMap{{QStringLiteral("field"), field},
                            {QStringLiteral("ledger"),
                             QVariant(ledgerList).toStringList().join(QStringLiteral(" | "))},
                            {QStringLiteral("board"),
                             QVariant(boardList).toStringList().join(QStringLiteral(" | "))}});
        }
    }

    if (filled > 0) {
        if (!m_exec->setExec(taskId, patch)) {
            out.insert(QStringLiteral("result"), QStringLiteral("failed"));
            out.insert(QStringLiteral("error"), QStringLiteral("setExec 拒绝写入（校验失败）"));
            return out;
        }
        out.insert(QStringLiteral("result"), QStringLiteral("imported"));
        out.insert(QStringLiteral("filled"), filled);
    } else {
        out.insert(QStringLiteral("result"), QStringLiteral("unchanged"));
    }
    if (!conflicts.isEmpty()) {
        out.insert(QStringLiteral("result"),
                   filled > 0 ? QStringLiteral("imported") : QStringLiteral("conflict"));
        out.insert(QStringLiteral("conflicts"), conflicts);
    }
    return out;
}

QVariantMap LedgerImport::importCard(const QString &code, const QString &path)
{
    const QString ledgerPath = path.trimmed().isEmpty() ? resolveLedgerPath() : path.trimmed();
    const QVariantMap parsed = parseLedgerFile(ledgerPath);
    if (!parsed.value(QStringLiteral("ok")).toBool())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), parsed.value(QStringLiteral("error"))}};

    const QString wanted = code.trimmed().toUpper();
    for (const QVariant &v : parsed.value(QStringLiteral("cards")).toList()) {
        const QVariantMap card = v.toMap();
        if (card.value(QStringLiteral("code")).toString().toUpper() == wanted) {
            QVariantMap out = importOne(card.value(QStringLiteral("code")).toString(), card);
            out.insert(QStringLiteral("ok"), true);
            return out;
        }
    }
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("台账里没有短号 %1").arg(code)}};
}

QVariantMap LedgerImport::importAll(const QString &path)
{
    const QString ledgerPath = path.trimmed().isEmpty() ? resolveLedgerPath() : path.trimmed();
    const QVariantMap parsed = parseLedgerFile(ledgerPath);
    if (!parsed.value(QStringLiteral("ok")).toBool())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), parsed.value(QStringLiteral("error"))}};

    QVariantList imported;
    QVariantList conflicts;
    QVariantList missing;
    QVariantList unchanged;
    bool touched = false;

    const QVariantList cards = parsed.value(QStringLiteral("cards")).toList();
    for (const QVariant &v : cards) {
        const QVariantMap card = v.toMap();
        const QString code = card.value(QStringLiteral("code")).toString();
        const QVariantMap r = importOne(code, card);
        const QString result = r.value(QStringLiteral("result")).toString();
        if (result == QLatin1String("imported")) {
            imported.append(code);
            touched = true;
        } else if (result == QLatin1String("missing")) {
            missing.append(code);
        } else if (result == QLatin1String("unchanged")) {
            unchanged.append(code);
        }
        if (r.contains(QStringLiteral("conflicts")))
            conflicts.append(r);
    }

    if (touched) {
        ++m_revision;
        emit changed();
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("imported"), imported},
            {QStringLiteral("conflicts"), conflicts},
            {QStringLiteral("missing"), missing},
            {QStringLiteral("unchanged"), unchanged}};
}
