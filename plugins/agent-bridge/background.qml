import QtQuick

// 后台常驻入口：由宿主在子 QQmlContext 里加载，注入的 pluginHost 是本插件专属门面。
// 职责：订阅 agent.task 事件域，消费 agent-task-event v1 载荷
//      —— 每个 todo 在看板上对应一张卡片，映射存在 meta 表里，标题不带任何短号。
// 协议见 docs/agent-task-event.md；本插件与具体 Agent 无关（opencode / 其他 Agent 的
// 适配器只要按协议投递即可）。
QtObject {
    id: root

    // 供面板展示的运行状态
    property string lastSummary: "尚未收到事件"
    property int lastActionCount: 0
    property int handledCount: 0

    readonly property string metaPrefix: "agentbridge."
    readonly property string protocolName: "agent-task-event"
    readonly property int protocolVersion: 1

    // 上游 todo 状态 → 看板三列
    function statusOf(upstream) {
        var s = String(upstream || "").toLowerCase();
        if (s === "pending")    return "todo";
        if (s === "in_progress") return "doing";
        if (s === "completed")  return "done";
        if (s === "todo" || s === "doing" || s === "done") return s;
        return "todo";
    }

    function mapKey(agent, sessionId, todoId) {
        return metaPrefix + agent + "." + sessionId + "." + todoId;
    }

    function findTask(taskId) {
        var list = pluginHost.tasks();
        for (var i = 0; i < list.length; i++) {
            if (String(list[i].id) === String(taskId))
                return list[i];
        }
        return null;
    }

    // 应用一条 todo：无映射则建卡（打 agent 标签），有映射则同步标题与状态
    function applyItem(agent, sessionId, item) {
        var todoId = item.id !== undefined ? String(item.id) : "";
        if (todoId.length === 0)
            return 0;
        var title = item.title ? String(item.title) : "";
        if (title.length === 0)
            return 0;
        var status = statusOf(item.status);

        var key = mapKey(agent, sessionId, todoId);
        var mapped = pluginHost.metaValue(key, "");
        var task = (mapped.length > 0) ? findTask(mapped) : null;
        var actions = 0;

        if (task === null) {
            // 映射不存在或卡片已被删除：重新建卡。恰好在 done 态到来时直接建在已办结列。
            var newId = pluginHost.addTask(title, status);
            if (newId && newId.length > 0) {
                var note = "来自 agent " + agent
                        + (sessionId.length > 8 ? " · session " + sessionId.substring(0, 8) : "");
                var fields = { "tags": "agent", "note": note };
                if (item.note) fields.note += "\n" + String(item.note);
                pluginHost.updateTask(newId, fields);
                pluginHost.setMetaValue(key, newId);
                actions += 1;
            }
            return actions;
        }

        if (task.title !== title) {
            pluginHost.updateTask(task.id, { "title": title });
            actions += 1;
        }
        if (task.status !== status) {
            pluginHost.moveTaskToEnd(task.id, status);
            actions += 1;
        }
        return actions;
    }

    // 应用一个完整批次：校验协议骨架，逐条同步
    function applyBatch(envelope) {
        if (!envelope || envelope.protocol !== protocolName || envelope.version !== protocolVersion) {
            pluginHost.log("agent-bridge: 丢弃不符合协议的事件");
            return 0;
        }
        var agent = envelope.agent ? String(envelope.agent) : "";
        var sessionId = envelope.sessionId ? String(envelope.sessionId) : "";
        if (agent.length === 0 || sessionId.length === 0 || !Array.isArray(envelope.items)) {
            pluginHost.log("agent-bridge: 事件缺少 agent / sessionId / items");
            return 0;
        }

        var actions = 0;
        for (var i = 0; i < envelope.items.length; i++) {
            if (envelope.items[i])
                actions += applyItem(agent, sessionId, envelope.items[i]);
        }

        handledCount += 1;
        lastActionCount = actions;
        lastSummary = agent + " · " + sessionId.substring(0, Math.min(8, sessionId.length))
                + " · 条目 " + envelope.items.length + " · 动作 " + actions;
        pluginHost.setMetaValue("agentbridge.status", lastSummary);
        pluginHost.setMetaValue("agentbridge.handled", String(handledCount));
        pluginHost.log("agent-bridge: " + lastSummary);
        return actions;
    }

    function onAgentTask(payload) {
        // 宿主投递链路统一把原始文本放进 payload.text（见 main.cpp 的 --emit-hook-file）
        var text = (payload && payload.text) ? String(payload.text) : "";
        if (text.length === 0)
            return;
        var envelope = null;
        try {
            envelope = JSON.parse(text);
        } catch (e) {
            pluginHost.log("agent-bridge: payload 不是合法 JSON，忽略");
            return;
        }
        applyBatch(envelope);
    }

    Component.onCompleted: {
        pluginHost.subscribe(["agent.task"]);
        pluginHost.eventReceived.connect(function (name, payload) {
            if (name === "agent.task")
                onAgentTask(payload);
        });
        pluginHost.log("agent-bridge: background ready");
    }
}
