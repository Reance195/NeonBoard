#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class Database;
class TaskModel;

// 任务数据的导入导出与自动备份。
// 导出支持 JSON（完整字段）/ Todo.txt / Markdown 三种格式；
// 导入按标题匹配已有任务，存在则更新，不存在则新增；
// 自动备份在每天首次启动时把 board.db 复制一份到 backups 目录。
class DataPort : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString backupDir READ backupDir CONSTANT)
    Q_PROPERTY(QStringList formats READ formats CONSTANT)

public:
    explicit DataPort(Database *database, TaskModel *tasks, QObject *parent = nullptr);

    QString backupDir() const;
    QStringList formats() const;

    // 返回空字符串表示成功，否则返回错误说明
    Q_INVOKABLE QString exportTasks(const QString &path, const QString &format = QStringLiteral("json"));
    Q_INVOKABLE QString importTasks(const QString &path, const QString &format = QStringLiteral("auto"));
    Q_INVOKABLE QString backupNow();
    // 每天只备份一次；当天已有备份则直接返回空串
    Q_INVOKABLE QString backupIfNeeded();
    Q_INVOKABLE QString defaultPath(const QString &format = QStringLiteral("json")) const;
    Q_INVOKABLE QStringList listBackups() const;
    // 数据目录下已导出的文件（绝对路径），供界面选择导入
    Q_INVOKABLE QStringList listExports() const;

signals:
    void finished(bool ok, const QString &message);

private:
    QString buildJson() const;
    QString buildTodo() const;
    QString buildMarkdown() const;

    int importJson(const QByteArray &data, QString *error);
    int importTodo(const QString &text, QString *error);
    int importMarkdown(const QString &text, QString *error);

    // 标题匹配：命中则更新，未命中则新增
    bool applyTask(const QString &title, const QString &note, const QString &status, int priority,
                   const QStringList &tags, qint64 dueAt, qint64 completedAt);

    static QString priorityText(int priority);
    static QStringList parseTags(const QString &raw);

    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
};
