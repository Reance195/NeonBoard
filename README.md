# NeonBoard

个人工作台：赛博霓虹风格的三列任务看板。Qt 6.8 + QML + C++20，本地 SQLite 存储。

![NeonBoard](docs/screenshot.png)

## 功能

- 待办 / 进行中 / 已办结三列看板，SQLite 本地持久化
- 快速添加（Enter）、双击编辑、优先级、标签、截止日期
- 搜索、统计芯片、完成 / 恢复 / 删除
- 删除后 5 秒内可撤销，或按 `Ctrl+Z`
- 卡片拖拽跨列移动（落到目标列末尾）
- 无边框窗口、自绘标题栏、霓虹 Shader 背景、玻璃卡片
- 外观面板：6 套主题预设（霓虹青 / 赛博紫 / 落日橙 / 深海蓝 / 森野绿 / 极夜灰），切换即全应用（含霓虹背景）渐变
- 画质三档（低 / 中 / 高）控制背景特效密度与刷新率，另有「减弱动效」开关
- 动效链：卡片滑入进场、悬停浮起、按下微沉、拖起放大、拖入目标列边框呼吸、落位涟漪、勾选完成光扫、删除故障抖动、撤销滑回、逾期呼吸描边、统计数字滚动
- 启动 Splash，主界面加载期间先显示品牌启动图
- 设置 / 插件 / 外观三个面板改为按需加载，首帧不再为空面板付出代价（QML 树构建耗时下降约 250–490 ms）
- 里程碑进度条：按 R0 / R1 / R2 / R3 聚合看板完成度，一眼看出哪个阶段收口了；点击阶段即按阶段过滤看板
- 路线图双向绑定：仓库 `progress.json` 与本地看板自动同步（启动拉取、看板改动写回、提交钩子投递）
- 系统托盘、全局热键、开机自启、单实例、窗口位置记忆
- 运行时 QML 插件：权限清单、独立存储、宿主任务 API

## 快捷键

| 快捷键 | 作用 |
| --- | --- |
| `Ctrl+Shift+Space` | 全局显示 / 隐藏窗口 |
| `Ctrl+N` | 聚焦快速添加 |
| `Ctrl+Z` | 撤销删除 |
| `Ctrl+,` | 打开设置 |
| `Enter` | 提交快速添加 |
| `Esc` | 清空搜索 / 关闭弹窗 |

## 运行

安装版：

```text
dist/NeonBoard-Setup-0.1.0.exe
```

便携版：

```text
dist/NeonBoard/NeonBoard.exe
```

数据与日志：

```text
%APPDATA%\com.lin\NeonBoard\board.db
%APPDATA%\com.lin\NeonBoard\neonboard.log
```

插件目录：

```text
%APPDATA%\com.lin\NeonBoard\plugins\<plugin-id>\
<安装目录>\plugins\<plugin-id>\
```

## 开发协作

本仓库采用分支工作流，完整规约见 [AGENTS.md](AGENTS.md)：

- 改动必须在独立分支上完成，分支名带 `feature/` `fix/` `docs/` 等类型前缀
- `main` 禁止直接提交，由 Git 钩子强制拦截
- 改完用 `git merge --no-ff` 合并回 `main`，随后删除分支

一键合并回主分支：

```powershell
pwsh -File scripts/git-done.ps1
```

逐步操作与钩子排查见 [docs/git-workflow.md](docs/git-workflow.md)，贡献约定见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 开发构建

依赖：Qt 6.8.3 MSVC 2022 x64、Visual Studio 2022 BuildTools、CMake 3.21+。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
```

生成便携目录：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/deploy.ps1
```

生成安装包：

```powershell
& "C:\Users\lschenglin\AppData\Local\Programs\Inno Setup 6\ISCC.exe" installer\neonboard.iss
```

调试参数：

```text
--selftest   数据库增删改查自检
--smoke      启动 4 秒后输出指标并退出（不占用单实例锁）
--benchload [minimal|controls|button]
             分档加载探针界面，量化启动各段开销
--settings   启动后打开设置
--plugins    启动后打开插件面板
--appearance 启动后打开外观面板（主题 / 画质 / 减弱动效）
--stage <R0|R1|R2|R3>
             启动后按里程碑过滤看板（不填则显示全部）
--sync-roadmap [path]
             从路线图快照导入看板（默认取仓库 progress.json）
--export-roadmap [path]
             把看板现状写回路线图快照
--newtask    启动后聚焦快速添加
--autostart  静默启动到托盘
```

环境变量：

```text
NEONBOARD_NO_BG=1   关闭动态霓虹背景（低配 / 排障）
NEONBOARD_ROADMAP   路线图快照路径（默认自动探测仓库内 progress.json）
NEONBOARD_ROADMAP_AUTOWRITE=0
                    关闭「看板改动自动写回快照」
```

## 项目结构

```text
src/                 C++：数据库、模型、托盘、热键、窗口、插件宿主、界面设置、路线图同步
qml/                 QML：主界面、霓虹背景
  theme/             主题与动效单例（Theme / Motion）
  components/        卡片、看板列、统计芯片、窗口按钮、外观面板、设置面板、插件面板、阶段进度条
shaders/             GLSL Shader
plugins-samples/     示例插件
installer/           Inno Setup 脚本
scripts/             构建部署脚本
docs/                截图、迭代路线图与插件文档
docs/themes/         6 套主题预设实机截图
.github/workflows/   GitHub Actions 持续集成流水线
```

## 持续集成

仓库内置 GitHub Actions 流水线（`.github/workflows/ci.yml`），推送或合并到 `main` 时自动在 `windows-2022` 上执行：

1. 安装 MSVC 开发环境与 Qt 6.8.3（`install-qt-action`，含 `qtshadertools` addon）
2. `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` 配置并编译 Release
3. `NeonBoard.exe --selftest` 数据库自检
4. `--smoke` 冒烟启动，并扫描运行日志，出现 QML / C++ 错误即判定失败
5. `scripts/deploy.ps1 -SkipBuild` 打包便携版，校验产物存在

本地脚本 `scripts/deploy.ps1` 的工具定位顺序为「环境变量 → PATH → 已知安装路径」，因此在 CI 与本机都能直接运行。

## 插件

插件使用 QML 编写，放入插件目录后重启应用生效。详见 [docs/plugin-api.md](docs/plugin-api.md)。

当前 MVP 的插件边界：

- 每个插件提供一个 `main.qml` 侧栏面板
- 支持权限：`storage`、`notifications`、`tasks.read`、`tasks.write`、`openUrl`
- 不支持热重载、多槽位、命令注册、HTTP API、插件设置页
- 插件运行在宿主 QML 引擎内，属于受信插件，不是沙箱

## 已知限制

- 首次运行受磁盘与页缓存影响，主界面加载约 5–9 秒；之后热启动稳定在 1.2–1.4 秒。Splash 会先显示
- 拖拽跨列时落到目标列末尾，不做列内精确插位
- 手工改 `progress.json` 时请同步抬高该条目的 `updatedAt`，否则会以看板版本为准
- 安装包未签名，Windows SmartScreen 可能提示
- 未实现 JSON 导入导出、命令面板、插件热重载
- Qt 采用 LGPL 动态链接；再分发时需保留 Qt 动态库与许可说明

## 第三方

Qt 6.8.3（LGPLv3，动态链接）、SQLite（Qt SQL 驱动）。示例插件与界面代码为本项目自有。
