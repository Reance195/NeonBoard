# Windows 原生集成（R4-02）

目标：让 NeonBoard 在 Windows 上表现得像"原生应用"，而不是"跑着 Qt 的窗口"。
所有系统调用一律 **fail-soft**：失败只记日志，绝不影响启动。

## 已落地

### 1. 应用标识（AppUserModelID）

进程启动时调用 `SetCurrentProcessExplicitAppUserModelID("com.lin.neonboard")`
（`src/WindowsIntegration.cpp` → `applyAppUserModelId()`，在 `main()` 建窗口前执行）。

**为什么必须有**：不设的话任务栏会把程序归到一个由 exe 路径推导的临时标识，
换目录安装就变成两个"不同应用"；系统通知也会归到"未知应用"、无法在操作中心里
按应用聚合。设了之后，任务栏分组、通知归属、快捷方式(AUMID)三者才能对上。

⚠ 该 AUMID 是对外契约：安装器给快捷方式写 `System.AppUserModel.ID` 时必须用同一个值，
否则通知仍然归错。改动点在 `kAppUserModelId`，同步改这里与安装器。

### 2. `.neon` 插件包文件关联

注册后双击插件包即安装（走既有 CLI `--install-plugin-file <path>`）。

```powershell
NeonBoard.exe --register-file-assoc     # 注册（写 HKCU，无需管理员）
NeonBoard.exe --assoc-status            # 查询：OK registered / OK not-registered + 当前命令
NeonBoard.exe --unregister-file-assoc    # 注销（只删自己写的键）
```

写入的键（全部位于 `HKCU\Software\Classes`，不碰 `HKLM`、不需要提权）：

| 键 | 值 |
| --- | --- |
| `.neon\(默认)` | `NeonBoard.PluginPackage` |
| `.neon\Content Type` | `application/x-neon-package` |
| `NeonBoard.PluginPackage\(默认)` | `NeonBoard 插件包` |
| `NeonBoard.PluginPackage\DefaultIcon\(默认)` | `"<exe路径>",0` |
| `NeonBoard.PluginPackage\shell\open\command\(默认)` | `"<exe路径>" --install-plugin-file "%1"` |

**边界与安全**：

- 只注册 `.neon` 这一个**本应用专属**扩展名，不抢 `.md`/`.json` 等公共类型。
- 命令里的 `%1` 是 shell 传入的文件路径占位符，**必须原样保留**（有单测守住这一点：
  被本地格式化吃掉的话，双击会"打开程序但不安装"）。
- 可执行路径整体加引号（路径常含空格），且路径取自 `applicationFilePath()`，
  换目录后 `--assoc-status` 会显示 not-registered（三者同时匹配才算已关联）。
- 注销时先确认 `.neon` 的默认值确实指向本 ProgID 才摘除，再删本 ProgID 子树 ——
  不动其他程序对该扩展名可能做的设置。
- 注册/注销后调用 `SHChangeNotify(SHCNE_ASSOCCHANGED)` 刷新资源管理器缓存，
  否则图标要等下次登录才变。

### 3. 二次启动的动作转发（`SingleInstance`）

单实例探测从"只发 `show`"升级为**带动作的消息**：第二次启动不再只是把窗口亮出来，
而是把"用户到底想干什么"转交给已在运行的主实例。

```
第二实例                           主实例
  connectToServer(管道名)  ──►  newConnection
  write("NB1 <action>\n")        readyRead → 累积到换行
  disconnectFromServer()   ──►   disconnected → 解析 → actionRequested(action)
  退出(0)                        dispatch(action, 0) → 唤起窗口 + 打开对应面板
```

要点：

- **帧格式单行 + 版本前缀** `NB1 `：旧版只发裸 `show`，没有前缀会被判为陌生消息并
  回退成"亮窗口"—— 新旧版本混跑时行为可控，不会把旧消息误解析成某个面板。
- **收端白名单**：`AppActions::isKnown()` 只放行固定动作名（含 `palette:<query>`）。
  本地管道名是可猜的，不设白名单等于把"让主实例执行任意 QML 方法"的路由暴露给同机进程。
- **单行约束**：命令面板查询串里的换行会被折叠成空格，否则后半截会被当成分帧边界。
- **静默拉起**：`--autostart` 撞上已在运行的实例时发 `noop`，主实例什么都不做 ——
  开机登录不该莫名弹窗。
- **主动断连再退出**：第二实例写完立刻退出时，内核里可能还压着未读数据；
  先 `disconnectFromServer()` 并等断连完成，主实例才能收到完整帧。

### 4. 跳转列表（任务栏右键任务项）

`ICustomDestinationList` + `IShellLinkW` 写入四个任务项，参数直接复用冷启动开关，
因此"点击任务项"与"命令行带参数启动"是同一条代码路径（见 `src/AppActions.h`）。

| 任务项 | 参数 |
| --- | --- |
| 新建任务 | `--newtask` |
| 搜索命令面板 | `--palette` |
| 插件库 | `--library` |
| 偏好设置 | `--settings` |

实现要点：

- **标题必须写 `PKEY_Title`**：`IShellLinkW::SetDescription` 只影响悬停提示，
  不写标题的话四项会全部显示成 exe 名。
- **AUMID 显式绑定**：`ICustomDestinationList::SetAppID()` 让任务项挂到与任务栏按钮
  同一个应用标识下；失败只记日志（精简 shell 可能不支持）。
- **最小 `IObjectArray` 实现**：SDK 不带现成实现，这里自带一个引用计数托管的只读数组；
  CLSID 也用本地常量，不额外依赖 `uuid.lib`。
- **单项失败不拖垮整张菜单**：任一 `IShellLink` 创建失败只跳过该项。
- **fail-soft**：整体失败只写日志 `JUMPLIST skipped:`，不影响启动。

## 验证方式

| 层次 | 手段 |
| --- | --- |
| 纯逻辑 | `tests/tst_windowsintegration.cpp`（命令行拼装、`%1` 保留、参数提取、状态结构、跳转列表任务项可点击） |
| 纯逻辑 | `tests/tst_appactions.cpp`（参数→动作优先级、动作白名单、动作→QML 调用、IPC 帧边界） |
| 编译 | CI `Windows 编译与自检`；本地可用 `cl /c` 直接编 `WindowsIntegration.cpp` |
| 注册表闭环 | 探针程序按 注册 → `--assoc-status` 语义查询 → 注销 → 复查 顺序执行，断言最终回到 not-registered（不留在开发机上） |
| 启动不回归 | CI 冒烟 `--smoke` 扫描日志关键字（AUMID 失败只应出现 `SetCurrentProcessExplicitAppUserModelID failed` 警告） |
| 二次启动 | 先开一个实例，再执行 `NeonBoard.exe --palette 周报`；预期：不开第二个窗口/进程，原实例被唤起并弹出命令面板（日志 `JUMPLIST applied` 附近无异常） |
| 跳转列表 | 右键任务栏图标 → 应看到「新建任务 / 搜索命令面板 / 插件库 / 偏好设置」四项；逐项点击应直接打开对应面板（不是只亮窗口） |

## 尚未实现（明确记录，避免误认为已完成）

- **Rich Toast（WinRT `ToastNotification`）**：未打包应用要用 WinRT toast 必须先在开始菜单
  建带 AUMID 的快捷方式，改动面大。当前通知走托盘气泡，在 Win10/11 上因已设 AUMID
  会归入操作中心并显示应用名，先满足"能收到、能回看"的诉求。
- **跳转列表的"最近打开"分类**：只做了"任务"（`AddUserTasks`），没有维护
  `IShellItemArray` 的 `Recent`/`Frequent` 分类 —— 那是另一套"记录用户访问文件"的
  逻辑，与当前"任务项即命令"的定位不同。
