#pragma once

#include <QByteArray>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class ThemePack;

// 主题包市场（R8-02）：浏览 / 安装 / 卸载。
//
// 三条硬要求决定了它的形状：
//
//   1. **离线也要有东西看**。目录先内置在客户端（三套官方主题），远端索引只是覆盖它。
//      断网时列表照常显示，外加一个「离线」标注 —— 空列表是最糟的失败方式，
//      用户会以为「市场坏了」，而不是「网断了」。
//   2. **安装是原子的**。先写 <id>.json.tmp，再 rename。中途断电只会留下 .tmp，
//      而扫描只认 *.json、reload 还会顺手清掉 .tmp —— 不会出现「半个主题」。
//   3. **卸载要回到内置预设**。删掉包文件后如果当前用的正是它，主题引擎会退回第一套。
class ThemeMarket : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool offline READ offline NOTIFY refreshed)
    Q_PROPERTY(bool loading READ loading NOTIFY refreshed)
    // 每次「目录/已装集合可能变了」都自增；QML 拿它当依赖重算列表
    Q_PROPERTY(int revision READ revision NOTIFY refreshed)

public:
    explicit ThemeMarket(ThemePack *packs, QObject *parent = nullptr);

    // 市场条目：{ key, name, desc, cyan, magenta, violet, bg, text, installed, official }
    Q_INVOKABLE QVariantList catalog() const;
    Q_INVOKABLE int installedCount() const;

    // 拉远端索引。空 url = 不联网，直接进离线态（默认就是这个）。
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setIndexUrl(const QString &url);
    Q_INVOKABLE QString indexUrl() const { return m_indexUrl; }

    // 从文件安装（用户手上有一个 themepack.json 时走这条）
    Q_INVOKABLE QVariantMap installFromFile(const QString &path);
    // 从内容安装（市场下载 / 测试走这条）。fileName 只用来定文件名。
    Q_INVOKABLE QVariantMap installJson(const QByteArray &json, const QString &fileName = QString());
    Q_INVOKABLE bool uninstall(const QString &id);

    bool offline() const { return m_offline; }
    bool loading() const { return m_loading; }
    int revision() const { return m_revision; }

    // 内置的三套官方主题（离线目录）。纯字符串，不依赖网络与文件系统。
    static QVariantList bundledCatalog();

signals:
    void refreshed();

private:
    QVariantMap installBytes(const QByteArray &json, const QString &fileName);
    // 清掉上次安装被打断时留下的 *.json.tmp（扫描只认 *.json，不清也只是堆着）
    void purgeStaleTemps();
    // 唯一的 refreshed 出口：自增 revision 再发信号，QML 的绑定才不会漏更新
    void bump();
    void finishRemote(const QByteArray &payload, bool ok);

    ThemePack *m_packs = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QVariantList m_remote;          // 远端索引里的条目（拉不到就空着）
    QString m_indexUrl;
    bool m_offline = true;
    bool m_loading = false;
    int m_revision = 0;
};
