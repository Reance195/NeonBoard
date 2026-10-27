#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QFileSystemWatcher;
class QTimer;

// 项目全景数据源：让「全景图」跟着真实仓库走，而不是跟着文案走。
//
// 四重事实源：
//   1. docs/iteration-roadmap.md §9  → 里程碑清单 / 主题 / 进度宣称（表里加一行 R8，面板自动多一行）
//   2. docs/board-ledger.json        → 卡片定义（title / acceptance / scope / depends / note）
//   3. git log 的 #短号=done 标记     → 卡片结卡证据；派生优先级 git 标记 > 台账 baseline
//                                      （标记制度是 R4 才有的，只看 git 会把历史卡误判成未完成）
//   4. 文件系统 + docs/project-map.json → 链路节点与代码骨架；声明的证据文件缺一个就「断链」
//
// 任一层与其它层不一致，都会在 drift() 里以 error / warn / info 暴露。
//
// 性能：一次完整派生要跑 3 次 git + 扫 ~180 个文件（本机实测 1.5–2 s），
// 所以 refresh() 在后台线程做快照，再用 apply() 回主线程发布——面板打开不卡界面。
class ProjectMap : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY changed)
    Q_PROPERTY(QString branch READ branch NOTIFY changed)
    Q_PROPERTY(QString head READ head NOTIFY changed)
    Q_PROPERTY(QString refreshedAt READ refreshedAt NOTIFY changed)
    Q_PROPERTY(int milestoneCount READ milestoneCount NOTIFY changed)
    Q_PROPERTY(int driftCount READ driftCount NOTIFY changed)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // 每次发布快照自增；QML 侧绑定它，才能让调用 Q_INVOKABLE 的表达式重新求值
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit ProjectMap(QObject *parent = nullptr);

    QString sourcePath() const;
    QString branch() const;
    QString head() const;
    QString refreshedAt() const;
    int milestoneCount() const;
    int driftCount() const;
    int errorCount() const;
    bool busy() const;
    int revision() const;

    // 里程碑（路线图 §9 声明 × 台账卡片 × 结卡证据）
    Q_INVOKABLE QVariantList milestones();
    // 单卡详情（含 status / statusSource / baseline / staleBaseline）
    Q_INVOKABLE QVariantMap card(const QString &code);
    // 开发链路（docs/project-map.json 声明 + 证据存在性实证）
    Q_INVOKABLE QVariantList chain();
    // 代码骨架（真实目录扫描：文件数 / 行数 / 最近提交）
    Q_INVOKABLE QVariantList skeleton();
    // 一致性体检结果
    Q_INVOKABLE QVariantList drift();

    // 触发一次后台派生；已在派生中则忽略（避免连点刷新叠线程）
    Q_INVOKABLE void refresh();

    // 路线图 §9 的阶段行序（轻量：只读一份 md，不跑 git、不扫文件）。
    // 阶段条要的是「有哪些阶段、按什么顺序」，不值得为它付一次完整派生的代价
    // （1.5–2 s：3 次 git + 扫 ~180 个文件）。解析走的是与派生同一份实现，
    // 所以阶段顺序只有一套事实源，不会出现「面板一个顺序、阶段条另一个顺序」。
    Q_INVOKABLE QStringList stageOrder() const;

signals:
    void changed();

private:
    // 后台线程算完的原始快照；apply() 在主线程消费，避免跨线程读写成员
    struct Scan
    {
        QString root;
        QString branch;
        QString head;
        QVariantList cards;
        QVariantMap baseline;
        QSet<QString> done;
        QVariantList roadmap;
        QVariantList chain;
        QVariantList skeleton;
    };

    void apply(const Scan &scan);
    void buildMilestones();
    void buildDrift();
    void watchFiles();

    QString m_sourcePath;
    QString m_branch;
    QString m_head;
    QString m_refreshedAt;

    QVariantList m_cards;
    QVariantMap m_baseline;
    QSet<QString> m_doneCodes;
    QVariantList m_roadmap; // 路线图 §9 的行序即里程碑顺序

    QVariantList m_milestones;
    QVariantList m_chain;
    QVariantList m_skeleton;
    QVariantList m_drift;

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    quint64 m_token = 0; // 只接受最新一次派生的结果
    int m_revision = 0;
    bool m_busy = false;
};
