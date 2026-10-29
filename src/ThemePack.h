#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// 外部主题包（R8-01）。
//
// 主题包是**数据不是代码**：一个 json + 九个颜色令牌，没有脚本、没有字体、
// 没有着色器。这是硬边界 —— 主题是一堆陌生人会互相传的文件，能执行东西就意味着
// 装一个主题等于跑一段任意代码。
//
// 扫描是「全有或全无」的逐包判定：某个包坏了就跳过它并记下原因，别的包照常加载，
// 应用照常启动。坏一个包把整个主题系统拖死，是这类功能最常见的翻车方式。
class ThemePack : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int skippedCount READ skippedCount NOTIFY changed)

public:
    explicit ThemePack(QObject *parent = nullptr);
    // 显式目录（测试 / 未来的「换一个主题目录」开关）
    explicit ThemePack(const QString &dir, QObject *parent = nullptr);

    // 重新扫描目录。不会抛、不会阻塞：读不了就当作 0 个包。
    Q_INVOKABLE void reload();

    // 与 Theme.presets 同构：{ key, name, desc, bg, text, muted, cyan, magenta,
    //                          violet, success, warning, danger, source }
    Q_INVOKABLE QVariantList packs() const { return m_packs; }
    // 被跳过的包：[{ file, reason }]，界面可以据此提示「哪个包装不上、为什么」
    Q_INVOKABLE QVariantList skipped() const { return m_skipped; }
    Q_INVOKABLE QString dir() const { return m_dir; }
    // 建目录并返回路径：「打开主题目录」按钮要用，用户第一次点的时候目录通常还不存在
    Q_INVOKABLE QString ensureDir();
    int count() const { return m_packs.size(); }
    int skippedCount() const { return m_skipped.size(); }

    // 纯函数：把一段 json 解析成预设。成功返回 true 并写 out；
    // 失败返回 false 并把原因写进 reason（给用户看的短句，不是异常栈）。
    static bool parsePack(const QByteArray &json, QVariantMap *out, QString *reason);

    // 必备的九个颜色令牌。少一个就不成一套主题：缺 bg 的地方会退回写死的深色，
    // 结果就是「装了主题但背景没变」这种半生效状态。
    static QStringList requiredTokens();

signals:
    void changed();

private:
    QString m_dir;
    QVariantList m_packs;
    QVariantList m_skipped;
};
