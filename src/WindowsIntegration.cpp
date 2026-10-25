#include "WindowsIntegration.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>   // SHChangeNotify
#include <shobjidl.h> // SetCurrentProcessExplicitAppUserModelID
#endif

namespace {

// 与安装器、快捷方式保持一致：改这里等于改对外契约，必须同步 docs/windows-integration.md
constexpr auto kAppUserModelId = "com.lin.neonboard";
// 自定义 ProgID：只用于 .neon 这一种扩展名，不碰系统既有类型
constexpr auto kClassRoot = "NeonBoard.PluginPackage";
constexpr auto kExtension = ".neon";
constexpr auto kInstallFlag = "--install-plugin-file";

QString currentExePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

} // namespace

namespace WindowsIntegration
{

QString appUserModelId()
{
    return QString::fromLatin1(kAppUserModelId);
}

void applyAppUserModelId()
{
#ifdef Q_OS_WIN
    const QString id = appUserModelId();
    const HRESULT hr =
        SetCurrentProcessExplicitAppUserModelID(reinterpret_cast<LPCWSTR>(id.utf16()));
    if (FAILED(hr)) {
        // 失败不影响功能，只是任务栏/通知会归到默认标识
        qWarning() << "WindowsIntegration: SetCurrentProcessExplicitAppUserModelID failed"
                   << Qt::hex << hr;
    }
#else
    // 其他平台无对应概念：静默跳过，保持调用点无分支
#endif
}

QString buildOpenCommand(const QString &exePath)
{
    // %1 是给 shell 的文件路径占位符，必须原样留在字符串里，
    // 因此这里用 arg() 的三参数形式一次性替换 exe / flag，其余部分不参与格式化。
    const QString exe = QDir::toNativeSeparators(exePath);
    return QStringLiteral("\"%1\" %2 \"%3\"")
        .arg(exe, QString::fromLatin1(kInstallFlag), QStringLiteral("%1"));
}

QString packagePathFromArgs(const QStringList &arguments)
{
    const int index = arguments.indexOf(QString::fromLatin1(kInstallFlag));
    if (index < 0 || index + 1 >= arguments.size())
        return QString();
    return arguments.at(index + 1);
}

bool registerFileAssociations(QString *error)
{
#ifdef Q_OS_WIN
    const QString exe = currentExePath();
    if (exe.isEmpty()) {
        if (error)
            *error = QStringLiteral("无法确定可执行文件路径");
        return false;
    }

    // QSettings 的 NativeFormat 在 Windows 上就是注册表，直接落到 HKCU
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"),
                      QSettings::NativeFormat);
    if (!classes.isWritable()) {
        if (error)
            *error = QStringLiteral("HKCU\\Software\\Classes 不可写（可能被组策略限制）");
        return false;
    }

    const QString classRoot = QString::fromLatin1(kClassRoot);
    classes.setValue(classRoot + QStringLiteral("/Default"), QStringLiteral("NeonBoard 插件包"));
    classes.setValue(classRoot + QStringLiteral("/DefaultIcon/Default"),
                     QStringLiteral("\"%1\",0").arg(exe));
    classes.setValue(classRoot + QStringLiteral("/shell/open/command/Default"),
                     buildOpenCommand(exe));

    // 扩展名 → ProgID；Content Type 让资源管理器按包类型显示
    classes.setValue(QString::fromLatin1(kExtension) + QStringLiteral("/Default"), classRoot);
    classes.setValue(QString::fromLatin1(kExtension) + QStringLiteral("/Content Type"),
                     QStringLiteral("application/x-neon-package"));
    classes.sync();

    if (classes.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("写入注册表失败（QSettings 状态异常）");
        return false;
    }

    // 通知资源管理器刷新关联缓存，否则双击图标要等下一次登录才生效
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
#else
    if (error)
        *error = QStringLiteral("文件关联仅支持 Windows");
    return false;
#endif
}

bool unregisterFileAssociations(QString *error)
{
#ifdef Q_OS_WIN
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"),
                      QSettings::NativeFormat);

    const QString classRoot = QString::fromLatin1(kClassRoot);
    const QString extension = QString::fromLatin1(kExtension);

    // 只删自己写过的键：扩展名先摘掉指向本 ProgID 的默认值，
    // 再移除本 ProgID 子树，不动其他程序对该扩展名的设置。
    const QString currentTarget = classes.value(extension + QStringLiteral("/Default")).toString();
    if (currentTarget.compare(classRoot, Qt::CaseInsensitive) == 0)
        classes.remove(extension + QStringLiteral("/Default"));
    classes.remove(extension + QStringLiteral("/Content Type"));
    classes.remove(classRoot);
    classes.sync();

    if (classes.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("删除注册表项失败（QSettings 状态异常）");
        return false;
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
#else
    if (error)
        *error = QStringLiteral("文件关联仅支持 Windows");
    return false;
#endif
}

QVariantMap associationStatus()
{
    const QString classRoot = QString::fromLatin1(kClassRoot);
    const QString expected = buildOpenCommand(currentExePath());

    QVariantMap status;
    status.insert(QStringLiteral("classRoot"), classRoot);
    status.insert(QStringLiteral("extension"), QString::fromLatin1(kExtension));
    status.insert(QStringLiteral("expectedCommand"), expected);

#ifdef Q_OS_WIN
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"),
                      QSettings::NativeFormat);
    const QString target = classes.value(QString::fromLatin1(kExtension)
                                         + QStringLiteral("/Default"))
                               .toString();
    const QString command = classes.value(classRoot + QStringLiteral("/shell/open/command/Default"))
                                .toString();
    status.insert(QStringLiteral("command"), command);
    // 三者同时匹配才算"已关联到当前这个 exe"，避免换目录后误报
    status.insert(QStringLiteral("registered"),
                  target.compare(classRoot, Qt::CaseInsensitive) == 0
                      && command.compare(expected, Qt::CaseInsensitive) == 0);
#else
    status.insert(QStringLiteral("command"), QString());
    status.insert(QStringLiteral("registered"), false);
#endif

    return status;
}

} // namespace WindowsIntegration
