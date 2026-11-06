#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class Database;
class ProjectStore;
class TaskModel;

// 路线图快照同步：让仓库里的 progress.json 与本地看板互为事实源。
// 合并规则：文件里被外部改过的条目以文件为准（不丢手工改动）；
// 其余条目按 updated_at 较新者胜，避免两边互相覆盖。
//
// 多项目（R18-02）：由单文件升级为「注册表」：
//   - 全局条目盯看板自己的仓库（NEONBOARD_ROADMAP > ini [roadmap]/path >
//     从 exe 向上探测），行为与改动前完全一致（向后兼容）；
//   - 每个绑定项目（project.repo_path）有独立条目，盯 <repo_path>/progress.json，
//     导入后把尚未归属的卡自动归到该项目，写回只写该项目的卡 —— 每张卡
//     恰好有一个「家」文件，避免同一张卡写进两个文件的双写者问题；
//   - 解绑 / 删项目：卡保留，家文件回落到全局文件。
//
// 快照文件只放机器可读的字段；note 等自由文本只留在本地数据库。
// 导入永远幂等：没有变化的重启不产生重复卡或字段被清空。
class RoadmapSync : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY stateChanged)
    Q_PROPERTY(QString path READ path NOTIFY stateChanged)
    Q_PROPERTY(QString lastReport READ lastReport NOTIFY stateChanged)
    Q_PROPERTY(int conflicts READ conflicts NOTIFY stateChanged)

public:
    // 一条「条目」= 一份 progress.json 文件 + 它的同步状态。
    // projectId 为空串 = 全局条目（看板自己的仓库）。
    struct SyncEntry {
        QString path;
        QString projectId;
        QHash<QString, QJsonObject> baseline; // 上次写出的内容；判定「外部改动」用
        qint64 lastMtime = 0;                 // 上次看到的文件 mtime；轮询判定用
        QString report;
        int conflicts = 0;
    };

    explicit RoadmapSync(Database *database, TaskModel *tasks, QObject *parent = nullptr);
    RoadmapSync(Database *database, TaskModel *tasks, ProjectStore *projects, QObject *parent = nullptr);

    bool enabled() const;
    // 全局条目路径（供旧 UI 显示；绑定项目的路径见 ProjectStore::boundRepos）
    QString path() const;
    // 全局条目的最近报告（旧 UI 兼容）
    QString lastReport() const;
    // 所有条目冲突数之和
    int conflicts() const;

    // 路线图文件路径：环境变量 NEONBOARD_ROADMAP > ini [roadmap]/path > 从 exe 目录向上探测（仅全局条目）
    static QString resolvePath();
    // 是否允许把看板改动写回文件（ini [roadmap]/autoExport，默认开）
    static bool autoExportEnabled();

    // 重建条目清单（全局 + 所有绑定项目）。启动与各轮询自动调用，
    // 绑定变化在一个轮询周期（15s）内自动生效。
    void refreshEntries();

    // 单条目导入 / 导出（CLI 与旧调用点保持签名；路径不在清单里按全局范围一次性处理）
    bool importFrom(const QString &filePath, bool force = false);
    bool exportTo(const QString &filePath);
    // 导入 / 导出全部条目（主流程入口）
    bool importAll(bool force = false);
    void exportAll();

    // 定期检查文件是否变化（mtime），有变化就导入
    void poll();

    // 正在导入中（此时不应触发写回，避免自激回路）
    bool isApplying() const;

    // 测试与 CLI 用：覆盖全局条目路径
    void setPath(const QString &filePath);
    // 注入项目门面（main 里 ProjectStore 构造晚于 RoadmapSync，故单独注入）
    void setProjectStore(ProjectStore *projects) { m_projects = projects; }

signals:
    void stateChanged();
    void imported(int applied, int skipped);

private:
    QJsonObject readFile(const QString &filePath, QString *error) const;
    bool writeFile(const QString &filePath, const QJsonObject &root) const;
    void setReport(const QString &text);
    SyncEntry *findEntry(const QString &filePath);
    bool importEntry(SyncEntry &entry, bool force);
    bool exportEntry(SyncEntry &entry);

    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    ProjectStore *m_projects = nullptr; // 可空：空 = 没有项目维度（旧行为）
    QString m_path;                     // 全局条目路径
    bool m_applying = false;
    QList<SyncEntry> m_entries;
};
