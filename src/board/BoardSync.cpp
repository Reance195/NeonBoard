#include "BoardSync.h"

#include "Database.h"
#include "TaskModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <utility>

namespace {

constexpr char kCodeMetaKey[] = "board.codes";

} // namespace

BoardSync::BoardSync(Database *database, TaskModel *tasks, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_tasks(tasks)
{
}

int BoardSync::revision() const
{
    return m_revision;
}

int BoardSync::activeClaims() const
{
    return m_activeClaims;
}

QVariantMap BoardSync::claimForTask(const QString &taskId) const
{
    if (taskId.isEmpty() || !m_tasks)
        return {};

    QString code = m_codeByTaskId.value(taskId);
    if (code.isEmpty()) {
        const QString title = m_tasks->taskById(taskId).value("title").toString();
        for (const Claim &claim : m_claims) {
            if (title.contains(QLatin1Char('#') + claim.code, Qt::CaseInsensitive)) {
                code = claim.code;
                break;
            }
        }
    }

    const auto it = m_claims.constFind(code);
    if (it == m_claims.constEnd())
        return {};

    QVariantMap out;
    out.insert(QStringLiteral("code"), it->code);
    out.insert(QStringLiteral("agent"), it->agent);
    out.insert(QStringLiteral("branch"), it->branch);
    out.insert(QStringLiteral("active"), it->active);
    out.insert(QStringLiteral("claimedAt"), it->claimedAt);
    return out;
}

QString BoardSync::claimAgentForTask(const QString &taskId) const
{
    return claimForTask(taskId).value(QStringLiteral("agent")).toString();
}

void BoardSync::refresh()
{
    if (m_refreshing)
        return;

    const QString root = findRepoRoot();
    if (root.isEmpty()) {
        if (!m_claims.isEmpty()) {
            m_claims.clear();
            m_activeClaims = 0;
            ++m_revision;
            emit changed();
        }
        return;
    }

    m_anchorRoot = anchorRootFor(root);
    m_pending = collectClaims(m_anchorRoot);
    reloadCodeMap();

    if (m_pending.isEmpty()) {
        m_branchTips.clear();
        m_mergedBranches.clear();
        finishRefresh();
        return;
    }

    m_refreshing = true;
    m_retriedMain = false;
    m_branchProcess = new QProcess(this);
    connect(m_branchProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
                Q_UNUSED(exitCode)
                const QByteArray output = m_branchProcess->readAllStandardOutput();
                m_branchProcess->deleteLater();
                m_branchProcess = nullptr;

                m_branchTips.clear();
                const QList<QByteArray> lines = output.split('\n');
                for (const QByteArray &line : lines) {
                    const QString tip = QString::fromUtf8(line).trimmed();
                    if (!tip.isEmpty())
                        m_branchTips.append(tip);
                }
                startMergedQuery();
            });
    m_branchProcess->start(QStringLiteral("git"),
                           {QStringLiteral("-C"), m_anchorRoot, QStringLiteral("for-each-ref"),
                            QStringLiteral("--format=%(refname:short)"), QStringLiteral("refs/heads/")});
}

void BoardSync::startMergedQuery()
{
    const QString ref = m_retriedMain ? QStringLiteral("main") : QStringLiteral("origin/main");
    m_mergedProcess = new QProcess(this);
    connect(m_mergedProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, ref](int exitCode, QProcess::ExitStatus) {
                const QByteArray output = m_mergedProcess->readAllStandardOutput();
                m_mergedProcess->deleteLater();
                m_mergedProcess = nullptr;

                if (exitCode != 0 && !m_retriedMain) {
                    m_retriedMain = true;
                    startMergedQuery();
                    return;
                }

                m_mergedBranches.clear();
                const QList<QByteArray> lines = output.split('\n');
                for (const QByteArray &line : lines) {
                    const QString branch = QString::fromUtf8(line).trimmed();
                    if (!branch.isEmpty())
                        m_mergedBranches.insert(branch);
                }
                finishRefresh();
            });
    m_mergedProcess->start(QStringLiteral("git"),
                           {QStringLiteral("-C"), m_anchorRoot, QStringLiteral("branch"),
                            QStringLiteral("--merged"), ref, QStringLiteral("--format=%(refname:short)")});
}

void BoardSync::finishRefresh()
{
    m_refreshing = false;

    QHash<QString, Claim> next;
    int active = 0;
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        Claim claim = it.value();
        claim.active = m_branchTips.contains(claim.branch) && !m_mergedBranches.contains(claim.branch);
        if (claim.active)
            ++active;
        next.insert(claim.code, claim);
    }

    m_claims = next;
    m_activeClaims = active;
    applyClaims();

    ++m_revision;
    emit changed();
}

void BoardSync::applyClaims()
{
    if (!m_tasks)
        return;

    for (const Claim &claim : std::as_const(m_claims)) {
        if (!claim.active)
            continue;

        const QString key = claim.code + QLatin1Char('|') + QString::number(claim.claimedAt);
        if (m_applied.contains(key))
            continue;

        const QString taskId = taskIdForCode(claim.code);
        if (taskId.isEmpty())
            continue;

        const QVariantMap task = m_tasks->taskById(taskId);
        if (task.value(QStringLiteral("status")).toString() != QStringLiteral("todo"))
            continue;

        m_tasks->moveTaskToEnd(taskId, QStringLiteral("doing"));
        m_applied.insert(key);
    }
}

QString BoardSync::findRepoRoot() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral(".git"))))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return {};
}

QString BoardSync::anchorRootFor(const QString &root)
{
    const QFileInfo gitInfo(QDir(root).filePath(QStringLiteral(".git")));
    if (!gitInfo.isFile() || gitInfo.size() > 4096)
        return root;

    QFile file(gitInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return root;

    const QString content = QString::fromUtf8(file.readAll()).trimmed();
    file.close();
    if (!content.startsWith(QStringLiteral("gitdir:")))
        return root;

    QDir gitDir(content.mid(7).trimmed());
    for (int i = 0; i < 3; ++i) {
        if (!gitDir.cdUp())
            return root;
    }
    return gitDir.absolutePath();
}

QHash<QString, BoardSync::Claim> BoardSync::collectClaims(const QString &anchorRoot) const
{
    QHash<QString, Claim> out;

    const auto readLedger = [&out](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;

        const QJsonObject claims = QJsonDocument::fromJson(file.readAll())
                                       .object()
                                       .value(QStringLiteral("claims"))
                                       .toObject();
        for (auto it = claims.begin(); it != claims.end(); ++it) {
            const QJsonObject obj = it.value().toObject();
            Claim claim;
            claim.code = it.key();
            claim.agent = obj.value(QStringLiteral("agent")).toString();
            claim.branch = obj.value(QStringLiteral("branch")).toString();
            claim.head = obj.value(QStringLiteral("head")).toString();
            claim.claimedAt = static_cast<qint64>(obj.value(QStringLiteral("claimedAt")).toDouble());
            if (claim.code.isEmpty() || claim.branch.isEmpty())
                continue;

            const auto existing = out.constFind(claim.code);
            if (existing == out.constEnd() || existing->claimedAt < claim.claimedAt)
                out.insert(claim.code, claim);
        }
    };

    readLedger(QDir(anchorRoot).filePath(QStringLiteral("docs/board-ledger.json")));

    const QDir worktrees(QDir(anchorRoot).filePath(QStringLiteral(".worktrees")));
    const QStringList agents = worktrees.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &agent : agents) {
        readLedger(worktrees.filePath(agent
                                      + QStringLiteral("/docs/board-ledger.json")));
    }

    return out;
}

QString BoardSync::taskIdForCode(const QString &code) const
{
    if (!m_tasks || code.isEmpty())
        return {};

    const QString mapped = m_taskIdByCode.value(code);
    if (!mapped.isEmpty() && !m_tasks->taskById(mapped).isEmpty())
        return mapped;

    const QRegularExpression re(
        QStringLiteral("#%1\\b").arg(QRegularExpression::escape(code)),
        QRegularExpression::CaseInsensitiveOption);
    const QVariantList all = m_tasks->allTasks();
    for (const QVariant &entry : all) {
        const QVariantMap task = entry.toMap();
        if (re.match(task.value(QStringLiteral("title")).toString()).hasMatch())
            return task.value(QStringLiteral("id")).toString();
    }
    return {};
}

void BoardSync::reloadCodeMap()
{
    m_taskIdByCode.clear();
    m_codeByTaskId.clear();
    if (!m_database)
        return;

    const QJsonObject obj =
        QJsonDocument::fromJson(m_database->metaValue(QString::fromLatin1(kCodeMetaKey)).toUtf8())
            .object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString taskId = it.key();
        const QString code = it.value().toString();
        if (taskId.isEmpty() || code.isEmpty())
            continue;
        m_taskIdByCode.insert(code, taskId);
        m_codeByTaskId.insert(taskId, code);
    }
}
