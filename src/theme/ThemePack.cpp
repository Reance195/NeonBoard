#include "ThemePack.h"

#include "AppPaths.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

namespace {

// 只认 #RGB / #RRGGBB：命名色（red / transparent）与函数式写法放进来，
// 主题作者与界面读到的就不是同一个东西了，出错时也难解释。
bool isHexColor(const QJsonValue &value, QString *normalized)
{
    if (!value.isString())
        return false;
    const QString text = value.toString().trimmed();
    static const QRegularExpression re(QStringLiteral("^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6})$"));
    if (!re.match(text).hasMatch())
        return false;
    if (normalized)
        *normalized = text.toLower();
    return true;
}

} // namespace

ThemePack::ThemePack(QObject *parent)
    : QObject(parent)
    , m_dir(AppPaths::themeDir())
{
    reload();
}

ThemePack::ThemePack(const QString &dir, QObject *parent)
    : QObject(parent)
    , m_dir(dir)
{
    reload();
}

QStringList ThemePack::requiredTokens()
{
    return { QStringLiteral("bg"),    QStringLiteral("text"),    QStringLiteral("muted"),
             QStringLiteral("cyan"),  QStringLiteral("magenta"), QStringLiteral("violet"),
             QStringLiteral("success"), QStringLiteral("warning"), QStringLiteral("danger") };
}

bool ThemePack::parsePack(const QByteArray &json, QVariantMap *out, QString *reason)
{
    const auto fail = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError)
        return fail(QStringLiteral("JSON 解析失败：%1").arg(error.errorString()));
    if (!doc.isObject())
        return fail(QStringLiteral("顶层必须是对象"));

    const QJsonObject root = doc.object();

    // id 同时是持久化用的键（uiSettings.themePreset）与去重依据，
    // 所以只允许小写字母 / 数字 / 短横线：换成中文或空格，QSettings 里就会变成一团乱码。
    const QString id = root.value(QStringLiteral("id")).toString().trimmed();
    static const QRegularExpression idRe(QStringLiteral("^[a-z0-9][a-z0-9-]{0,31}$"));
    if (!idRe.match(id).hasMatch())
        return fail(QStringLiteral("id 非法（只允许小写字母/数字/短横线，1~32 字符）"));

    const QString name = root.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty())
        return fail(QStringLiteral("缺少 name"));
    if (name.size() > 24)
        return fail(QStringLiteral("name 过长（最多 24 字）"));

    const QJsonValue tokensValue = root.value(QStringLiteral("tokens"));
    if (!tokensValue.isObject())
        return fail(QStringLiteral("缺少 tokens 对象"));
    const QJsonObject tokens = tokensValue.toObject();

    QVariantMap pack;
    for (const QString &token : requiredTokens()) {
        const QJsonValue value = tokens.value(token);
        if (value.isUndefined())
            return fail(QStringLiteral("缺少令牌 %1").arg(token));
        QString color;
        if (!isHexColor(value, &color))
            return fail(QStringLiteral("色值非法 %1（只认 #RGB / #RRGGBB）").arg(token));
        pack.insert(token, color);
    }

    pack.insert(QStringLiteral("key"), id);
    pack.insert(QStringLiteral("name"), name);
    pack.insert(QStringLiteral("desc"),
                root.value(QStringLiteral("desc")).toString().trimmed().left(40));
    pack.insert(QStringLiteral("source"), QStringLiteral("pack"));
    if (out)
        *out = pack;
    return true;
}

QString ThemePack::ensureDir()
{
    QDir().mkpath(m_dir);
    return m_dir;
}

void ThemePack::reload()
{
    m_packs.clear();
    m_skipped.clear();

    const QDir dir(m_dir);
    // 目录不存在是最常见的情况（谁也没装过主题包）：不是错误，直接当 0 个包
    if (!dir.exists()) {
        emit changed();
        return;
    }

    const QStringList files = dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        QFile handle(dir.filePath(file));
        if (!handle.open(QIODevice::ReadOnly)) {
            m_skipped.append(QVariantMap{ { QStringLiteral("file"), file },
                                          { QStringLiteral("reason"), QStringLiteral("读不了这个文件") } });
            continue;
        }

        QVariantMap pack;
        QString reason;
        if (!parsePack(handle.readAll(), &pack, &reason)) {
            m_skipped.append(QVariantMap{ { QStringLiteral("file"), file },
                                          { QStringLiteral("reason"), reason } });
            continue;
        }

        // 撞 key：后装的覆盖先装的（文件名排序决定），但同一批里不允许两份同 key ——
        // 否则用户装了两个「同名主题」，界面上分辨不出哪个是哪个。
        bool replaced = false;
        for (int i = 0; i < m_packs.size(); ++i) {
            if (m_packs.at(i).toMap().value(QStringLiteral("key")) == pack.value(QStringLiteral("key"))) {
                m_packs[i] = pack;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            m_packs.append(pack);
    }

    // 一行汇总就够定位问题：哪个包没好、为什么，都能从日志直接读出来
    if (!m_skipped.isEmpty()) {
        qInfo().noquote() << "ThemePack: 跳过" << m_skipped.size() << "个主题包";
        for (const QVariant &item : m_skipped) {
            const QVariantMap info = item.toMap();
            qInfo().noquote() << "  -" << info.value(QStringLiteral("file")).toString()
                              << info.value(QStringLiteral("reason")).toString();
        }
    }
    emit changed();
}
