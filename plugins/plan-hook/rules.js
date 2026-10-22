// 计划钩子规则引擎（纯函数，无 QML 依赖）。
// background.qml（事件驱动）与 main.qml（手动执行）共用同一套解析与匹配逻辑，
// 保证「提交触发」和「界面手动触发」的结果完全一致。
//
// 标记语法：
//   #T12        → 默认目标状态（doing）
//   #T12=done   → 已办结
//   #T12=todo   → 待办
// 短号大小写不敏感，比较时统一大写；整词匹配（#T1 不会命中 #T12）。
// 任务侧的短号写在标题里（也可按规则改匹配备注 / 标签）。

var DEFAULT_RULES = {
    "version": 1,
    "enabled": true,
    "defaultStatus": "doing",
    "match": {
        "field": "title",
        "pattern": "#([A-Za-z][A-Za-z0-9]*(?:-[A-Za-z0-9]+)?)(?:=(todo|doing|done))?"
    },
    "statusMap": {
        "todo": "todo",
        "doing": "doing",
        "done": "done"
    },
    "insert": "end",
    "onAlreadyInTarget": "skip"
};

var MARKER_PATTERN = /#([A-Za-z][A-Za-z0-9]*(?:-[A-Za-z0-9]+)?)(?:=(todo|doing|done))?/g;
var CODE_PATTERN = /#([A-Za-z][A-Za-z0-9]*(?:-[A-Za-z0-9]+)?)/g;

function defaultRules() {
    return JSON.parse(JSON.stringify(DEFAULT_RULES));
}

// 宽松解析：坏 JSON 或缺字段一律回落到默认值，不让插件因脏数据崩掉
function parseRules(raw) {
    var rules = defaultRules();
    if (typeof raw !== "string" || raw.length === 0)
        return rules;

    var parsed = null;
    try {
        parsed = JSON.parse(raw);
    } catch (e) {
        return rules;
    }
    if (!parsed || typeof parsed !== "object")
        return rules;

    for (var key in parsed) {
        if (Object.prototype.hasOwnProperty.call(parsed, key))
            rules[key] = parsed[key];
    }
    if (!rules.match || typeof rules.match !== "object")
        rules.match = defaultRules().match;
    if (!rules.statusMap || typeof rules.statusMap !== "object")
        rules.statusMap = defaultRules().statusMap;
    return rules;
}

// 从提交信息里提取标记：[{ code: "T12", status: "doing" }]
function extractMarkers(text, defaultStatus) {
    var markers = [];
    if (typeof text !== "string" || text.length === 0)
        return markers;

    var seen = {};
    MARKER_PATTERN.lastIndex = 0;
    var matched = MARKER_PATTERN.exec(text);
    while (matched !== null) {
        var code = matched[1].toUpperCase();
        if (!seen[code]) {
            seen[code] = true;
            markers.push({
                code: code,
                status: matched[2] ? matched[2].toLowerCase() : (defaultStatus || "doing")
            });
        }
        matched = MARKER_PATTERN.exec(text);
    }
    return markers;
}

// 取出任务在指定匹配字段上的文本
function taskField(task, field) {
    if (!task)
        return "";
    if (field === "note")
        return task.note ? String(task.note) : "";
    if (field === "tags")
        return task.tags ? String(task.tags) : "";
    return task.title ? String(task.title) : "";
}

// 文本里出现的所有短号（大写）
function codesIn(text) {
    var codes = {};
    if (typeof text !== "string" || text.length === 0)
        return codes;
    CODE_PATTERN.lastIndex = 0;
    var matched = CODE_PATTERN.exec(text);
    while (matched !== null) {
        codes[matched[1].toUpperCase()] = true;
        matched = CODE_PATTERN.exec(text);
    }
    return codes;
}

// 计算待执行动作：[{ id, title, status }]
function planActions(text, tasks, rules) {
    var config = parseRules(rules);
    var actions = [];
    if (!config.enabled)
        return actions;

    var markers = extractMarkers(text, config.defaultStatus);
    if (markers.length === 0)
        return actions;

    var markerByCode = {};
    for (var i = 0; i < markers.length; i++)
        markerByCode[markers[i].code] = markers[i];

    var list = tasks || [];
    for (var j = 0; j < list.length; j++) {
        var task = list[j];
        var codes = codesIn(taskField(task, config.match.field));
        for (var code in codes) {
            if (!Object.prototype.hasOwnProperty.call(codes, code))
                continue;
            var marker = markerByCode[code];
            if (!marker)
                continue;
            var target = config.statusMap[marker.status] || marker.status;
            if (config.onAlreadyInTarget === "skip" && task.status === target)
                break;
            actions.push({ id: task.id, title: task.title, status: target });
            break;
        }
    }
    return actions;
}
