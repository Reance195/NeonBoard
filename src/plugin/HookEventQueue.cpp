#include "HookEventQueue.h"
#include "PluginEventBus.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QVariant>

namespace {

// 已投递事件保留上限，避免队列无限增长
constexpr int MaxRetainedEvents = 500;
// 单次轮询最多处理的事件数
constexpr int MaxBatchSize = 50;
// 无人订阅（插件未 ready / 未安装）时的最大重投次数：按退避策略约 10 分钟
constexpr int MaxDeliverAttempts = 60;
// 事件最长存活时间：超过一天仍未投出去就判失败，防止历史事件突然「诈尸」
constexpr qint64 MaxEventTtlMs = 24LL * 60 * 60 * 1000;
// 重投退避上限：首次 1s、逐次加 1s，最多 10s 一次
constexpr qint64 MaxRetryDelayMs = 10LL * 1000;

} // namespace

HookEventQueue::HookEventQueue(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_path(dbPath)
{
    m_connectionName = QStringLiteral("neonboard-hooks-%1")
                           .arg(reinterpret_cast<quintptr>(this), 0, 16);
}

HookEventQueue::~HookEventQueue()
{
    stop();
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool HookEventQueue::open(QString *error)
{
    if (m_db.isOpen())
        return true;

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_path);
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));

    return ensureSchema(error);
}

bool HookEventQueue::ensureSchema(QString *error)
{
    QSqlQuery query(m_db);
    const char *sql = R"(
        CREATE TABLE IF NOT EXISTS hook_event (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            event_name      TEXT    NOT NULL,
            payload         TEXT    NOT NULL,
            created_at      INTEGER NOT NULL,
            delivered       INTEGER NOT NULL DEFAULT 0,
            attempts        INTEGER NOT NULL DEFAULT 0,
            last_attempt_at INTEGER NOT NULL DEFAULT 0,
            failed          INTEGER NOT NULL DEFAULT 0
        )
    )";
    if (!query.exec(QString::fromLatin1(sql))) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    return ensureColumns(error);
}

// 老版本 hooks.db 只有 5 列：CREATE TABLE IF NOT EXISTS 不会补列，必须 ALTER TABLE。
// 该库是独立事件库，加列向后兼容（老程序读多出来的列不受影响）。
bool HookEventQueue::ensureColumns(QString *error)
{
    QSet<QString> existing;
    QSqlQuery info(m_db);
    if (!info.exec(QStringLiteral("PRAGMA table_info(hook_event)"))) {
        if (error)
            *error = info.lastError().text();
        return false;
    }
    while (info.next())
        existing.insert(info.value(1).toString());

    struct Column {
        const char *name;
        const char *definition;
    };
    const Column columns[] = {
        {"attempts", "INTEGER NOT NULL DEFAULT 0"},
        {"last_attempt_at", "INTEGER NOT NULL DEFAULT 0"},
        {"failed", "INTEGER NOT NULL DEFAULT 0"},
    };
    for (const Column &column : columns) {
        const QString name = QString::fromLatin1(column.name);
        if (existing.contains(name))
            continue;
        QSqlQuery alter(m_db);
        const QString sql = QStringLiteral("ALTER TABLE hook_event ADD COLUMN %1 %2")
                                .arg(name, QString::fromLatin1(column.definition));
        if (!alter.exec(sql)) {
            if (error)
                *error = alter.lastError().text();
            return false;
        }
    }
    return true;
}

bool HookEventQueue::enqueue(const QString &eventName, const QVariantMap &payload, QString *error)
{
    if (!m_db.isOpen() && !open(error))
        return false;

    const QJsonObject object = QJsonObject::fromVariantMap(payload);
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO hook_event (event_name, payload, created_at, delivered) VALUES (?, ?, ?, 0)"));
    query.addBindValue(eventName);
    query.addBindValue(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return false;
    }

    trim();
    return true;
}

void HookEventQueue::start(int intervalMs)
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(intervalMs);
        connect(m_timer, &QTimer::timeout, this, &HookEventQueue::poll);
    }
    m_timer->start();
    // 启动即补投递：应用没开时攒下的事件在这一轮被消费掉
    poll();
}

void HookEventQueue::stop()
{
    if (m_timer)
        m_timer->stop();
}

void HookEventQueue::poll()
{
    if (!m_db.isOpen() || !m_bus)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT id, event_name, payload, created_at, attempts, last_attempt_at "
            "FROM hook_event WHERE delivered = 0 AND failed = 0 ORDER BY id ASC LIMIT %1")
            .arg(MaxBatchSize))) {
        emit queueError(query.lastError().text());
        return;
    }

    while (query.next()) {
        const qint64 id = query.value(0).toLongLong();
        const QString name = query.value(1).toString();
        const QByteArray raw = query.value(2).toString().toUtf8();
        const qint64 createdAt = query.value(3).toLongLong();
        const int attempts = query.value(4).toInt();
        const qint64 lastAttemptAt = query.value(5).toLongLong();

        // 退避：无人订阅时不要每秒空转写库。首次立即投，之后 2s、3s … 最多 10s 一次。
        if (attempts > 0 && lastAttemptAt > 0
            && now - lastAttemptAt < qMin<qint64>(1000LL * (attempts + 1), MaxRetryDelayMs)) {
            continue;
        }

        QVariantMap payload;
        const QJsonDocument document = QJsonDocument::fromJson(raw);
        if (document.isObject())
            payload = document.object().toVariantMap();

        const int subscribers = m_bus->publish(name, payload);
        if (subscribers > 0) {
            markDelivered(id);
            m_published += subscribers;
            emit eventPublished(name, payload);
            continue;
        }

        // 没有订阅方（插件还没 ready / 未安装）时**不得**标记已投递：
        // 否则启动 1s 内投递的事件会在插件 20s 后 ready 时被永久丢弃。
        // 保留待投，直到重投次数或存活时间超限才判失败，避免无限堆积。
        const bool exhausted = attempts + 1 >= MaxDeliverAttempts;
        const bool expired = createdAt > 0 && now - createdAt > MaxEventTtlMs;
        if (exhausted || expired) {
            markFailed(id);
            qWarning().noquote() << "HookEventQueue: give up event" << name << "id" << id
                                 << "attempts" << attempts + 1
                                 << (expired ? QStringLiteral("(ttl expired)")
                                             : QStringLiteral("(max attempts)"));
        } else {
            bumpAttempt(id, attempts + 1, now);
            if (attempts == 0) {
                qInfo().noquote() << "HookEventQueue: no subscriber for" << name
                                  << "- keep pending and retry";
            }
        }
    }
}

bool HookEventQueue::markDelivered(qint64 id)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE hook_event SET delivered = 1 WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        emit queueError(query.lastError().text());
        return false;
    }
    return true;
}

bool HookEventQueue::bumpAttempt(qint64 id, int attempts, qint64 attemptedAt)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE hook_event SET attempts = ?, last_attempt_at = ? WHERE id = ?"));
    query.addBindValue(attempts);
    query.addBindValue(attemptedAt);
    query.addBindValue(id);
    if (!query.exec()) {
        emit queueError(query.lastError().text());
        return false;
    }
    return true;
}

bool HookEventQueue::markFailed(qint64 id)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE hook_event SET failed = 1, last_attempt_at = ? WHERE id = ?"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(id);
    if (!query.exec()) {
        emit queueError(query.lastError().text());
        return false;
    }
    return true;
}

void HookEventQueue::trim()
{
    QSqlQuery count(m_db);
    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM hook_event")))
        return;
    const int total = count.next() ? count.value(0).toInt() : 0;
    const int excess = total - MaxRetainedEvents;
    if (excess <= 0)
        return;

    // 只回收已投递 / 已失败的历史记录；未投递事件由重试上限兜底，不能被 trim 悄悄删掉
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM hook_event WHERE id IN ("
        "SELECT id FROM hook_event WHERE delivered = 1 OR failed = 1 ORDER BY id ASC LIMIT ?)"));
    query.addBindValue(excess);
    if (!query.exec())
        emit queueError(query.lastError().text());
}

int HookEventQueue::pendingCount() const
{
    if (!m_db.isOpen())
        return 0;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM hook_event WHERE delivered = 0 AND failed = 0"))) {
        return 0;
    }
    return query.next() ? query.value(0).toInt() : 0;
}
