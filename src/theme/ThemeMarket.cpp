#include "ThemeMarket.h"

#include "AppPaths.h"
#include "ThemePack.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

namespace {

QByteArray officialTheme(const QString &id, const QString &name, const QString &desc,
                         const QString &bg, const QString &text, const QString &muted,
                         const QString &cyan, const QString &magenta, const QString &violet)
{
    QJsonObject tokens;
    tokens.insert(QStringLiteral("bg"), bg);
    tokens.insert(QStringLiteral("text"), text);
    tokens.insert(QStringLiteral("muted"), muted);
    tokens.insert(QStringLiteral("cyan"), cyan);
    tokens.insert(QStringLiteral("magenta"), magenta);
    tokens.insert(QStringLiteral("violet"), violet);
    // 语义色（成功 / 警告 / 危险）走统一的一套：主题作者最容易在这里调出
    // 「删除按钮看着像成功」这种反向语义，官方主题就不给这个自由度了。
    tokens.insert(QStringLiteral("success"), QStringLiteral("#4ADE80"));
    tokens.insert(QStringLiteral("warning"), QStringLiteral("#FCD34D"));
    tokens.insert(QStringLiteral("danger"), QStringLiteral("#FB7185"));

    QJsonObject root;
    root.insert(QStringLiteral("id"), id);
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("desc"), desc);
    root.insert(QStringLiteral("tokens"), tokens);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

ThemeMarket::ThemeMarket(ThemePack *packs, QObject *parent)
    : QObject(parent)
    , m_packs(packs)
{
    // 上次安装中途被打断留下的 .tmp：开机先扫掉，别让「半个主题」堆在目录里
    purgeStaleTemps();
}

void ThemeMarket::bump()
{
    ++m_revision;
    emit refreshed();
}

void ThemeMarket::purgeStaleTemps()
{
    QDir dir(AppPaths::themeDir());
    if (!dir.exists())
        return;
    const QStringList temps = dir.entryList({ QStringLiteral("*.json.tmp") }, QDir::Files);
    for (const QString &name : temps) {
        if (dir.remove(name))
            qInfo().noquote() << "ThemeMarket: 清理未完成的安装" << name;
    }
}

QVariantList ThemeMarket::bundledCatalog()
{
    // 离线目录：三套官方主题。市场列表在断网时也不该是空的。
    const QList<QByteArray> raw = {
        officialTheme(QStringLiteral("ocean"), QStringLiteral("深海蓝"),
                      QStringLiteral("官方 · 静谧的深水"), QStringLiteral("#030A14"),
                      QStringLiteral("#E0F2FE"), QStringLiteral("#6B8399"),
                      QStringLiteral("#38BDF8"), QStringLiteral("#2DD4BF"),
                      QStringLiteral("#6366F1")),
        officialTheme(QStringLiteral("ember"), QStringLiteral("余烬橙"),
                      QStringLiteral("官方 · 黄昏与余温"), QStringLiteral("#140A05"),
                      QStringLiteral("#FFF1E6"), QStringLiteral("#A98874"),
                      QStringLiteral("#FF9F45"), QStringLiteral("#FF6B6B"),
                      QStringLiteral("#FFD166")),
        officialTheme(QStringLiteral("matrix"), QStringLiteral("矩阵绿"),
                      QStringLiteral("官方 · 终端里的雨"), QStringLiteral("#04100A"),
                      QStringLiteral("#DCFFE9"), QStringLiteral("#6B9E7F"),
                      QStringLiteral("#4ADE80"), QStringLiteral("#22D3EE"),
                      QStringLiteral("#A3E635")),
    };

    QVariantList out;
    for (const QByteArray &json : raw) {
        QVariantMap pack;
        QString reason;
        if (!ThemePack::parsePack(json, &pack, &reason)) {
            // 内置目录自己坏了属于打包事故：记一行，别把整个市场拖空
            qWarning().noquote() << "ThemeMarket: 内置目录条目无效" << reason;
            continue;
        }
        pack.insert(QStringLiteral("official"), true);
        out.append(pack);
    }
    return out;
}

QVariantList ThemeMarket::catalog() const
{
    // 顺序：远端（或内置）目录 → 本地已装但目录里没有的包。
    // 后面这类必须出现：用户自己丢进来的包也得能在市场里卸载，否则只能去翻目录。
    QVariantList available = m_remote.isEmpty() ? bundledCatalog() : m_remote;

    QSet<QString> installedKeys;
    if (m_packs) {
        for (const QVariant &item : m_packs->packs())
            installedKeys.insert(item.toMap().value(QStringLiteral("key")).toString());
    }

    QVariantList out;
    QSet<QString> seen;
    for (QVariant item : available) {
        QVariantMap entry = item.toMap();
        const QString key = entry.value(QStringLiteral("key")).toString();
        if (key.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        entry.insert(QStringLiteral("installed"), installedKeys.contains(key));
        if (!entry.contains(QStringLiteral("official")))
            entry.insert(QStringLiteral("official"), false);
        out.append(entry);
    }

    if (m_packs) {
        for (const QVariant &item : m_packs->packs()) {
            const QVariantMap pack = item.toMap();
            const QString key = pack.value(QStringLiteral("key")).toString();
            if (key.isEmpty() || seen.contains(key))
                continue;
            seen.insert(key);
            QVariantMap entry = pack;
            entry.insert(QStringLiteral("installed"), true);
            entry.insert(QStringLiteral("official"), false);
            out.append(entry);
        }
    }
    return out;
}

int ThemeMarket::installedCount() const
{
    return m_packs ? m_packs->count() : 0;
}

void ThemeMarket::setIndexUrl(const QString &url)
{
    m_indexUrl = url;
}

void ThemeMarket::refresh()
{
    if (m_loading)
        return;

    // 没配索引地址 = 纯离线模式：列表仍然有内置目录，只是标上「离线」
    if (m_indexUrl.isEmpty()) {
        m_offline = true;
        bump();
        return;
    }

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    m_loading = true;
    m_offline = false;
    bump();

    QNetworkRequest request{QUrl(m_indexUrl)};
    request.setTransferTimeout(3000);   // 断网时别让界面一直转
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_loading = false;
        if (reply->error() != QNetworkReply::NoError) {
            // 拉不到就退回内置目录并标离线 —— 市场照常可浏览
            qInfo().noquote() << "ThemeMarket: 索引拉取失败，进入离线模式" << reply->errorString();
            m_remote.clear();
            m_offline = true;
            bump();
            return;
        }
        finishRemote(reply->readAll(), true);
    });
}

void ThemeMarket::finishRemote(const QByteArray &payload, bool ok)
{
    if (!ok) {
        m_remote.clear();
        m_offline = true;
        bump();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    QVariantList fresh;
    if (doc.isArray()) {
        for (const QJsonValue &value : doc.array()) {
            QVariantMap pack;
            QString reason;
            // 远端条目走和内建条目同一套校验：远端也不能塞一个不合规的包进来
            if (!ThemePack::parsePack(QJsonDocument(value.toObject()).toJson(), &pack, &reason)) {
                qInfo().noquote() << "ThemeMarket: 跳过索引里的坏条目" << reason;
                continue;
            }
            pack.insert(QStringLiteral("official"), true);
            fresh.append(pack);
        }
    }

    m_remote = fresh;          // 空数组也算成功：远端就是在说「现在没有可装的」
    m_offline = false;
    bump();
}

QVariantMap ThemeMarket::installFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { { QStringLiteral("ok"), false },
                 { QStringLiteral("reason"), QStringLiteral("读不了这个文件") } };
    const QString name = QFileInfo(path).fileName();
    return installBytes(file.readAll(), name);
}

QVariantMap ThemeMarket::installJson(const QByteArray &json, const QString &fileName)
{
    return installBytes(json, fileName);
}

QVariantMap ThemeMarket::installBytes(const QByteArray &json, const QString &fileName)
{
    const auto fail = [](const QString &why) {
        return QVariantMap{ { QStringLiteral("ok"), false }, { QStringLiteral("reason"), why } };
    };

    QVariantMap pack;
    QString reason;
    // 装之前先按同一套规则验一遍：宁可在这里拒绝，也别把一个坏包写进目录 ——
    // 写进去之后它会被每次扫描跳过，用户只看到「装了但没出现」。
    if (!ThemePack::parsePack(json, &pack, &reason))
        return fail(reason == QString() ? QStringLiteral("不是合法的主题包") : reason);

    const QString id = pack.value(QStringLiteral("key")).toString();
    const QDir dir(AppPaths::themeDir());
    if (!QDir().mkpath(dir.path()))
        return fail(QStringLiteral("主题目录不可写"));

    const QString target = dir.filePath(id + QStringLiteral(".json"));
    const QString temp = dir.filePath((fileName.isEmpty() ? id : fileName) + QStringLiteral(".tmp"));

    // 原子替换：写临时文件 → 落盘 → rename。中途断电只会剩 .tmp，
    // 扫描只认 *.json，所以「半个主题」不会出现。
    {
        QSaveFile staging(temp);
        if (!staging.open(QIODevice::WriteOnly))
            return fail(QStringLiteral("写不了临时文件"));
        staging.write(json);
        if (!staging.commit())
            return fail(QStringLiteral("写入失败"));
    }

    // QSaveFile 已经做了「写临时 + rename」，这里再搬一次是为了把文件名
    // 规整成 <id>.json：市场下载下来的文件名五花八门，目录里必须一眼认得出是谁。
    if (temp != target) {
        QFile::remove(target);
        if (!QFile::rename(temp, target)) {
            QFile::remove(temp);
            return fail(QStringLiteral("保存失败"));
        }
    }

    if (m_packs)
        m_packs->reload();

    return { { QStringLiteral("ok"), true },
             { QStringLiteral("key"), id },
             { QStringLiteral("name"), pack.value(QStringLiteral("name")) } };
}

bool ThemeMarket::uninstall(const QString &id)
{
    if (id.isEmpty())
        return false;
    QDir dir(AppPaths::themeDir());
    if (!dir.exists())
        return false;

    // 目录里的文件名与 id 不一定一致（用户可以重命名成任意 .json），
    // 所以按内容找：解析每个包，key 命中才删。
    bool removed = false;
    const QStringList files = dir.entryList({ QStringLiteral("*.json") }, QDir::Files);
    for (const QString &name : files) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QVariantMap pack;
        QString reason;
        if (!ThemePack::parsePack(file.readAll(), &pack, &reason))
            continue;
        file.close();
        if (pack.value(QStringLiteral("key")).toString() != id)
            continue;
        if (dir.remove(name)) {
            removed = true;
            break;
        }
    }

    if (removed && m_packs) {
        m_packs->reload();
        // 卸载的正是当前外观：主题引擎在下一次 applyPresetByKey 找不到 key 时会退回
        // 第一套内置预设（Theme.qml 里那条兜底），这里补一次通知让界面立刻换掉。
        bump();
    }
    return removed;
}
