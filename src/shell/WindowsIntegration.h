#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

// Windows 原生集成（R4-02）：进程身份、文件关联、跳转列表。
//
// 设计约束：
//   1. **fail-soft**：系统调用失败只记日志，绝不影响主流程启动。这类"锦上添花"
//      的能力不该成为启动失败的原因（跳转列表在受限环境会直接失败）。
//   2. **只写 HKCU\Software\Classes**：不需要管理员，也不改系统级关联、不抢默认
//      打开方式；卸载时按同一批键精确删除，不碰第三方写入的值。
//   3. **纯逻辑与系统调用分离**：命令行拼装、参数提取做成纯函数，便于单元测试
//      （见 tests/tst_windowsintegration.cpp）。
namespace WindowsIntegration
{

// 任务栏分组与通知归属用的 AppUserModelID。
// ⚠ 安装器写快捷方式时用的 AUMID 必须与此一致，否则通知会归到"未知应用"。
QString appUserModelId();

// 进程级身份：须在窗口创建前调用，否则任务栏已完成分组、改不动了。
void applyAppUserModelId();

// ---- 纯函数（可单测，不含任何系统调用）----

// shell\open\command 的值。可执行路径可能含空格，必须整体加引号；
// 末尾的 %1 是 shell 传入的文件路径占位符，由系统替换，不参与本地格式化。
QString buildOpenCommand(const QString &exePath);

// 从命令行参数里取 --install-plugin-file 之后的值；缺失返回空串。
QString packagePathFromArgs(const QStringList &arguments);

// ---- 文件关联（Windows 专用；其他平台返回 false 并说明原因）----
bool registerFileAssociations(QString *error);
bool unregisterFileAssociations(QString *error);

// 当前注册状态：{registered, classRoot, extension, command, expectedCommand}
// 只读，不产生任何写入 —— 供 --assoc-status 与排障使用。
QVariantMap associationStatus();

// ---- 跳转列表（任务栏右键菜单里的自定义任务）----

// 一个任务项：title 显示在右键菜单里，arguments 是点击后传给本进程的命令行。
// 二者的联系是硬约束 —— 参数必须是冷启动认得的开关（--newtask 等），
// 否则点击后只会亮一下窗口，等于装饰。
struct JumpListTask
{
    QString title;
    QString arguments;
};

// 任务项定义（纯数据，顺序即菜单里的顺序）。
// 单测守着"参数是已知开关"，避免改动后与 AppActions 脱节。
QVector<JumpListTask> jumpListTasks();

// 把任务项写进系统跳转列表。Windows 专用；其他平台返回 false 并说明原因。
// fail-soft：系统调用失败只写日志，调用方无需处理（跳转列表只是锦上添花）。
bool applyJumpList(QString *error);

} // namespace WindowsIntegration
