#include "DataPort.h"

#include "AppPaths.h"
#include "Database.h"
#include "TaskModel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

namespace {

QStringList tagsFromVariant(const QVariant &value)
{
    QStringList result;
    if (value.typeId() == QMetaType::QStringList) {
        return value.toStringList();
    }
    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        for (const QVariant &item : list)
            result << item.toString();
        return result;
    }
    const QString raw = value.toString().trimmed();
    if (raw.isEmpty())
        return result;
    if (raw.startsWith(QLatin1Char('['))) {
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        const QJsonArray array = doc.array();
        for (const QJsonValue &item : array)
            result << item.toString();
        return result;
    }
    const QStringList parts = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            result << trimmed;
    }
    return result;
}

QString tagsToJson(const QStringList &tags)
{
    QStringList clean;
    for (const QString &tag : tags) {
        const QString trimmed = tag.trimmed();
        if (!trimmed.isEmpty() && !clean.contains(trimmed))
            clean << trimmed;
    }
    QJsonArray array;
    for (const QString &tag : clean)
        array.append(tag);
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString dueDateText(qint64 dueAt)
{
    if (dueAt <= 0)
        return {};
    return QDateTime::fromSecsSinceEpoch(dueAt).toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace

DataPort::DataPort(Database *database, TaskModel *tasks, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_tasks(tasks)
{
}

QString DataPort::backupDir() const
{
    return AppPaths::dataDir() + QStringLiteral("/backups");
}

QStringList DataPort::formats() const
{
    return {QStringLiteral("json"), QStringLiteral("todo"), QStringLiteral("markdown")};
}

QString DataPort::priorityText(int priority)
{
    if (priority >= 3)
        return QStringLiteral("高");
    if (priority == 2)
        return QStringLiteral("中");
    if (priority == 1)
        return QStringLiteral("低");
    return QStringLiteral("无");
}

QStringList DataPort::parseTags(const QString &raw)
{
    QStringList result;
    const QStringList parts = raw.split(QRegularExpression(QStringLiteral("[\\s,，]+")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        QString tag = part.trimmed();
        if (tag.startsWith(QLatin1Char('#')))
            tag = tag.mid(1);
        if (tag.startsWith(QLatin1Char('@')))
            tag = tag.mid(1);
        if (!tag.isEmpty() && !result.contains(tag))
            result << tag;
    }
    return result;
}

QString DataPort::buildJson() const
{
    QJsonArray array;
    const QVariantList tasks = m_tasks->allTasks();
    for (const QVariant &item : tasks) {
        const QVariantMap map = item.toMap();
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), map.value(QStringLiteral("id")).toString());
        obj.insert(QStringLiteral("title"), map.value(QStringLiteral("title")).toString());
        obj.insert(QStringLiteral("note"), map.value(QStringLiteral("note")).toString());
        obj.insert(QStringLiteral("status"), map.value(QStringLiteral("status")).toString());
        obj.insert(QStringLiteral("priority"), map.value(QStringLiteral("priority")).toInt());
        obj.insert(QStringLiteral("tags"),
                   QJsonArray::fromStringList(tagsFromVariant(map.value(QStringLiteral("tagsFull")))));
        obj.insert(QStringLiteral("orderKey"), static_cast<double>(map.value(QStringLiteral("orderKey")).toLongLong()));
        obj.insert(QStringLiteral("dueAt"), static_cast<double>(map.value(QStringLiteral("dueAt")).toLongLong()));
        obj.insert(QStringLiteral("createdAt"), static_cast<double>(map.value(QStringLiteral("createdAt")).toLongLong()));
        obj.insert(QStringLiteral("updatedAt"), static_cast<double>(map.value(QStringLiteral("updatedAt")).toLongLong()));
        obj.insert(QStringLiteral("completedAt"), static_cast<double>(map.value(QStringLiteral("completedAt")).toLongLong()));
        array.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("app"), QStringLiteral("NeonBoard"));
    root.insert(QStringLiteral("exportedAt"),
                QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("tasks"), array);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString DataPort::buildTodo() const
{
    QStringList lines;
    const QVariantList tasks = m_tasks->allTasks();
    for (const QVariant &item : tasks) {
        const QVariantMap map = item.toMap();
        const QString status = map.value(QStringLiteral("status")).toString();
        const QString mark = status == QLatin1String("done") ? QStringLiteral("[x]") : QStringLiteral("[ ]");
        lines << mark + QLatin1Char(' ') + map.value(QStringLiteral("title")).toString();
        const QStringList tags = tagsFromVariant(map.value(QStringLiteral("tagsFull")));
        if (!tags.isEmpty()) {
            QStringList prefixed;
            for (const QString &tag : tags)
                prefixed << QLatin1Char('@') + tag;
            lines << QStringLiteral("  ") + prefixed.join(QLatin1Char(' '));
        }
        const QString note = map.value(QStringLiteral("note")).toString().trimmed();
        if (!note.isEmpty())
            lines << QStringLiteral("  # ") + note;
        lines << QString();
    }
    return lines.join(QLatin1Char('\n'));
}

QString DataPort::buildMarkdown() const
{
    QStringList lines;
    lines << QStringLiteral("# NeonBoard 导出");
    lines << QStringLiteral("导出时间：") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    lines << QString();

    const QStringList order = {QStringLiteral("todo"), QStringLiteral("doing"), QStringLiteral("done")};
    const QStringList titles = {QStringLiteral("待办"), QStringLiteral("进行中"), QStringLiteral("已办结")};
    const QVariantList tasks = m_tasks->allTasks();

    for (int i = 0; i < order.size(); ++i) {
        lines << QStringLiteral("## ") + titles.at(i);
        for (const QVariant &item : tasks) {
            const QVariantMap map = item.toMap();
            if (map.value(QStringLiteral("status")).toString() != order.at(i))
                continue;
            QString line = order.at(i) == QLatin1String("done") ? QStringLiteral("- [x] ")
                                                                : QStringLiteral("- [ ] ");
            line += map.value(QStringLiteral("title")).toString();
            const QStringList tags = tagsFromVariant(map.value(QStringLiteral("tagsFull")));
            if (!tags.isEmpty()) {
                QStringList inline_;
                for (const QString &tag : tags)
                    inline_ << QLatin1Char('`') + tag + QLatin1Char('`');
                line += QStringLiteral("  ") + inline_.join(QLatin1Char(' '));
            }
            const QString due = dueDateText(map.value(QStringLiteral("dueAt")).toLongLong());
            if (!due.isEmpty())
                line += QStringLiteral("  截止：") + due;
            const int priority = map.value(QStringLiteral("priority")).toInt();
            if (priority > 0)
                line += QStringLiteral("  优先级：") + priorityText(priority);
            const QString note = map.value(QStringLiteral("note")).toString().trimmed();
            if (!note.isEmpty())
                line += QStringLiteral("  —— ") + note;
            lines << line;
        }
        lines << QString();
    }
    return lines.join(QLatin1Char('\n'));
}

QString DataPort::exportTasks(const QString &path, const QString &format)
{
    const QString target = path.trimmed();
    if (target.isEmpty())
        return QStringLiteral("导出路径为空");

    const QFileInfo info(target);
    const QDir dir = info.absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return QStringLiteral("无法创建目录：") + dir.absolutePath();

    QString content;
    const QString fmt = format.trimmed().toLower();
    if (fmt == QLatin1String("todo"))
        content = buildTodo();
    else if (fmt == QLatin1String("markdown") || fmt == QLatin1String("md"))
        content = buildMarkdown();
    else
        content = buildJson();

    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return QStringLiteral("写入失败：") + file.errorString();
    }
    const QByteArray bytes = content.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        file.close();
        return QStringLiteral("写入不完整：") + target;
    }
    file.close();

    emit finished(true, QStringLiteral("已导出到 ") + target);
    return {};
}

bool DataPort::applyTask(const QString &title, const QString &note, const QString &status, int priority,
                         const QStringList &tags, qint64 dueAt, qint64 completedAt)
{
    if (title.trimmed().isEmpty())
        return false;

    QString taskId;
    const QVariantList existing = m_tasks->allTasks();
    for (const QVariant &item : existing) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("title")).toString().trimmed() == title.trimmed()) {
            taskId = map.value(QStringLiteral("id")).toString();
            break;
        }
    }
    if (taskId.isEmpty())
        taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QVariantMap fields;
    fields.insert(QStringLiteral("title"), title.trimmed());
    fields.insert(QStringLiteral("note"), note);
    fields.insert(QStringLiteral("status"), status);
    fields.insert(QStringLiteral("priority"), priority);
    fields.insert(QStringLiteral("tags"), tagsToJson(tags));
    fields.insert(QStringLiteral("dueAt"), dueAt);
    if (completedAt > 0)
        fields.insert(QStringLiteral("completedAt"), completedAt);

    return !m_tasks->upsertTask(taskId, fields).isEmpty();
}

int DataPort::importJson(const QByteArray &data, QString *error)
{
    QJsonParseError parseError {};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("JSON 解析失败：") + parseError.errorString();
        return -1;
    }
    const QJsonObject root = doc.object();
    const int schema = root.value(QStringLiteral("schemaVersion")).toInt(0);
    if (schema <= 0) {
        if (error)
            *error = QStringLiteral("缺少 schemaVersion，无法导入");
        return -1;
    }
    const QJsonArray array = root.value(QStringLiteral("tasks")).toArray();
    int count = 0;
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        QStringList tags;
        const QJsonArray tagArray = obj.value(QStringLiteral("tags")).toArray();
        for (const QJsonValue &tag : tagArray)
            tags << tag.toString();
        const QString status = obj.value(QStringLiteral("status")).toString(QStringLiteral("todo"));
        if (applyTask(obj.value(QStringLiteral("title")).toString(),
                      obj.value(QStringLiteral("note")).toString(),
                      status,
                      obj.value(QStringLiteral("priority")).toInt(),
                      tags,
                      static_cast<qint64>(obj.value(QStringLiteral("dueAt")).toDouble()),
                      static_cast<qint64>(obj.value(QStringLiteral("completedAt")).toDouble()))) {
            ++count;
        }
    }
    return count;
}

int DataPort::importTodo(const QString &text, QString *error)
{
    Q_UNUSED(error)
    const QStringList lines = text.split(QLatin1Char('\n'));
    int count = 0;
    QString title;
    QString note;
    QStringList tags;
    bool done = false;

    auto flush = [&]() {
        if (!title.isEmpty()) {
            if (applyTask(title, note, done ? QStringLiteral("done") : QStringLiteral("todo"),
                          1, tags, 0, 0)) {
                ++count;
            }
        }
        title.clear();
        note.clear();
        tags.clear();
        done = false;
    };

    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        static const QRegularExpression re(QStringLiteral("^\\[([ xX])\\]\\s*(.+)$"));
        const QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            flush();
            done = match.captured(1).compare(QLatin1String("x"), Qt::CaseInsensitive) == 0;
            title = match.captured(2).trimmed();
            continue;
        }
        if (title.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('@'))) {
            tags << parseTags(line);
        } else if (line.startsWith(QLatin1Char('#'))) {
            note = line.mid(1).trimmed();
        }
    }
    flush();
    return count;
}

int DataPort::importMarkdown(const QString &text, QString *error)
{
    Q_UNUSED(error)
    const QStringList lines = text.split(QLatin1Char('\n'));
    int count = 0;

    static const QRegularExpression re(QStringLiteral("^\\s*-\\s*\\[([ xX])\\]\\s*(.+)$"));
    for (const QString &raw : lines) {
        const QRegularExpressionMatch match = re.match(raw);
        if (!match.hasMatch())
            continue;
        const bool done = match.captured(1).compare(QLatin1String("x"), Qt::CaseInsensitive) == 0;
        QString rest = match.captured(2);

        QStringList tags;
        static const QRegularExpression tagRe(QStringLiteral("`([^`]+)`"));
        QRegularExpressionMatchIterator tagIt = tagRe.globalMatch(rest);
        while (tagIt.hasNext()) {
            const QRegularExpressionMatch tagMatch = tagIt.next();
            tags << tagMatch.captured(1).trimmed();
            rest.remove(tagMatch.captured(0));
        }

        int priority = 0;
        static const QRegularExpression priRe(QStringLiteral("优先级[：:]\\s*([高中低])"));
        const QRegularExpressionMatch priMatch = priRe.match(rest);
        if (priMatch.hasMatch()) {
            const QString text = priMatch.captured(1);
            priority = text == QStringLiteral("高") ? 3 : (text == QStringLiteral("中") ? 2 : 1);
            rest.remove(priMatch.captured(0));
        }

        qint64 dueAt = 0;
        static const QRegularExpression dueRe(QStringLiteral("截止[：:]\\s*(\\d{4}-\\d{2}-\\d{2})"));
        const QRegularExpressionMatch dueMatch = dueRe.match(rest);
        if (dueMatch.hasMatch()) {
            const QDate date = QDate::fromString(dueMatch.captured(1), Qt::ISODate);
            if (date.isValid()) {
                dueAt = QDateTime(date, QTime(23, 59)).toSecsSinceEpoch();
            }
            rest.remove(dueMatch.captured(0));
        }

        static const QRegularExpression noteRe(QStringLiteral("——\\s*(.+)$"));
        QString note;
        const QRegularExpressionMatch noteMatch = noteRe.match(rest);
        if (noteMatch.hasMatch()) {
            note = noteMatch.captured(1).trimmed();
            rest.remove(noteMatch.captured(0));
        }

        const QString title = rest.trimmed();
        if (title.isEmpty())
            continue;
        if (applyTask(title, note, done ? QStringLiteral("done") : QStringLiteral("todo"), priority,
                      tags, dueAt, 0)) {
            ++count;
        }
    }
    return count;
}

QString DataPort::importTasks(const QString &path, const QString &format)
{
    const QString target = path.trimmed();
    if (target.isEmpty())
        return QStringLiteral("导入路径为空");
    if (!QFile::exists(target))
        return QStringLiteral("文件不存在：") + target;

    QFile file(target);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("读取失败：") + file.errorString();
    }
    const QByteArray raw = file.readAll();
    file.close();

    QString fmt = format.trimmed().toLower();
    if (fmt.isEmpty() || fmt == QLatin1String("auto")) {
        if (target.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
            fmt = QStringLiteral("json");
        else if (target.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)
                 || target.endsWith(QLatin1String(".markdown"), Qt::CaseInsensitive))
            fmt = QStringLiteral("markdown");
        else
            fmt = QStringLiteral("todo");
    }

    QString error;
    int count = -1;
    if (fmt == QLatin1String("json"))
        count = importJson(raw, &error);
    else if (fmt == QLatin1String("markdown") || fmt == QLatin1String("md"))
        count = importMarkdown(QString::fromUtf8(raw), &error);
    else
        count = importTodo(QString::fromUtf8(raw), &error);

    if (count < 0)
        return error.isEmpty() ? QStringLiteral("导入失败") : error;

    m_tasks->load();
    emit finished(true, QStringLiteral("已导入 %1 条任务").arg(count));
    return {};
}

QString DataPort::backupNow()
{
    if (!m_database)
        return QStringLiteral("数据库不可用");

    const QString dirPath = backupDir();
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return QStringLiteral("无法创建备份目录：") + dirPath;

    const QString stamp = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    QString target = dirPath + QStringLiteral("/board-") + stamp + QStringLiteral(".db");
    int suffix = 1;
    while (QFile::exists(target)) {
        target = dirPath + QStringLiteral("/board-") + stamp + QStringLiteral("-") + QString::number(suffix)
            + QStringLiteral(".db");
        ++suffix;
    }

    // 先把 WAL 落盘，避免备份文件缺少最新数据
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (db.isOpen())
        db.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));

    QFile::remove(target);
    if (!QFile::copy(m_database->path(), target)) {
        return QStringLiteral("备份失败：无法复制数据库文件");
    }
    emit finished(true, QStringLiteral("已备份到 ") + target);
    return {};
}

QString DataPort::backupIfNeeded()
{
    const QString stamp = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    const QString today = backupDir() + QStringLiteral("/board-") + stamp + QStringLiteral(".db");
    if (QFile::exists(today))
        return {};
    return backupNow();
}

QString DataPort::defaultPath(const QString &format) const
{
    const QString stamp = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    const QString fmt = format.trimmed().toLower();
    if (fmt == QLatin1String("todo"))
        return AppPaths::dataDir() + QStringLiteral("/neonboard-") + stamp + QStringLiteral(".todo.txt");
    if (fmt == QLatin1String("markdown") || fmt == QLatin1String("md"))
        return AppPaths::dataDir() + QStringLiteral("/neonboard-") + stamp + QStringLiteral(".md");
    return AppPaths::dataDir() + QStringLiteral("/neonboard-") + stamp + QStringLiteral(".json");
}

QStringList DataPort::listBackups() const
{
    QDir dir(backupDir());
    if (!dir.exists())
        return {};
    QStringList files = dir.entryList({QStringLiteral("*.db")}, QDir::Files, QDir::Time);
    return files;
}

QStringList DataPort::listExports() const
{
    QDir dir(AppPaths::dataDir());
    if (!dir.exists())
        return {};
    QStringList files = dir.entryList({QStringLiteral("neonboard-*.json"),
                                        QStringLiteral("neonboard-*.md"),
                                        QStringLiteral("neonboard-*.todo.txt")},
                                       QDir::Files, QDir::Time);
    QStringList result;
    for (const QString &name : files)
        result << dir.filePath(name);
    return result;
}
