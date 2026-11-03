#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Database;

// 项目维度门面（R10-01）。
//
// 设计要点（与 task_link / task_extra / task_exec 同一先例）：
//   1. 项目与归属存在独立侧表 project / task_project，不往 task 表加列、
//      不升 user_version —— 老版本程序照常能开库。
//   2. 一卡一项目：task_project 以 task_id 为主键，改归属是 REPLACE 而非追加，
//      天然不会留下旧行。未归属的卡在表里没有行，查不到即「未归属」。
//   3. 删除项目只清归属、不级联删卡 —— 卡片是人的数据，项目只是它的一层分类。
//   4. 侧表读写只走本门面；QML 侧经上下文属性 projectStore 调用。
//
// 这一层解决的是「看板分不出这是哪个项目的事」：R4-01 的事件桥不区分来源项目，
// 别的项目的卡会直接落进同一张 task 表。
class ProjectStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit ProjectStore(Database *database, QObject *parent = nullptr);

    int revision() const;

    // 项目清单，按 order_key 升序：[{ id, name, color, orderKey, taskCount }]
    Q_INVOKABLE QVariantList projects() const;
    // 空名字返回空串（不建）；否则返回新项目 id
    Q_INVOKABLE QString createProject(const QString &name, const QString &color = QString());
    Q_INVOKABLE bool renameProject(const QString &id, const QString &name);
    Q_INVOKABLE bool setProjectColor(const QString &id, const QString &color);
    // 只清归属，不删卡
    Q_INVOKABLE bool removeProject(const QString &id);

    // 按名字取项目 id，没有就以该名字建一个（幂等）。批量归类脚本与首启种子用。
    Q_INVOKABLE QString ensureProject(const QString &name, const QString &color = QString());

    // 归属读写：projectId 传空串等于解除归属
    Q_INVOKABLE QString projectOfTask(const QString &taskId) const;
    Q_INVOKABLE bool assignTask(const QString &taskId, const QString &projectId);
    Q_INVOKABLE int taskCount(const QString &projectId) const;
    // 未归属的卡片数（task 表里有、task_project 里没有）
    Q_INVOKABLE int unassignedCount() const;

    // 某项目下的全部卡片 id（一次查询）。视图过滤用：TaskFilterModel 在范围切换时
    // 取一次存成集合，之后逐行判定只查内存 —— 别在 filterAcceptsRow 里逐卡查库，
    // 那是 O(卡片数 × 每次过滤)，滚动一下就是几百次 SQL。
    QStringList taskIdsOf(const QString &projectId) const;
    // 卡片角标用：{ id, name, color }；未归属返回空 map。
    // 带内存缓存（随 changed() 失效），因为卡片 delegate 会在重绘时反复问同一个任务。
    Q_INVOKABLE QVariantMap taskProject(const QString &taskId) const;

    // 任务被删时清掉归属行（由使用方订阅 TaskModel::taskRemoved 后调用）
    void forgetTask(const QString &taskId);

signals:
    void changed();

private:
    int nextOrderKey() const;
    void bump();

    Database *m_database = nullptr;
    int m_revision = 0;
    // mutable：查询是 const 的，但缓存要写。失效点在 bump()（任何归属变更都走它）。
    mutable QHash<QString, QVariantMap> m_taskProjectCache;
};
