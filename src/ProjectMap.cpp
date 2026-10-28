#include "ProjectMap.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTime>
#include <QTimer>
#include <QThreadPool>

#include <algorithm>

namespace {

constexpr int kGitTimeoutMs = 6000;
// 扫最近多少个提交来判定「某目录最近一次被改」。200 个足够覆盖活跃目录，
// 又不会把整条历史读进内存。
constexpr int kRecentCommits = 200;

// 进度宣称 → 结构化等级。顺序敏感：「部分完成 / 基本完成」必须先于「完成」判定。
QString claimLevel(const QString &text)
{
    if (text.contains(QStringLiteral("规划中")) || text.contains(QStringLiteral("未开工"))
        || text.contains(QStringLiteral("待开工")))
        return QStringLiteral("planning");
    if (text.contains(QStringLiteral("部分完成")) || text.contains(QStringLiteral("基本完成"))
        || text.contains(QStringLiteral("核心完成")) || text.contains(QStringLiteral("待做"))
        || text.contains(QStringLiteral("剩余")) || text.contains(QStringLiteral("⬜")))
        return QStringLiteral("partial");
    if (text.contains(QStringLiteral("完成")))
        return QStringLiteral("done");
    return QStringLiteral("unknown");
}

// 去掉 markdown 强调符与首尾空白
QString plain(QString s)
{
    s.remove(QLatin1Char('*'));
    s.remove(QLatin1Char('`'));
    return s.trimmed();
}

int countLines(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray data = f.readAll();
    if (data.isEmpty())
        return 0;
    int lines = static_cast<int>(data.count('\n'));
    if (!data.endsWith('\n'))
        ++lines;
    return lines;
}

// 从可执行文件目录向上探测，找到含 docs/board-ledger.json 的仓库根。
QString findRepoRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("docs/board-ledger.json"))))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return {};
}

QString runGit(const QString &root, const QStringList &args, int timeoutMs = kGitTimeoutMs)
{
    if (root.isEmpty())
        return {};
    QProcess git;
    git.setWorkingDirectory(root);
    git.start(QStringLiteral("git"), args);
    if (!git.waitForStarted(2000))
        return {};
    if (!git.waitForFinished(timeoutMs)) {
        git.kill();
        git.waitForFinished(500);
        return {};
    }
    return QString::fromUtf8(git.readAllStandardOutput());
}

// 用 git 历史里的 #短号=done 标记判定结卡证据。
// 注意：标记制度是 R4 才有的，历史卡不会出现在这里——派生时还要并上台账 baseline。
QSet<QString> scanDoneCodes(const QString &root)
{
    QSet<QString> done;
    const QString out = runGit(root, {QStringLiteral("log"), QStringLiteral("--all"),
                                      QStringLiteral("--pretty=format:%s")});
    static const QRegularExpression re(QStringLiteral("#([A-Za-z0-9]+(?:-[A-Za-z0-9]+)*)=done"),
                                       QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = re.globalMatch(out);
    while (it.hasNext())
        done.insert(it.next().captured(1).toUpper());
    return done;
}

void readLedger(const QString &root, QVariantList *cards, QVariantMap *baseline)
{
    QFile file(QDir(root).filePath(QStringLiteral("docs/board-ledger.json")));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();

    const QJsonObject source = obj.value(QStringLiteral("cards")).toObject();
    for (auto it = source.begin(); it != source.end(); ++it) {
        const QJsonObject c = it.value().toObject();
        QVariantMap m;
        m.insert(QStringLiteral("code"), it.key().toUpper());
        m.insert(QStringLiteral("id"), c.value(QStringLiteral("id")).toString());
        m.insert(QStringLiteral("title"), c.value(QStringLiteral("title")).toString());
        m.insert(QStringLiteral("stage"), c.value(QStringLiteral("stage")).toString().toUpper());
        m.insert(QStringLiteral("note"), c.value(QStringLiteral("note")).toString());
        m.insert(QStringLiteral("acceptance"), c.value(QStringLiteral("acceptance")).toArray().toVariantList());
        m.insert(QStringLiteral("scope"), c.value(QStringLiteral("scope")).toArray().toVariantList());
        m.insert(QStringLiteral("depends"), c.value(QStringLiteral("depends")).toArray().toVariantList());
        cards->append(m);
    }

    const QJsonObject bl = obj.value(QStringLiteral("baseline")).toObject();
    for (auto it = bl.begin(); it != bl.end(); ++it)
        baseline->insert(it.key().toUpper(), it.value().toString());
}

// 解析 iteration-roadmap.md §9 的里程碑表：| 版本 | 主题 | 内容 | 出口标准 | 进度 |
// 表里加一行 R8，面板自动多一行，不需要改任何代码。
QVariantList readRoadmap(const QString &root)
{
    QVariantList rows;
    QFile file(QDir(root).filePath(QStringLiteral("docs/iteration-roadmap.md")));
    if (!file.open(QIODevice::ReadOnly))
        return rows;

    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    bool inSection = false;
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QStringLiteral("## "))) {
            inSection = line.startsWith(QStringLiteral("## 9"));
            continue;
        }
        if (!inSection || !line.startsWith(QLatin1Char('|')))
            continue;

        const QStringList cells = line.split(QLatin1Char('|'));
        if (cells.size() < 6)
            continue;

        // 里程碑行 = 第二列整格加粗（| **R0** | … |）。不写死 "R" 前缀，
        // 这样以后加 DX / OPS 这类非 R 阶段，面板不用改代码就能多一行。
        const QString stageCell = cells.at(1).trimmed();
        if (!stageCell.startsWith(QStringLiteral("**")) || !stageCell.endsWith(QStringLiteral("**")))
            continue; // 表头 / 分隔行 / 说明行

        const QString stage = plain(stageCell).toUpper();
        if (stage.isEmpty())
            continue;

        const QString claimText = plain(cells.at(5));
        QVariantMap m;
        m.insert(QStringLiteral("stage"), stage);
        m.insert(QStringLiteral("topic"), plain(cells.at(2)));
        m.insert(QStringLiteral("goal"), plain(cells.at(3)));
        m.insert(QStringLiteral("exit"), plain(cells.at(4)));
        m.insert(QStringLiteral("claimText"), claimText);
        m.insert(QStringLiteral("claim"), claimLevel(claimText));
        rows.append(m);
    }
    return rows;
}

// docs/project-map.json：链路节点 + 骨架分组的声明源。缺失时退化为空（面板提示，不乱猜）。
void readDeclarations(const QString &root, QVariantList *chain, QVariantList *skeleton)
{
    QFile file(QDir(root).filePath(QStringLiteral("docs/project-map.json")));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    *chain = obj.value(QStringLiteral("chain")).toArray().toVariantList();
    *skeleton = obj.value(QStringLiteral("skeleton")).toArray().toVariantList();
}

// 链路：声明 + 实证。某个脚本被改名/删除，这里立刻变「断链」，不会继续撒谎。
QVariantList buildChainNodes(const QString &root, const QVariantList &decl)
{
    QVariantList nodes;
    for (const QVariant &v : decl) {
        QVariantMap node = v.toMap();
        const QVariantList declared = node.value(QStringLiteral("evidence")).toList();
        QVariantList evidence;
        int missing = 0;
        for (const QVariant &p : declared) {
            const QString rel = p.toString();
            const bool exists = QFileInfo::exists(QDir(root).filePath(rel));
            QVariantMap e;
            e.insert(QStringLiteral("path"), rel);
            e.insert(QStringLiteral("exists"), exists);
            evidence.append(e);
            if (!exists)
                ++missing;
        }
        node.insert(QStringLiteral("evidence"), evidence);
        node.insert(QStringLiteral("missing"), missing);
        node.insert(QStringLiteral("ok"), missing == 0 && !declared.isEmpty());
        nodes.append(node);
    }
    return nodes;
}

struct CommitTouch
{
    QString hash;
    QString subject;
    QString when;
    QStringList paths;
};

// 一次 git log 拿到最近若干提交的「提交 → 改动文件」，用于给每个骨架分组找最近一次改动。
QVector<CommitTouch> readRecentTouches(const QString &root)
{
    const QStringList args = {QStringLiteral("log"), QStringLiteral("--name-only"),
                              QStringLiteral("--max-count=%1").arg(kRecentCommits),
                              QStringLiteral("--pretty=format:%x01%h%x1f%s%x1f%cr")};
    const QString out = runGit(root, args, 8000);

    QVector<CommitTouch> commits;
    const QStringList lines = out.split(QLatin1Char('\n'));
    CommitTouch current;
    bool open = false;
    for (const QString &line : lines) {
        if (line.startsWith(QChar(0x01))) {
            if (open)
                commits.append(current);
            current = CommitTouch();
            const QStringList meta = line.mid(1).split(QChar(0x1f));
            current.hash = meta.value(0);
            current.subject = meta.value(1);
            current.when = meta.value(2);
            open = true;
        } else if (open && !line.trimmed().isEmpty()) {
            current.paths.append(line.trimmed());
        }
    }
    if (open)
        commits.append(current);
    return commits;
}

// 骨架：真实目录扫描。文件数 / 行数 / 最近提交，都是仓库现状，不是文案。
QVariantList buildSkeletonGroups(const QString &root, const QVariantList &decl,
                                 const QVector<CommitTouch> &touches)
{
    QVariantList groups;
    for (const QVariant &v : decl) {
        const QVariantMap meta = v.toMap();
        const QStringList paths = meta.value(QStringLiteral("paths")).toStringList();

        QStringList exts;
        for (const QVariant &e : meta.value(QStringLiteral("ext")).toList())
            exts.append(e.toString().toLower());

        int files = 0, lines = 0;
        QVariantList all;
        for (const QString &rel : paths) {
            const QString abs = QDir(root).filePath(rel);
            if (!QFileInfo::exists(abs))
                continue;
            QDirIterator it(abs, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo fi = it.fileInfo();
                if (!exts.isEmpty() && !exts.contains(QStringLiteral(".") + fi.suffix().toLower()))
                    continue;
                const int loc = countLines(fi.absoluteFilePath());
                ++files;
                lines += loc;
                QVariantMap f;
                f.insert(QStringLiteral("name"),
                         rel + QLatin1Char('/') + QDir(abs).relativeFilePath(fi.absoluteFilePath()));
                f.insert(QStringLiteral("loc"), loc);
                all.append(f);
            }
        }
        std::sort(all.begin(), all.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("loc")).toInt()
                   > b.toMap().value(QStringLiteral("loc")).toInt();
        });
        QVariantList top;
        for (int i = 0; i < all.size() && i < 5; ++i)
            top.append(all.at(i));

        // 最近一次改动：提交按时间倒序，第一个碰过这组路径的就是答案
        QString lastCommit, lastWhen;
        for (const CommitTouch &c : touches) {
            bool hit = false;
            for (const QString &p : c.paths) {
                for (const QString &prefix : paths) {
                    if (p.startsWith(prefix + QLatin1Char('/'))) {
                        hit = true;
                        break;
                    }
                }
                if (hit)
                    break;
            }
            if (hit) {
                lastCommit = c.subject;
                lastWhen = c.when;
                break;
            }
        }
        // 兜底：最近 kRecentCommits 个提交都没碰过这组（冷门目录），单独问一次 git
        if (lastCommit.isEmpty()) {
            QStringList args = {QStringLiteral("log"), QStringLiteral("-1"),
                                QStringLiteral("--pretty=format:%s%x1f%cr"), QStringLiteral("--")};
            args += paths;
            const QString line = runGit(root, args, 4000).trimmed();
            if (!line.isEmpty()) {
                const QStringList parts = line.split(QChar(0x1f));
                lastCommit = parts.value(0);
                lastWhen = parts.value(1);
            }
        }

        QVariantMap group;
        group.insert(QStringLiteral("group"), meta.value(QStringLiteral("group")).toString());
        group.insert(QStringLiteral("desc"), meta.value(QStringLiteral("desc")).toString());
        group.insert(QStringLiteral("paths"), paths);
        group.insert(QStringLiteral("files"), files);
        group.insert(QStringLiteral("loc"), lines);
        group.insert(QStringLiteral("lastCommit"), lastCommit);
        group.insert(QStringLiteral("lastWhen"), lastWhen);
        group.insert(QStringLiteral("top"), top);
        groups.append(group);
    }
    return groups;
}

} // namespace

// §9 的阶段行序（DX-04）。必须定义在匿名命名空间之外 —— 类成员函数不能在
// 匿名命名空间内定义（MSVC C2888）。只读一份 md，不跑 git、不扫目录。
QStringList ProjectMap::stageOrder() const
{
    QStringList order;
    for (const QVariant &v : readRoadmap(findRepoRoot())) {
        const QString stage = v.toMap().value(QStringLiteral("stage")).toString();
        if (!stage.isEmpty() && !order.contains(stage))
            order.append(stage);
    }
    return order;
}

ProjectMap::ProjectMap(QObject *parent) : QObject(parent)
{
    // 刻意不在构造时扫描：启动路径上不做 git 往返与目录遍历，等面板首次打开再 refresh()。
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(700);
    connect(m_debounce, &QTimer::timeout, this, [this] { refresh(); });

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        // 编辑器多是「删旧建新」，路径会掉出监视列表，由 apply() 里的 watchFiles() 补回
        m_debounce->start();
    });
}

QString ProjectMap::sourcePath() const
{
    return m_sourcePath;
}

QString ProjectMap::branch() const
{
    return m_branch;
}

QString ProjectMap::head() const
{
    return m_head;
}

QString ProjectMap::refreshedAt() const
{
    return m_refreshedAt;
}

int ProjectMap::milestoneCount() const
{
    return m_milestones.size();
}

int ProjectMap::driftCount() const
{
    return m_drift.size();
}

int ProjectMap::errorCount() const
{
    int n = 0;
    for (const QVariant &v : m_drift) {
        if (v.toMap().value(QStringLiteral("level")).toString() == QStringLiteral("error"))
            ++n;
    }
    return n;
}

bool ProjectMap::busy() const
{
    return m_busy;
}

int ProjectMap::revision() const
{
    return m_revision;
}

void ProjectMap::refresh()
{
    if (m_busy)
        return; // 连点刷新不叠线程

    m_busy = true;
    emit changed();

    const quint64 token = ++m_token;
    QElapsedTimer cost;
    cost.start();
    // 用全局线程池起一次性任务：不需要 QFuture，也不引入 QtConcurrent 依赖
    QThreadPool::globalInstance()->start([this, token, cost]() mutable {
        Scan scan;
        scan.root = findRepoRoot();
        if (!scan.root.isEmpty()) {
            const QString head = runGit(scan.root, {QStringLiteral("log"), QStringLiteral("-1"),
                                                    QStringLiteral("--pretty=format:%h%x1f%s%x1f%D")},
                                        4000)
                                     .trimmed();
            const QStringList meta = head.split(QChar(0x1f));
            scan.head = QStringLiteral("%1 %2").arg(meta.value(0), meta.value(1)).trimmed();
            static const QRegularExpression headRef(QStringLiteral("HEAD -> ([^,]+)"));
            const QRegularExpressionMatch rm = headRef.match(meta.value(2));
            scan.branch = rm.hasMatch() ? rm.captured(1).trimmed() : QString();

            scan.done = scanDoneCodes(scan.root);

            QVariantList declChain, declSkeleton;
            readLedger(scan.root, &scan.cards, &scan.baseline);
            readDeclarations(scan.root, &declChain, &declSkeleton);
            scan.roadmap = readRoadmap(scan.root);
            scan.chain = buildChainNodes(scan.root, declChain);
            scan.skeleton = buildSkeletonGroups(scan.root, declSkeleton, readRecentTouches(scan.root));
        }

        QMetaObject::invokeMethod(
            this,
            [this, scan, token, cost]() mutable {
                if (token != m_token) {
                    m_busy = false;
                    emit changed();
                    return;
                }
                apply(scan);
                qInfo().nospace() << "[map] 派生完成：里程碑 " << m_milestones.size()
                                  << " / 链路节点 " << m_chain.size() << " / 体检项 "
                                  << m_drift.size() << " / 卡 " << m_cards.size() << "，用时 "
                                  << cost.elapsed() << " ms";
            },
            Qt::QueuedConnection);
    });
}

void ProjectMap::apply(const Scan &scan)
{
    m_sourcePath = scan.root;
    m_branch = scan.branch;
    m_head = scan.head;
    m_cards = scan.cards;
    m_baseline = scan.baseline;
    m_doneCodes = scan.done;
    m_roadmap = scan.roadmap;
    m_chain = scan.chain;
    m_skeleton = scan.skeleton;
    m_refreshedAt = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));

    buildMilestones();
    buildDrift();
    watchFiles();

    m_busy = false;
    ++m_revision;
    emit changed();
}

void ProjectMap::buildMilestones()
{
    m_milestones.clear();

    QHash<QString, QVariantList> byStage;
    for (const QVariant &v : m_cards) {
        const QVariantMap m = v.toMap();
        const QString stage = m.value(QStringLiteral("stage")).toString();
        if (stage.isEmpty())
            continue;
        QVariantList list = byStage.value(stage);
        list.append(m);
        byStage.insert(stage, list);
    }

    auto makeRow = [this, &byStage](const QString &stage, const QVariantMap &meta, bool inRoadmap) {
        const QVariantList cards = byStage.value(stage);
        int done = 0, doing = 0, todo = 0, gitDone = 0;
        QVariantList codes;
        for (const QVariant &v : cards) {
            const QVariantMap m = v.toMap();
            const QString code = m.value(QStringLiteral("code")).toString();
            codes.append(code);
            // 派生优先级：git #短号=done > 台账 baseline。
            // 为什么不能只看 git：#短号=done 是 R4 才立的规矩，之前的卡只留在 baseline 里，
            // 只看 git 会把 R0 这类历史里程碑误判成 0/N。
            const QString st = m_baseline.value(code).toString().toLower();
            if (m_doneCodes.contains(code)) {
                ++done;
                ++gitDone;
            } else if (st == QStringLiteral("done")) {
                ++done;
            } else if (st == QStringLiteral("doing") || st == QStringLiteral("in_progress")) {
                ++doing;
            } else {
                ++todo;
            }
        }

        const QString claim = meta.value(QStringLiteral("claim")).toString();
        QVariantMap agg;
        agg.insert(QStringLiteral("stage"), stage);
        QString topic = meta.value(QStringLiteral("topic")).toString();
        if (topic.isEmpty())
            topic = stage;
        agg.insert(QStringLiteral("topic"), topic);
        agg.insert(QStringLiteral("goal"), meta.value(QStringLiteral("goal")).toString());
        agg.insert(QStringLiteral("exit"), meta.value(QStringLiteral("exit")).toString());
        agg.insert(QStringLiteral("claimText"), meta.value(QStringLiteral("claimText")).toString());
        agg.insert(QStringLiteral("claim"), claim.isEmpty() ? QStringLiteral("unknown") : claim);
        agg.insert(QStringLiteral("inRoadmap"), inRoadmap);
        agg.insert(QStringLiteral("total"), cards.size());
        agg.insert(QStringLiteral("done"), done);
        agg.insert(QStringLiteral("gitDone"), gitDone);
        agg.insert(QStringLiteral("doing"), doing);
        agg.insert(QStringLiteral("todo"), todo);
        agg.insert(QStringLiteral("cards"), codes);

        // 里程碑级漂移：路线图宣称 vs 派生事实
        QString driftText;
        QString driftLevel;
        if (!inRoadmap) {
            driftText = QStringLiteral("台账有 %1 卡，但路线图 §9 无此里程碑").arg(cards.size());
            driftLevel = QStringLiteral("warn");
        } else if (cards.isEmpty()) {
            driftText = QStringLiteral("路线图已声明，台账里还没有卡");
            driftLevel = QStringLiteral("info");
        } else if (claim == QStringLiteral("done") && done < cards.size()) {
            driftText = QStringLiteral("宣称已完成，派生只有 %1/%2").arg(done).arg(cards.size());
            driftLevel = QStringLiteral("warn");
        } else if (claim == QStringLiteral("partial") && done == cards.size()) {
            driftText = QStringLiteral("宣称部分完成，台账内卡片已全绿（剩余项还没登记成卡）");
            driftLevel = QStringLiteral("info");
        } else if (claim == QStringLiteral("planning") && done > 0) {
            driftText = QStringLiteral("宣称规划中，但已有 %1 卡结项").arg(done);
            driftLevel = QStringLiteral("info");
        }
        agg.insert(QStringLiteral("driftText"), driftText);
        agg.insert(QStringLiteral("driftLevel"), driftLevel);
        m_milestones.append(agg);
    };

    // 路线图行序 = 里程碑顺序（R8 加进表里就自动出现，无需改代码）
    QSet<QString> inRoadmap;
    for (const QVariant &v : m_roadmap) {
        const QVariantMap m = v.toMap();
        inRoadmap.insert(m.value(QStringLiteral("stage")).toString());
        makeRow(m.value(QStringLiteral("stage")).toString(), m, true);
    }

    // 台账里出现、路线图没写的阶段：排到末尾并标黄，提示「需求层漏登记」
    QStringList extras;
    for (auto it = byStage.begin(); it != byStage.end(); ++it) {
        if (!inRoadmap.contains(it.key()))
            extras.append(it.key());
    }
    extras.sort();
    for (const QString &stage : extras) {
        QVariantMap empty;
        empty.insert(QStringLiteral("stage"), stage);
        empty.insert(QStringLiteral("topic"), stage);
        makeRow(stage, empty, false);
    }
}

void ProjectMap::buildDrift()
{
    m_drift.clear();

    // 1) 断链：链路节点的证据文件缺失
    for (const QVariant &v : m_chain) {
        const QVariantMap node = v.toMap();
        if (node.value(QStringLiteral("ok")).toBool())
            continue;
        QStringList gone;
        for (const QVariant &e : node.value(QStringLiteral("evidence")).toList()) {
            const QVariantMap em = e.toMap();
            if (!em.value(QStringLiteral("exists")).toBool())
                gone.append(em.value(QStringLiteral("path")).toString());
        }
        QVariantMap item;
        item.insert(QStringLiteral("level"), QStringLiteral("error"));
        item.insert(QStringLiteral("title"),
                    QStringLiteral("断链：%1").arg(node.value(QStringLiteral("title")).toString()));
        item.insert(QStringLiteral("detail"),
                    gone.isEmpty() ? QStringLiteral("节点未声明任何证据文件")
                                   : QStringLiteral("找不到：%1").arg(gone.join(QStringLiteral("、"))));
        m_drift.append(item);
    }

    // 2) 里程碑宣称 vs 派生事实
    for (const QVariant &v : m_milestones) {
        const QVariantMap ms = v.toMap();
        const QString text = ms.value(QStringLiteral("driftText")).toString();
        if (text.isEmpty())
            continue;
        QVariantMap item;
        item.insert(QStringLiteral("level"), ms.value(QStringLiteral("driftLevel")).toString());
        item.insert(QStringLiteral("title"),
                    QStringLiteral("%1 %2").arg(ms.value(QStringLiteral("stage")).toString(),
                                                ms.value(QStringLiteral("topic")).toString()));
        item.insert(QStringLiteral("detail"), text);
        m_drift.append(item);
    }

    // 3) 声明源本身缺失
    if (m_chain.isEmpty()) {
        QVariantMap item;
        item.insert(QStringLiteral("level"), QStringLiteral("error"));
        item.insert(QStringLiteral("title"), QStringLiteral("链路声明为空"));
        item.insert(QStringLiteral("detail"),
                    QStringLiteral("docs/project-map.json 未解析出 chain 节点"));
        m_drift.append(item);
    }
}

void ProjectMap::watchFiles()
{
    if (!m_watcher)
        return;
    const QStringList rels = {QStringLiteral("docs/board-ledger.json"),
                              QStringLiteral("docs/iteration-roadmap.md"),
                              QStringLiteral("docs/project-map.json")};
    QStringList absolute;
    for (const QString &rel : rels) {
        const QString abs = QDir(m_sourcePath).filePath(rel);
        if (QFileInfo::exists(abs))
            absolute.append(abs);
    }
    if (m_watcher->files() == absolute)
        return;
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    if (!absolute.isEmpty())
        m_watcher->addPaths(absolute);
}

void ProjectMap::startWatching()
{
    // 只定路径 + 挂监视，不做派生（DX-08 / #11）。
    // m_sourcePath 原本只在 apply() 里赋值，而 apply() 排在「面板首次打开」之后 ——
    // 从没开过面板的会话因此永远不会 watchFiles()，阶段条收不到任何文件变更信号。
    if (m_sourcePath.isEmpty())
        m_sourcePath = findRepoRoot();
    watchFiles();
}

QVariantList ProjectMap::milestones()
{
    return m_milestones;
}

QVariantList ProjectMap::chain()
{
    return m_chain;
}

QVariantList ProjectMap::skeleton()
{
    return m_skeleton;
}

QVariantList ProjectMap::drift()
{
    return m_drift;
}

QVariantMap ProjectMap::card(const QString &code)
{
    const QString key = code.toUpper();
    for (const QVariant &v : m_cards) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("code")).toString() != key)
            continue;

        const QString base = m_baseline.value(key).toString().toLower();
        const bool gitDone = m_doneCodes.contains(key);
        QString st;
        QString source;
        if (gitDone) {
            st = QStringLiteral("done");
            source = QStringLiteral("git");
        } else if (base == QStringLiteral("done")) {
            st = QStringLiteral("done");
            source = QStringLiteral("ledger");
        } else if (base == QStringLiteral("doing") || base == QStringLiteral("in_progress")) {
            st = QStringLiteral("doing");
            source = QStringLiteral("ledger");
        } else {
            st = QStringLiteral("todo");
            source = base.isEmpty() ? QString() : QStringLiteral("ledger");
        }

        m.insert(QStringLiteral("status"), st);
        m.insert(QStringLiteral("statusSource"), source);
        m.insert(QStringLiteral("baseline"), base);
        // 台账 baseline 陈旧：git 已结卡但种子状态还写 todo
        m.insert(QStringLiteral("staleBaseline"),
                 gitDone && !base.isEmpty() && base != QStringLiteral("done"));
        return m;
    }
    return {};
}
