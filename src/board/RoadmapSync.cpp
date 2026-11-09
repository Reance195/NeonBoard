#include "RoadmapSync.h"

#include "Database.h"
#include "ProjectStore.h"
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
#include <QSet>
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

// 清掉标题尾部孤立的 "#"：旧版本对「无短号」的卡片也会嵌入空标记（title + " #"），
// 这份脏数据会被写回快照。只认「前置空白 + 尾部 #」，避免误伤 C# / F# 这类正常标题。
QString stripTrailingEmptyMarker(const QString &title)
{
    static const QRegularExpression rx(QStringLiteral("\\s+#\\s*$"));
    QString out = title;
    out.remove(rx);
    return out.trimmed();
}

QString embedCode(const QString &title, const QString &code)
{
    const QString clean = stripTrailingEmptyMarker(title);
    if (code.isEmpty())
        return clean;
    if (titleContainsCode(clean, code))
        return clean;
    if (clean.isEmpty())
        return title;
    return clean + QStringLiteral(" #") + code;
}

// 导出时把标题里嵌入的快照短号剥掉：快照的 code 只保存在 code 字段里，
// 标题保持原样，「导入 → 导出」往返不会越嵌越脏。
QString stripCode(const QString &title, const QString &code)
{
    if (code.isEmpty())
        return stripTrailingEmptyMarker(title);
    static thread_local QRegularExpression rx;
    rx.setPattern(QStringLiteral("\\s*#") + QRegularExpression::escape(code) + QStringLiteral("\\b"));
    rx.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    QString out = title;
    out.remove(rx);
    return stripTrailingEmptyMarker(out);
}

// 写回时把磁盘上已有、但本程序这一轮没有生成的字段原样带回。
// 历史事故：旧版程序按「已知字段重建」条目，把 v3 的 code 整片抹掉；
// 以后快照再加新字段时，老版本程序写回也不该丢数据。
QJsonObject preserveExistingFields(const QJsonObject &generated, const QJsonObject &existing)
{
    QJsonObject merged = generated;
    for (auto it = existing.constBegin(); it != existing.constEnd(); ++it) {
        if (!merged.contains(it.key()))
            merged.insert(it.key(), it.value());
    }
    return merged;
}

} // namespace

RoadmapSync::RoadmapSync(Database *database, TaskModel *tasks, QObject *parent)
    : RoadmapSync(database, tasks, nullptr, parent)
{
}

RoadmapSync::RoadmapSync(Database *database, TaskModel *tasks, ProjectStore *projects, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_tasks(tasks)
    , m_projects(projects)
{
}

bool RoadmapSync::enabled() const
{
    if (!m_path.isEmpty())
        return true;
    for (const SyncEntry &entry : m_entries)
        if (!entry.projectId.isEmpty() && !entry.path.isEmpty())
            return true;
    return false;
}

QString RoadmapSync::path() const
{
    return m_path;
}

QString RoadmapSync::lastReport() const
{
    for (const SyncEntry &entry : m_entries)
        if (entry.projectId.isEmpty())
            return entry.report;
    return {};
}

int RoadmapSync::conflicts() const
{
    int total = 0;
    for (const SyncEntry &entry : m_entries)
        total += entry.conflicts;
    return total;
}

void RoadmapSync::setPath(const QString &filePath)
{
    m_path = filePath.isEmpty() ? QString() : QDir::cleanPath(filePath);
    emit stateChanged();
}

void RoadmapSync::setReport(const QString &text)
{
    // 兼容入口：报告落到全局条目（旧 UI 只显示全局那条）
    for (auto &entry : m_entries)
        if (entry.projectId.isEmpty()) {
            entry.report = text;
            break;
        }
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
    // --smoke 是一次性启动探测（DX-28）：没有用户在等结果，写下的快照只会污染工作区。
    // resolvePath() 会从 applicationDirPath 向上找「同时含 .git 与 progress.json」的目录，
    // 手工在仓库里跑 dist\NeonBoard.exe --smoke 时必然命中，于是自动导出把仓库快照改脏
    // —— 提交时没看清 git status，派生产物就会被夹进实现提交。
    // 判据放在这里而不是 main.cpp：开关本来就只在本函数判定，且 main.cpp 行数已顶格。
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke")))
        return false;
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
    if (SyncEntry *entry = findEntry(filePath))
        return importEntry(*entry, force);
    // 临时路径（CLI --import-roadmap）：按全局范围一次性处理，不入注册表
    SyncEntry adhoc{QDir::cleanPath(filePath), QString(), {}, 0, {}, 0};
    return importEntry(adhoc, force);
}

RoadmapSync::HomeScope RoadmapSync::homeScope(const SyncEntry &entry) const
{
    // 作用域（R18-02）：全局文件只放未归属绑定项目的卡，项目文件只放
    // 该项目的卡 —— 每张卡恰好有一个「家」文件，避免同一张卡写进
    // 两个文件的双写者问题。导入与导出共用这一份判据（DX-133）。
    HomeScope scope;
    scope.isGlobal = entry.projectId.isEmpty();
    if (!m_projects)
        return scope;
    if (scope.isGlobal) {
        for (const auto &bound : m_projects->boundRepos())
            for (const QString &id : m_projects->taskIdsOf(bound.first))
                scope.homeIds.insert(id);
    } else {
        for (const QString &id : m_projects->taskIdsOf(entry.projectId))
            scope.homeIds.insert(id);
    }
    scope.scoped = true;
    return scope;
}

bool RoadmapSync::importEntry(SyncEntry &entry, bool force)
{
    if (entry.path.isEmpty()) {
        entry.report = QStringLiteral("未配置路线图文件路径");
        emit stateChanged();
        return false;
    }

    QString error;
    const QJsonObject root = readFile(entry.path, &error);
    if (root.isEmpty()) {
        entry.report = error;
        emit stateChanged();
        return false;
    }

    const int version = root.value(QStringLiteral("version")).toInt();
    if (version < kSnapshotMinVersion) {
        entry.report = QStringLiteral("路线图文件格式过旧（v%1），请重新导出后再同步").arg(version);
        emit stateChanged();
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
    const bool hasBaseline = !entry.baseline.isEmpty();

    // 作用域判据（DX-133 防线用）：墓碑只删「家在本文件」的卡
    const HomeScope scope = homeScope(entry);

    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;

        const QString code = item.value(QStringLiteral("code")).toString();
        if (!code.isEmpty())
            codes.insert(id, code);

        const bool external = hasBaseline
            && (!entry.baseline.contains(id)
                || compactJson(entry.baseline.value(id)) != compactJson(item));

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
            // 墓碑只对「家在本文件」的卡生效（DX-133 防线）：家在别处的墓碑
            // 条目 —— 归属迁移后的残留、双项目绑同路径时对方的条目 —— 不执行
            // 删除，按冲突计（以看板为准）。文件里可能已经躺着历史误标的
            // 墓碑，这道防线保证导入侧永远删不到别人家的卡。
            const bool homeHere = !scope.scoped
                || (scope.isGlobal ? !scope.homeIds.contains(id) : scope.homeIds.contains(id));
            if (!homeHere && !board.isEmpty()) {
                ++conflicts;
                ++skipped;
                continue;
            }
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
    // 导入卡的归属（R18-02）：绑定项目的文件即「成员清单」——
    // 尚未归属任何项目的卡自动归到该项目；已归属别的项目的卡不抢
    // （归属是用户显式动作，文件只补齐不覆盖）。
    if (!entry.projectId.isEmpty() && m_projects) {
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            if (item.value(QStringLiteral("deleted")).toBool())
                continue;
            const QString id = item.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || m_tasks->taskById(id).isEmpty())
                continue;
            if (!m_projects->projectOfTask(id).isEmpty())
                continue;
            m_projects->assignTask(id, entry.projectId);
        }
    }

    entry.baseline.clear();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            entry.baseline.insert(id, item);
    }
    const QFileInfo info(entry.path);
    entry.lastMtime = info.exists() ? info.lastModified().toSecsSinceEpoch() : 0;
    entry.conflicts = conflicts;

    entry.report = QStringLiteral("同步完成：应用 %1 项，跳过 %2 项，删除 %3 项%4")
                       .arg(applied).arg(skipped).arg(deleted)
                       .arg(conflicts > 0 ? QStringLiteral("，保留看板侧 %1 项更新").arg(conflicts) : QString());
    emit imported(applied, skipped);
    emit stateChanged();
    return true;
}

bool RoadmapSync::exportTo(const QString &filePath)
{
    if (SyncEntry *entry = findEntry(filePath))
        return exportEntry(*entry);
    // 临时路径（CLI --export-roadmap）：按全局范围一次性处理
    SyncEntry adhoc{QDir::cleanPath(filePath), QString(), {}, 0, {}, 0};
    return exportEntry(adhoc);
}

bool RoadmapSync::exportEntry(SyncEntry &entry)
{
    if (entry.path.isEmpty())
        return false;

    // 作用域（R18-02）：全局文件只放未归属绑定项目的卡，项目文件只放
    // 该项目的卡 —— 每张卡恰好有一个「家」文件，避免同一张卡写进
    // 两个文件的双写者问题。判据与导入侧共用一份实现（DX-133）。
    const HomeScope scope = homeScope(entry);
    const bool isGlobal = scope.isGlobal;
    const QSet<QString> homeIds = scope.homeIds;
    const bool scoped = scope.scoped;

    QString error;
    const QJsonObject root = readFile(entry.path, &error);
    QHash<QString, QJsonObject> current;
    if (!root.isEmpty() && root.value(QStringLiteral("version")).toInt() >= kSnapshotVersion) {
        const QJsonArray items = root.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            const QString id = item.value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                current.insert(id, item);
        }
        if (entry.baseline.isEmpty()) {
            // 首次写回：以文件现有内容为基线，避免把「文件里已有但我们没写过」的条目误判为冲突
            entry.baseline = current;
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
        // 家不在本文件的卡必须先计入 seen：seen 的语义是「看板里存在」，
        // 不是「要写进本文件」—— scoped 过滤只决定写不写。否则墓碑循环
        // 会把文件里它的旧条目误标 deleted，下次导入就把卡删了
        // （DX-133：归属迁移 / 同路径多项目的对方卡被清掉）。
        seen.append(id);
        // 作用域过滤（R18-02）：这张卡的「家」不是本文件，不写
        if (scoped) {
            const bool homeHere = isGlobal ? !homeIds.contains(id) : homeIds.contains(id);
            if (!homeHere)
                continue;
        }

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
            && (!entry.baseline.contains(id)
                || compactJson(current.value(id)) != compactJson(entry.baseline.value(id)));
        if (external) {
            item = current.value(id);
            ++conflicts;
        } else if (current.contains(id)) {
            // 未外部改动：以看板生成的字段为准，但文件里本程序不认识的字段
            // （如 code 短号、外部工具附加的自定义字段）原样保留
            item = preserveExistingFields(item, current.value(id));
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
    // 顶层未知字段（例如市场索引同款 signature 预留位、外部工具附加的元信息）
    // 同样原样保留，只有本程序负责的 version/updated/items 会被重写。
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.key() != QStringLiteral("version") && it.key() != QStringLiteral("updated")
            && it.key() != QStringLiteral("items")) {
            out.insert(it.key(), it.value());
        }
    }

    // 内容没有实质变化就不动文件：避免每轮轮询都改写 mtime 与 updated 字段，
    // 否则会污染 git diff，还会触发「导入→重载→再导出」的自激循环。
    if (!root.isEmpty() && root.value(QStringLiteral("version")).toInt() == kSnapshotVersion) {
        QJsonObject lhs = out;
        QJsonObject rhs = root;
        lhs.remove(QStringLiteral("updated"));
        rhs.remove(QStringLiteral("updated"));
        if (compactJson(lhs) == compactJson(rhs)) {
            entry.baseline = next;
            entry.conflicts = conflicts;
            entry.lastMtime = QFileInfo(entry.path).lastModified().toSecsSinceEpoch();
            entry.report = QStringLiteral("已写回 %1 条任务（内容无变化，跳过写盘）").arg(seen.size());
            emit stateChanged();
            return true;
        }
    }

    if (!writeFile(entry.path, out))
        return false;

    entry.baseline = next;
    entry.conflicts = conflicts;
    const QFileInfo info(entry.path);
    entry.lastMtime = info.lastModified().toSecsSinceEpoch();
    entry.report = QStringLiteral("已写回 %1 条任务%2")
                       .arg(seen.size())
                       .arg(conflicts > 0 ? QStringLiteral("，保留 %1 条外部改动").arg(conflicts) : QString());
    emit stateChanged();
    return true;
}

bool RoadmapSync::isApplying() const
{
    return m_applying;
}

void RoadmapSync::poll()
{
    refreshEntries();
    for (auto &entry : m_entries) {
        if (entry.path.isEmpty())
            continue;
        const QFileInfo info(entry.path);
        if (!info.exists())
            continue;
        const qint64 mtime = info.lastModified().toSecsSinceEpoch();
        if (mtime == entry.lastMtime)
            continue;
        // DX-142：导入成功才记 mtime——先记后导入的话，外部写一半 / 坏 JSON
        // 触发的导入失败会把这次变更吞掉，直到文件再次被外部改动
        if (importEntry(entry, false))
            entry.lastMtime = mtime;
    }
}

void RoadmapSync::refreshEntries()
{
    // 注册表每次重建：全局条目（m_path，启动时设置）+ 所有绑定项目。
    // 同路径的条目继承状态（基线 / mtime / 报告），绑定清单变化不会
    // 丢掉「外部改动」判定基准。
    QList<SyncEntry> next;
    if (!m_path.isEmpty())
        next.append(SyncEntry{m_path, QString(), {}, 0, {}, 0});
    if (m_projects) {
        for (const auto &bound : m_projects->boundRepos())
            next.append(SyncEntry{QDir(bound.second).filePath(QStringLiteral("progress.json")),
                                  bound.first, {}, 0, {}, 0});
    }
    auto fingerprint = [](const QList<SyncEntry> &list) {
        QStringList keys;
        for (const auto &entry : list)
            keys << entry.path + QStringLiteral("|") + entry.projectId;
        return keys.join(QStringLiteral("\n"));
    };
    const QString before = fingerprint(m_entries);
    for (auto &entry : next)
        if (SyncEntry *prev = findEntry(entry.path)) {
            entry.baseline = prev->baseline;
            entry.lastMtime = prev->lastMtime;
            entry.report = prev->report;
            entry.conflicts = prev->conflicts;
        }
    if (fingerprint(next) == before)
        return; // 绑定清单没变，不触发 stateChanged
    m_entries = next;
    emit stateChanged();
}

bool RoadmapSync::importAll(bool force)
{
    refreshEntries();
    bool any = false;
    for (auto &entry : m_entries)
        any = importEntry(entry, force) || any;
    return any;
}

void RoadmapSync::exportAll()
{
    refreshEntries();
    for (auto &entry : m_entries)
        exportEntry(entry);
}

RoadmapSync::SyncEntry *RoadmapSync::findEntry(const QString &filePath)
{
    if (filePath.isEmpty())
        return nullptr;
    const QString cleaned = QDir::cleanPath(filePath);
    for (auto &entry : m_entries)
        if (!entry.path.isEmpty() && QDir::cleanPath(entry.path) == cleaned)
            return &entry;
    return nullptr;
}
