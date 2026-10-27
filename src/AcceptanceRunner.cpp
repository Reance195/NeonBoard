#include "AcceptanceRunner.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace {

// shell 语法字符：出现任意一个即拒绝整条命令（不经过 shell 是 R6 硬约束，
// 「拒绝」必须发生在解析期而不是执行期——交给 shell 就晚了）。
// 按 docs/r6-dispatcher-plan.md §R6-05 只拦 | > < & ;：反引号与 $ 是 pwsh 脚本
// 里的合法字符（台账 acceptance 自己就带 `-C Release`、`$LASTEXITCODE` 这类写法），
// 拦了会把正常验收项全判死。
bool containsShellSyntax(const QString &text)
{
    static const QString kChars = QStringLiteral("|><&;");
    for (const QChar &ch : text)
        if (kChars.contains(ch))
            return true;
    return false;
}

const QRegularExpression &fileExistsPattern()
{
    // file.exists("path") / file.exists('path') / file.exists(path)
    // 用自定义分隔符 rx：模式里含 `)"` 序列，默认分隔符会提前截断原始字符串
    static const QRegularExpression re(QStringLiteral(
        R"rx(^\s*file\s*\.\s*exists\s*\(\s*(?:"([^"]*)"|'([^']*)'|([^\s)]+))\s*\)\s*$)rx"));
    return re;
}

const QRegularExpression &httpOrJsonPattern()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*(http|json)\s*\.)"), QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString programBaseName(const QString &token)
{
    QString name = token;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const int slash = name.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0)
        name = name.mid(slash + 1);
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0)
        name = name.left(dot);
    return name.toLower();
}

} // namespace

AcceptanceRunner::AcceptanceRunner(QObject *parent)
    : QObject(parent),
      m_allowedPrograms{QStringLiteral("pwsh"),      QStringLiteral("powershell"),
                        QStringLiteral("python"),    QStringLiteral("python3"),
                        QStringLiteral("ctest"),     QStringLiteral("cmake"),
                        QStringLiteral("git"),       QStringLiteral("node"),
                        QStringLiteral("clang-format")}
{}

QStringList AcceptanceRunner::allowedPrograms() const
{
    return m_allowedPrograms;
}

void AcceptanceRunner::setAllowedPrograms(const QStringList &programs)
{
    m_allowedPrograms.clear();
    for (const QString &p : programs) {
        const QString name = programBaseName(p);
        if (!name.isEmpty())
            m_allowedPrograms.append(name);
    }
}

QStringList AcceptanceRunner::tokenize(const QString &command)
{
    QStringList out;
    QString current;
    bool inQuotes = false;
    bool hasToken = false;
    for (const QChar &ch : command) {
        if (ch == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            hasToken = true;
        } else if (!inQuotes && ch.isSpace()) {
            if (hasToken) {
                out.append(current);
                current.clear();
                hasToken = false;
            }
        } else {
            current.append(ch);
            hasToken = true;
        }
    }
    if (hasToken)
        out.append(current);
    return out;
}

QVariantMap AcceptanceRunner::parseItem(const QString &item, const QString &cwd) const
{
    const QString text = item.trimmed();
    QVariantMap out;
    if (text.isEmpty()) {
        out.insert(QStringLiteral("kind"), QStringLiteral("empty"));
        return out;
    }
    if (text.startsWith(QStringLiteral("assert"), Qt::CaseInsensitive)) {
        out.insert(QStringLiteral("kind"), QStringLiteral("assert"));
        out.insert(QStringLiteral("expr"), text.mid(6).trimmed());
        return out;
    }

    out.insert(QStringLiteral("kind"), QStringLiteral("command"));
    if (containsShellSyntax(text)) {
        out.insert(QStringLiteral("rejected"), true);
        out.insert(QStringLiteral("reason"), QStringLiteral("不允许 shell 语法（| > < & ; ` $）"));
        return out;
    }

    const QStringList tokens = tokenize(text);
    if (tokens.isEmpty()) {
        out.insert(QStringLiteral("rejected"), true);
        out.insert(QStringLiteral("reason"), QStringLiteral("命令为空"));
        return out;
    }

    const QString program = tokens.first();
    const QString base = programBaseName(program);
    bool allowed = m_allowedPrograms.contains(base);
    if (!allowed && !cwd.trimmed().isEmpty()) {
        // 白名单外但指向 cwd（仓库）内的真实文件（脚本路径）也放行——
        // 「command 走白名单脚本路径」：脚本本身在受控目录里，argv 直起无 shell。
        const QFileInfo info(QDir(cwd), program);
        allowed = info.exists() && info.isFile();
    }
    if (!allowed) {
        out.insert(QStringLiteral("rejected"), true);
        out.insert(QStringLiteral("reason"),
                   QStringLiteral("程序不在白名单：%1（白名单：%2）")
                       .arg(program, m_allowedPrograms.join(QLatin1Char(','))));
        return out;
    }

    out.insert(QStringLiteral("program"), program);
    out.insert(QStringLiteral("args"), tokens.mid(1));
    return out;
}

QVariantMap AcceptanceRunner::evalAssert(const QString &expr, const QString &cwd) const
{
    QVariantMap out;
    const QString text = expr.trimmed();
    if (text.isEmpty()) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("reason"), QStringLiteral("断言为空"));
        return out;
    }

    const QRegularExpressionMatch m = fileExistsPattern().match(text);
    if (m.hasMatch()) {
        QString path = m.captured(1);
        if (path.isEmpty())
            path = m.captured(2);
        if (path.isEmpty())
            path = m.captured(3);
        const QFileInfo info(cwd.trimmed().isEmpty() ? QDir::current() : QDir(cwd), path);
        const bool exists = info.exists();
        out.insert(QStringLiteral("ok"), exists);
        if (!exists)
            out.insert(QStringLiteral("reason"), QStringLiteral("文件不存在：%1").arg(path));
        return out;
    }

    if (httpOrJsonPattern().match(text).hasMatch()) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("reason"), QStringLiteral("http/json 断言暂不支持（v1 只做 file.exists）"));
        return out;
    }

    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("reason"), QStringLiteral("无法识别的断言：%1").arg(text));
    return out;
}

QVariantMap AcceptanceRunner::run(const QVariantList &acceptance, const QString &cwd,
                                  int timeoutSec) const
{
    QVariantList results;
    int passed = 0;
    int stoppedAt = -1;

    for (int i = 0; i < acceptance.size(); ++i) {
        const QString item = acceptance.at(i).toString();
        QVariantMap entry;
        entry.insert(QStringLiteral("item"), item);
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();

        const QVariantMap parsed = parseItem(item, cwd);
        const QString kind = parsed.value(QStringLiteral("kind")).toString();
        entry.insert(QStringLiteral("kind"), kind);

        if (kind == QStringLiteral("empty")) {
            entry.insert(QStringLiteral("ok"), false);
            entry.insert(QStringLiteral("reason"), QStringLiteral("验收项为空"));
        } else if (kind == QStringLiteral("assert")) {
            const QVariantMap ev =
                evalAssert(parsed.value(QStringLiteral("expr")).toString(), cwd);
            entry.insert(QStringLiteral("ok"), ev.value(QStringLiteral("ok")).toBool());
            if (ev.contains(QStringLiteral("reason")))
                entry.insert(QStringLiteral("reason"), ev.value(QStringLiteral("reason")));
        } else if (parsed.value(QStringLiteral("rejected")).toBool()) {
            entry.insert(QStringLiteral("ok"), false);
            entry.insert(QStringLiteral("reason"), parsed.value(QStringLiteral("reason")));
        } else {
            // command：argv 直起进程，无 shell
            QProcess proc;
            proc.setProgram(parsed.value(QStringLiteral("program")).toString());
            const QStringList args =
                parsed.value(QStringLiteral("args")).toStringList();
            proc.setArguments(args);
            if (!cwd.trimmed().isEmpty())
                proc.setWorkingDirectory(cwd);
            proc.setProcessChannelMode(QProcess::MergedChannels);
            proc.start();
            bool ok = false;
            if (proc.waitForStarted(5000)) {
                ok = proc.waitForFinished(qMax(1, timeoutSec) * 1000);
                if (!ok) {
                    proc.kill();
                    proc.waitForFinished(2000);
                    entry.insert(QStringLiteral("reason"), QStringLiteral("命令执行超时"));
                }
            } else {
                entry.insert(QStringLiteral("reason"),
                             QStringLiteral("命令启动失败：%1").arg(proc.errorString()));
            }
            const int exitCode = ok ? proc.exitCode() : -1;
            entry.insert(QStringLiteral("exitCode"), exitCode);
            const QString output = QString::fromUtf8(proc.readAll()).trimmed();
            // 输出只留末尾 2000 字符，避免长日志灌爆 lastError
            entry.insert(QStringLiteral("output"),
                         output.size() > 2000 ? output.right(2000) : output);
            if (ok)
                entry.insert(QStringLiteral("ok"), exitCode == 0);
            else
                entry.insert(QStringLiteral("ok"), false);
        }

        entry.insert(QStringLiteral("ms"), QDateTime::currentMSecsSinceEpoch() - t0);
        results.append(entry);

        if (entry.value(QStringLiteral("ok")).toBool()) {
            ++passed;
            continue;
        }
        stoppedAt = i; // fail-fast：第一条失败即停
        break;
    }

    QVariantMap out;
    out.insert(QStringLiteral("total"), acceptance.size());
    out.insert(QStringLiteral("passed"), passed);
    out.insert(QStringLiteral("results"), results);
    out.insert(QStringLiteral("ok"), stoppedAt < 0 && passed == acceptance.size());
    if (stoppedAt >= 0) {
        out.insert(QStringLiteral("stoppedAt"), stoppedAt);
        const QVariantMap failed = results.at(stoppedAt).toMap();
        const QString reason = failed.value(QStringLiteral("reason")).toString();
        out.insert(QStringLiteral("lastError"),
                   reason.isEmpty()
                       ? QStringLiteral("第 %1 条验收失败：%2")
                             .arg(stoppedAt + 1)
                             .arg(failed.value(QStringLiteral("item")).toString())
                       : QStringLiteral("第 %1 条验收失败：%2").arg(stoppedAt + 1).arg(reason));
    }
    return out;
}

QVariantMap AcceptanceRunner::verdict(int exitCode, const QVariantMap &acceptanceResult,
                                      const QVariantMap &scopeResult) const
{
    QStringList errors;

    if (exitCode != 0)
        errors.append(exitCode < 0 ? QStringLiteral("进程未能正常退出")
                                   : QStringLiteral("agent 退出码 %1").arg(exitCode));

    if (!acceptanceResult.isEmpty()
        && !acceptanceResult.value(QStringLiteral("ok")).toBool()) {
        const QString reason = acceptanceResult.value(QStringLiteral("lastError")).toString();
        errors.append(reason.isEmpty() ? QStringLiteral("acceptance 未通过") : reason);
    }

    if (!scopeResult.isEmpty() && !scopeResult.value(QStringLiteral("skipped")).toBool()
        && !scopeResult.value(QStringLiteral("ok")).toBool()) {
        const QStringList violations =
            scopeResult.value(QStringLiteral("violations")).toStringList();
        errors.append(QStringLiteral("越界改动：%1").arg(violations.join(QStringLiteral("、"))));
    }

    QVariantMap out;
    if (errors.isEmpty()) {
        out.insert(QStringLiteral("status"), QStringLiteral("passed"));
    } else {
        out.insert(QStringLiteral("status"), QStringLiteral("blocked"));
        out.insert(QStringLiteral("lastError"), errors.join(QStringLiteral("；")));
    }
    return out;
}
