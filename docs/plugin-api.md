# NeonBoard 插件 API（MVP / apiVersion 1）

NeonBoard 的插件是运行时加载的 QML 面板，使用宿主提供的 `pluginHost` 访问任务、存储和通知。

> 安全边界：插件运行在宿主 QML 引擎内，**不是沙箱**。权限清单只约束 `pluginHost` API，无法阻止 QML 使用 Qt 自带的其他能力。只安装自己信任的插件。

## 目录结构

```text
plugins/<plugin-id>/
  plugin.json
  main.qml
```

可放在：

```text
%APPDATA%\com.lin\NeonBoard\plugins\<plugin-id>\
<安装目录>\plugins\<plugin-id>\
```

应用启动时扫描这两个目录。修改插件后需要重启 NeonBoard。

## plugin.json

```json
{
  "id": "com.example.timer",
  "name": "计时器",
  "version": "1.0.0",
  "apiVersion": 1,
  "description": "一个示例插件",
  "main": "main.qml",
  "permissions": ["storage", "notifications", "tasks.read"]
}
```

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `id` | 是 | 唯一标识，建议反向域名；重复 id 只保留第一个 |
| `name` | 是 | 侧栏显示名称 |
| `version` | 是 | 插件版本 |
| `apiVersion` | 是 | 当前必须为 `1` |
| `description` | 否 | 描述 |
| `main` | 是 | 相对插件目录的 QML 入口，必须存在 |
| `permissions` | 否 | 权限字符串数组，缺省为空 |

## 加载与生命周期

1. 应用启动时扫描插件目录并校验 manifest。
2. 侧栏显示可用插件；点击后面板打开。
3. 宿主调用 `pluginHost.activate(id)`，然后加载 `main.qml`。
4. 关闭面板时调用 `pluginHost.deactivate()` 并卸载 QML。
5. 当前没有热重载；安装、更新、禁用插件都需要重启应用。

插件 QML 可以直接使用全局上下文对象 `pluginHost`，它始终指向当前激活的插件。

## 权限

| 权限 | 允许的 API |
| --- | --- |
| `storage` | `storageGet` / `storageSet` / `storageRemove` |
| `tasks.read` | `tasks()` |
| `tasks.write` | `addTask` / `updateTask` / `moveTask` / `deleteTask` |
| `notifications` | `notify()` |
| `openUrl` | `openUrl()`，仅 `http` / `https` |

未授权时不会抛异常：读操作返回空字符串 / 空数组 / `false`，写操作返回 `false`，`notify` 不执行。

## pluginHost API

### 属性

| 属性 | 类型 | 说明 |
| --- | --- | --- |
| `pluginId` | string | 当前插件 id；未激活时为空 |
| `permissions` | list<string> | 当前插件权限 |
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

### 任务

```qml
list<object> tasks()
string       addTask(string title, string status = "todo")
bool         updateTask(string id, object fields)
bool         moveTask(string id, string status)
bool         deleteTask(string id)
```

`status` 只能是 `todo`、`doing`、`done`。

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

## 当前限制

- 每个插件只有一个 `main.qml` 面板，没有 `settings.qml`
- 没有 `contributes` 多槽位、命令注册、快捷键注册
- 没有插件 HTTP API 或网络白名单
- 没有热重载、启用/禁用开关、插件市场
- 加载失败通常表现为面板空白并写入日志；修复后重启应用
- 没有沙箱；不要安装来源不明的插件

后续版本可以在此 API 上增加多槽位、设置页、命令与热重载，同时保持 `apiVersion` 兼容。
