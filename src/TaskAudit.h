#pragma once

#include <QObject>
#include <QVariantList>

class Database;
class TaskModel;

// 卡片事件审计（R8-04）。
//
// 详情抽屉里那条「这张卡经历过什么」的时间线：创建 / 移动 / 完成 / 恢复 / 删除。
// 数据落在侧表 task_event（Database::open 里建，不升 user_version）。
//
// 刻意**不记「编辑」**：updateTask 每次移动都会发一遍 taskUpdated，
// 记进去的话半屏都是「编辑了卡片」，真正想看的生命周期被淹掉；
// 而「谁在什么时候改了备注」靠 updated_at 与 Ctrl+Z 也够了。
//
// 也刻意**不随卡删除**：卡没了，事件留着 —— 审计的意义就在于证明「它存在过」。
class TaskAudit : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    static constexpr int kMaxEventsPerTask = 200;

    TaskAudit(Database *db, TaskModel *tasks, QObject *parent = nullptr);

    // 倒序（最新在前）。limit <= 0 表示不限。
    Q_INVOKABLE QVariantList eventsOf(const QString &taskId, int limit = 50) const;
    Q_INVOKABLE int countOf(const QString &taskId) const;
    // 事件种类 → 中文标签。未知种类原样返回，新增种类不必改界面。
    Q_INVOKABLE QString labelOf(const QString &kind) const;
    // 手动记一条（插件 / 将来的「编辑」事件用得上）。空 taskId 直接拒绝。
    Q_INVOKABLE bool record(const QString &taskId, const QString &kind,
                            const QString &detail = QString());

    int revision() const { return m_revision; }

signals:
    void changed();

private:
    void trim(const QString &taskId);

    Database *m_db = nullptr;
    int m_revision = 0;
};
