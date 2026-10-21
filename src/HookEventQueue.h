#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>

class PluginEventBus;
class QTimer;

// 钩子事件队列：Git 钩子 / CLI 把「提交信息」投递到这里，主进程轮询消费后转成插件事件。
// 独立库（hooks.db），与看板库（board.db）物理隔离 —— 投递动作不会写任务表，
// 因此不会干扰看板的内存模型，也不会触发整表重载。
class HookEventQueue : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path CONSTANT)

public:
    explicit HookEventQueue(const QString &dbPath, QObject *parent = nullptr);
    ~HookEventQueue() override;

    bool open(QString *error = nullptr);
    bool isOpen() const { return m_db.isOpen(); }
    QString path() const { return m_path; }

    // 投递一个事件：只落库，不执行。执行交给主进程里的插件。
    bool enqueue(const QString &eventName, const QVariantMap &payload, QString *error = nullptr);

    // 事件总线：轮询取到事件后转发给订阅方
    void setEventBus(PluginEventBus *bus) { m_bus = bus; }

    // 启动 / 停止轮询消费
    void start(int intervalMs = 1000);
    void stop();

    int pendingCount() const;

signals:
    void eventPublished(const QString &name, const QVariantMap &payload);
    void queueError(const QString &message);

private:
    bool ensureSchema(QString *error);
    void poll();
    bool markDelivered(qint64 id);
    void trim();

    QSqlDatabase m_db;
    QString m_path;
    QString m_connectionName;
    PluginEventBus *m_bus = nullptr;
    QTimer *m_timer = nullptr;
    int m_published = 0;
};
