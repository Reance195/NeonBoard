#pragma once

// CLI 命令处理（MOD-10）：AppMain.cpp 的派发块抽成函数，调用点保持在原来的
// 位置 —— CLI 块穿插在对象构造之间，不能统一后移（MOD-7 论证）。
// 返回语义统一为 std::optional<int>：nullopt = 未命中继续，值 = 进程退出码
// （0 之外还有 3 / 4，各命令原有语义逐字保留）。
//
// emitCliLine：GUI 子系统下命令行结果写 stdout（脚本读结果的唯一通道）。
// 原为 AppMain.cpp 的文件级 static，随派发块迁入此处作 inline 共享。
//
// LateCliRunner：HookEventQueue 是常驻事件队列（GUI 段 hookQueue.start(1000)
// 仍用），作成员随结构体存活到 main 退出，与原局部声明等价（MOD-9
// BackgroundWiring 同款论证）。previewHub.setSelfRegisterEnabled(false) 是
// GUI 模式也会执行的无条件副作用，留在 AppMain 的两次 run 调用之间（原位）。

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstdio>
#include <optional>

#include "HookEventQueue.h"

class AgentProfile;
class DataPort;
class HookEventQueue;
class LedgerImport;
class PluginEventBus;
class PluginLibrary;
class PluginLoader;
class PluginMarket;
class PluginRuntime;
class PreviewHub;
class RoadmapSync;
class TaskRunStore;
class UpdateManager;

inline void emitCliLine(const QString &line)
{
    const QByteArray bytes = line.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// ---- 早期命令（依赖对象构造早、彼此之间穿插散点构造）----

std::optional<int> runDataMode(DataPort &dataPort, const QStringList &arguments);
std::optional<int> runSyncMode(RoadmapSync &roadmap, const QStringList &arguments);
std::optional<int> runEmitHook(const QStringList &arguments);
std::optional<int> runFileAssoc(const QStringList &arguments);
std::optional<int> runInstallPluginFile(const QStringList &arguments);

// ---- plugin-report（HookEventQueue 构造之前）----

std::optional<int> runPluginReport(PluginLoader &pluginLoader,
                                   PluginRuntime &pluginRuntime,
                                   AgentProfile &agentProfile,
                                   const QStringList &arguments);

// ---- 晚期命令集（HookEventQueue 构造之后；GUI 段还要用它，故作成员）----

struct LateCliRunner {
    LateCliRunner(PluginMarket &market, PluginLibrary &library,
                  PreviewHub &preview, UpdateManager &updates,
                  TaskRunStore &runs, LedgerImport &ledger,
                  PluginEventBus &eventBus);
    ~LateCliRunner();

    // --market-source / --market-sources / --market-refresh / --market-install /
    // --install-plugin-url
    std::optional<int> runMarketCli(const QStringList &arguments);
    // --preview-* / --update-* / --run-summary / --import-ledger
    std::optional<int> runPreviewAndUpdateCli(const QStringList &arguments);

    HookEventQueue hookQueue;
    PluginMarket &pluginMarket;
    PluginLibrary &pluginLibrary;
    PreviewHub &previewHub;
    UpdateManager &updateManager;
    TaskRunStore &taskRuns;
    LedgerImport &ledgerImport;
};
