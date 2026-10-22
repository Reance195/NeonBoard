#include "PreviewHub.h"

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

namespace {

const char *kRepoRootVar = "NEONBOARD_REPO_ROOT";
const char *kAgentIdVar = "NEONBOARD_AGENT_ID";
const char *kCmakeVar = "NEONBOARD_CMAKE";
const char *kQtPrefixVar = "NEONBOARD_QT_PREFIX";

QString runCommand(const QString &program, const QStringList &args, const QString &cwd, bool *ok)
{
    if (ok)
        *ok = false;
    QProcess proc;
    if (!cwd.isEmpty())
        proc.setWorkingDirectory(cwd);
    proc.start(program, args);
    if (!proc.waitForStarted(8000))
        return QString();
    if (!proc.waitForFinished(20000)) {
        proc.kill();
        proc.waitForFinished(2000);
        return QString();
    }
    if (ok)
        *ok = (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0);
    return QString::fromUtf8(proc.readAllStandardOutput());
}

// 向上找到第一个真实存在的目录：build/Release 可能还没建出来
QString existingAncestor(const QString &base)
{
    if (base.isEmpty())
        return QString();
    QDir dir(base);
    for (int i = 0; i < 8; ++i) {
        if (dir.exists())
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return QString();
}

bool processAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!handle)
        return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
#else
    return QFile::exists(QStringLiteral("/proc/%1").arg(pid));
#endif
}

} // namespace

PreviewHub::PreviewHub(QObject *parent)
    : QObject(parent)
{
    m_instanceLabel = qEnvironmentVariable(kAgentIdVar).trimmed();
    m_selfId = m_instanceLabel.isEmpty() ? QStringLiteral("main") : m_instanceLabel;

    m_timer = new QTimer(this);
    m_timer->setInterval(10000);
    connect(m_timer, &QTimer::timeout, this, &PreviewHub::refresh);

    // 只有主线实例做扫描：预览实例自己也在跑，再扫一遍既浪费又会和主线抢写运行时文件
    if (hostInstance()) {
        m_timer->start();
        // 首次扫描延后：构造期同步跑 git 会拖慢启动，交给事件循环空闲时再做
        QTimer::singleShot(200, this, &PreviewHub::refresh);
    }
}

PreviewHub::~PreviewHub()
{
    for (QProcess *proc : m_instances)
        proc->kill();
}

QVariantList PreviewHub::agents() const
{
    QVariantList list;
    list.reserve(m_agents.size());
    for (const AgentInfo &info : m_agents) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), info.id);
        item.insert(QStringLiteral("path"), info.path);
        item.insert(QStringLiteral("branch"), info.branch);
        item.insert(QStringLiteral("head"), info.head);
        item.insert(QStringLiteral("subject"), info.subject);
        item.insert(QStringLiteral("dirty"), info.dirty);
        item.insert(QStringLiteral("detached"), info.detached);
        item.insert(QStringLiteral("built"), info.built);
        item.insert(QStringLiteral("builtAt"), info.builtAt);
        item.insert(QStringLiteral("exe"), info.exe);
        item.insert(QStringLiteral("running"), info.running);
        item.insert(QStringLiteral("pid"), info.pid);
        item.insert(QStringLiteral("current"), info.current);
        list.append(item);
    }
    return list;
}

QString PreviewHub::statusText() const
{
    return m_status;
}

bool PreviewHub::available() const
{
    return !m_anchorRoot.isEmpty();
}

QString PreviewHub::instanceLabel() const
{
    if (m_instanceLabel.isEmpty())
        return QStringLiteral("NeonBoard");
    return QStringLiteral("NeonBoard · %1").arg(m_instanceLabel);
}

bool PreviewHub::hostInstance() const
{
    return m_instanceLabel.isEmpty();
}

QString PreviewHub::anchorRoot() const
{
    if (!m_anchorRoot.isEmpty())
        return m_anchorRoot;

    const QString env = qEnvironmentVariable(kRepoRootVar).trimmed();
    if (!env.isEmpty() && QDir(env).exists())
        return QDir::fromNativeSeparators(QDir(env).absolutePath());

    // 已在仓库里：git-common-dir 的上级就是锚点根目录（worktree 的 .git 是文件）
    const QString common = commonGitDir();
    if (!common.isEmpty()) {
        QDir dir(common);
        if (dir.dirName() == QLatin1String(".git") && dir.cdUp())
            return QDir::fromNativeSeparators(dir.absolutePath());
    }

    // 回退：从可执行文件位置 / 当前目录向上找同时含 .git 与 progress.json 的目录
    const QStringList bases = {QCoreApplication::applicationDirPath(), QDir::currentPath()};
    for (const QString &base : bases) {
        QDir dir(base);
        for (int i = 0; i < 6; ++i) {
            if (dir.exists(QStringLiteral(".git")) && dir.exists(QStringLiteral("progress.json")))
                return QDir::fromNativeSeparators(dir.absolutePath());
            if (!dir.cdUp())
                break;
        }
    }
    return QString();
}

QString PreviewHub::commonGitDir() const
{
    const QStringList bases = {QCoreApplication::applicationDirPath(), QDir::currentPath()};
    for (const QString &base : bases) {
        const QString dir = existingAncestor(base);
        if (dir.isEmpty())
            continue;
        bool ok = false;
        const QString out = runCommand(QStringLiteral("git"),
                                      {QStringLiteral("rev-parse"),
                                       QStringLiteral("--path-format=absolute"),
                                       QStringLiteral("--git-common-dir")},
                                      dir, &ok);
        if (ok && !out.trimmed().isEmpty())
            return QDir::fromNativeSeparators(out.trimmed());
    }
    return QString();
}

QString PreviewHub::runtimeFilePath() const
{
    const QString common = commonGitDir();
    if (common.isEmpty())
        return QString();
    return common + QStringLiteral("/preview-runtime.json");
}

QString PreviewHub::agentIdFor(const QString &path) const
{
    const QString rel = QDir(m_anchorRoot).relativeFilePath(path);
    if (rel.isEmpty() || rel == QStringLiteral("."))
        return QStringLiteral("main");
    if (rel.startsWith(QStringLiteral(".worktrees/"))) {
        const QString rest = rel.mid(QStringLiteral(".worktrees/").size());
        const QString name = rest.section(QLatin1Char('/'), 0, 0);
        return name.isEmpty() ? QString() : name;
    }
    return QDir(path).dirName();
}

void PreviewHub::scan()
{
    if (m_scanning)
        return;
    m_scanning = true;

    m_anchorRoot = anchorRoot();
    if (m_anchorRoot.isEmpty()) {
        m_agents.clear();
        setStatus(QStringLiteral("未定位到仓库根目录（可设 NEONBOARD_REPO_ROOT 指定）"));
        m_scanning = false;
        emit agentsChanged();
        return;
    }

    bool ok = false;
    const QString out = runCommand(QStringLiteral("git"),
                                  {QStringLiteral("worktree"), QStringLiteral("list"), QStringLiteral("--porcelain")},
                                  m_anchorRoot, &ok);
    if (!ok) {
        setStatus(QStringLiteral("读取 worktree 列表失败"));
        m_scanning = false;
        return;
    }

    struct Raw {
        QString path;
        QString branch;
        bool detached = false;
    };
    QVector<Raw> raws;
    Raw current;
    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            if (!current.path.isEmpty())
                raws.append(current);
            current = Raw();
            continue;
        }
        if (line.startsWith(QStringLiteral("worktree "))) {
            current.path = QDir::fromNativeSeparators(line.mid(9).trimmed());
        } else if (line.startsWith(QStringLiteral("branch "))) {
            QString ref = line.mid(7).trimmed();
            if (ref.startsWith(QStringLiteral("refs/heads/")))
                ref = ref.mid(QStringLiteral("refs/heads/").size());
            current.branch = ref;
        } else if (line == QStringLiteral("detached")) {
            current.detached = true;
        }
    }
    if (!current.path.isEmpty())
        raws.append(current);

    QVector<AgentInfo> next;
    next.reserve(raws.size());
    for (const Raw &raw : raws) {
        const QString id = agentIdFor(raw.path);
        if (id.isEmpty())
            continue;
        AgentInfo info = makeAgent(raw.path, id);
        if (!raw.branch.isEmpty())
            info.branch = raw.branch;
        if (raw.detached)
            info.detached = true;
        detectBuild(&info);
        info.current = (info.id == m_selfId);
        next.append(info);
    }
    m_agents = next;

    loadRuntime();
    for (AgentInfo &info : m_agents) {
        const auto it = m_runtime.constFind(info.id);
        if (it != m_runtime.constEnd() && isAlive(it->pid)) {
            info.running = true;
            info.pid = it->pid;
        }
    }
    saveRuntime();

    int running = 0;
    int built = 0;
    for (const AgentInfo &info : m_agents) {
        if (info.running)
            ++running;
        if (info.built)
            ++built;
    }
    setStatus(QStringLiteral("%1 个工作区 · %2 个已构建 · %3 个运行中")
                  .arg(m_agents.size())
                  .arg(built)
                  .arg(running));

    m_scanning = false;
    emit agentsChanged();
}

PreviewHub::AgentInfo PreviewHub::makeAgent(const QString &path, const QString &id) const
{
    AgentInfo info;
    info.id = id;
    info.path = path;

    bool ok = false;
    const QString status = runCommand(QStringLiteral("git"),
                                      {QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
                                       QStringLiteral("--branch")},
                                      path, &ok);
    if (ok) {
        const QStringList lines = status.split(QLatin1Char('\n'));
        for (const QString &rawLine : lines) {
            const QString line = rawLine.trimmed();
            if (line.startsWith(QStringLiteral("# branch.oid "))) {
                info.head = line.mid(QStringLiteral("# branch.oid ").size()).left(7);
            } else if (line.startsWith(QStringLiteral("# branch.head "))) {
                const QString head = line.mid(QStringLiteral("# branch.head ").size()).trimmed();
                if (head == QStringLiteral("(detached)"))
                    info.detached = true;
                else if (!head.isEmpty())
                    info.branch = head;
            } else if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))) {
                info.dirty = true;
            }
        }
    }
    if (info.head.isEmpty()) {
        bool headOk = false;
        const QString head = runCommand(QStringLiteral("git"),
                                        {QStringLiteral("rev-parse"), QStringLiteral("--short"), QStringLiteral("HEAD")},
                                        path, &headOk);
        if (headOk)
            info.head = head.trimmed();
    }
    bool logOk = false;
    const QString subject = runCommand(QStringLiteral("git"),
                                       {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--pretty=%s")},
                                       path, &logOk);
    if (logOk)
        info.subject = subject.trimmed();
    return info;
}

void PreviewHub::detectBuild(AgentInfo *info) const
{
    const QStringList candidates = {info->path + QStringLiteral("/build/Release/NeonBoard.exe"),
                                     info->path + QStringLiteral("/build/NeonBoard.exe")};
    for (const QString &candidate : candidates) {
        const QFileInfo fi(candidate);
        if (fi.exists()) {
            info->built = true;
            info->exe = fi.absoluteFilePath();
            info->builtAt = fi.lastModified().toString(QStringLiteral("MM-dd HH:mm"));
            return;
        }
    }
}

void PreviewHub::loadRuntime()
{
    m_runtime.clear();
    const QString path = runtimeFilePath();
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject agents = doc.object().value(QStringLiteral("agents")).toObject();
    for (auto it = agents.constBegin(); it != agents.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        RuntimeEntry entry;
        entry.pid = static_cast<qint64>(obj.value(QStringLiteral("pid")).toDouble());
        entry.startedAt = obj.value(QStringLiteral("startedAt")).toString();
        entry.exe = obj.value(QStringLiteral("exe")).toString();
        m_runtime.insert(it.key(), entry);
    }

    // 自己也是一个实例：登记进去，预览中心才能看到「当前实例」在跑。
    // 若已有活的登记（多半是主线拉起的预览实例），保留原记录，不要覆盖 exe 等信息。
    if (!m_selfRegisterEnabled)
        return;
    const auto self = m_runtime.constFind(m_selfId);
    if (self == m_runtime.constEnd() || !isAlive(self->pid)) {
        RuntimeEntry entry;
        entry.pid = QCoreApplication::applicationPid();
        entry.startedAt = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        entry.exe = QCoreApplication::applicationFilePath();
        m_runtime.insert(m_selfId, entry);
    }
}

void PreviewHub::saveRuntime() const
{
    const QString path = runtimeFilePath();
    if (path.isEmpty())
        return;
    QJsonObject agents;
    for (auto it = m_runtime.constBegin(); it != m_runtime.constEnd(); ++it) {
        if (!isAlive(it->pid))
            continue;
        QJsonObject obj;
        obj.insert(QStringLiteral("pid"), static_cast<double>(it->pid));
        obj.insert(QStringLiteral("startedAt"), it->startedAt);
        obj.insert(QStringLiteral("exe"), it->exe);
        agents.insert(it.key(), obj);
    }
    QJsonObject root;
    root.insert(QStringLiteral("agents"), agents);
    // 原子写：主线与预览实例可能同时写这个文件，避免半截 JSON
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        qWarning().noquote() << "PreviewHub: runtime file write failed" << path;
}

bool PreviewHub::isAlive(qint64 pid) const
{
    return processAlive(pid);
}

void PreviewHub::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void PreviewHub::refresh()
{
    scan();
}

const PreviewHub::AgentInfo *PreviewHub::findAgent(const QString &agentId) const
{
    for (const AgentInfo &info : m_agents) {
        if (info.id == agentId)
            return &info;
    }
    return nullptr;
}

PreviewHub::BuildSettings PreviewHub::resolveBuildSettings() const
{
    BuildSettings settings;

    // 参考缓存：宿主自己的 build 目录，或锚点根目录的 build 目录
    QString cachePath;
    const QStringList bases = {QCoreApplication::applicationDirPath(), m_anchorRoot};
    for (const QString &base : bases) {
        if (base.isEmpty())
            continue;
        QDir dir(base);
        for (int i = 0; i < 4 && cachePath.isEmpty(); ++i) {
            const QString candidate = dir.filePath(QStringLiteral("build/CMakeCache.txt"));
            if (QFileInfo::exists(candidate))
                cachePath = candidate;
            else if (!dir.cdUp())
                break;
        }
    }

    QHash<QString, QString> cache;
    if (!cachePath.isEmpty()) {
        QFile file(cachePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine()).trimmed();
                const int eq = line.indexOf(QLatin1Char('='));
                if (eq <= 0)
                    continue;
                QString key = line.left(eq);
                const int colon = key.indexOf(QLatin1Char(':'));
                if (colon > 0)
                    key = key.left(colon);
                cache.insert(key, line.mid(eq + 1));
            }
        }
    }

    settings.cmake = qEnvironmentVariable(kCmakeVar).trimmed();
    if (settings.cmake.isEmpty()) {
        const QString found = QStandardPaths::findExecutable(QStringLiteral("cmake"));
        settings.cmake = found.isEmpty() ? cache.value(QStringLiteral("CMAKE_COMMAND")) : found;
    }
    if (settings.cmake.isEmpty()) {
        settings.cmake = QStringLiteral(
            "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/"
            "CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe");
    }
    settings.generator = cache.value(QStringLiteral("CMAKE_GENERATOR"), QStringLiteral("Visual Studio 17 2022"));
    settings.platform = cache.value(QStringLiteral("CMAKE_GENERATOR_PLATFORM"), QStringLiteral("x64"));
    settings.qtPrefix = qEnvironmentVariable(kQtPrefixVar).trimmed();
    if (settings.qtPrefix.isEmpty())
        settings.qtPrefix = cache.value(QStringLiteral("CMAKE_PREFIX_PATH"));
    if (settings.qtPrefix.isEmpty())
        settings.qtPrefix = QStringLiteral("C:/Qt/6.8.3/msvc2022_64");
    return settings;
}

bool PreviewHub::runConfigure(const AgentInfo &agent, const BuildSettings &settings, QString *error)
{
    QStringList args;
    args << QStringLiteral("-S") << agent.path
         << QStringLiteral("-B") << agent.path + QStringLiteral("/build")
         << QStringLiteral("-G") << settings.generator;
    if (settings.generator.contains(QStringLiteral("Visual Studio")))
        args << QStringLiteral("-A") << settings.platform;
    args << QStringLiteral("-DCMAKE_PREFIX_PATH=") + settings.qtPrefix;

    QProcess proc;
    proc.setWorkingDirectory(agent.path);
    proc.start(settings.cmake, args);
    if (!proc.waitForStarted(10000)) {
        if (error)
            *error = QStringLiteral("无法启动 cmake：%1").arg(settings.cmake);
        return false;
    }
    if (!proc.waitForFinished(600000)) {
        proc.kill();
        proc.waitForFinished(3000);
        if (error)
            *error = QStringLiteral("cmake 配置超时");
        return false;
    }
    if (proc.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}

bool PreviewHub::runBuild(const AgentInfo &agent, const BuildSettings &settings, QString *error)
{
    QProcess proc;
    proc.setWorkingDirectory(agent.path);
    proc.start(settings.cmake,
               {QStringLiteral("--build"), agent.path + QStringLiteral("/build"), QStringLiteral("--config"),
                QStringLiteral("Release")});
    if (!proc.waitForStarted(10000)) {
        if (error)
            *error = QStringLiteral("无法启动 cmake：%1").arg(settings.cmake);
        return false;
    }
    if (!proc.waitForFinished(7200000)) {
        proc.kill();
        proc.waitForFinished(3000);
        if (error)
            *error = QStringLiteral("构建超时");
        return false;
    }
    if (proc.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}

bool PreviewHub::build(const QString &agentId)
{
    const AgentInfo *info = findAgent(agentId);
    if (!info) {
        setStatus(QStringLiteral("未知工作区：%1").arg(agentId));
        return false;
    }
    if (m_builds.contains(agentId)) {
        setStatus(QStringLiteral("%1 正在构建中").arg(agentId));
        return false;
    }
    const BuildSettings settings = resolveBuildSettings();
    spawnBuildStage(*info, settings,
                    QFileInfo::exists(info->path + QStringLiteral("/build/CMakeCache.txt"))
                        ? QStringLiteral("build")
                        : QStringLiteral("configure"));
    return true;
}

void PreviewHub::spawnBuildStage(const AgentInfo &agent, const BuildSettings &settings, const QString &stage)
{
    QStringList args;
    if (stage == QStringLiteral("configure")) {
        args << QStringLiteral("-S") << agent.path
             << QStringLiteral("-B") << agent.path + QStringLiteral("/build")
             << QStringLiteral("-G") << settings.generator;
        if (settings.generator.contains(QStringLiteral("Visual Studio")))
            args << QStringLiteral("-A") << settings.platform;
        args << QStringLiteral("-DCMAKE_PREFIX_PATH=") + settings.qtPrefix;
    } else {
        args << QStringLiteral("--build") << agent.path + QStringLiteral("/build")
             << QStringLiteral("--config") << QStringLiteral("Release");
    }

    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(agent.path);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    m_builds.insert(agent.id, proc);
    m_buildStage.insert(agent.id, stage);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, agentId = agent.id, proc]() {
        appendLog(agentId, QString::fromUtf8(proc->readAllStandardOutput()));
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, agentId = agent.id, proc]() {
        appendLog(agentId, QString::fromUtf8(proc->readAllStandardError()));
    });
    connect(proc, &QProcess::finished, this, [this, agentId = agent.id, proc](int exitCode) {
        onBuildFinished(agentId, exitCode);
        proc->deleteLater();
    });

    appendLog(agent.id, QStringLiteral("=== %1 : %2 ===\n").arg(agent.id, stage));
    proc->start(settings.cmake, args);
    setStatus(QStringLiteral("%1 %2 中").arg(agent.id,
                                            stage == QStringLiteral("configure") ? QStringLiteral("配置")
                                                                              : QStringLiteral("构建")));
}

void PreviewHub::onBuildFinished(const QString &agentId, int exitCode)
{
    m_builds.remove(agentId);
    const QString stage = m_buildStage.value(agentId);
    m_buildStage.remove(agentId);

    if (exitCode == 0 && stage == QStringLiteral("configure")) {
        const AgentInfo *info = findAgent(agentId);
        if (info) {
            spawnBuildStage(*info, resolveBuildSettings(), QStringLiteral("build"));
            return;
        }
    }

    appendLog(agentId, exitCode == 0 ? QStringLiteral("\n=== 构建成功 ===\n")
                                     : QStringLiteral("\n=== 构建失败 (exit %1) ===\n").arg(exitCode));
    setStatus(exitCode == 0 ? QStringLiteral("%1 构建完成").arg(agentId)
                             : QStringLiteral("%1 构建失败").arg(agentId));
    scan();
}

void PreviewHub::appendLog(const QString &agentId, const QString &text)
{
    if (text.isEmpty())
        return;
    QString &log = m_logs[agentId];
    log += text;
    if (log.size() > 8000)
        log = log.right(6000);
    emit agentsChanged();
}

QString PreviewHub::buildLog(const QString &agentId) const
{
    return m_logs.value(agentId);
}

bool PreviewHub::start(const QString &agentId)
{
    QString error;
    if (!startBlocking(agentId, &error)) {
        setStatus(error);
        return false;
    }
    setStatus(QStringLiteral("%1 预览已启动").arg(agentId));
    return true;
}

bool PreviewHub::startBlocking(const QString &agentId, QString *error)
{
    const AgentInfo *info = findAgent(agentId);
    if (!info) {
        if (error)
            *error = QStringLiteral("未知工作区：%1").arg(agentId);
        return false;
    }
    return spawnInstance(*info, error);
}

void PreviewHub::setDetachOnStart(bool detached)
{
    m_detachOnStart = detached;
}

void PreviewHub::setSelfRegisterEnabled(bool enabled)
{
    m_selfRegisterEnabled = enabled;
}

bool PreviewHub::spawnInstance(const AgentInfo &agent, QString *error)
{
    if (agent.running) {
        if (error)
            *error = QStringLiteral("%1 已在运行（pid %2）").arg(agent.id).arg(agent.pid);
        return false;
    }
    if (!agent.built || agent.exe.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1 尚未构建，请先点击构建").arg(agent.id);
        return false;
    }
    if (!QFileInfo::exists(agent.exe)) {
        if (error)
            *error = QStringLiteral("找不到可执行文件：%1").arg(agent.exe);
        return false;
    }

    const QString sandbox = agent.path + QStringLiteral("/.sandbox");
    QDir().mkpath(sandbox);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("NEONBOARD_DATA_DIR"), QDir::toNativeSeparators(sandbox));
    env.insert(QStringLiteral("NEONBOARD_INSTANCE"), QStringLiteral("preview-%1").arg(agent.id));
    env.insert(QStringLiteral("NEONBOARD_ROADMAP"),
               QDir::toNativeSeparators(sandbox + QStringLiteral("/progress.json")));
    env.insert(QStringLiteral("NEONBOARD_ROADMAP_AUTOWRITE"), QStringLiteral("0"));
    env.insert(QStringLiteral("NEONBOARD_AGENT_ID"), agent.id);

    // 开发态 exe 需要 Qt 的 DLL 在 PATH 上（部署包自带，这里补一下）
    const BuildSettings settings = resolveBuildSettings();
    if (!settings.qtPrefix.isEmpty()) {
        const QString qtBin = QDir::fromNativeSeparators(settings.qtPrefix) + QStringLiteral("/bin");
        QString path = env.value(QStringLiteral("PATH"));
        if (!path.contains(qtBin, Qt::CaseInsensitive))
            path = qtBin + QLatin1Char(';') + path;
        env.insert(QStringLiteral("PATH"), path);
    }

    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(env);
    proc->setWorkingDirectory(agent.path);
    proc->setProgram(agent.exe);

    if (m_detachOnStart) {
        // 命令行模式：静态脱离启动，实例不随命令退出而结束。
        // startDetached 继承当前进程环境，所以先把沙箱变量（含补过 Qt bin 的 PATH）写进去。
        static const char *vars[] = {"NEONBOARD_DATA_DIR", "NEONBOARD_INSTANCE", "NEONBOARD_ROADMAP",
                                    "NEONBOARD_ROADMAP_AUTOWRITE", "NEONBOARD_AGENT_ID", "PATH"};
        for (const char *name : vars)
            qputenv(name, env.value(QString::fromLatin1(name)).toLocal8Bit());

        qint64 pid = 0;
        // 标准流指到 nul：脱离的实例不继承父进程的管道/控制台，
        // 否则调用方（比如脚本的输出重定向）会一直等不到 EOF 而挂死
        proc->setStandardInputFile(QProcess::nullDevice());
        proc->setStandardOutputFile(QProcess::nullDevice());
        proc->setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
        // 再进一步：不要新控制台，也不要继承父进程句柄。
        // 否则脱离的实例会占着调用方的终端，终端/脚本要等它结束才返回。
        proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags &= ~static_cast<unsigned long>(CREATE_NEW_CONSOLE);
            args->flags |= static_cast<unsigned long>(DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP);
            args->inheritHandles = false;
            if (args->startupInfo)
                args->startupInfo->dwFlags &= ~static_cast<DWORD>(STARTF_USESTDHANDLES);
        });
#endif
        if (!proc->startDetached(&pid)) {
            proc->deleteLater();
            if (error)
                *error = QStringLiteral("启动失败：%1").arg(agent.exe);
            return false;
        }
        proc->deleteLater();

        RuntimeEntry entry;
        entry.pid = pid;
        entry.startedAt = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        entry.exe = agent.exe;
        m_runtime.insert(agent.id, entry);
        saveRuntime();
        scan();
        return true;
    }

    proc->start();
    if (!proc->waitForStarted(10000)) {
        proc->deleteLater();
        if (error)
            *error = QStringLiteral("启动失败：%1").arg(agent.exe);
        return false;
    }

    m_instances.insert(agent.id, proc);
    connect(proc, &QProcess::finished, this, [this, agentId = agent.id, proc]() {
        m_instances.remove(agentId);
        proc->deleteLater();
        scan();
    });

    RuntimeEntry entry;
    entry.pid = proc->processId();
    entry.startedAt = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    entry.exe = agent.exe;
    m_runtime.insert(agent.id, entry);
    saveRuntime();
    scan();
    return true;
}

bool PreviewHub::stop(const QString &agentId)
{
    QString error;
    if (!stopBlocking(agentId, &error)) {
        setStatus(error);
        return false;
    }
    setStatus(QStringLiteral("%1 预览已停止").arg(agentId));
    return true;
}

bool PreviewHub::stopBlocking(const QString &agentId, QString *error)
{
    if (agentId == m_selfId) {
        if (error)
            *error = QStringLiteral("不能停止当前实例");
        return false;
    }

    QProcess *proc = m_instances.take(agentId);
    if (proc) {
        proc->kill();
        proc->waitForFinished(3000);
        proc->deleteLater();
    }

    const RuntimeEntry entry = m_runtime.value(agentId);
    if (entry.pid > 0 && isAlive(entry.pid)) {
        // 不是本进程拉起的实例：按 pid 结束
        bool ok = false;
        runCommand(QStringLiteral("taskkill"),
                   {QStringLiteral("/PID"), QString::number(entry.pid), QStringLiteral("/T"), QStringLiteral("/F")},
                   QString(), &ok);
    }
    m_runtime.remove(agentId);
    saveRuntime();
    scan();
    return true;
}

QString PreviewHub::listJson() const
{
    QJsonArray array;
    for (const AgentInfo &info : m_agents) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), info.id);
        obj.insert(QStringLiteral("path"), info.path);
        obj.insert(QStringLiteral("branch"), info.branch);
        obj.insert(QStringLiteral("head"), info.head);
        obj.insert(QStringLiteral("subject"), info.subject);
        obj.insert(QStringLiteral("dirty"), info.dirty);
        obj.insert(QStringLiteral("detached"), info.detached);
        obj.insert(QStringLiteral("built"), info.built);
        obj.insert(QStringLiteral("builtAt"), info.builtAt);
        obj.insert(QStringLiteral("exe"), info.exe);
        obj.insert(QStringLiteral("running"), info.running);
        obj.insert(QStringLiteral("pid"), static_cast<double>(info.pid));
        obj.insert(QStringLiteral("current"), info.current);
        array.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Indented));
}

bool PreviewHub::buildBlocking(const QString &agentId, QString *error)
{
    const AgentInfo *info = findAgent(agentId);
    if (!info) {
        if (error)
            *error = QStringLiteral("未知工作区：%1").arg(agentId);
        return false;
    }
    const BuildSettings settings = resolveBuildSettings();
    if (!QFileInfo::exists(info->path + QStringLiteral("/build/CMakeCache.txt"))) {
        if (!runConfigure(*info, settings, error))
            return false;
    }
    if (!runBuild(*info, settings, error))
        return false;
    scan();
    return true;
}
