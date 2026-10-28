#include "AppActions.h"

namespace {

constexpr auto kFramePrefix = "NB1 ";
// 一条消息只有动作名 + 命令面板查询串，正常几十字节。上限是防御性的：
// 管道名可猜，对端可以无限写，收端不能跟着无限缓冲。
constexpr int kMaxFrameBytes = 4096;
// 命令面板查询串再长也没有意义，先按字符截断，避免 UTF-8 多字节放大撑爆上限
constexpr int kMaxActionChars = 512;

QString flag(const char *name)
{
    return QString::fromLatin1(name);
}

bool hasFlag(const QStringList &arguments, const char *name)
{
    return arguments.contains(flag(name));
}

} // namespace

namespace AppActions
{

QString fromArguments(const QStringList &arguments)
{
    // palette 带自己的值，必须先单独处理：否则 --palette "foo" 里的 "foo"
    // 会被当成位置参数，落到别的分支上。
    const int paletteIndex = arguments.indexOf(flag("--palette"));
    if (paletteIndex >= 0) {
        QString query;
        if (paletteIndex + 1 < arguments.size()
            && !arguments.at(paletteIndex + 1).startsWith(QLatin1String("--"))) {
            query = arguments.at(paletteIndex + 1);
        }
        return QString::fromLatin1(kPalettePrefix) + query;
    }

    // 卡片详情（R8-03）：--card-detail <taskId>。id 同样可能省略（空串），
    // 这时也照样返回前缀动作，让界面拿到空 id 后自己什么都不做。
    const int cardDetailIndex = arguments.indexOf(flag("--card-detail"));
    if (cardDetailIndex >= 0) {
        QString taskId;
        if (cardDetailIndex + 1 < arguments.size()
            && !arguments.at(cardDetailIndex + 1).startsWith(QLatin1String("--"))) {
            taskId = arguments.at(cardDetailIndex + 1);
        }
        return QString::fromLatin1(kCardDetailPrefix) + taskId;
    }

    if (hasFlag(arguments, "--newtask"))
        return QString::fromLatin1(kNewTask);
    if (hasFlag(arguments, "--settings"))
        return QString::fromLatin1(kSettings);
    if (hasFlag(arguments, "--plugins"))
        return QString::fromLatin1(kPlugins);
    if (hasFlag(arguments, "--appearance"))
        return QString::fromLatin1(kAppearance);
    if (hasFlag(arguments, "--map"))
        return QString::fromLatin1(kMap);
    if (hasFlag(arguments, "--library"))
        return QString::fromLatin1(kLibrary);
    return QString::fromLatin1(kShow);
}

bool isKnown(const QString &action)
{
    // 空串不是合法动作：IPC 解析失败与「没有动作」必须能被区分开
    if (action.isEmpty())
        return false;
    if (action.startsWith(QLatin1String(kPalettePrefix)))
        return true;
    if (action.startsWith(QLatin1String(kCardDetailPrefix)))
        return true;
    return action == QLatin1String(kShow) || action == QLatin1String(kNewTask)
        || action == QLatin1String(kSettings) || action == QLatin1String(kPlugins)
        || action == QLatin1String(kAppearance) || action == QLatin1String(kLibrary)
        || action == QLatin1String(kMap)
        || action == QLatin1String(kNoop);
}

Target targetFor(const QString &action)
{
    if (action == QLatin1String(kNewTask))
        // 新建面板要落到某个分组上，与托盘菜单的「新建任务」保持一致
        return {QStringLiteral("openTaskComposer"), QStringLiteral("todo"), true};

    if (action == QLatin1String(kSettings))
        return {QStringLiteral("openSettings"), QString(), false};
    if (action == QLatin1String(kPlugins))
        return {QStringLiteral("openPlugins"), QString(), false};
    if (action == QLatin1String(kAppearance))
        return {QStringLiteral("openAppearance"), QString(), false};
    if (action == QLatin1String(kMap))
        return {QStringLiteral("openProjectMap"), QString(), false};
    if (action == QLatin1String(kLibrary))
        return {QStringLiteral("openPluginLibrary"), QString(), false};

    const QLatin1String cardDetailPrefix(kCardDetailPrefix);
    if (action.startsWith(cardDetailPrefix)) {
        // id 为空也照样调用：openCardDetail 自己会空 id 直接返回，
        // 这里不替它判——「isKnown 认不认」与「值合不合法」是两件事。
        return {QStringLiteral("openCardDetail"), action.mid(cardDetailPrefix.size()), true};
    }

    const QLatin1String palettePrefix(kPalettePrefix);
    if (action.startsWith(palettePrefix)) {
        const QString query = action.mid(palettePrefix.size());
        // 空查询也要传参：CommandPalette 的 query 参数没有默认值，
        // 无参调用会拿到 undefined 而不是空串
        return {QStringLiteral("openPalette"), query, true};
    }

    return {};
}

QByteArray encodeFrame(const QString &action)
{
    // 单行协议：换行会被当成分帧边界，查询串里带换行必须折叠，
    // 否则后半截会被当成第二条消息（或直接丢弃）。
    QString safe = action.left(kMaxActionChars);
    safe.replace(QLatin1Char('\n'), QLatin1Char(' '));
    safe.replace(QLatin1Char('\r'), QLatin1Char(' '));

    QByteArray frame;
    frame.reserve(32);
    frame.append(kFramePrefix);
    frame.append(safe.toUtf8());
    frame.append('\n');
    return frame;
}

QString decodeFrame(const QByteArray &payload)
{
    if (payload.isEmpty() || payload.size() > kMaxFrameBytes)
        return QString();

    // 只认第一行：调用方可能在同一个连接里连发几条，多余的留给下一次读取
    const int newline = payload.indexOf('\n');
    QByteArray line = newline >= 0 ? payload.left(newline) : payload;
    if (line.endsWith('\r'))
        line.chop(1);

    const QByteArray prefix(kFramePrefix);
    if (!line.startsWith(prefix))
        return QString();

    const QString action = QString::fromUtf8(line.mid(prefix.size())).trimmed();
    if (!isKnown(action))
        return QString();
    return action;
}

} // namespace AppActions
