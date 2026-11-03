#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Database;

class PluginLoader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY pluginsChanged)
    Q_PROPERTY(QString pluginDir READ pluginDir CONSTANT)
    Q_PROPERTY(QStringList disabledPlugins READ disabledPlugins NOTIFY pluginsChanged)

public:
    static constexpr int MinimumApiVersion = 1;
    static constexpr int MaximumApiVersion = 2;

    explicit PluginLoader(Database *database = nullptr, QObject *parent = nullptr);

    /**
     * 来源标签是否属于「随分发管理」的官方来源。
     *
     * `appdir`（程序安装目录）与 `official`（源码树 `plugins/`）随分发包一起走，
     * 其权限已由分发方审核过 —— 用户装完就该能用，不该再让「没点过授权弹窗」
     * 变成「插件装了没生效」。`appdata`（用户自装）是第三方，必须显式授权；
     * `sample`（示例）同理：示例插件是给人看的模板，不承担「装好即用」的期待。
     *
     * R14-01：这四个标签由 `scan()` 在扫描时打上（`appdata` / `appdir` /
     * `official` / `sample`），此处只做语义判定，不参与目录解析。
     */
    static bool isOfficialPluginSource(const QString &source);

    /**
     * 计算需要补齐的官方插件授权（R14-01）。
     *
     * 只处理「官方来源 + 状态 ok + 声明了权限」的插件，并且：
     *   - 已在 `grants` 里的权限保持不动（幂等，可反复调用）；
     *   - `revoked` 里记录的权限不补 —— 那是用户显式撤销过的，自动补回等于撤销无效。
     *
     * @param plugins `plugins()` 的条目（含 `id` / `source` / `status` / `permissions`）
     * @param grants  现有授权表
     * @param revoked 用户显式撤销记录（结构同 `grants`）
     * @return 仅包含**发生变化**的 pluginId → 补齐后的权限列表；无变化则为空，
     *         调用方可据此跳过写库。返回值的权限列表是**完整替换值**，不是增量。
     */
    static QHash<QString, QStringList> officialAutoGrants(
        const QVariantList &plugins, const QHash<QString, QStringList> &grants,
        const QHash<QString, QStringList> &revoked);

    QVariantList plugins() const { return m_plugins; }
    /** 扫描到的全部插件条目（含被同 id 覆盖掉的），供插件库区分「已装」与「可安装」 */
    QVariantList candidates() const { return m_candidates; }
    QString pluginDir() const { return m_pluginDir; }
    /** 四个插件源目录（用户安装区 / 程序目录 / 官方源 / 示例源），供热插拔监视使用 */
    QStringList sourceDirs() const;
    QStringList disabledPlugins() const;

    Q_INVOKABLE void scan();
    Q_INVOKABLE bool openPluginDir();
    Q_INVOKABLE QVariantMap pluginById(const QString &id) const;
    Q_INVOKABLE bool setPluginEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool isPluginEnabled(const QString &id) const;

signals:
    void pluginsChanged();

private:
    QStringList readDisabledList() const;
    bool writeDisabledList(const QStringList &ids) const;

    Database *m_database = nullptr;
    QVariantList m_plugins;
    QVariantList m_candidates;
    QString m_pluginDir;
};
