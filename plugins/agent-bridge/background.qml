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

    // tags 集合比较（忽略顺序与大小写），避免无实质变化时反复写库
    function splitTags(raw) {
        var out = [];
        var parts = String(raw || "").split(",");
        for (var i = 0; i < parts.length; i++) {
            var t = parts[i].trim().toLowerCase();
            if (t.length > 0)
                out.push(t);
        }
        out.sort();
        return out;
    }

    function tagsEqual(a, b) {
        return splitTags(a).join(",") === splitTags(b).join(",");
    }

    // 协议是否显式声明了 tags（未声明时不覆盖人工标签）
    function declaredTags(item) {
        if (item.tags === undefined || item.tags === null)
            return false;
        if (Array.isArray(item.tags))
            return item.tags.length > 0;
        return String(item.tags).trim().length > 0;
    }

    // 协议可选 tags（数组或逗号串）：保持原顺序（首个标签决定阶段归属），
    // 末尾补 agent 标记便于筛选；未声明时只打 agent。
    function tagsOf(item) {
        var list = [];
        var raw = item.tags;
        if (Array.isArray(raw)) {
            for (var i = 0; i < raw.length; i++) {
                var t = String(raw[i] || "").trim();
                if (t.length > 0)
                    list.push(t);
            }
        } else if (raw !== undefined && raw !== null) {
            var parts = String(raw).split(",");
            for (var j = 0; j < parts.length; j++) {
                var p = parts[j].trim();
                if (p.length > 0)
                    list.push(p);
            }
        }
        var hasAgent = false;
        for (var k = 0; k < list.length; k++) {
            if (list[k].toLowerCase() === "agent")
                hasAgent = true;
        }
        if (!hasAgent)
            list.push("agent");
        return list.join(",");
    }

    function findTask(taskId) {
        var list = pluginHost.tasks();
        for (var i = 0; i < list.length; i++) {
            if (String(list[i].id) === String(taskId))
                return list[i];
        }
        return null;
    }

    // 应用一条 todo：item.taskId 可显式指定已有卡片（补卡 / 接管人工卡），
    // 命中后补写映射，让后续同 todoId 的事件继续按映射对账；
    // 其次按 meta 映射查找；都没有则建卡（打 agent 标签）。
    function applyItem(agent, sessionId, item) {
        var todoId = item.id !== undefined ? String(item.id) : "";
        if (todoId.length === 0)
            return 0;
        var title = item.title ? String(item.title) : "";
        if (title.length === 0)
            return 0;
        var status = statusOf(item.status);

        var key = mapKey(agent, sessionId, todoId);
        var explicitId = (item.taskId !== undefined && item.taskId !== null)
                ? String(item.taskId).trim() : "";
        var task = (explicitId.length > 0) ? findTask(explicitId) : null;
        if (task === null) {
            var mapped = pluginHost.metaValue(key, "");
            task = (mapped.length > 0) ? findTask(mapped) : null;
        }
        var actions = 0;

        if (task === null) {
            // 映射不存在或卡片已被删除：重新建卡。恰好在 done 态到来时直接建在已办结列。
            var newId = pluginHost.addTask(title, status);
            if (newId && newId.length > 0) {
                // note 以协议为准（幂等：重复投递同 item 状态完全收敛）；
                // 协议没带 note 才写归属说明。归属不再拼在 note 前端，保同步对账干净。
                var note = (item.note && String(item.note).length > 0)
                        ? String(item.note)
                        : ("来自 agent " + agent
                            + (sessionId.length > 8 ? " · session " + sessionId.substring(0, 8) : ""));
                pluginHost.updateTask(newId, { "tags": tagsOf(item), "note": note });
                pluginHost.setMetaValue(key, newId);
                actions += 1;
            }
            return actions;
        }

        if (task.title !== title) {
            pluginHost.updateTask(task.id, { "title": title });
            actions += 1;
        }
        // note 全量对账：协议带 note 且与卡片当前不同才写。
        // 事件没带 note 时不覆盖 —— 保留人工备注。
        var incomingNote = (item.note !== undefined && item.note !== null) ? String(item.note) : "";
        if (incomingNote.length > 0 && String(task.note || "") !== incomingNote) {
            pluginHost.updateTask(task.id, { "note": incomingNote });
            actions += 1;
        }
        // tags 全量对账：协议显式声明 tags 才覆盖，未声明时保留人工标签。
        if (declaredTags(item)) {
            var incomingTags = tagsOf(item);
            if (!tagsEqual(task.tags, incomingTags)) {
                pluginHost.updateTask(task.id, { "tags": incomingTags });
                actions += 1;
            }
        }
        if (task.status !== status) {
            pluginHost.moveTaskToEnd(task.id, status);
            actions += 1;
        }
        // 显式 taskId 命中后登记映射，后续同 todoId 事件走映射对账
        if (explicitId.length > 0 && pluginHost.metaValue(key, "") !== String(task.id))
            pluginHost.setMetaValue(key, String(task.id));
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
