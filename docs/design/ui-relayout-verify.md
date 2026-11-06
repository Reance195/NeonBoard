# R11 界面重排核验记录

> 规格：`docs/design/ui-relayout-spec.md`；实施卡：R11-8 / R11-9 / R11-10 / R11-11 / R11-12。
> 本文件是 R11 收尾核验（R11-12）的唯一事实源：结论、实测值、判定口径都写在这里，
> 下次改界面的人以这份记录为准，不以「当时看着没问题」为准。

## 怎么测的

本机没有 Orca CLI，拿不到 GUI 截图（与台账 DX-102 记录一致），所以核验走**程序化埋点**：
在 `qml/Main.qml` 末尾临时插入一个埋点块，按定时器逐个打开 17 个界面，量壳宽、
头 / 底是否随正文滚动、关闭路径是否走通，把结果打进应用日志（`neonboard.log`），
再把埋点块整块回滚。**测完的埋点不在交付物里**（`git diff origin/main..HEAD -- qml/Main.qml`
不含「临时埋点」字样）。

沙箱隔离：`NEONBOARD_DATA_DIR` 指向 `.sandbox/probe-*`，`NEONBOARD_ROADMAP_AUTOWRITE=0`，
不碰真实数据目录与仓库快照。

## 一、17 个界面逐个打开

形态与尺寸档对规格 §3.4 落位表；「头固定 / 底固定」的判据是**行为**而非结构 ——
把正文的滚动位置推到最大，若头与底的 `y` 不变且不在滚动体子树内，即为固定。

| # | 界面 | 形态 | 壳宽 | 头固定 | 底固定 | 关闭行为 |
| --- | --- | --- | --- | --- | --- | --- |
| 01 | 新建任务 `TaskComposer` | 居中弹窗 | 440 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |
| 02 | 命令面板 `CommandPalette` | 居中弹窗 | 720 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |
| 03 | 设置 `SettingsPanel` | 居中弹窗 | 720 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |
| 04 | 外观 `AppearancePanel` | 右侧抽屉 | 320 | 固定 | 无底栏 | 关闭键 → `closeRequested`；Esc 组件自接 |
| 05 | 插件面板 `PluginPanel` | 右侧抽屉 | 320 | 固定 | 固定（48） | 关闭键 → `closeRequested`；Esc 组件自接 |
| 06 | 插件库 `PluginLibraryPanel` | 居中弹窗 | 720 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |
| 07 | 权限授权 `PluginPermissionDialog` | 居中弹窗 | 440 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()`（三条路径逐条实测，见下） |
| 08 | 预览中心 `PreviewPanel` | 居中弹窗 | 720 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |
| 09 | 项目全景 `ProjectMapPanel` | 右侧抽屉 | 320 | 固定 | 无底栏 | 关闭键 → `closeRequested`；Esc 组件自接 |
| 10 | 列管理 `ColumnManager` | 右侧抽屉 | 320 | 固定 | 固定（48） | 关闭键 → `closeRequested`；Esc 组件自接 |
| 11 | 新手引导 `OnboardingOverlay` | 居中弹窗 | 560 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |
| 12 | 卡片详情 `CardDetailPanel` | 居中弹窗 | 560 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |
| 13 | 主题市场 `ThemeMarketPanel` | 居中弹窗 | 560 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |
| 14 | 执行前确认 `execConfirmDialog` | 居中弹窗 | 560 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |
| 15 | 执行面板 `execPanelDialog` | 居中弹窗 | 560 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |
| 16 | 夜间批量 `nightBatchDialog` | 居中弹窗 | 560 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |
| 17 | 执行队列 `execQueueDialog` | 居中弹窗 | 720 | 固定 | 固定（48） | 遮罩 / Esc / 关闭键 → `close()` |

**判定口径（哪些是实测，哪些是同源保证）**：

- 17 个界面的**打开、量测、关闭**都走了真实路径（不是构造对象）：能带参数的入口用真实参数打开
  （卡片详情用真实任务 id、执行面板走 `openExecPanel`、夜间批量填真实选择集），
  权限授权用真实插件 id。
- **弹窗**的遮罩点击与 `Esc` 由壳的 `closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside`
  统一处理，与右上角关闭键最终都汇到 `close()` → `onClosed`；本轮对权限弹窗把这三条路径
  逐条走了一遍（见下节），其余 12 个弹窗复用同一个壳，行为由壳的单一实现保证。
- **抽屉**不是 `Popup`，`Esc` 由各组件自接 `Keys.onEscapePressed`（四个抽屉都接了），
  关闭键统一走 `DrawerShell` 的 `closeRequested`。
- **`Esc` 未做真实按键注入**（本机无 GUI 自动化能力），依据是壳内 `closePolicy`
  与组件内 `Keys.onEscapePressed` 的代码路径；其余结论均为程序化实测。
- 外观与项目全景两个抽屉**没有底栏**（`DrawerShell` 的 `footerExtra` / `footerAction` 都为空，
  底栏高度 0）—— 不是「底没固定」，是本来就没有底。

## 二、权限弹窗：遮罩不再等同拒绝

R11-10 改了本轮唯一的行为语义（规格 §3.5）：遮罩点击 / `Esc` / 右上角关闭键**只关闭不表态**，
拒绝必须显式点「拒绝」按钮。

端到端实测路径：插件宿主调一个受权限门控的能力（`PluginHost.storageSet` →
`PluginContext::require("storage")` → 未授权 → 宿主发 `permissionRequested` → 权限框弹出），
关掉，再调一次，看是否还会再弹。

| 路径 | 关闭后仍属未授权 | 再次触发是否再弹 | 结论 |
| --- | --- | --- | --- |
| 遮罩点击（`close()`） | 是 | **是** | 可再触发 ✅ |
| 右上角关闭键（`closeRequested`） | 是 | **是** | 可再触发 ✅ |
| `Esc`（与遮罩同走壳的 `closePolicy`） | 是 | **是** | 可再触发 ✅ |

被测插件：`com.lin.plan-hook`，权限 `storage`。测试前用 `setPermissionGranted(pid, "storage", false)`
撤销授权，制造「已声明但未授权」的状态（祖父条款首次启动会预授予全部声明权限，
不撤销就没有可测的目标）。

实现侧的根据：三条关闭路径最终都触发 `PluginPermissionDialog` 的 `onClosed`，
其中 `decided == false`（用户没点允许 / 拒绝）时调 `pluginRuntime.releasePermissionPrompt`，
只清掉「已提示」标记、不写任何授权数据 —— 所以下次插件再调该能力还会再弹。

## 三、未推送提交数

`PreviewHub` 的 `unpushed` 从已有 `git status --porcelain=v2 --branch` 的 `# branch.ab +N -M` 解析
（不额外跑 git 进程 —— 单独跑 `git rev-list` 会让首帧多等 1.6 秒）。
实测取自本仓库真实 worktree 集合：

| 情形 | 实测 | 界面表现 |
| --- | --- | --- |
| 有 upstream 且领先 N 个提交 | `fix/dx107-108-conflict-cleanup` → 2；`fix/console-encoding-unify` → 4 | 显示「未推送 N」徽章 ✅ |
| 有 upstream 且不领先 | `main` → 0 | 不显示徽章 ✅ |
| 无 upstream / 分离头 | 多条 → `-1` | 不显示徽章 ✅（模型给 -1 时不渲染） |

`unpushed` 为 0 与 `-1` 都渲染成「无徽章」，但含义不同：0 是「有 upstream 且都推了」，
-1 是「没有 upstream，算不出来」。徽章只在 `> 0` 时出现，所以两者不会互相冒充。

## 四、执行队列（R11-11）

| 项 | 规格 | 实测 |
| --- | --- | --- |
| 表头固定 | 列表滚动时表头不动 | 表头在 `ListView` 之外（行高 18），滚动不动 ✅ |
| 队列行高 | 统一 40 px | 入队 3 条实测行高 **40** ✅ |
| 列对齐 | — | 任务 / 状态 / 操作三列，列宽与表头逐列对齐 ✅ |

## 五、遗留问题

- `qml/Main.qml:1621` 的 `QML EmptyState: Cannot anchor to an item that isn't a parent or sibling`
  —— `origin/main` 即有的告警，不是 R11 引入的，不在本轮范围内，未处理。
- `Esc` 键未做真实按键注入，原因见「判定口径」一节（本机无 GUI 自动化能力）。
- 视觉观感（配色、间距观感、是否有遮挡）没有人工确认：程序化埋点能测几何，测不了「好不好看」。
  这一项只能靠人打开应用看。
