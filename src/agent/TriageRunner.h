#pragma once

#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

class QProcess;
class Database;
class AgentProfile;
class TaskExec;
class TaskModel;

// Triage 只读分析（R6-07）：一句话需求 → 结构化执行草案（docs/agent-triage.md）。
//
// 设计要点：
//   1. 强制只读不靠 prompt 约定：用 profile 的 readonlyArgs（--agent plan /
//      --permission-mode plan / --plan）拉起 agent，且分析前后各取一次
//      `git status --porcelain`，有差异直接判失败（只读失效）。
//   2. 输出严格 JSON 校验：缺必填字段 → 重跑一次；仍失败标记 status=draft
//      并附原始输出，人在界面上自己看。
//   3. 自动清洗：questions 截断到 3 条；非法 glob（绝对路径 / 含 .. / 反斜杠 /
//      空串）丢弃；acceptance_draft 为空补默认 `pwsh -File scripts/verify.ps1`。
//   4. executable = confidence >= 0.5：低于 0.5 时 UI 禁止一键执行（先拆分或澄清）。
//   5. 草案只写 board.db 的 meta 表（exec.triage.draft.<taskId>），不进台账、
//      不加表、不升 user_version；「采纳」由 QML 把草案回填到执行页签表单。
class TriageRunner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    TriageRunner(Database *database, AgentProfile *profiles, TaskExec *exec, TaskModel *tasks,
                 QObject *parent = nullptr);
    ~TriageRunner() override;

    int revision() const;

    // 拉起一次只读分析（异步）。缺 agent / cwd / profile 或已有分析在跑时返回
    // {ok:false, message}；成功 {ok:true}，结果走 triageFinished。
    Q_INVOKABLE QVariantMap start(const QString &taskId);

    // 已存草案：{status:"ok"|"draft", draft:{...}, problems[], raw?, updatedAt}；
    // 没有草案返回空 map。
    Q_INVOKABLE QVariantMap draftOf(const QString &taskId) const;
    Q_INVOKABLE bool clearDraft(const QString &taskId);

    // ---- 纯逻辑（静态，供测试与 QML 直接调）----

    // 从 agent 输出里抠出第一个平衡花括号 JSON 对象（输出可能带解释性文字）。
    // 找不到或解析失败返回 isNull() 的 QJsonDocument。
    static QJsonDocument extractJson(const QString &text);

    // scope_draft 的 glob 合法性：非空、相对路径、不含 ..、不含反斜杠与盘符。
    static bool isValidGlob(const QString &glob);

    // 校验 + 清洗草案：
    //   {ok:true, draft:{...清洗后...}, problems:[...]}   必填字段齐全
    //   {ok:false, missing:[...]}                          缺字段（触发重跑）
    // draft 里带 executable（confidence >= 0.5）。
    static QVariantMap validateDraft(const QVariantMap &raw);

    // prompt 模板（docs/agent-triage.md 的通用骨架 + workflow 专项段）。
    static QString buildPrompt(const QString &title, const QString &note, const QString &workflow);

signals:
    // result: {ok, taskId, status:"ok"|"draft"|"readonly-violation"|"failed",
    //          draft?, problems?, raw?, message?}
    void triageFinished(const QString &taskId, const QVariantMap &result);
    void changed();

private:
    struct Session
    {
        QString taskId;
        QProcess *proc = nullptr;
        QStringList before; // 分析前 porcelain 基线
        bool gitBaselineOk = false;
        int attempt = 0; // 0 首跑 / 1 重跑
        QByteArray output;
    };

    void launch(Session &s, const QString &exe, const QStringList &args, const QString &cwd,
                const QString &prompt, bool stdinPrompt, int timeoutSec);
    void finish(Session &s, const QVariantMap &result);
    bool saveDraft(const QString &taskId, const QVariantMap &record);
    void bump();

    Database *m_database = nullptr;
    AgentProfile *m_profiles = nullptr;
    TaskExec *m_exec = nullptr;
    TaskModel *m_tasks = nullptr;
    QList<Session *> m_sessions;
    int m_revision = 0;
};
