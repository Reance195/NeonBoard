#include "CliHandlers.h"

#include "AgentProfile.h"
#include "AppPaths.h"
#include "DataPort.h"
#include "HookEventQueue.h"
#include "LedgerImport.h"
#include "PluginLibrary.h"
#include "PluginLoader.h"
#include "PluginMarket.h"
#include "PluginPackage.h"
#include "PluginRuntime.h"
#include "PreviewHub.h"
#include "RoadmapSync.h"
#include "TaskRunStore.h"
#include "UpdateManager.h"
#include "WindowsIntegration.h"

#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringConverter>
#include <QTextStream>
#include <QTimer>
#include <QVariant>

// 块体逐字照搬拆分前的派发块（MOD-10）；return 0/2/3/4 即函数返回值，
// 未命中落到 return std::nullopt，调用点保持原位。

std::optional<int> runDataMode(DataPort &dataPort, const QStringList &arguments)
{
    const int exportIndex = arguments.indexOf(QStringLiteral("--export-tasks"));
    const int importIndex = arguments.indexOf(QStringLiteral("--import-tasks"));
    const bool backupMode = arguments.contains(QStringLiteral("--backup"));
    if (exportIndex < 0 && importIndex < 0 && !backupMode)
        return std::nullopt;
    QString message;
    bool ok = false;
    if (backupMode) {
        message = dataPort.backupNow();
        ok = message.isEmpty();
        if (ok)
            qInfo().noquote() << "BACKUP OK" << dataPort.backupDir();
    } else if (exportIndex >= 0) {
        QString path;
        if (exportIndex + 1 < arguments.size())
            path = arguments.at(exportIndex + 1);
        QString format = QStringLiteral("json");
        if (exportIndex + 2 < arguments.size())
            format = arguments.at(exportIndex + 2);
        if (path.isEmpty())
            path = dataPort.defaultPath(format);
        message = dataPort.exportTasks(path, format);
        ok = message.isEmpty();
        if (ok)
            qInfo().noquote() << "EXPORT OK" << path;
    } else {
        QString path;
        if (importIndex + 1 < arguments.size())
            path = arguments.at(importIndex + 1);
        QString format = QStringLiteral("auto");
        if (importIndex + 2 < arguments.size())
            format = arguments.at(importIndex + 2);
        message = dataPort.importTasks(path, format);
        ok = message.isEmpty();
        if (ok)
            qInfo().noquote() << "IMPORT OK" << path;
    }
    if (!ok)
        qWarning().noquote() << "DATA FAIL" << message;
    return ok ? 0 : 4;
}

std::optional<int> runSyncMode(RoadmapSync &roadmap, const QStringList &arguments)
{
    const int syncIndex = arguments.indexOf(QStringLiteral("--sync-roadmap"));
    const int exportIndex = arguments.indexOf(QStringLiteral("--export-roadmap"));
    if (syncIndex < 0 && exportIndex < 0)
        return std::nullopt;
    QString target;
    if (syncIndex >= 0 && syncIndex + 1 < arguments.size())
        target = arguments.at(syncIndex + 1);
    if (exportIndex >= 0 && exportIndex + 1 < arguments.size())
        target = arguments.at(exportIndex + 1);
    if (target.isEmpty())
        target = RoadmapSync::resolvePath();
    const bool force = arguments.contains(QStringLiteral("--force"));
    const bool ok = exportIndex >= 0 ? roadmap.exportTo(target)
                                      : roadmap.importFrom(target, force);
    qInfo().noquote() << "ROADMAP" << (ok ? "OK" : "FAIL") << target << roadmap.lastReport();
    return ok ? 0 : 3;
}

std::optional<int> runEmitHook(const QStringList &arguments)
{
    // 钩子事件投递：--emit-hook <text> / --emit-hook-file <path>
    // 只把提交信息写进事件队列，不在这里执行规则 —— 规则由主进程里的插件执行。
    // 走 utilityMode：应用正在运行时也能投递。
    const int emitHookIndex = arguments.indexOf(QStringLiteral("--emit-hook"));
    const int emitHookFileIndex = arguments.indexOf(QStringLiteral("--emit-hook-file"));
    if (emitHookIndex < 0 && emitHookFileIndex < 0)
        return std::nullopt;
    // --hook-event <name>：可选投递事件名，默认 hook.commit。
    // 让其他来源（如 agent.task 事件域）复用同一条投递链路而不污染计划钩子的解析。
    const int eventNameIndex = arguments.indexOf(QStringLiteral("--hook-event"));
    QString eventName = QStringLiteral("hook.commit");
    if (eventNameIndex >= 0 && eventNameIndex + 1 < arguments.size()
        && !arguments.at(eventNameIndex + 1).trimmed().isEmpty())
        eventName = arguments.at(eventNameIndex + 1).trimmed();
    QString payloadText;
    if (emitHookFileIndex >= 0 && emitHookFileIndex + 1 < arguments.size()) {
        QFile payloadFile(arguments.at(emitHookFileIndex + 1));
        if (payloadFile.open(QIODevice::ReadOnly | QIODevice::Text))
            payloadText = QString::fromUtf8(payloadFile.readAll());
        else
            qWarning().noquote() << "EMIT-HOOK cannot read" << arguments.at(emitHookFileIndex + 1);
    } else if (emitHookIndex >= 0 && emitHookIndex + 1 < arguments.size()) {
        payloadText = arguments.at(emitHookIndex + 1);
    }
    if (payloadText.trimmed().isEmpty()) {
        qWarning() << "EMIT-HOOK empty payload";
        return 2;
    }
    HookEventQueue emitQueue(AppPaths::hookDbPath());
    QString emitError;
    const bool emitted = emitQueue.open(&emitError)
        && emitQueue.enqueue(eventName,
                              {{QStringLiteral("text"), payloadText},
                               {QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch()}},
                              &emitError);
    qInfo().noquote() << "EMIT-HOOK" << (emitted ? "OK" : "FAIL") << emitError;
    return emitted ? 0 : 2;
}

std::optional<int> runFileAssoc(const QStringList &arguments)
{
    // Windows 原生集成（R4-02）：.neon 文件关联的注册 / 注销 / 状态查询。
    // 只写 HKCU，不需要管理员；注册后双击插件包即走 --install-plugin-file。
    if (arguments.contains(QStringLiteral("--assoc-status"))) {
        const QVariantMap status = WindowsIntegration::associationStatus();
        emitCliLine(status.value(QStringLiteral("registered")).toBool()
                        ? QStringLiteral("OK registered")
                        : QStringLiteral("OK not-registered"));
        emitCliLine(QStringLiteral("class: %1")
                        .arg(status.value(QStringLiteral("classRoot")).toString()));
        emitCliLine(QStringLiteral("command: %1")
                        .arg(status.value(QStringLiteral("command")).toString()));
        return 0;
    }
    if (arguments.contains(QStringLiteral("--register-file-assoc"))
        || arguments.contains(QStringLiteral("--unregister-file-assoc"))) {
        const bool registering = arguments.contains(QStringLiteral("--register-file-assoc"));
        QString assocError;
        const bool assocOk = registering
            ? WindowsIntegration::registerFileAssociations(&assocError)
            : WindowsIntegration::unregisterFileAssociations(&assocError);
        if (!assocOk) {
            qWarning().noquote() << "ASSOC FAIL" << assocError;
            emitCliLine(QStringLiteral("FAIL %1").arg(assocError));
            return 2;
        }
        emitCliLine(QStringLiteral("OK"));
        return 0;
    }
    return std::nullopt;
}

std::optional<int> runInstallPluginFile(const QStringList &arguments)
{
    // 本地 .neon 插件包安装：--install-plugin-file <path>
    // 供脚本化安装与自检使用；包格式见 docs/plugin-api.md
    const int installPluginFileIndex = arguments.indexOf(QStringLiteral("--install-plugin-file"));
    if (installPluginFileIndex < 0)
        return std::nullopt;
    if (installPluginFileIndex + 1 >= arguments.size()) {
        qWarning() << "INSTALL-PLUGIN missing package path";
        return 2;
    }
    const QString packagePath = arguments.at(installPluginFileIndex + 1);
    const QString pluginId = PluginPackage::neonPluginId(packagePath);
    const QString dirName = PluginPackage::dirNameForId(pluginId);
    if (pluginId.isEmpty() || dirName.isEmpty()) {
        qWarning().noquote() << "INSTALL-PLUGIN invalid package" << packagePath;
        return 2;
    }
    const QString targetDir = AppPaths::pluginDir() + QLatin1Char('/') + dirName;
    QString installError;
    if (!PluginPackage::installNeonFile(packagePath, targetDir, &installError)) {
        qWarning().noquote() << "INSTALL-PLUGIN FAIL" << installError;
        return 2;
    }
    qInfo().noquote() << "INSTALL-PLUGIN OK" << pluginId << targetDir;
    return 0;
}

std::optional<int> runPluginReport(PluginLoader &pluginLoader,
                                   PluginRuntime &pluginRuntime,
                                   AgentProfile &agentProfile,
                                   const QStringList &arguments)
{
    // --plugin-report：输出插件平台表面清单（插件 / 命令 / 槽位 / 设置页 / 授权缺口），
    // 走 stdout JSON，方便脚本与无头环境验收。
    if (!arguments.contains(QStringLiteral("--plugin-report")))
        return std::nullopt;
    QJsonArray plugins;
    for (const QVariant &entry : pluginLoader.plugins()) {
        const QVariantMap map = entry.toMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        QJsonObject item;
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("name"), map.value(QStringLiteral("name")).toString());
        item.insert(QStringLiteral("version"), map.value(QStringLiteral("version")).toString());
        item.insert(QStringLiteral("status"), map.value(QStringLiteral("status")).toString());
        item.insert(QStringLiteral("source"), map.value(QStringLiteral("source")).toString());
        QJsonArray permissions;
        for (const QVariant &permission : map.value(QStringLiteral("permissions")).toList())
            permissions.append(permission.toString());
        item.insert(QStringLiteral("permissions"), permissions);
        QJsonArray missing;
        for (const QString &permission : pluginRuntime.missingPermissions(id))
            missing.append(permission);
        item.insert(QStringLiteral("missingPermissions"), missing);
        plugins.append(item);
    }

    QJsonObject slotMap;
    slotMap.insert(QStringLiteral("sidebar"),
                   QJsonArray::fromVariantList(pluginRuntime.sidebarSlotItems()));
    slotMap.insert(QStringLiteral("bottom"),
                   QJsonArray::fromVariantList(pluginRuntime.bottomSlotItems()));
    slotMap.insert(QStringLiteral("card"),
                   QJsonArray::fromVariantList(pluginRuntime.cardSlotItems()));

    // R9-01：agent profile 的三层合并结果 + 插件声明缺口。
    // 要能一眼看出「谁声明的、授权没有、这台机器上跑不跑得起来」——
    // cursor-agent 那种「列表里有却调不动」的坑就是靠 available 暴露的。
    QStringList pluginIds;
    for (const QVariant &value : pluginRuntime.declaredAgentProfiles())
        pluginIds.append(value.toMap().value(QStringLiteral("id")).toString());
    QStringList builtinIds;
    for (const QVariant &value : AgentProfile::builtinProfiles())
        builtinIds.append(value.toMap().value(QStringLiteral("id")).toString());

    QJsonArray agentEffective;
    for (const QVariant &value : agentProfile.profiles()) {
        const QVariantMap profile = value.toMap();
        const QString id = profile.value(QStringLiteral("id")).toString();
        QJsonObject item;
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("displayName"),
                    profile.value(QStringLiteral("displayName")).toString());
        item.insert(QStringLiteral("exe"), profile.value(QStringLiteral("exe")).toString());
        item.insert(QStringLiteral("available"), agentProfile.isAvailable(id));
        item.insert(QStringLiteral("source"),
                    pluginIds.contains(id) ? QStringLiteral("plugin")
                    : builtinIds.contains(id) ? QStringLiteral("builtin")
                                              : QStringLiteral("user"));
        agentEffective.append(item);
    }

    QJsonArray agentDeclared;
    for (const QVariant &value : pluginRuntime.agentProfileDeclarations()) {
        const QVariantMap row = value.toMap();
        QJsonArray declaredIds;
        for (const QVariant &profile : row.value(QStringLiteral("profiles")).toList())
            declaredIds.append(profile.toMap().value(QStringLiteral("id")).toString());
        QJsonObject item;
        item.insert(QStringLiteral("pluginId"),
                    row.value(QStringLiteral("pluginId")).toString());
        item.insert(QStringLiteral("authorized"),
                    row.value(QStringLiteral("authorized")).toBool());
        item.insert(QStringLiteral("profiles"), declaredIds);
        agentDeclared.append(item);
    }

    QJsonObject agentSection;
    agentSection.insert(QStringLiteral("effective"), agentEffective);
    agentSection.insert(QStringLiteral("declared"), agentDeclared);

    QJsonObject report;
    report.insert(QStringLiteral("plugins"), plugins);
    report.insert(QStringLiteral("commands"),
                  QJsonArray::fromVariantList(pluginRuntime.pluginCommands()));
    report.insert(QStringLiteral("settingsPages"),
                  QJsonArray::fromVariantList(pluginRuntime.settingsPages()));
    report.insert(QStringLiteral("slots"), slotMap);
    report.insert(QStringLiteral("agentProfiles"), agentSection);

    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    out << QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented));
    out.flush();
    return 0;
}

LateCliRunner::LateCliRunner(PluginMarket &market, PluginLibrary &library,
                             PreviewHub &preview, UpdateManager &updates,
                             TaskRunStore &runs, LedgerImport &ledger,
                             PluginEventBus &eventBus)
    : hookQueue(AppPaths::hookDbPath()),
      pluginMarket(market),
      pluginLibrary(library),
      previewHub(preview),
      updateManager(updates),
      taskRuns(runs),
      ledgerImport(ledger)
{
    // 钩子事件队列：与看板库物理隔离，主进程轮询消费后转成插件事件
    QString hookQueueError;
    if (!hookQueue.open(&hookQueueError))
        qWarning().noquote() << "HookEventQueue: open failed" << hookQueueError;
    else
        hookQueue.setEventBus(&eventBus);
}

LateCliRunner::~LateCliRunner() = default;

std::optional<int> LateCliRunner::runMarketCli(const QStringList &arguments)
{
    // 插件市场命令行：--market-source <spec>... 设源（可重复、可逗号分隔）；
    // --market-sources 列出当前源；--market-refresh 拉索引；
    // --market-install <id> 装插件；--install-plugin-url <url> 直链安装。
    // 都走同步接口，方便脚本与无头环境验证。
    QStringList marketSpecs;
    for (int i = 0; i < arguments.size(); ++i) {
        if (arguments.at(i) != QStringLiteral("--market-source"))
            continue;
        if (i + 1 >= arguments.size())
            break;
        const QStringList parts =
            arguments.at(i + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QString trimmed = part.trimmed();
            if (!trimmed.isEmpty() && !marketSpecs.contains(trimmed))
                marketSpecs.append(trimmed);
        }
    }
    if (!marketSpecs.isEmpty()) {
        QVariantList sources;
        for (const QString &spec : marketSpecs) {
            QVariantMap record;
            record.insert(QStringLiteral("spec"), spec);
            record.insert(QStringLiteral("name"), QString());
            record.insert(QStringLiteral("enabled"), true);
            sources.append(record);
        }
        pluginMarket.setSources(sources);
        qInfo().noquote() << "MARKET-SOURCE OK" << sources.size() << "source(s)"
                          << marketSpecs.join(QStringLiteral(", "));
        return 0;
    }

    if (arguments.contains(QStringLiteral("--market-sources"))) {
        const QVariantList sources = pluginLibrary.marketSources();
        qInfo().noquote() << "MARKET-SOURCES" << sources.size();
        for (const QVariant &value : sources) {
            const QVariantMap record = value.toMap();
            qInfo().noquote() << "  " << record.value(QStringLiteral("name")).toString()
                              << "|" << record.value(QStringLiteral("spec")).toString()
                              << (record.value(QStringLiteral("enabled")).toBool()
                                      ? QStringLiteral("|enabled")
                                      : QStringLiteral("|disabled"));
        }
        return 0;
    }

    if (arguments.contains(QStringLiteral("--market-refresh"))) {
        QString marketError;
        const bool refreshed = pluginLibrary.refreshMarketBlocking(10000, &marketError);
        if (refreshed) {
            qInfo().noquote() << "MARKET-REFRESH OK" << pluginMarket.entries().size()
                              << pluginMarket.statusText();
            for (const QVariant &value : pluginMarket.sourceStatuses()) {
                const QVariantMap status = value.toMap();
                qInfo().noquote() << "MARKET-SOURCE" << status.value(QStringLiteral("name")).toString()
                                  << (status.value(QStringLiteral("ok")).toBool() ? QStringLiteral("OK")
                                                                                : QStringLiteral("FAIL"))
                                  << status.value(QStringLiteral("message")).toString();
            }
            return 0;
        }
        qWarning().noquote() << "MARKET-REFRESH FAIL" << marketError;
        return 2;
    }

    const int marketInstallIndex = arguments.indexOf(QStringLiteral("--market-install"));
    if (marketInstallIndex >= 0) {
        const QString pluginId = marketInstallIndex + 1 < arguments.size()
            ? arguments.at(marketInstallIndex + 1) : QString();
        if (pluginId.isEmpty()) {
            qWarning() << "MARKET-INSTALL missing plugin id";
            return 2;
        }
        QString installError;
        if (pluginLibrary.installBlocking(pluginId, &installError)) {
            qInfo().noquote() << "MARKET-INSTALL OK" << pluginId;
            return 0;
        }
        qWarning().noquote() << "MARKET-INSTALL FAIL" << installError;
        return 2;
    }

    const int directInstallIndex = arguments.indexOf(QStringLiteral("--install-plugin-url"));
    if (directInstallIndex >= 0) {
        const QString packageUrl = directInstallIndex + 1 < arguments.size()
            ? arguments.at(directInstallIndex + 1) : QString();
        if (packageUrl.trimmed().isEmpty()) {
            qWarning() << "INSTALL-PLUGIN-URL missing url";
            return 2;
        }
        QString installError;
        if (pluginLibrary.installFromUrlBlocking(packageUrl.trimmed(), &installError)) {
            qInfo().noquote() << "INSTALL-PLUGIN-URL OK" << packageUrl.trimmed();
            return 0;
        }
        qWarning().noquote() << "INSTALL-PLUGIN-URL FAIL" << installError;
        return 2;
    }

    return std::nullopt;
}

std::optional<int> LateCliRunner::runPreviewAndUpdateCli(const QStringList &arguments)
{
    // 预览中心命令行：--preview-list 列工作区；--preview-build <id> 构建；
    // --preview-start <id> 启动预览实例；--preview-stop <id> 停止实例。
    // 命令行进程跑完即退，不该把自己登记成“运行中的实例”。
    if (arguments.contains(QStringLiteral("--preview-list"))) {
        previewHub.refresh();
        qInfo().noquote() << "PREVIEW-LIST" << previewHub.listJson();
        emitCliLine(previewHub.listJson());
        return 0;
    }

    const int previewBuildIndex = arguments.indexOf(QStringLiteral("--preview-build"));
    if (previewBuildIndex >= 0) {
        const QString agentId = previewBuildIndex + 1 < arguments.size()
            ? arguments.at(previewBuildIndex + 1) : QString();
        if (agentId.isEmpty()) {
            qWarning() << "PREVIEW-BUILD missing agent id";
            emitCliLine(QStringLiteral("FAIL --preview-build missing agent id"));
            return 2;
        }
        previewHub.refresh();
        QString buildError;
        if (previewHub.buildBlocking(agentId, &buildError)) {
            qInfo().noquote() << "PREVIEW-BUILD OK" << agentId;
            emitCliLine(QStringLiteral("OK %1").arg(agentId));
            return 0;
        }
        qWarning().noquote() << "PREVIEW-BUILD FAIL" << agentId << buildError;
        emitCliLine(QStringLiteral("FAIL %1 %2").arg(agentId, buildError));
        return 2;
    }

    const int previewPackageIndex = arguments.indexOf(QStringLiteral("--preview-package"));
    if (previewPackageIndex >= 0) {
        const QString agentId = previewPackageIndex + 1 < arguments.size()
            ? arguments.at(previewPackageIndex + 1) : QString();
        if (agentId.isEmpty()) {
            qWarning() << "PREVIEW-PACKAGE missing agent id";
            emitCliLine(QStringLiteral("FAIL --preview-package missing agent id"));
            return 2;
        }
        previewHub.refresh();
        QString packageError;
        if (previewHub.packageBlocking(agentId, &packageError)) {
            qInfo().noquote() << "PREVIEW-PACKAGE OK" << agentId;
            emitCliLine(QStringLiteral("OK %1").arg(agentId));
            return 0;
        }
        qWarning().noquote() << "PREVIEW-PACKAGE FAIL" << agentId << packageError;
        emitCliLine(QStringLiteral("FAIL %1 %2").arg(agentId, packageError));
        return 2;
    }

    const int previewStartIndex = arguments.indexOf(QStringLiteral("--preview-start"));    if (previewStartIndex >= 0) {
        const QString agentId = previewStartIndex + 1 < arguments.size()
            ? arguments.at(previewStartIndex + 1) : QString();
        if (agentId.isEmpty()) {
            qWarning() << "PREVIEW-START missing agent id";
            emitCliLine(QStringLiteral("FAIL --preview-start missing agent id"));
            return 2;
        }
        previewHub.setDetachOnStart(true);
        previewHub.refresh();
        QString startError;
        if (previewHub.startBlocking(agentId, &startError)) {
            qInfo().noquote() << "PREVIEW-START OK" << agentId;
            emitCliLine(QStringLiteral("OK %1").arg(agentId));
            return 0;
        }
        qWarning().noquote() << "PREVIEW-START FAIL" << agentId << startError;
        emitCliLine(QStringLiteral("FAIL %1 %2").arg(agentId, startError));
        return 2;
    }

    const int previewStopIndex = arguments.indexOf(QStringLiteral("--preview-stop"));
    if (previewStopIndex >= 0) {
        const QString agentId = previewStopIndex + 1 < arguments.size()
            ? arguments.at(previewStopIndex + 1) : QString();
        if (agentId.isEmpty()) {
            qWarning() << "PREVIEW-STOP missing agent id";
            emitCliLine(QStringLiteral("FAIL --preview-stop missing agent id"));
            return 2;
        }
        previewHub.refresh();
        QString stopError;
        if (previewHub.stopBlocking(agentId, &stopError)) {
            qInfo().noquote() << "PREVIEW-STOP OK" << agentId;
            emitCliLine(QStringLiteral("OK %1").arg(agentId));
            return 0;
        }
        qWarning().noquote() << "PREVIEW-STOP FAIL" << agentId << stopError;
        emitCliLine(QStringLiteral("FAIL %1 %2").arg(agentId, stopError));
        return 2;
    }

    // ---- 软件更新（R5-01）：命令行检查与清单源配置 ----
    // 走 utilityMode：应用正在运行时也能查，不会撞单实例锁。
    if (arguments.contains(QStringLiteral("--update-sources"))) {
        const QStringList sources = updateManager.updateSources();
        for (const QString &source : sources)
            emitCliLine(source);
        return 0;
    }

    const int updateSourceIndex = arguments.indexOf(QStringLiteral("--update-source"));
    if (updateSourceIndex >= 0) {
        const QString spec = updateSourceIndex + 1 < arguments.size()
            ? arguments.at(updateSourceIndex + 1).trimmed() : QString();
        if (spec.isEmpty()) {
            emitCliLine(QStringLiteral("FAIL --update-source missing url"));
            return 2;
        }
        updateManager.setUpdateSource(spec);
        qInfo().noquote() << "UPDATE-SOURCE OK" << spec;
        emitCliLine(QStringLiteral("OK %1").arg(spec));
        return 0;
    }

    if (arguments.contains(QStringLiteral("--check-update"))) {
        QString checkError;
        // 用默认总预算（25 秒）：候选源串行 + 单源 6 秒看门狗，最坏 ≈18 秒。
        // 这里写死过 15 秒，配 10 秒单源超时时，挂住的 raw 源会把兜底源挤掉。
        if (!updateManager.checkBlocking(UpdateManager::kDefaultCheckTimeoutMs, &checkError)) {
            qWarning().noquote() << "CHECK-UPDATE FAIL" << checkError;
            emitCliLine(QStringLiteral("FAIL %1").arg(checkError));
            return 2;
        }
        emitCliLine(updateManager.updateAvailable()
                        ? QStringLiteral("OK available %1").arg(updateManager.latestVersion())
                        : QStringLiteral("OK uptodate %1").arg(updateManager.currentVersion()));
        // 同步打进日志：CI 会扫日志找异常，命令行结果只进 stdout 不够追溯
        qInfo().noquote() << "CHECK-UPDATE" << (updateManager.updateAvailable() ? "available" : "uptodate")
                          << "current" << updateManager.currentVersion()
                          << "latest" << updateManager.latestVersion()
                          << "kind" << updateManager.installKind();
        emitCliLine(QStringLiteral("current  %1").arg(updateManager.currentVersion()));
        emitCliLine(QStringLiteral("latest   %1").arg(updateManager.latestVersion()));
        emitCliLine(QStringLiteral("kind     %1").arg(updateManager.installKind()));
        emitCliLine(QStringLiteral("state    %1").arg(updateManager.state()));
        emitCliLine(QStringLiteral("notes    %1").arg(updateManager.releaseNotes()));
        return 0;
    }

    // ---- 软件更新（R5-03）：无人值守走完「检查 → 下载 → 校验 → 静默安装」----
    // 静默升级是整条更新链路的最后一环。只能靠人手点设置面板验证的话，这一环就永远
    // 停在「代码看起来对」；有了这个开关，升级链路可以脚本化跑通、也能用于计划内
    // 批量升级（升级动作本身仍由安装包的 /VERYSILENT 完成，不改任何安装语义）。
    if (arguments.contains(QStringLiteral("--update-apply"))) {
        QString applyError;
        if (!updateManager.checkBlocking(UpdateManager::kDefaultCheckTimeoutMs, &applyError)) {
            qWarning().noquote() << "UPDATE-APPLY FAIL check" << applyError;
            emitCliLine(QStringLiteral("FAIL check %1").arg(applyError));
            return 2;
        }
        if (!updateManager.updateAvailable()) {
            qInfo().noquote() << "UPDATE-APPLY UPTODATE" << updateManager.currentVersion();
            emitCliLine(QStringLiteral("OK uptodate %1").arg(updateManager.currentVersion()));
            return 0;
        }

        QEventLoop loop;
        int exitCode = 2;
        // 进度只按 10% 打点：几十 MB 的下载每 1% 打一行会把日志刷成噪声
        int lastDecile = -1;
        QObject::connect(&updateManager, &UpdateManager::progressChanged, &loop, [&]() {
            const int decile = updateManager.progress() / 10;
            if (decile != lastDecile) {
                lastDecile = decile;
                qInfo().noquote() << "UPDATE-APPLY progress" << updateManager.progress() << '%';
            }
        });
        QObject::connect(&updateManager, &UpdateManager::installLaunched, &loop, [&]() {
            // 安装包已启动（/VERYSILENT /NORESTART /SUPPRESSMSGBOXES，宿主已在启动前
            // 被 taskkill，安装完成由 [Run] 段拉起宿主）。本进程到此结束，不等待安装结束。
            qInfo().noquote() << "UPDATE-APPLY INSTALLING" << updateManager.latestVersion()
                              << updateManager.installKind();
            emitCliLine(QStringLiteral("OK installing %1 %2")
                            .arg(updateManager.latestVersion(), updateManager.installKind()));
            exitCode = 0;
            loop.quit();
        });
        QObject::connect(&updateManager, &UpdateManager::errorOccurred, &loop,
                         [&](const QString &message) {
            qWarning().noquote() << "UPDATE-APPLY FAIL" << message;
            emitCliLine(QStringLiteral("FAIL %1").arg(message));
            exitCode = 2;
            loop.quit();
        });
        // 总闸：下载 + 校验 10 分钟没结果就判失败退出，避免脚本调用时无限挂起。
        // 这里不 abort 请求，让 UpdateManager 自行收尾 —— 进程马上退出，无意义。
        QTimer::singleShot(10 * 60 * 1000, &loop, [&]() {
            qWarning().noquote() << "UPDATE-APPLY FAIL timeout";
            emitCliLine(QStringLiteral("FAIL timeout"));
            exitCode = 2;
            loop.quit();
        });

        qInfo().noquote() << "UPDATE-APPLY START" << updateManager.currentVersion() << "->"
                          << updateManager.latestVersion() << updateManager.installKind();
        emitCliLine(QStringLiteral("DOWNLOAD %1").arg(updateManager.latestVersion()));
        updateManager.downloadAndInstall();
        loop.exec();
        return exitCode;
    }

    // ---- 执行结果（R6-06）：--run-summary <taskId> 输出该卡最近一次 run 的 JSON ----
    // 走 utilityMode：应用正在运行时也能查，不会撞单实例锁。
    const int runSummaryIndex = arguments.indexOf(QStringLiteral("--run-summary"));
    if (runSummaryIndex >= 0) {
        const QString taskId = runSummaryIndex + 1 < arguments.size()
            ? arguments.at(runSummaryIndex + 1).trimmed() : QString();
        if (taskId.isEmpty()) {
            emitCliLine(QStringLiteral("FAIL --run-summary missing task id"));
            return 2;
        }
        const QVariantMap summary = taskRuns.summaryOf(taskId);
        const QJsonDocument doc(QJsonObject::fromVariantMap(summary));
        emitCliLine(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        return 0;
    }

    // ---- 台账单向导入（R6-09，ADR-0001）：--import-ledger [path|短号] ----
    // 参数是已存在的文件 → 按该台账全量导入；否则当短号只导一张卡（台账自动探测）。
    // 只补空值不覆盖；冲突 / 未建卡只报告。反向写台账没有代码路径。
    const int ledgerIndex = arguments.indexOf(QStringLiteral("--import-ledger"));
    if (ledgerIndex >= 0) {
        const QString arg = ledgerIndex + 1 < arguments.size()
            ? arguments.at(ledgerIndex + 1).trimmed() : QString();
        QVariantMap result;
        if (!arg.isEmpty() && QFileInfo::exists(arg))
            result = ledgerImport.importAll(arg);
        else if (arg.isEmpty())
            result = ledgerImport.importAll();
        else
            result = ledgerImport.importCard(arg);
        if (!result.value(QStringLiteral("ok")).toBool()) {
            emitCliLine(QStringLiteral("FAIL %1").arg(result.value(QStringLiteral("error")).toString()));
            return 2;
        }
        const QJsonDocument doc(QJsonObject::fromVariantMap(result));
        emitCliLine(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        return 0;
    }

    return std::nullopt;
}
