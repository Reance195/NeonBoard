#pragma once

// R0-13：启动时序收尾 helper（header-only）。
//
// 为什么单独成文件：`src/main.cpp` 有 1910 行的硬棘轮（scripts/check_module_deps.py
// 的 LINE_LIMITS），本批四项改动若全部内联会直接撑破上限。逻辑外移到这里，
// main.cpp 只保留几行调用点；本文件是头文件，不需要进 CMakeLists 的源文件列表。
//
// 背景：`Main.qml` 根窗口 `visible:false`，靠 dispatch 里的 showAndActivate() 显示；
// 只有窗口真正渲染出首帧，Metrics::startupMeasured 才会触发。因此「首帧之后」
// 的判据就是这个信号，另加 2 秒兜底，防止某些启动路径（无窗口 / 无动作参数）
// 永远等不到首帧而把应用卡在空态。

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QObject>
#include <QTimer>

#include <memory>
#include <thread>

// Splash BOOT 阶段并行预读 .qsb：把 Qt 资源的首次冷 I/O 从首帧挪走。
// 纯读、不等结果，与启动路径并行，不拉长总启动时间。
// 注意：这里预热的是文件加载，GPU 侧的 shader 链接仍由 Qt Quick 首帧完成
// （Qt 6.8.3 把 QShader 放在 RHI 内部头，不在标准 include 路径，无法在此反序列化）。
inline void nbPrewarmShader()
{
    std::thread([]() {
        QElapsedTimer prewarm;
        prewarm.start();
        QFile shaderFile(QStringLiteral(":/shaders/neon.frag.qsb"));
        if (shaderFile.open(QIODevice::ReadOnly))
            qInfo() << "BOOT qsb prewarm" << shaderFile.readAll().size() << prewarm.elapsed();
    }).detach();
}

// 字体族全量枚举 + 度量探针：冷启动不再挡首帧，BOOT font families 日志口径保留。
inline void nbProbeFontFamilies()
{
    QElapsedTimer fontTimer;
    fontTimer.start();
    qInfo() << "BOOT font families" << QFontDatabase::families().size() << fontTimer.elapsed();
    QFontMetrics uiMetrics(QApplication::font());
    qInfo() << "BOOT font metrics"
            << uiMetrics.horizontalAdvance(QStringLiteral("工作台待办进行中已办结"))
            << fontTimer.elapsed();
}

// 首帧信号后执行一次 finalize（字体探针 + 调用方的收尾动作）；
// 信号不来时 2 秒兜底，且用 shared_ptr 保证只执行一次。
template <typename FirstFrameSource, typename FinalizeFn>
inline void nbDeferUntilFirstFrame(FirstFrameSource *source, QObject *context,
                                   FinalizeFn finalize)
{
    auto done = std::make_shared<bool>(false);
    const auto once = [done, finalize]() {
        if (*done)
            return;
        *done = true;
        nbProbeFontFamilies();
        finalize();
    };
    QObject::connect(source, &FirstFrameSource::startupMeasured, context,
                     [context, once]() { QTimer::singleShot(0, context, once); });
    QTimer::singleShot(2000, context, once);
}

// R0-15：空态期一次性初始化的「就绪后重放」接线。
//
// 两阶段建表（openDeferred → 首帧后 finalizeSchema）下，启动早期构造的组件会把
// 一次性初始化打在**还不存在的表**上，失败即永久错过。老库（表已存在）与工具模式
// 的 open() 没有空态，初始化本来就成功，调用方以 deferredSchema 决定是否接线。
//
// 模板化原因（模块守卫）：本文件属 shell(L6)，层序禁止反向 include task / plugin /
// core 的头文件 —— 依赖在调用方 main.cpp（组合根，无模块归属）实例化时才落地，
// 与本文件已有的 nbDeferUntilFirstFrame 泛型风格一致。被调方约定（全部幂等）：
//   tree->reload()                —— 重载边表并刷新派生快照
//   runs->startupMaintenance()    —— 取消跨会话排队 + 恢复心跳超时孤儿 run
//   plugins->replayGrantsBootstrap() —— 空态期被守卫跳过的首次授权引导
template <typename DatabasePtr, typename ReadySignal, typename TreePtr,
          typename RunsPtr, typename PluginsPtr>
inline void nbConnectStartupReplay(bool deferredSchema, DatabasePtr database,
                                   ReadySignal ready, TreePtr tree, RunsPtr runs,
                                   PluginsPtr plugins, QObject *context)
{
    if (!deferredSchema)
        return;
    QObject::connect(database, ready, context, [tree, runs, plugins]() {
        if (tree)
            tree->reload();
        if (runs)
            runs->startupMaintenance();
        if (plugins)
            plugins->replayGrantsBootstrap();
    });
}
