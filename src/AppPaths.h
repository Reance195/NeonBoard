#pragma once

#include <QString>

// 本地状态路径解析：集中处理多代理并行时的沙箱隔离开关。
// NEONBOARD_DATA_DIR 生效时，数据库、插件目录、日志、界面设置全部落在该目录下，
// 多个 agent / 多份 worktree 互不干扰。
class AppPaths
{
public:
    // 沙箱数据目录：NEONBOARD_DATA_DIR 优先，否则回落到系统应用数据目录
    static QString dataDir();
    // 插件安装目录（唯一可写区）
    static QString pluginDir();
    // 日志文件路径
    static QString logFilePath();
    // 钩子事件队列数据库路径（与看板库物理隔离）
    static QString hookDbPath();
    // 单实例本地服务器 key，NEONBOARD_INSTANCE 可加后缀实现多实例共存
    static QString singleInstanceKey();
    // 当前是否处于沙箱隔离状态
    static bool isolated();
    // 可执行文件位于 .worktrees/<id>/ 下时返回 <id>，否则返回空：
    // 用于在忘记设 NEONBOARD_DATA_DIR 时自动落沙箱，避免写脏真实数据
    static QString worktreeAgentId();
    // 可执行文件所属 worktree 的绝对路径，不在 worktree 内时返回空
    static QString worktreeRoot();
};
