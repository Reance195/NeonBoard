# NeonBoard 插件 API（apiVersion 1 / 2）

NeonBoard 的插件是运行时加载的 QML 组件，使用宿主提供的 `pluginHost` 访问任务、存储、通知与事件。除主面板 `main.qml` 外，插件还可以贡献设置页、三种界面槽位与命令 / 快捷键。

> 安全边界：插件运行在宿主 QML 引擎内，**不是沙箱**。权限清单只约束 `pluginHost` API，无法阻止 QML 使用 Qt 自带的其他能力。只安装自己信任的插件。

**版本兼容**：宿主同时接受 `apiVersion: 1` 与 `apiVersion: 2`。v1 插件无需任何改动即可继续运行；v2 在 v1 基础上追加「后台常驻」「事件订阅」「meta 读写」「设置页 / 槽位 / 命令」四组能力。

## 目录结构

```text
plugins/<plugin-id>/
  plugin.json        清单（必填）
  main.qml           面板入口（必填）
  background.qml     后台常驻入口（可选，apiVersion 2）
  settings.qml       设置页（可选，apiVersion 2）
  sidebar.qml        侧栏槽位（可选，apiVersion 2）
  bottom.qml         底部面板槽位（可选，apiVersion 2）
  card.qml           任务卡槽位（可选，apiVersion 2）
  rules.js           任意 JS 资源，可被两个 QML 用 import "rules.js" as X 共用
```

应用启动时扫描四个目录，**靠前优先，同 id 只保留第一个**：

| 顺序 | 目录 | 用途 |
| --- | --- | --- |
| 1 | `%APPDATA%\com.lin\NeonBoard\plugins\` | 用户安装区，**唯一可写区** |
| 2 | `<安装目录>\plugins\` | 随安装包分发 |
| 3 | `<源码目录>\plugins\` | 官方插件（只读） |
| 4 | `<源码目录>\plugins-samples\` | 示例插件（只读） |

因此把插件放进用户安装区即可**覆盖**同 id 的内置插件。运行时自动监视上述四个源目录（含各插件子目录内的文件），新增、覆盖或删除插件后约 400 ms 防抖即自动重扫并热重载，无需重启；也可在设置面板点「重新扫描插件」手动触发。

## plugin.json

```json
{
  "id": "com.example.timer",
  "name": "计时器",
  "version": "1.0.0",
  "apiVersion": 2,
  "description": "一个示例插件",
  "main": "main.qml",
  "background": "background.qml",
  "settings": "settings.qml",
  "permissions": ["storage", "notifications", "tasks.read"],
  "contributes": {
    "events": ["hook.commit"],
    "slots": {
      "sidebar": "sidebar.qml",
      "bottom": "bottom.qml",
      "card": "card.qml"
    },
    "commands": [
      {
        "id": "timer.start",
        "title": "开始计时",
        "category": "计时器",
        "description": "启动一轮 25 分钟专注计时",
        "shortcut": "Ctrl+Shift+T"
      }
    ]
  }
}
```

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `id` | 是 | 唯一标识，建议反向域名；重复 id 只保留第一个 |
| `name` | 是 | 侧栏显示名称；缺省用 `id` |
| `version` | 是 | 插件版本；缺省 `0.0.0` |
| `apiVersion` | 否 | `1` 或 `2`；缺省 `1` |
| `description` | 否 | 描述 |
| `main` | 是 | 相对插件目录的 QML 入口，必须存在 |
| `background` | 否 | 后台常驻入口（apiVersion 2），必须存在 |
| `settings` | 否 | 设置页入口（apiVersion 2），必须存在；也可写在 `contributes.settings` |
| `permissions` | 否 | 权限字符串数组，缺省为空 |
| `contributes.events` | 否 | 声明要订阅的宿主事件名（apiVersion 2） |
| `contributes.slots` | 否 | 槽位入口：`sidebar` / `bottom` / `card`，文件必须存在（apiVersion 2） |
| `contributes.commands` | 否 | 命令列表：`id` / `title` / `category` / `description` / `shortcut`（apiVersion 2） |

## 加载与生命周期

1. 应用启动时扫描四个目录并校验 manifest。
2. 侧栏显示可用插件；点击后面板打开。
3. 宿主调用 `pluginHost.activate(id)`，然后加载 `main.qml`。
4. 关闭面板时调用 `pluginHost.deactivate()` 并卸载 QML。
5. 声明了 `background` 的插件在启动时**额外**常驻加载，不依赖面板是否打开。
6. 设置面板的「插件设置」区按需加载各插件的 `settings.qml`；侧栏 / 底部 / 卡片槽位在主界面加载时按需展开。
7. 插件命令被触发时，若插件还没运行，命令会**排队**并自动打开该插件面板，面板加载完成后再投递。
8. 设置面板的「重新扫描插件」会重建全部插件上下文与后台实例；插件目录发生变化时宿主也会自动完成同样的重扫与重载。

前台插件的 QML 通过全局上下文对象 `pluginHost` 访问宿主，它始终指向当前激活的插件。后台插件则运行在**独立的子 QML 上下文**里，同名的 `pluginHost` 被替换成它自己的门面对象——因此权限天然隔离，后台插件拿不到前台插件的权限。

## 权限

### 权限清单

| 权限 | 允许的 API | 风险 |
| --- | --- | --- |
| `storage` | `storageGet` / `storageSet` / `storageRemove` | 低 |
| `tasks.read` | `tasks()` | 低 |
| `tasks.write` | `addTask` / `updateTask` / `moveTask` / `moveTaskToEnd` / `deleteTask` | 高 |
| `notifications` | `notify()` | 低 |
| `openUrl` | `openUrl()`，仅 `http` / `https` | 中 |
| `meta` | `metaValue` / `setMetaValue` / `removeMetaValue`（apiVersion 2） | 高（跨插件共享区） |
| `events` | `subscribe()` / `subscriptions()`（apiVersion 2） | 低 |

### 声明与授权

manifest 里的 `permissions` 只是**声明**，真正调用 API 还要求**已授权**：

- 授权结果存在 meta 表 `plugins.grants`，格式为 `{ "<pluginId>": ["storage", ...] }`。
- **祖父条款**：升级到带授权机制的版本时，已装插件会按自己的声明权限**一次性预授予**，老插件（如计划钩子、任务桥）不需要重新点一遍。
- 新安装 / 更新插件后，若存在未授予的声明权限，宿主弹出授权框：展示插件名、来源（用户安装 / 随包 / 官方 / 示例）、每项权限的说明与风险等级。
- 插件首次调用未授权 API 时同样会请求授权；同一插件同一权限**每次会话只弹一次**，拒绝后本次会话不再打扰。
- 用户点「允许」后立即写库并生效；点「拒绝」或关闭弹窗则保持未授权。

未授权时不会抛异常：读操作返回空字符串 / 空数组 / `false`，写操作返回 `false`，`notify` 不执行，并在日志里写一条 `PluginContext: permission not granted ...`；权限根本没声明时日志写 `permission not declared ...`。

## pluginHost API

### 属性

| 属性 | 类型 | 说明 |
| --- | --- | --- |
| `pluginId` | string | 当前插件 id；未激活时为空 |
| `name` | string | 插件名（apiVersion 2） |
| `version` | string | 插件版本（apiVersion 2） |
| `permissions` | list<string> | 当前插件权限 |
| `baseUrl` | string | 插件目录的本地文件 URL（apiVersion 2） |
| `background` | string | 后台入口文件名；没有则为 `""`（apiVersion 2） |
| `active` | bool | 是否已激活 |

### 通用

```qml
bool activate(string pluginId)   // 宿主调用，插件一般不需要
void deactivate()                // 宿主调用
bool hasPermission(string name)
void log(string message)         // 写入 NeonBoard 日志
```

### 存储

每个插件的数据按 `pluginId` 隔离，保存在 `board.db` 的 `plugin_kv` 表中。

```qml
string storageGet(string key)
bool   storageSet(string key, string value)
bool   storageRemove(string key)
```

只支持字符串值；复杂对象请自行 `JSON.stringify` / `JSON.parse`。

### 全局键值（meta）

`meta` 表是**跨插件共享**的键值区（`board.db` 的 `meta` 表），适合放全局配置或规则；`storage` 则是按插件隔离的私有区。

```qml
string metaValue(string key, string defaultValue = "")
bool   setMetaValue(string key, string value)
bool   removeMetaValue(string key)
```

### 任务

```qml
list<object> tasks()
string       addTask(string title, string status = "todo")
bool         updateTask(string id, object fields)
bool         moveTask(string id, string status)
bool         moveTaskToEnd(string id, string status)
bool         deleteTask(string id)
```

`status` 只能是 `todo`、`doing`、`done`。

- `moveTask(id, status)`：插入目标列**最前**（历史行为，保持不变）。
- `moveTaskToEnd(id, status)`：插入目标列**末尾**（apiVersion 2 新增，自动化场景更常用）。

`updateTask` 支持的字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `title` | string | 非空 |
| `note` | string | 备注 |
| `priority` | int | 0–3 |
| `tags` | string | 标签字符串 |
| `dueAt` | int | 截止时间，毫秒时间戳；0 表示无 |
| `status` | string | `todo` / `doing` / `done` |

`tasks()` 返回数组，每项包含：

```text
id, title, note, status, priority, tags, orderKey,
dueAt, createdAt, updatedAt, completedAt
```

`tags` 是首个标签字符串；`completedAt` 为毫秒时间戳，未完成时为 0。

### 通知与链接

```qml
void notify(string title, string body)  // 需要 notifications
bool openUrl(string url)                // 需要 openUrl，仅 http/https
```

### 事件（apiVersion 2）

```qml
bool          subscribe(list<string> eventNames)  // 需要 events
list<string>  subscriptions()
```

订阅后通过信号接收：

```qml
Connections {
    target: pluginHost
    function onEventReceived(name, payload) {
        if (name === "task.moved")
            console.log("任务移动:", JSON.stringify(payload))
    }
}
```

> QML 里顶层对象是 `QtObject` 时不能放 `Connections{}`（会报「Cannot assign to non-existent default property」），改用 JS 连接：`pluginHost.eventReceived.connect(function(name, payload) { ... })`。

宿主内置事件：

| 事件名 | payload | 触发时机 |
| --- | --- | --- |
| `task.created` | `{id, title}` | 新建任务 |
| `task.updated` | `{id}` | 任务字段被修改 |
| `task.moved` | `{id, from, to}` | 任务换列 |
| `task.completed` | `{id}` | 任务进入已办结 |
| `task.removed` | `{id}` | 任务被删除或清空 |
| `hook.commit` | `{text, ts}` | Git 提交信息投递（见 [plan-hook.md](plan-hook.md)） |

### 命令回调（apiVersion 2）

```qml
Connections {
    target: pluginHost
    function onCommandInvoked(commandId) {
        console.log("收到命令:", commandId)
    }
}
```

命令触发后，宿主把 `commandId` 投递给该插件对应的 `pluginHost`——前台面板、后台实例、槽位与设置页都能收到。插件尚未运行时命令会排队，等面板或后台实例就绪后再投递。

## 设置页、槽位与命令（apiVersion 2）

### 设置页

```json
{ "settings": "settings.qml" }
```

设置页会聚合进宿主设置面板的「插件设置」区，**默认折叠、展开时才加载**。根节点可以直接接收宿主注入的门面：

```qml
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    property var host   // 宿主注入：绑定到本插件的 pluginHost

    Text { text: "插件：" + host.pluginId }
}
```

`host` 与面板里的全局 `pluginHost` 能力完全一致，但**只作用于本插件**（不需要先 activate），权限仍按本插件的授权校验。

### 槽位

```json
{
  "contributes": {
    "slots": {
      "sidebar": "sidebar.qml",
      "bottom": "bottom.qml",
      "card": "card.qml"
    }
  }
}
```

| 槽位 | 位置 | 注入内容 |
| --- | --- | --- |
| `sidebar` | 左侧栏导航与插件库卡片之间 | `host` |
| `bottom` | 看板三列下方的通栏区域 | `host` |
| `card` | 每张任务卡底部扩展区 | `host` + `taskId`（任务 id，字符串） |

槽位根节点按需声明注入属性，宿主加载后自动赋值：

```qml
import QtQuick

Rectangle {
    property var host
    property string taskId   // 仅 card 槽位有值

    implicitHeight: 40
    // …
}
```

槽位高度取根节点的 `implicitHeight`；`card` 槽位会让任务卡自动增高，不占用原有的 82 高度区域。

### 命令与快捷键

```json
{
  "contributes": {
    "commands": [
      { "id": "timer.start", "title": "开始计时", "category": "计时器",
        "description": "启动一轮 25 分钟专注计时", "shortcut": "Ctrl+Shift+T" }
    ]
  }
}
```

- 命令自动进入 `Ctrl+K` 命令面板，分类缺省为「插件」。
- `shortcut` 支持 `Ctrl / Alt / Shift / Meta` 组合，键名可写 `A`–`Z`、`0`–`9`、`F1`–`F24`、`Esc`、`Enter`、`Space`、`Tab`、`Up`/`Down`/`Left`/`Right` 等；大小写与顺序无关，宿主会规范化。
- 宿主保留 `Ctrl+Z`、`Ctrl+N`、`Ctrl+K`、`Ctrl+,`；与其他插件撞车时**先注册者生效**，冲突项的快捷键自动置空并在命令面板里标注「快捷键冲突」。

## 最小示例

`main.qml`：

```qml
import QtQuick
import QtQuick.Controls

Item {
    Component.onCompleted: {
        if (pluginHost.hasPermission("tasks.read"))
            console.log("当前任务数:", pluginHost.tasks().length)
    }

    Column {
        anchors.centerIn: parent
        spacing: 12

        Text {
            text: "插件：" + pluginHost.pluginId
            color: "#22D3EE"
            font.pixelSize: 16
        }

        Button {
            text: "添加一条待办"
            onClicked: {
                const id = pluginHost.addTask("来自插件的任务", "todo")
                if (id.length > 0)
                    pluginHost.notify("NeonBoard", "任务已创建")
            }
        }
    }
}
```

`background.qml`（apiVersion 2）：

```qml
import QtQuick

QtObject {
    Component.onCompleted: {
        pluginHost.subscribe(["task.moved"])
        pluginHost.eventReceived.connect(function(name, payload) {
            if (name === "task.moved")
                pluginHost.log("任务移动到 " + payload.to)
        })
    }
}
```

## 宿主侧插件管理 API

QML 里还能用到两个上下文对象：

```qml
// pluginLoader
pluginLoader.scan()                          // 重新扫描
pluginLoader.openPluginDir()                 // 打开用户插件目录
pluginLoader.pluginById("com.lin.today")      // 查询单个插件信息
pluginLoader.setPluginEnabled("com.lin.today", false)  // 禁用 / 启用
pluginLoader.isPluginEnabled("com.lin.today")
pluginLoader.plugins                         // 全部插件信息
pluginLoader.pluginDir                       // 用户插件目录
pluginLoader.disabledPlugins                 // 被禁用的插件 id 列表

// pluginRuntime
pluginRuntime.reload()                       // 卸载并重建所有插件上下文与后台实例
pluginRuntime.backgroundCount                // 常驻后台插件数
pluginRuntime.backgroundPlugins              // 后台插件 id 列表
pluginRuntime.pluginCommands                 // 全部插件命令（含 shortcut / conflict 仲裁结果）
pluginRuntime.settingsPages                  // 设置页列表 {pluginId, pluginName, url, sourceLabel}
pluginRuntime.sidebarSlotItems               // 侧栏槽位项（bottomSlotItems / cardSlotItems 同理）
pluginRuntime.slotItems("card")              // 按槽位名查询
pluginRuntime.hostFor("com.example.timer")   // 取绑定到指定插件的 pluginHost 门面
pluginRuntime.invokeCommand(id, commandId)   // 触发命令；返回 false 表示已排队
pluginRuntime.dispatchPending(id)            // 投递该插件排队中的命令，返回投递数
pluginRuntime.isRunning(id)                  // 插件当前是否在运行（面板打开 / 后台常驻）
pluginRuntime.missingPermissions(id)         // 声明但未授权的权限
pluginRuntime.grantPermissions(id, names)    // 授予权限（写 meta 并刷新界面）
pluginRuntime.permissionPrompt(id)           // 授权弹窗数据（名称 / 来源 / 风险说明）
```

禁用清单存在 `meta` 表的 `plugins.disabled` 键下（JSON 数组），禁用中的插件 `status` 为 `disabled`，不会加载。

## 插件库与在线市场

侧栏的「插件」卡片即插件库入口，点开是全屏的插件库面板。宿主侧对应两个上下文对象：

```qml
// pluginLibrary
pluginLibrary.entries            // 全部条目（本地插件 + 市场插件，已按 id 合并）
pluginLibrary.marketSource       // 当前市场索引地址
pluginLibrary.marketStatus       // 市场状态描述（在线 / 离线模式…）
pluginLibrary.statusText         // 最近一次操作的结果
pluginLibrary.refresh()          // 重新扫描本地目录 + 拉取市场索引
pluginLibrary.install(id)        // 安装（市场包或本地只读源）
pluginLibrary.uninstall(id)      // 卸载（只删用户安装区的副本）
pluginLibrary.setEnabled(id, true)   // 启用 / 禁用
pluginLibrary.update(id)         // 市场有新版本时重装
pluginLibrary.openPluginDir()    // 打开用户插件目录
pluginLibrary.requestOpen(id)    // 请求宿主打开某插件面板

// pluginMarket
pluginMarket.source               // 市场索引地址（可读写）
pluginMarket.entries              // 市场索引条目
pluginMarket.online               // 当前是否在线
pluginMarket.refreshing           // 是否正在拉取
pluginMarket.statusText           // 状态描述
pluginMarket.refresh()            // 拉取索引（失败降级到缓存）
```

`entries` 里每一项的字段：

| 字段 | 说明 |
| --- | --- |
| `id` / `name` / `version` | 插件标识与展示信息 |
| `status` / `error` | `ok` / `disabled` / `error` 及错误说明 |
| `installed` | 是否已装在用户安装区 |
| `enabled` | 是否处于启用状态 |
| `source` | `appdata` / `appdir` / `official` / `sample` |
| `origin` | `local` / `bundled` / `market` |
| `dir` | 插件目录路径 |
| `hasMarket` / `marketVersion` / `marketDescription` | 市场侧信息 |

### `.neon` 单文件包格式

市场分发的是 `.neon` 后缀的单个 JSON 文件，**manifest 与源码打包在一起**：

```json
{
  "manifest": {
    "id": "com.example.timer",
    "name": "计时器",
    "version": "1.0.0",
    "apiVersion": 2,
    "main": "main.qml",
    "background": "background.qml",
    "permissions": ["tasks.read", "events"]
  },
  "files": {
    "main.qml": "import QtQuick\nItem { }\n",
    "background.qml": "import QtQuick\nQtObject { }\n"
  }
}
```

- `manifest` 会写成目标目录下的 `plugin.json`，字段语义与手写插件一致。
- `files` 是「相对路径 → 文件内容」的映射，安装时逐个落盘。
- 路径必须安全：不允许绝对路径、反斜杠、`..`；只允许单层子目录。

### 市场源（多源并列）

市场源可以配多个，**并列生效**：刷新时按源顺序逐个拉取，条目按 `id` 合并，同 id 取版本高者；条目上带 `marketSource` / `marketSpec` / `marketUrl` 三个字段，说明它来自哪个源。源列表存在 meta 表 `plugin.market.sources`（JSON 数组），老版本只写过 `plugin.market.source` 单地址的会自动迁移。

源地址支持三种写法（`PluginMarket::resolveCandidates`）：

| 写法 | 例子 | 展开成 |
| --- | --- | --- |
| 仓库简写 | `owner/neonboard-market` | `https://gitee.com/<owner>/<repo>/raw/main/market.json` → `https://raw.githubusercontent.com/<owner>/<repo>/main/market.json`（前者失败才用后者） |
| 完整地址 | `https://host/path/market.json` | 原样使用 |
| 本地目录 / 文件 | `D:\market`、`file:///D:/market` | 读 `<目录>/market.json` |

索引是一个 JSON 文件，默认源写在 `PluginMarket::defaultSource()`（仓库简写 `Reance195/neonboard-market`，索引仓库见 <https://github.com/Reance195/neonboard-market>）：

```json
{
  "version": 1,
  "updatedAt": "2026-10-21",
  "plugins": [
    {
      "id": "com.example.timer",
      "name": "计时器",
      "version": "1.0.0",
      "description": "一个示例插件",
      "author": "someone",
      "homepage": "https://example.com",
      "download": "https://example.com/timer.neon",
      "sha256": "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08",
      "size": 469
    }
  ]
}
```

`sha256` 给了就强制校验；`download` 写完整 URL 即可，也可以只写文件名（如 `timer.neon`），客户端会按**索引所在目录**解析成完整地址。索引顶层预留了 `signature` 字段（minisign 格式），本轮**不校验**，以后接上不改协议。

### 四道校验

安装一个市场插件时依次检查，任何一道不过就整单失败、**不落任何文件**：

1. **包格式合法** —— 是 JSON 对象，含对象型 `manifest`（`id` 非空）与对象型 `files`。
2. **manifest.id 一致** —— 包内 `manifest.id` 必须与索引中请求的 id 相同。
3. **SHA-256 一致** —— 包内容摘要与索引里的 `sha256` 相同（索引没给则跳过）。
4. **路径不越界** —— `files` 的每个键必须是安全相对路径。

落盘时先写进临时目录（`.neon-stage-XXXXXX`），再**原子替换**目标目录：旧目录先改名成 `.backup`，替换失败则回滚，成功后删除 `.backup`。

### 缓存与离线降级

市场索引缓存到 `%APPDATA%\com.lin\NeonBoard\market-cache.json`，格式为：

```json
{
  "version": 2,
  "sources": [
    { "spec": "<源地址>", "url": "<实际索引地址>", "index": "<索引 JSON 原文>",
      "etag": "", "lastModified": "", "fetchedAt": 1792655000 }
  ]
}
```

**每个源一条记录**，互不覆盖；老版本写过的「裸索引 JSON」（顶层有 `plugins` 数组）仍能读。刷新时带 `If-None-Match` / `If-Modified-Since`，服务端回 `304` 就直接用缓存（状态提示「索引未变化（304）」）；TTL 内（默认 6 小时）且未强制刷新时也直接用缓存不发请求。某个源拉取失败（断网、超时、地址写错）时**只降级这个源**到它自己的缓存，其他源照常生效。

### 发布一个市场源

仓库带发布脚本，把插件目录打成 `.neon` 包并写进市场索引：

```powershell
pwsh -File scripts/publish-market.ps1 -PluginPath plugins\plan-hook `
    -OutputDir D:\market -DownloadBase https://gitee.com/<owner>/<repo>/raw/main
```

脚本读 `plugin.json` → 把插件文件打成单文件 `.neon` → 算 SHA-256 与体积 → 更新 `market.json`（保留其他条目，`signature` 字段位留空）。`-RemoveStale` 可清理索引里已不存在的旧包。

### 边界

```text
--library                      启动后打开插件库
--install-plugin-file <path>   从本地 .neon 包安装插件
--market-source <spec>         设置市场源（可重复 / 逗号分隔，写入 meta）
--market-sources               列出当前市场源
--market-refresh               拉取所有启用源的索引并合并（失败时降级到缓存）
--market-install <id>          从市场安装指定插件
--install-plugin-url <url>     直接从 .neon 链接安装（可加 #sha256=...）
--plugin-report                输出插件平台清单 JSON（插件 / 命令 / 槽位 / 设置页 / 授权缺口）
```

这些参数都走 `utilityMode`，不占用单实例，可在应用运行时或无界面环境下调用，退出码 `0` 成功 / `2` 失败。

## 从 apiVersion 1 迁移

v1 插件**不需要改动**，直接照常工作。若要用上 v2 能力：

1. `plugin.json` 里把 `apiVersion` 改成 `2`。
2. 需要常驻就加 `background` 字段（文件必须存在）。
3. 需要订阅事件就加 `permissions: ["events"]`，并声明 `contributes.events`。
4. 需要共享配置就加 `permissions: ["meta"]`。
5. 在 `background.qml` 里用 `pluginHost.subscribe([...])` 订阅，`eventReceived` 接收。
6. 需要设置页 / 槽位 / 命令，按上文的 `settings`、`contributes.slots`、`contributes.commands` 声明（同样走 apiVersion 2）。

行为差异只有一处需要注意：v2 的后台插件拥有**独立的** `pluginHost` 门面（权限按自己的 manifest 校验），而 v1 时代所有插件共享同一个全局上下文。

## 当前限制

- 槽位只有 `sidebar` / `bottom` / `card` 三个固定位置，不能自定义顺序
- 设置页只做聚合展示，宿主不提供统一的设置项控件样式
- 命令只支持「触发」语义，没有参数输入与撤销
- 没有插件 HTTP API 或网络白名单
- 插件市场只支持 `.neon` 单文件包（JSON），不支持 zip 等压缩包格式
- 官方插件（`plugins/` 目录）不随安装包分发，发布态需从市场获取或手工复制到用户插件目录
- 加载失败通常表现为面板空白并写入日志；可点「重新扫描插件」重试
- 没有沙箱；不要安装来源不明的插件

后续版本可以在此 API 上增加设置页、命令面板与快捷键注册，同时保持 `apiVersion` 兼容。
