#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Database;

// 任务执行运行记录（R6-02）。
//
// 设计要点（docs/adr/0002-run-state-and-executor-safety.md）：
//   1. 运行态独立落在侧表 task_run，不往 task 表加列、不升 user_version，
//      看板列（todo/doing/done）不会被执行结果改动；
//   2. 同一张卡同一时刻最多一个活跃 run（queued/running），由部分唯一索引兜底；
//   3. M1 忙策略是拒绝而不是排队：acquire 遇到活跃 run 直接返回 busy 错误码；
//   4. 启动时把心跳超时的活跃 run 判为 blocked —— 上次进程异常退出后不能永久占锁。
class TaskRunStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    static constexpr int kStaleSeconds = 90;

    explicit TaskRunStore(Database *database, QObject *parent = nullptr);

    int revision() const;

    // fields: agent / cwd / workflow / attempt / status（queued|running，默认 running）
    // 成功 {ok:true, runId, status}；同卡已有活跃 run {ok:false, code:"busy", runId, message}
    Q_INVOKABLE QVariantMap acquire(const QString &taskId, const QVariantMap &fields = {});
    Q_INVOKABLE bool heartbeat(const QString &runId);
    // status 只接受终态 passed / blocked / cancelled；
    // detail 可带 exitCode / lastError / logPath / scopeViolation，未提供的列保持原值
    Q_INVOKABLE bool release(const QString &runId, const QString &status,
                             const QVariantMap &detail = {});
    Q_INVOKABLE QVariantMap activeRun(const QString &taskId) const;
    Q_INVOKABLE QVariantMap run(const QString &runId) const;
    Q_INVOKABLE QVariantList runsOf(const QString &taskId, int limit = 20) const;

    // 把心跳早于 now-staleSeconds 的活跃 run 判为 blocked，返回处理条数
    Q_INVOKABLE int recoverOrphans(int staleSeconds = kStaleSeconds);

signals:
    void changed();

private:
    void bump();

    Database *m_database = nullptr;
    int m_revision = 0;
};
