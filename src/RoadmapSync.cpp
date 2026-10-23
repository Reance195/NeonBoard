#include "RoadmapSync.h"

#include "Database.h"
#include "TaskModel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QVariantMap>

#include <algorithm>

namespace {

// 快照格式版本：v1 是早期的手工快照（语义 id + 日期字符串），v2 起改为
// 「看板 UUID 作 id + epoch 时间戳 + 完整字段」，支持双向幂等合并；
// v3 起每张路线图卡片额外带 code 短号（#R1-03 这类），用于把卡片状态
// 与 git 提交历史里的 #短号 标记对应起来，状态由 scripts/derive_board_status.py 派生。
constexpr int kSnapshotVersion = 3;
constexpr int kSnapshotMinVersion = 2;
constexpr char kCodeMetaKey[] = "board.codes";

QString compactJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonArray tagsToArray(const QString &tagsJson)
{
    QJsonArray array;
    const QJsonDocument document = QJsonDocument::fromJson(tagsJson.toUtf8());
    if (document.isArray()) {
        array = document.array();
        return array;
    }
    const QStringList parts = tagsJson.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString value = part.trimmed();
        if (!value.isEmpty())
            array.append(value);
    }
    return array;
}

// 快照短号（如 R3-01）对应的标题标记：#R3-01。
// 计划钩子按「标题内嵌短号」匹配任务，导入时把 code 嵌进标题尾，
// 让 git 提交标记 #R3-01 能命中来自路线图快照的卡片。
bool titleContainsCode(const QString &title, const QString &code)
{
    if (code.isEmpty())
        return false;
    static const QRegularExpression rx(
        QStringLiteral("#") + QRegularExpression::escape(code) + QStringLiteral("\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return rx.match(title).hasMatch();
}

QString embedCode(const QString &title, const QString &code)
{
    if (code.isEmpty())
        return title;
    if (titleContainsCode(title, code))
        return title;
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty())
        return title;
    return trimmed + QStringLiteral(" #") + code;
}

// 导出时把标题里嵌入的快照短号剥掉：快照的 code 只保存在 code 字段里，
// 标题保持原样，「导入 → 导出」往返不会越嵌越脏。
QString stripCode(const QString &title, const QString &code)
{
    if (code.isEmpty())
        return title;
    static thread_local QRegularExpression rx;
    rx.setPattern(QStringLiteral("\\s*#") + QRegularExpression::escape(code) + QStringLiteral("\\b"));
    rx.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    QString out = title;
    out.remove(rx);
    return out.trimmed();
}

} // namespace

RoadmapSync::RoadmapSync(Database *database, TaskModel *tasks, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_tasks(tasks)
{
}

bool RoadmapSync::enabled() const
{
    return !m_path.isEmpty();
}

QString RoadmapSync::path() const
{
    return m_path;
}

QString RoadmapSync::lastReport() const
{
    return m_lastReport;
}

int RoadmapSync::conflicts() const
{
    return m_conflicts;
}

void RoadmapSync::setPath(const QString &filePath)
{
    m_path = filePath.isEmpty() ? QString() : QDir::cleanPath(filePath);
    emit stateChanged();
}

void RoadmapSync::setReport(const QString &text)
{
    m_lastReport = text;
    emit stateChanged();
}

QString RoadmapSync::resolvePath()
{
    const QString env = qEnvironmentVariable("NEONBOARD_ROADMAP");
    if (!env.isEmpty())
        return QDir::cleanPath(env);

    QSettings settings;
    const QString configured = settings.value(QStringLiteral("roadmap/path")).toString();
    if (!configured.isEmpty())
        return QDir::cleanPath(configured);

    // 自动探测：从可执行文件所在目录向上找同时含 .git 与 progress.json 的目录
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
        if (dir.exists(QStringLiteral(".git")) && dir.exists(QStringLiteral("progress.json")))
            return dir.filePath(QStringLiteral("progress.json"));
        if (!dir.cdUp())
            break;
    }
    return {};
}

bool RoadmapSync::autoExportEnabled()
{
    if (qEnvironmentVariable("NEONBOARD_ROADMAP_AUTOWRITE") == QLatin1String("0"))
        return false;
    QSettings settings;
    return settings.value(QStringLiteral("roadmap/autoExport"), true).toBool();
}

QJsonObject RoadmapSync::readFile(const QString &filePath, QString *error) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 %1").arg(filePath);
        return {};
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("JSON 解析失败：%1").arg(parseError.errorString());
        return {};
    }
    if (error)
        error->clear();
    return document.object();
}

bool RoadmapSync::writeFile(const QString &filePath, const QJsonObject &root) const
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "RoadmapSync: cannot write" << filePath;
        return false;
    }
    // 末尾补一个换行，与仓库里 progress.json 的既有格式保持一致（避免无意义 diff）
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.write("\n");
    if (!file.commit()) {
        qWarning() << "RoadmapSync: commit failed" << filePath;
        return false;
    }
    return true;
}

bool RoadmapSync::importFrom(const QString &filePath, bool force)
{
    if (filePath.isEmpty()) {
        setReport(QStringLiteral("未配置路线图文件路径"));
        return false;
    }

    QString error;
    const QJsonObject root = readFile(filePath, &error);
    if (root.isEmpty()) {
        setReport(error);
        return false;
    }

    const int version = root.value(QStringLiteral("version")).toInt();
    if (version < kSnapshotMinVersion) {
        setReport(QStringLiteral("路线图文件格式过旧（v%1），请重新导出后再同步").arg(version));
        return false;
    }

    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    int applied = 0;
    int skipped = 0;
    int deleted = 0;
    int conflicts = 0;
    m_applying = true;
    QHash<QString, QString> codes;

    // 有基线 = 这个进程之前读过/写过这份文件，能识别「文件被外部改过」，
    // 这类条目一律以文件为准；没有基线（CLI / 首次启动）则退化为
    // 「按 updatedAt 大者胜」，避免旧文件覆盖看板里的新改动。
    const bool hasBaseline = !m_baseline.isEmpty();

    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;

        const QString code = item.value(QStringLiteral("code")).toString();
        if (!code.isEmpty())
            codes.insert(id, code);

        const bool external = hasBaseline
            && (!m_baseline.contains(id)
                || compactJson(m_baseline.value(id)) != compactJson(item));

        const QVariantMap board = m_tasks->taskById(id);
        const qint64 fileUpdated = static_cast<qint64>(item.value(QStringLiteral("updatedAt")).toDouble());
        const qint64 boardUpdated = board.value(QStringLiteral("updatedAt")).toLongLong();

        bool shouldApply = force || external;
        if (!shouldApply) {
            if (board.isEmpty()) {
                shouldApply = true;
            } else if (fileUpdated > boardUpdated) {
                shouldApply = true;
            } else if (fileUpdated < boardUpdated) {
                // 看板更新 → 保留看板版本，导出时会写回文件
                ++conflicts;
            }
        }

        if (!shouldApply) {
            ++skipped;
            continue;
        }

        if (item.value(QStringLiteral("deleted")).toBool()) {
            if (!board.isEmpty() && m_tasks->deleteTask(id))
                ++deleted;
            ++applied;
            continue;
        }

        QVariantMap fields;
        // 标题尾嵌入快照短号（幂等，已含则保持原样），供计划钩子按标题匹配
        fields[QStringLiteral("title")] = embedCode(item.value(QStringLiteral("title")).toString(), code);
        fields[QStringLiteral("note")] = item.value(QStringLiteral("note")).toString();
        fields[QStringLiteral("status")] = item.value(QStringLiteral("status")).toString();
        fields[QStringLiteral("priority")] = item.value(QStringLiteral("priority")).toInt();
        const QJsonArray tagArray = item.value(QStringLiteral("tags")).toArray();
        fields[QStringLiteral("tags")] = QString::fromUtf8(
            QJsonDocument(tagArray).toJson(QJsonDocument::Compact));
        fields[QStringLiteral("dueAt")] = static_cast<qint64>(item.value(QStringLiteral("dueAt")).toDouble());
        fields[QStringLiteral("orderKey")] = static_cast<qint64>(item.value(QStringLiteral("orderKey")).toDouble());
        fields[QStringLiteral("createdAt")] = static_cast<qint64>(item.value(QStringLiteral("createdAt")).toDouble());
        fields[QStringLiteral("updatedAt")] = static_cast<qint64>(item.value(QStringLiteral("updatedAt")).toDouble());
        fields[QStringLiteral("completedAt")] = static_cast<qint64>(item.value(QStringLiteral("completedAt")).toDouble());

        if (m_tasks->upsertTask(id, fields).isEmpty())
            ++skipped;
        else
            ++applied;
    }

    // 刷新模型并记录基线（load 会触发 countsChanged，导入期间保持 applying 标记）
    m_tasks->load();
    m_applying = false;

    // 卡片短号（#R1-03 这类）持久化到 meta 表，导出时带回快照，
    // 这样短号不会在「文件 → 看板 → 文件」往返中丢失。
    if (!codes.isEmpty() && m_database) {
        const QString key = QString::fromLatin1(kCodeMetaKey);
        QJsonObject mapping = QJsonDocument::fromJson(m_database->metaValue(key).toUtf8()).object();
        for (auto it = codes.constBegin(); it != codes.constEnd(); ++it)
            mapping.insert(it.key(), it.value());
        m_database->setMetaValue(key, QString::fromUtf8(QJsonDocument(mapping).toJson(QJsonDocument::Compact)));
    }
    m_baseline.clear();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            m_baseline.insert(id, item);
    }
    const QFileInfo info(filePath);
    m_lastMtime = info.exists() ? info.lastModified().toSecsSinceEpoch() : 0;
    m_conflicts = conflicts;

    setReport(QStringLiteral("同步完成：应用 %1 项，跳过 %2 项，删除 %3 项%4")
                  .arg(applied).arg(skipped).arg(deleted)
                  .arg(conflicts > 0 ? QStringLiteral("，保留看板侧 %1 项更新").arg(conflicts) : QString()));
    emit imported(applied, skipped);
    return true;
}

bool RoadmapSync::exportTo(const QString &filePath)
{
    if (filePath.isEmpty())
        return false;

    QString error;
    const QJsonObject root = readFile(filePath, &error);
    QHash<QString, QJsonObject> current;
    if (!root.isEmpty() && root.value(QStringLiteral("version")).toInt() >= kSnapshotVersion) {
        const QJsonArray items = root.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            const QString id = item.value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                current.insert(id, item);
        }
        if (m_baseline.isEmpty()) {
            // 首次写回：以文件现有内容为基线，避免把「文件里已有但我们没写过」的条目误判为冲突
            m_baseline = current;
        }
    }

    QJsonArray items;
    QHash<QString, QJsonObject> next;
    int conflicts = 0;
    QStringList seen;

    // 导出条目按「列内顺序」排列（待办 → 进行中 → 已办结，列内按 order_key 升序），
    // 保证同一份看板数据每次导出得到完全一致的文本，避免无意义的 git diff。
    struct SnapshotEntry {
        QString status;
        qint64 orderKey = 0;
        QString id;
        QJsonObject object;
    };
    QList<SnapshotEntry> entries;
    const auto appendEntry = [&entries](const QString &status, qint64 orderKey, const QString &id, const QJsonObject &object) {
        SnapshotEntry entry;
        entry.status = status;
        entry.orderKey = orderKey;
        entry.id = id;
        entry.object = object;
        entries.append(entry);
    };

    const QVariantList tasks = m_tasks->allTasks();
    QJsonObject codeMap;
    if (m_database) {
        const QString raw = m_database->metaValue(QString::fromLatin1(kCodeMetaKey));
        if (!raw.isEmpty())
            codeMap = QJsonDocument::fromJson(raw.toUtf8()).object();
    }
    for (const QVariant &value : tasks) {
        const QVariantMap task = value.toMap();
        const QString id = task.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        seen.append(id);

        QJsonObject item;
        item[QStringLiteral("id")] = id;
        item[QStringLiteral("title")] = task.value(QStringLiteral("title")).toString();
        item[QStringLiteral("note")] = task.value(QStringLiteral("note")).toString();
        item[QStringLiteral("status")] = task.value(QStringLiteral("status")).toString();
        item[QStringLiteral("priority")] = task.value(QStringLiteral("priority")).toInt();
        item[QStringLiteral("tags")] = tagsToArray(task.value(QStringLiteral("tagsFull")).toString());
        item[QStringLiteral("stage")] = task.value(QStringLiteral("stage")).toString();
        item[QStringLiteral("orderKey")] = static_cast<qint64>(task.value(QStringLiteral("orderKey")).toLongLong());
        item[QStringLiteral("dueAt")] = static_cast<qint64>(task.value(QStringLiteral("dueAt")).toLongLong());
        item[QStringLiteral("createdAt")] = static_cast<qint64>(task.value(QStringLiteral("createdAt")).toLongLong());
        item[QStringLiteral("updatedAt")] = static_cast<qint64>(task.value(QStringLiteral("updatedAt")).toLongLong());
        item[QStringLiteral("completedAt")] = static_cast<qint64>(task.value(QStringLiteral("completedAt")).toLongLong());
        item[QStringLiteral("deleted")] = false;

        // 卡片短号：优先取 meta 里的映射，取不到时回退到文件里已有的值
        QString code = codeMap.value(id).toString();
        if (code.isEmpty() && current.contains(id))
            code = current.value(id).value(QStringLiteral("code")).toString();
        if (!code.isEmpty())
            item[QStringLiteral("code")] = code;

        // 看板标题里嵌入的快照短号只属于看板侧匹配，导出时剥掉，
        // 保证快照标题与导入前的原始标题一致（往返幂等）
        item[QStringLiteral("title")] = stripCode(task.value(QStringLiteral("title")).toString(), code);

        // 文件里这一条被外部改过 → 原样保留，交给导入流程应用到看板
        const bool external = current.contains(id)
            && (!m_baseline.contains(id)
                || compactJson(current.value(id)) != compactJson(m_baseline.value(id)));
        if (external) {
            item = current.value(id);
            ++conflicts;
        }

        appendEntry(item.value(QStringLiteral("status")).toString(),
                    item.value(QStringLiteral("orderKey")).toInteger(),
                    id, item);
        next.insert(id, item);
    }

    // 文件里有、看板里没有的条目 → 标记删除并保留记录
    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        if (seen.contains(it.key()))
            continue;
        QJsonObject item = it.value();
        if (item.value(QStringLiteral("deleted")).toBool())
            continue;
        item[QStringLiteral("deleted")] = true;
        item[QStringLiteral("updatedAt")] = QDateTime::currentSecsSinceEpoch();
        appendEntry(item.value(QStringLiteral("status")).toString(),
                    item.value(QStringLiteral("orderKey")).toInteger(),
                    it.key(), item);
        next.insert(it.key(), item);
    }

    // 列顺序：待办 → 进行中 → 已办结，其余按名称
    const auto statusRank = [](const QString &status) {
        if (status == QStringLiteral("todo"))
            return 0;
        if (status == QStringLiteral("doing"))
            return 1;
        if (status == QStringLiteral("done"))
            return 2;
        return 3;
    };
    std::sort(entries.begin(), entries.end(), [&statusRank](const SnapshotEntry &left, const SnapshotEntry &right) {
        const int leftRank = statusRank(left.status);
        const int rightRank = statusRank(right.status);
        if (leftRank != rightRank)
            return leftRank < rightRank;
        if (left.status != right.status)
            return left.status < right.status;
        if (left.orderKey != right.orderKey)
            return left.orderKey < right.orderKey;
        return left.id < right.id;
    });
    for (const SnapshotEntry &entry : entries)
        items.append(entry.object);

    QJsonObject out;
    out[QStringLiteral("version")] = kSnapshotVersion;
    out[QStringLiteral("updated")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    out[QStringLiteral("items")] = items;

    // 内容没有实质变化就不动文件：避免每轮轮询都改写 mtime 与 updated 字段，
    // 否则会污染 git diff，还会触发「导入→重载→再导出」的自激循环。
    if (!root.isEmpty() && root.value(QStringLiteral("version")).toInt() == kSnapshotVersion) {
        QJsonObject lhs = out;
        QJsonObject rhs = root;
        lhs.remove(QStringLiteral("updated"));
        rhs.remove(QStringLiteral("updated"));
        if (compactJson(lhs) == compactJson(rhs)) {
            m_baseline = next;
            m_conflicts = conflicts;
            m_lastMtime = QFileInfo(filePath).lastModified().toSecsSinceEpoch();
            setReport(QStringLiteral("已写回 %1 条任务（内容无变化，跳过写盘）").arg(seen.size()));
            return true;
        }
    }

    if (!writeFile(filePath, out))
        return false;

    m_baseline = next;
    m_conflicts = conflicts;
    const QFileInfo info(filePath);
    m_lastMtime = info.lastModified().toSecsSinceEpoch();
    setReport(QStringLiteral("已写回 %1 条任务%2")
                  .arg(seen.size())
                  .arg(conflicts > 0 ? QStringLiteral("，保留 %1 条外部改动").arg(conflicts) : QString()));
    return true;
}

bool RoadmapSync::isApplying() const
{
    return m_applying;
}

void RoadmapSync::poll()
{
    if (m_path.isEmpty())
        return;
    const QFileInfo info(m_path);
    if (!info.exists())
        return;
    const qint64 mtime = info.lastModified().toSecsSinceEpoch();
    if (mtime == m_lastMtime)
        return;
    m_lastMtime = mtime;
    importFrom(m_path, false);
}
