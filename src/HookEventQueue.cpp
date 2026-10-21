#include "HookEventQueue.h"
#include "PluginEventBus.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QVariant>

namespace {

// 已投递事件保留上限，避免队列无限增长
constexpr int MaxRetainedEvents = 500;
// 单次轮询最多处理的事件数
constexpr int MaxBatchSize = 50;

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
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            event_name  TEXT    NOT NULL,
            payload     TEXT    NOT NULL,
            created_at  INTEGER NOT NULL,
            delivered   INTEGER NOT NULL DEFAULT 0
        )
    )";
    if (!query.exec(QString::fromLatin1(sql))) {
        if (error)
            *error = query.lastError().text();
        return false;
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

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT id, event_name, payload FROM hook_event WHERE delivered = 0 ORDER BY id ASC LIMIT %1")
            .arg(MaxBatchSize))) {
        emit queueError(query.lastError().text());
        return;
    }

    while (query.next()) {
        const qint64 id = query.value(0).toLongLong();
        const QString name = query.value(1).toString();
        const QByteArray raw = query.value(2).toString().toUtf8();

        QVariantMap payload;
        const QJsonDocument document = QJsonDocument::fromJson(raw);
        if (document.isObject())
            payload = document.object().toVariantMap();

        const int subscribers = m_bus->publish(name, payload);
        // 没有订阅方（例如插件还没装）时同样标记为已处理，避免队列无限堆积；
        // 事件本身保留在表里，可在插件面板里看到「无人认领」的情况。
        markDelivered(id);
        m_published += subscribers;
        emit eventPublished(name, payload);
        if (subscribers == 0)
            qInfo().noquote() << "HookEventQueue: no subscriber for" << name;
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

void HookEventQueue::trim()
{
    QSqlQuery count(m_db);
    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM hook_event")))
        return;
    const int total = count.next() ? count.value(0).toInt() : 0;
    const int excess = total - MaxRetainedEvents;
    if (excess <= 0)
        return;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM hook_event WHERE id IN (SELECT id FROM hook_event ORDER BY id ASC LIMIT ?)"));
    query.addBindValue(excess);
    if (!query.exec())
        emit queueError(query.lastError().text());
}

int HookEventQueue::pendingCount() const
{
    if (!m_db.isOpen())
        return 0;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM hook_event WHERE delivered = 0")))
        return 0;
    return query.next() ? query.value(0).toInt() : 0;
}
