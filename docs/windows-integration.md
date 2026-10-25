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

## 验证方式

| 层次 | 手段 |
| --- | --- |
| 纯逻辑 | `tests/tst_windowsintegration.cpp`（命令行拼装、`%1` 保留、参数提取、状态结构） |
| 编译 | CI `Windows 编译与自检`；本地可用 `cl /c` 直接编 `WindowsIntegration.cpp` |
| 注册表闭环 | 探针程序按 注册 → `--assoc-status` 语义查询 → 注销 → 复查 顺序执行，断言最终回到 not-registered（不留在开发机上） |
| 启动不回归 | CI 冒烟 `--smoke` 扫描日志关键字（AUMID 失败只应出现 `SetCurrentProcessExplicitAppUserModelID failed` 警告） |

## 尚未实现（明确记录，避免误认为已完成）

- **跳转列表（Jump List）**：`ICustomDestinationList` 已具备实现条件，但任务项必须有
  可执行语义。当前 `--newtask` / `--palette` / `--settings` 只在**冷启动**生效：
  `SingleInstance` 只给主实例发 `"show"`，不转发动作。因此跳转列表要等**单实例参数转发**
  做完（第二实例把动作名写进 `QLocalSocket`，主实例 `actionRequested` 分发）才有意义，
  否则就是"点了没反应"的装饰。
- **Rich Toast（WinRT `ToastNotification`）**：未打包应用要用 WinRT toast 必须先在开始菜单
  建带 AUMID 的快捷方式，改动面大。当前通知走托盘气泡，在 Win10/11 上因已设 AUMID
  会归入操作中心并显示应用名，先满足"能收到、能回看"的诉求。
