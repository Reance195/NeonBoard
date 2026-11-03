#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// 应用动作（R4-02）：把「冷启动参数」与「二次启动的本地 socket 消息」收敛成
// 同一个字符串标识，由 main() 走同一条分发路径。
//
// 为什么需要这层：跳转列表、托盘菜单、二次启动、命令行开关，本质都是
// 「请已经在跑的那个实例做某事」的不同入口。各入口各写一套的话行为必然分叉
// —— 典型症状是托盘能开新建面板、跳转列表点了却只亮窗口。
//
// 这里全部是纯字符串换算，不碰窗口/QML 对象，因此可以离线单测
// （见 tests/tst_appactions.cpp）。
namespace AppActions
{

// 动作标识：既是 IPC 消息体，也是命令行开关去掉 "--" 后的名字。
inline constexpr auto kShow = "show";
inline constexpr auto kNewTask = "newtask";
inline constexpr auto kSettings = "settings";
inline constexpr auto kPlugins = "plugins";
inline constexpr auto kAppearance = "appearance";
// 主题包市场（R8-02）：--theme-market
inline constexpr auto kThemeMarket = "thememarket";
inline constexpr auto kLibrary = "library";
// 项目全景（docs/project-map.json 驱动的只读地图）：排除故障时用命令行直接开
inline constexpr auto kMap = "map";
// 静默拉起：开机自启/计划任务在应用已运行时又起一份，主实例收到后只登记、
// 不亮窗口（否则每次登录都会莫名弹窗）。
inline constexpr auto kNoop = "noop";
// 前缀形式，查询串跟在冒号后：palette:<query>
inline constexpr auto kPalettePrefix = "palette:";
// 卡片详情抽屉（R8-03）：带卡片 id 的前缀形式，carddetail:<taskId>。
// 单独一个 id 开关也要走这条既有的「前缀动作」路子，而不是新开一条分支 ——
// 命令行、二次启动、托盘菜单最后是同一份 Target，行为不会分叉。
inline constexpr auto kCardDetailPrefix = "carddetail:";

// 冷启动参数 → 动作。
// 多个面板开关同时给时按固定优先级取第一个：
// palette > carddetail > newtask > settings > plugins > appearance > map > library，
// 都没有则 show。优先级写死成一条链，是为了「同一组参数永远得到同一个动作」。
QString fromArguments(const QStringList &arguments);

// 是否为本模块认识的动作（含 palette:<query>）。
// IPC 收端用它拒掉陌生消息：本地管道名是可猜的，不设白名单等于把
// 「让主实例执行任意 QML 方法」的路由暴露给同机任意进程。
bool isKnown(const QString &action);

// 动作 → QML 调用。
// method 为空表示「把窗口亮出来就够」；hasArgument 区分「无参方法」与
// 「参数恰为空串」（例如空查询的命令面板），二者不能用空串混为一谈。
struct Target
{
    QString method;
    QString argument;
    bool hasArgument = false;
};

Target targetFor(const QString &action);

// ---- IPC 帧 ----
// 单行文本，带版本前缀：新旧版本混跑时，旧版发来的裸 "show" 会被判为陌生消息
// 并回退到默认行为，而不是被误解析成某个动作。
QByteArray encodeFrame(const QString &action);

// 解析失败（无前缀 / 超长 / 动作不认识）返回空串，调用方退回 show。
QString decodeFrame(const QByteArray &payload);

} // namespace AppActions
