#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

class Database;
class TaskModel;

// 路线图快照同步：把仓库里的 progress.json 与本地看板互为事实源。
// 合并规则：内容被外部改动过的条目一律以文件为准（不丢人工修改），
// 其余条目按 updated_at 大者胜，避免两侧互相覆盖。
class RoadmapSync : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY stateChanged)
    Q_PROPERTY(QString path READ path NOTIFY stateChanged)
    Q_PROPERTY(QString lastReport READ lastReport NOTIFY stateChanged)
    Q_PROPERTY(int conflicts READ conflicts NOTIFY stateChanged)

public:
    RoadmapSync(Database *database, TaskModel *tasks, QObject *parent = nullptr);

    bool enabled() const;
    QString path() const;
    QString lastReport() const;
    int conflicts() const;

    // 路线图文件路径：环境变量 NEONBOARD_ROADMAP > ini [roadmap]/path > 从 exe 目录向上探测仓库根
    static QString resolvePath();
    // 是否允许把看板改动写回文件（ini [roadmap]/autoExport，默认开）
    static bool autoExportEnabled();

    // 从文件导入（幂等）。force = true 时一律以文件为准
    bool importFrom(const QString &filePath, bool force = false);
    // 导出到文件（与文件既有内容合并，外部改动原样保留）
    bool exportTo(const QString &filePath);
    // 定时检查文件是否变化，变了就导入
    void poll();

    // 正在执行导入（此时不应触发写回，避免自激循环）
    bool isApplying() const;

    void setPath(const QString &filePath);

signals:
    void stateChanged();
    void imported(int applied, int skipped);

private:
    QJsonObject readFile(const QString &filePath, QString *error) const;
    bool writeFile(const QString &filePath, const QJsonObject &root) const;
    void setReport(const QString &text);

    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    QString m_path;
    QString m_lastReport;
    int m_conflicts = 0;
    qint64 m_lastMtime = 0;
    // 上次写回/读入的文件内容，作为「是否被外部改动」的判定基准
    QHash<QString, QJsonObject> m_baseline;
    bool m_applying = false;
};
