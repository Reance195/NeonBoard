# NeonBoard 插件 API（apiVersion 1 / 2）

NeonBoard 的插件是运行时加载的 QML 面板，使用宿主提供的 `pluginHost` 访问任务、存储、通知与事件。

> 安全边界：插件运行在宿主 QML 引擎内，**不是沙箱**。权限清单只约束 `pluginHost` API，无法阻止 QML 使用 Qt 自带的其他能力。只安装自己信任的插件。

**版本兼容**：宿主同时接受 `apiVersion: 1` 与 `apiVersion: 2`。v1 插件无需任何改动即可继续运行；v2 在 v1 基础上追加「后台常驻」「事件订阅」「meta 读写」三项能力。

## 目录结构

```text
plugins/<plugin-id>/
  plugin.json        清单（必填）
  main.qml           面板入口（必填）
  background.qml     后台常驻入口（可选，apiVersion 2）
  rules.js           任意 JS 资源，可被两个 QML 用 import "rules.js" as X 共用
```

应用启动时扫描四个目录，**靠前优先，同 id 只保留第一个**：

| 顺序 | 目录 | 用途 |
| --- | --- | --- |
| 1 | `%APPDATA%\com.lin\NeonBoard\plugins\` | 用户安装区，**唯一可写区** |
| 2 | `<安装目录>\plugins\` | 随安装包分发 |
| 3 | `<源码目录>\plugins\` | 官方插件（只读） |
| 4 | `<源码目录>\plugins-samples\` | 示例插件（只读） |

因此把插件放进用户安装区即可**覆盖**同 id 的内置插件。改完插件后，在设置面板点「重新扫描插件」即可生效，无需重启。

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
  "permissions": ["storage", "notifications", "tasks.read"],
  "contributes": {
    "events": ["hook.commit"],
    "commands": [{ "id": "timer.start", "title": "开始计时" }]
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
| `permissions` | 否 | 权限字符串数组，缺省为空 |
| `contributes.events` | 否 | 声明要订阅的宿主事件名（apiVersion 2） |
| `contributes.commands` | 否 | 声明可注册的命令（apiVersion 2，当前仅声明与展示） |

## 加载与生命周期

1. 应用启动时扫描四个目录并校验 manifest。
2. 侧栏显示可用插件；点击后面板打开。
3. 宿主调用 `pluginHost.activate(id)`，然后加载 `main.qml`。
4. 关闭面板时调用 `pluginHost.deactivate()` 并卸载 QML。
5. 声明了 `background` 的插件在启动时**额外**常驻加载，不依赖面板是否打开。
6. 设置面板的「重新扫描插件」会重建全部插件上下文与后台实例。

前台插件的 QML 通过全局上下文对象 `pluginHost` 访问宿主，它始终指向当前激活的插件。后台插件则运行在**独立的子 QML 上下文**里，同名的 `pluginHost` 被替换成它自己的门面对象——因此权限天然隔离，后台插件拿不到前台插件的权限。

## 权限

| 权限 | 允许的 API |
| --- | --- |
| `storage` | `storageGet` / `storageSet` / `storageRemove` |
| `tasks.read` | `tasks()` |
| `tasks.write` | `addTask` / `updateTask` / `moveTask` / `moveTaskToEnd` / `deleteTask` |
| `notifications` | `notify()` |
| `openUrl` | `openUrl()`，仅 `http` / `https` |
| `meta` | `metaValue` / `setMetaValue` / `removeMetaValue`（apiVersion 2） |
| `events` | `subscribe()` / `subscriptions()`（apiVersion 2） |

未授权时不会抛异常：读操作返回空字符串 / 空数组 / `false`，写操作返回 `false`，`notify` 不执行，并在日志里写一条 `PluginContext: permission denied ...`。

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
```

禁用清单存在 `meta` 表的 `plugins.disabled` 键下（JSON 数组），禁用中的插件 `status` 为 `disabled`，不会加载。

## 从 apiVersion 1 迁移

v1 插件**不需要改动**，直接照常工作。若要用上 v2 能力：

1. `plugin.json` 里把 `apiVersion` 改成 `2`。
2. 需要常驻就加 `background` 字段（文件必须存在）。
3. 需要订阅事件就加 `permissions: ["events"]`，并声明 `contributes.events`。
4. 需要共享配置就加 `permissions: ["meta"]`。
5. 在 `background.qml` 里用 `pluginHost.subscribe([...])` 订阅，`eventReceived` 接收。

行为差异只有一处需要注意：v2 的后台插件拥有**独立的** `pluginHost` 门面（权限按自己的 manifest 校验），而 v1 时代所有插件共享同一个全局上下文。

## 当前限制

- 每个插件只有一个 `main.qml` 面板，没有 `settings.qml`
- `contributes.commands` 目前只做声明与展示，命令面板尚未实现
- 没有快捷键注册
- 没有插件 HTTP API 或网络白名单
- 没有插件市场（官方插件需手工复制到用户插件目录）
- 加载失败通常表现为面板空白并写入日志；可点「重新扫描插件」重试
- 没有沙箱；不要安装来源不明的插件

后续版本可以在此 API 上增加设置页、命令面板与快捷键注册，同时保持 `apiVersion` 兼容。
