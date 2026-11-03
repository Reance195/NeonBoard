#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class Database;

class TaskModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countsChanged)
    Q_PROPERTY(int todoCount READ todoCount NOTIFY countsChanged)
    Q_PROPERTY(int doingCount READ doingCount NOTIFY countsChanged)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY countsChanged)
    Q_PROPERTY(int doneTodayCount READ doneTodayCount NOTIFY countsChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        NoteRole,
        StatusRole,
        ColumnIdRole,
        PriorityRole,
        TagsRole,
        OrderKeyRole,
        DueAtRole,
        CreatedAtRole,
        UpdatedAtRole,
        CompletedAtRole,
        TagRole,
        DueRole
    };
    Q_ENUM(Role)

    explicit TaskModel(Database *database, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int todoCount() const;
    int doingCount() const;
    int doneCount() const;
    int doneTodayCount() const;

    Q_INVOKABLE void load();
    // columnId 非空且有效时以列为准（status 由列定义推导），否则按 status 落默认列。
    Q_INVOKABLE QString addTask(const QString &title,
                                const QString &status = QStringLiteral("todo"),
                                int priority = 1,
                                const QString &tags = QString(),
                                qint64 dueAt = 0,
                                const QString &columnId = QString());
    Q_INVOKABLE bool updateTask(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool deleteTask(const QString &id);
    Q_INVOKABLE QVariantMap removeTask(const QString &id);
    Q_INVOKABLE QString restoreTask(const QVariantMap &task);
    // 撤销辅助：把任务放回指定状态与列内位置（撤销跨列移动用）。
    // columnId 为空或无效时按 status 落默认列 —— 老的撤销记录没有列字段。
    Q_INVOKABLE bool restorePlacement(const QString &id, const QString &status,
                                      qint64 orderKey, qint64 completedAt,
                                      const QString &columnId = QString());
    // 撤销辅助：按快照回写任务字段（撤销编辑用），任务仍存在，走 UPDATE
    Q_INVOKABLE bool restoreSnapshot(const QVariantMap &snapshot);
    Q_INVOKABLE bool moveTask(const QString &id, const QString &status, int newIndex);
    // 按列移动：status 由列定义推导，order_key 在该列内独立取值。
    // 与 moveTask(status) 并存 —— QML 三列仍走 status 版本，动态列走这里。
    Q_INVOKABLE bool moveTaskToColumn(const QString &id, const QString &columnId);
    // 精确插位：把任务挪到目标状态列中 beforeId 这张卡片之前；beforeId 为空表示追加到列尾
    Q_INVOKABLE bool moveTaskBefore(const QString &id, const QString &status, const QString &beforeId);
    // 指定状态下按 order_key 升序的任务 id 列表，供界面计算插入落点（不受搜索过滤影响）
    Q_INVOKABLE QStringList taskIdsInStatus(const QString &status) const;
    // 指定列内按 order_key 升序的任务 id 列表（同状态的多个列各排各的）
    Q_INVOKABLE QStringList taskIdsInColumn(const QString &columnId) const;
    // 列内精确插位：插到 beforeId 这张卡片之前；beforeId 为空/不在本列时落到列尾
    Q_INVOKABLE bool moveTaskBeforeColumn(const QString &id, const QString &columnId,
                                          const QString &beforeId);
    Q_INVOKABLE bool moveTaskToEnd(const QString &id, const QString &status);
    Q_INVOKABLE bool clearDone();
    Q_INVOKABLE QVariantMap taskById(const QString &id) const;
    Q_INVOKABLE QVariantList allTasks() const;
    // 按指定 id 幂等写入（不存在则插入，存在则按字段更新），用于路线图快照同步
    Q_INVOKABLE QString upsertTask(const QString &id, const QVariantMap &fields);
    // 快速录入预览：把输入文本拆成标题/标签/优先级/到期时间，供界面实时提示
    Q_INVOKABLE QVariantMap previewInput(const QString &text) const;
    // 按里程碑标签（整份 tags 里第一个形如 R0/R1/DX 的阶段标签，R10-03 起与顺序无关）
    // 聚合各阶段的任务完成度
    Q_INVOKABLE QVariantMap stageStats() const;
    // 阶段条数据：[{ key, label, total, done, doing, todo }]，按 stageOrder 输出。
    // 与 stageStats() 的区别：stageStats 是「账」，key 有多少算多少；
    // stageList 是「报表」——按调用方给的顺序输出，并把没被列进来的阶段折进「其他」，
    // 保证 其他 = 全部任务数 − 已显示各阶段之和，不再有「算出来却没渲染」的卡。
    //
    // 顺序由调用方传入（界面侧传 projectMap.stageOrder()，即路线图 §9 的行序），
    // 模型层刻意不直接依赖 ProjectMap —— 否则每个链了 TaskModel 的测试二进制都得
    // 把 QProcess / 文件监听一起链进来（分层倒置，链接期就会暴露）。
    Q_INVOKABLE QVariantList stageList(const QStringList &stageOrder) const;

    // —— 标签派生（纯函数，无副作用，不依赖模型实例）——
    // 放在 public 是为了让测试与外部工具直接复用同一份判定：R10-03 的验收要求
    // 「阶段归属与标签顺序无关」，这条规则必须能被单独测，而不是只能靠起一个
    // 数据库 + 模型实例去间接观察。

    // 第一个标签（原样）。导出（DataPort 三格式）与插件快照仍用这个语义，勿改。
    static QString firstTag(const QString &tags);
    // 里程碑标签：在**整份** tags 里找第一个形如 R0/R1/DX/OPS 的阶段标签，与它排第几位无关。
    // R10-03 之前只看 tags[0]，于是把主题排在阶段前面的卡会被判错阶段、只有 ["agent"] 的卡
    // 直接落进「其他」桶 —— 顺序一变分类就变。找不到阶段归 OTHER（由 stageList() 折进兜底桶）。
    static QString stageOf(const QString &tags);
    // 展示用分类：取第一个既不是阶段、也不是角色词（agent 等）的标签。
    // tags 一个数组里混着三种语义（阶段 / 分类 / 角色），卡片只该显示分类；
    // 全被阶段与角色占满时返回空串 —— 「不显示」比错标成 agent 更诚实。
    static QString categoryOf(const QString &tags);
    // 角色词表：表示「谁在执行」而不是「这是什么」，不参与分类展示，也不当阶段。
    static bool isRoleTag(const QString &tag);

signals:
    void countsChanged();
    void taskCreated(const QString &id, const QString &title);
    void taskUpdated(const QString &id);
    void taskMoved(const QString &id, const QString &fromStatus, const QString &toStatus);
    void taskCompleted(const QString &id);
    void taskRemoved(const QString &id);

private:
    struct Task {
        QString id;
        QString title;
        QString note;
        QString status;
        // 卡片所在列（schema v2）。模型侧永远非空：加载时按 status 回落默认列。
        QString columnId;
        int priority = 0;
        QString tags;
        qint64 orderKey = 0;
        qint64 dueAt = 0;
        qint64 createdAt = 0;
        qint64 updatedAt = 0;
        qint64 completedAt = 0;
    };

    int findRow(const QString &id) const;
    qint64 nextOrderKey(const QString &status) const;
    QVector<qint64> orderKeys(const QString &status, const QString &excludeId) const;
    bool renumberStatus(const QString &status);
    // 列内排序键（按 column_id 取，排除自身）
    QVector<qint64> orderKeysInColumn(const QString &columnId, const QString &excludeId) const;
    bool renumberColumn(const QString &columnId);
    // 落库并同步内存副本：状态 / 列 / 排序键 / 完成时间一起写，避免字段撕裂
    bool commitMove(int row, const QString &columnId, const QString &status, qint64 orderKey,
                    qint64 completedAt);
    // 把任务搬到该状态的默认列（状态变更时保持列归属自洽）
    QString columnForStatus(const QString &columnId, const QString &status) const;
    static QString normalizeTags(const QString &tags);
    static QString mergeTags(const QString &baseTags, const QStringList &extraTags);
    static QString dueText(qint64 dueAt);

    Database *m_database = nullptr;
    QVector<Task> m_tasks;
};
