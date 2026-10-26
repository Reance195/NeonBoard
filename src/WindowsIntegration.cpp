#include "WindowsIntegration.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>   // SHChangeNotify
#include <shobjidl.h> // SetCurrentProcessExplicitAppUserModelID / IShellLinkW / ICustomDestinationList
#include <propidl.h>  // PROPVARIANT
#include <propsys.h>  // IPropertyStore
#include <propkey.h>  // PKEY_Title
#include <cstring>    // memcpy / memset
#include <vector>
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

#ifdef Q_OS_WIN

// SDK 是否把这些 CLSID 以 selectany 常量提供，随工具链/宏设置而变；
// 自带常量可以完全不依赖 uuid.lib —— 跳转列表不值得为它新增链接依赖。
const CLSID kClsidShellLink = {0x00021401, 0x0000, 0x0000,
                               {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const CLSID kClsidDestinationList = {0x77f10cf0, 0x3db5, 0x4966,
                                     {0xb5, 0x20, 0xb7, 0xc5, 0x4f, 0xd3, 0x5e, 0xd6}};

QString hresultHex(HRESULT hr)
{
    return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

// ICustomDestinationList::AddUserTasks 只接受 IObjectArray，而 SDK 不带现成实现。
// 这里给一个最小只读数组：append 时 AddRef 托管元素，析构统一 Release。
class TaskObjectArray final : public IObjectArray
{
public:
    TaskObjectArray() = default;

    void append(IUnknown *item)
    {
        if (!item)
            return;
        item->AddRef();
        m_items.push_back(item);
    }

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
    {
        if (!out)
            return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IObjectArray)) {
            *out = static_cast<IObjectArray *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --m_refs;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    // ---- IObjectArray ----
    HRESULT STDMETHODCALLTYPE GetCount(UINT *count) override
    {
        if (!count)
            return E_POINTER;
        *count = static_cast<UINT>(m_items.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAt(UINT index, REFIID riid, void **out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (index >= static_cast<UINT>(m_items.size()))
            return E_INVALIDARG;
        return m_items[index]->QueryInterface(riid, out);
    }

private:
    ~TaskObjectArray()
    {
        for (IUnknown *item : m_items)
            item->Release();
    }

    ULONG m_refs = 1;
    std::vector<IUnknown *> m_items;
};

// 菜单里显示的名字来自 PKEY_Title（IShellLink 的 SetDescription 只影响悬停提示）。
// 不设标题的话四项会全部显示成 exe 名字，用户根本分不清。
bool setLinkTitle(IShellLinkW *link, const QString &title)
{
    IPropertyStore *store = nullptr;
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(&store))) || !store)
        return false;

    PROPVARIANT value;
    std::memset(&value, 0, sizeof(value)); // 全零即 VT_EMPTY
    const UINT bytes = static_cast<UINT>((title.size() + 1) * sizeof(wchar_t));
    value.pwszVal = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
    bool ok = false;
    if (value.pwszVal) {
        std::memcpy(value.pwszVal, title.utf16(), bytes);
        value.vt = VT_LPWSTR;
        ok = SUCCEEDED(store->SetValue(PKEY_Title, value)) && SUCCEEDED(store->Commit());
        CoTaskMemFree(value.pwszVal); // 属性存储已复制一份
    }
    store->Release();
    return ok;
}

IShellLinkW *createTaskLink(const QString &exe, const WindowsIntegration::JumpListTask &task)
{
    IShellLinkW *link = nullptr;
    if (FAILED(CoCreateInstance(kClsidShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link)))
        || !link) {
        return nullptr;
    }

    // 路径/参数都是 UTF-16 直传：绕开本地代码页，中文安装路径不会变乱码
    const auto *exeWide = reinterpret_cast<LPCWSTR>(exe.utf16());
    link->SetPath(exeWide);
    link->SetArguments(reinterpret_cast<LPCWSTR>(task.arguments.utf16()));
    link->SetIconLocation(exeWide, 0);
    setLinkTitle(link, task.title);
    return link;
}

#endif // Q_OS_WIN

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

QVector<JumpListTask> jumpListTasks()
{
    // 顺序即菜单顺序：最常用的"新建任务"放第一条。
    // 参数一律是冷启动开关 —— 二次启动时由 SingleInstance 转发给主实例执行，
    // 因此这里不需要为"应用已在运行"另写一套逻辑。
    return {
        {QStringLiteral("新建任务"), QStringLiteral("--newtask")},
        {QStringLiteral("搜索命令面板"), QStringLiteral("--palette")},
        {QStringLiteral("插件库"), QStringLiteral("--library")},
        {QStringLiteral("偏好设置"), QStringLiteral("--settings")},
    };
}

bool applyJumpList(QString *error)
{
#ifdef Q_OS_WIN
    // 跳转列表依赖 COM。主线程一般已是 STA：此时返回 S_FALSE 且仍需成对
    // CoUninitialize；RPC_E_CHANGED_MODE 表示已被初始化成别的套间模型，
    // 沿用现有模型即可，不做回滚。
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool ownsInit = SUCCEEDED(initResult);
    struct ApartmentGuard
    {
        bool owns;
        ~ApartmentGuard()
        {
            if (owns)
                CoUninitialize();
        }
    } guard{ownsInit};

    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
        if (error)
            *error = QStringLiteral("COM 初始化失败（%1）").arg(hresultHex(initResult));
        return false;
    }

    const QString exe = currentExePath();
    if (exe.isEmpty()) {
        if (error)
            *error = QStringLiteral("无法确定可执行文件路径");
        return false;
    }

    ICustomDestinationList *list = nullptr;
    HRESULT hr = CoCreateInstance(kClsidDestinationList, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&list));
    if (FAILED(hr) || !list) {
        if (error)
            *error = QStringLiteral("创建 ICustomDestinationList 失败（%1）").arg(hresultHex(hr));
        return false;
    }

    // 显式绑定 AUMID：任务项要挂到与任务栏按钮同一个应用标识下，
    // 否则会散落在"隐式标识"里，右键看不到
    const QString appId = appUserModelId();
    hr = list->SetAppID(reinterpret_cast<LPCWSTR>(appId.utf16()));
    if (FAILED(hr)) {
        // 精简 shell / 受限会话可能不支持；不影响后续 AddUserTasks
        qWarning() << "WindowsIntegration: jump list SetAppID failed" << hresultHex(hr);
    }

    UINT reservedSlots = 0;
    IObjectArray *removed = nullptr;
    hr = list->BeginList(&reservedSlots, IID_PPV_ARGS(&removed));
    if (removed)
        removed->Release();
    if (FAILED(hr)) {
        list->Release();
        if (error)
            *error = QStringLiteral("BeginList 失败（%1）").arg(hresultHex(hr));
        return false;
    }

    TaskObjectArray *tasks = new TaskObjectArray; // 引用计数托管，随 Release 自毁
    for (const JumpListTask &task : jumpListTasks()) {
        IShellLinkW *link = createTaskLink(exe, task);
        if (!link) {
            // 单项失败不拖垮整张菜单：其余的照样提交
            qWarning().noquote() << "WindowsIntegration: 跳转列表任务项创建失败" << task.arguments;
            continue;
        }
        tasks->append(link);
        link->Release();
    }

    hr = list->AddUserTasks(tasks);
    if (SUCCEEDED(hr))
        hr = list->CommitList();
    tasks->Release();
    list->Release();

    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("提交跳转列表失败（%1）").arg(hresultHex(hr));
        return false;
    }
    return true;
#else
    if (error)
        *error = QStringLiteral("跳转列表仅支持 Windows");
    return false;
#endif
}

} // namespace WindowsIntegration
