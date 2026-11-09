#include "ScopeMatcher.h"

#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <utility>

namespace {

// glob → 正则：与 scripts/check_scope.py 的 glob_to_regex 保持同一语义
// （`**/` 可零跨层、`**` 任意、`*` 与 `?` 不跨 `/`）。两处必须一起改，
// 否则「CI 说没越界、看板说越界」就会各说各话。
QRegularExpression globToRegex(const QString &pattern)
{
    QString out;
    out.reserve(pattern.size() * 2);
    int i = 0;
    while (i < pattern.size()) {
        if (pattern.mid(i, 3) == QLatin1StringView("**/")) {
            out += QStringLiteral("(?:.*/)?");
            i += 3;
        } else if (pattern.mid(i, 2) == QLatin1StringView("**")) {
            out += QStringLiteral(".*");
            i += 2;
        } else {
            const QChar ch = pattern.at(i);
            if (ch == QLatin1Char('*')) {
                out += QStringLiteral("[^/]*");
            } else if (ch == QLatin1Char('?')) {
                out += QStringLiteral("[^/]");
            } else {
                out += QRegularExpression::escape(QString(ch));
            }
            ++i;
        }
    }
    return QRegularExpression(QStringLiteral("^") + out + QStringLiteral("$"));
}

// git 固定名字，不经过 shell；PATH 找不到 git 时 ok=false，调用方按「跳过」处理。
QStringList runGit(const QString &cwd, const QStringList &args, bool *ok, QString *error)
{
    QProcess proc;
    proc.setProgram(QStringLiteral("git"));
    proc.setArguments(args);
    if (!cwd.trimmed().isEmpty())
        proc.setWorkingDirectory(cwd);
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start();
    if (!proc.waitForStarted(5000)) {
        if (ok)
            *ok = false;
        if (error)
            *error = QStringLiteral("无法启动 git：%1").arg(proc.errorString());
        return {};
    }
    if (!proc.waitForFinished(30000)) {
        proc.kill();
        proc.waitForFinished(2000);
        if (ok)
            *ok = false;
        if (error)
            *error = QStringLiteral("git 执行超时");
        return {};
    }
    if (proc.exitCode() != 0) {
        if (ok)
            *ok = false;
        if (error)
            *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return {};
    }
    if (ok)
        *ok = true;
    if (error)
        error->clear();
    return QString::fromUtf8(proc.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

// porcelain 会把带空格/非 ASCII 的路径用 C 风格引号包起来（core.quotepath 默认开），
// 引号内是 \"、\\ 与 \NNN 八进制转义——必须先反转义成真实字节再按 UTF-8 解码，
// 否则中文路径永远是字面 "\346\226\207..." 串，越界判定对合法改动整体假阳性（DX-138）。
// quotepath=false 时引号内也可能是原始非 ASCII 字节，按 UTF-8 原样保留。
QString unquotePorcelain(const QString &raw)
{
    QString p = raw.trimmed();
    if (p.size() < 2 || !p.startsWith(QLatin1Char('"')) || !p.endsWith(QLatin1Char('"')))
        return p; // 无引号：没有转义，原样返回

    p = p.mid(1, p.size() - 2);
    QByteArray bytes;
    int segStart = 0;
    for (int i = 0; i < p.size(); ++i) {
        if (p.at(i) != QLatin1Char('\\'))
            continue;
        bytes.append(QStringView(p).mid(segStart, i - segStart).toUtf8());
        if (i + 1 >= p.size()) {
            bytes.append('\\');
            segStart = p.size();
            break;
        }
        const QChar next = p.at(i + 1);
        if (next == QLatin1Char('"') || next == QLatin1Char('\\')) {
            bytes.append(next.toLatin1());
            i += 1;
        } else if (next >= QLatin1Char('0') && next <= QLatin1Char('7')) {
            // \NNN 八进制，最多三位
            int value = 0;
            int digits = 0;
            int j = i + 1;
            while (j < p.size() && digits < 3
                   && p.at(j) >= QLatin1Char('0') && p.at(j) <= QLatin1Char('7')) {
                value = value * 8 + (p.at(j).unicode() - '0');
                ++j;
                ++digits;
            }
            bytes.append(static_cast<char>(value));
            i = j - 1;
        } else {
            // 未识别的转义：按 C 语义保留反斜杠与该字符
            bytes.append('\\');
            bytes.append(next.toLatin1());
            i += 1;
        }
        segStart = i + 1;
    }
    bytes.append(QStringView(p).mid(segStart).toUtf8());
    return QString::fromUtf8(bytes);
}

QString normalizePath(const QString &raw)
{
    QString p = unquotePorcelain(raw);
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // 去掉 ./ 前缀，统一成仓库相对路径
    while (p.startsWith(QStringLiteral("./")))
        p = p.mid(2);
    return p;
}

} // namespace

ScopeMatcher::ScopeMatcher(QObject *parent)
    : QObject(parent)
{}

QStringList ScopeMatcher::porcelainStatus(const QString &cwd, bool *ok, QString *error)
{
    // -uall：未跟踪目录展开到文件级。默认 porcelain 只报目录名（`?? src/`），
    // 会让「改了 src/ 下哪个文件」的越界判定失去精度。
    return runGit(cwd,
                  {QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("-uall")},
                  ok, error);
}

QVariantMap ScopeMatcher::captureBaseline(const QString &cwd) const
{
    bool ok = false;
    QString error;
    const QStringList lines = porcelainStatus(cwd, &ok, &error);
    QVariantMap out;
    out.insert(QStringLiteral("ok"), ok);
    if (!ok) {
        out.insert(QStringLiteral("error"), error);
        return out;
    }
    QVariantList list;
    list.reserve(lines.size());
    for (const QString &line : lines)
        list.append(line);
    out.insert(QStringLiteral("lines"), list);
    return out;
}

QString ScopeMatcher::pathFromPorcelain(const QString &line)
{
    // porcelain 格式：XY <空格> 路径；重命名是 `R  old -> new`，取新路径。
    if (line.size() < 4)
        return {};
    QString rest = line.mid(3);
    const int arrow = rest.indexOf(QStringLiteral(" -> "));
    if (arrow >= 0)
        rest = rest.mid(arrow + 4);
    return normalizePath(rest);
}

QVariantList ScopeMatcher::changedPaths(const QString &cwd, const QVariantList &baseline) const
{
    QSet<QString> baseSet;
    for (const QVariant &v : baseline)
        baseSet.insert(v.toString());

    bool ok = false;
    const QStringList now = porcelainStatus(cwd, &ok);
    QVariantList out;
    if (!ok)
        return out;
    QSet<QString> seen;
    for (const QString &line : now) {
        if (baseSet.contains(line))
            continue; // 基线里就有的改动不算本次 run 产生
        const QString path = pathFromPorcelain(line);
        if (path.isEmpty() || seen.contains(path))
            continue;
        seen.insert(path);
        out.append(path);
    }
    return out;
}

bool ScopeMatcher::matches(const QString &path, const QStringList &globs)
{
    const QString p = normalizePath(path);
    if (p.isEmpty())
        return false;
    for (const QString &g : globs) {
        const QString pattern = normalizePath(g);
        if (pattern.isEmpty())
            continue;
        if (globToRegex(pattern).match(p).hasMatch())
            return true;
    }
    return false;
}

QVariantMap ScopeMatcher::check(const QString &cwd, const QVariantList &baseline,
                                const QVariantList &scope) const
{
    QVariantList globs;
    QStringList globStrings;
    for (const QVariant &v : scope) {
        const QString s = v.toString().trimmed();
        if (s.isEmpty())
            continue;
        globs.append(s);
        globStrings.append(s);
    }

    QVariantMap out;
    if (globs.isEmpty()) {
        out.insert(QStringLiteral("ok"), true);
        out.insert(QStringLiteral("skipped"), true);
        out.insert(QStringLiteral("reason"), QStringLiteral("卡片未声明 scope，跳过越界检查"));
        out.insert(QStringLiteral("changed"), QVariantList{});
        out.insert(QStringLiteral("violations"), QVariantList{});
        return out;
    }

    bool ok = false;
    QString error;
    const QStringList now = porcelainStatus(cwd, &ok, &error);
    if (!ok) {
        // DX-138：git 失败按 fail-closed 处理——「跳过检查」会把 .git 被删 /
        // index.lock 残留 / git 超时都当放行，安全边界不能 fail-open。
        // 错误文本放进 violations，让 verdict 侧的「越界改动：」行直接可见。
        const QString reason =
            error.isEmpty() ? QStringLiteral("git 不可用，无法执行越界检查") : error;
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("skipped"), false);
        out.insert(QStringLiteral("reason"), reason);
        out.insert(QStringLiteral("changed"), QVariantList{});
        out.insert(QStringLiteral("violations"), QVariantList{reason});
        return out;
    }

    QSet<QString> baseSet;
    for (const QVariant &v : baseline)
        baseSet.insert(v.toString());
    QSet<QString> nowSet(now.begin(), now.end());

    QVariantList changed;
    QStringList violations;
    QSet<QString> seen;
    for (const QString &line : now) {
        if (baseSet.contains(line))
            continue;
        const QString path = pathFromPorcelain(line);
        if (path.isEmpty() || seen.contains(path))
            continue;
        seen.insert(path);
        changed.append(path);
        if (!matches(path, globStrings))
            violations.append(path);
    }

    // DX-138：双向比对——「基线有、现在没有」同样是 agent 产生的改动
    //（还原用户未提交改动 / 删 untracked 文件），只比单向会漏掉这一半。
    for (const QString &line : baseSet) {
        if (nowSet.contains(line))
            continue;
        const QString path = pathFromPorcelain(line);
        if (path.isEmpty() || seen.contains(path))
            continue;
        seen.insert(path);
        changed.append(path);
        if (!matches(path, globStrings))
            violations.append(path);
    }

    QVariantList violationList;
    for (const QString &v : std::as_const(violations))
        violationList.append(v);

    out.insert(QStringLiteral("ok"), violations.isEmpty());
    out.insert(QStringLiteral("skipped"), false);
    out.insert(QStringLiteral("changed"), changed);
    out.insert(QStringLiteral("violations"), violationList);
    if (!violations.isEmpty())
        out.insert(QStringLiteral("reason"),
                   QStringLiteral("改动越出卡片 scope：%1").arg(violations.join(QStringLiteral("、"))));
    return out;
}
