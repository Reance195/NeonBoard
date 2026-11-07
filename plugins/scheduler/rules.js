// 定时任务规则引擎（纯函数 + pluginHost 门面调用，无 QML 依赖）。
// background.qml（时间驱动巡检）与 main.qml（规则编辑 + 手动执行）共用同一套
// 解析、槽位计算与执行逻辑，保证「到点自动执行」和「面板立即执行」结果一致。
//
// 规则模型（存插件 storage 键 scheduler.rules，JSON 数组）：
//   { id, enabled, label, kind: "daily"|"weekly"|"once",
//     time: "HH:MM",          // daily / weekly 的本地时点
//     weekdays: [1..7],       // weekly 用，1=周一 … 7=周日
//     onceAt: <毫秒时间戳>,    // once 用
//     actions: [ ... ] }      // 按序执行，三型：create / move / notify
//
// 槽位幂等：每条规则「最近的已到期槽位键」（字典序=时间序）与已触发槽位
// （storage 键 scheduler.state 里的 lastSlot）比较，严格大于才触发；
// 错过多个槽位只补最近一个，不逐日重放。
// 防旧账补发：计划字段指纹（fingerprint）与状态里记录的不一致即视为
// 新建或被修改，从当前时点起生效，不追改前的历史槽位。

var RULES_KEY = "scheduler.rules";
var STATE_KEY = "scheduler.state";
var RECORDS_KEY = "scheduler.records";
var RECORDS_LIMIT = 20;

var STATUSES = ["todo", "doing", "done"];
var WEEKDAY_NAMES = ["", "周一", "周二", "周三", "周四", "周五", "周六", "周日"];
var STATUS_NAMES = { "todo": "待办", "doing": "进行中", "done": "已办结" };

function pad2(n) {
    return (n < 10 ? "0" : "") + n;
}

function defaultRule() {
    return {
        "id": "sched-" + Date.now().toString(36) + "-" + Math.floor(Math.random() * 46656).toString(36),
        "enabled": true,
        "label": "",
        "kind": "daily",
        "time": "09:00",
        "weekdays": [1],
        "onceAt": 0,
        "actions": []
    };
}

function defaultAction(type) {
    if (type === "move")
        return { "type": "move", "keyword": "", "tag": "", "from": "", "to": "doing" };
    if (type === "notify")
        return { "type": "notify", "title": "", "body": "" };
    return { "type": "create", "title": "", "status": "todo", "tags": "", "note": "", "dueOffsetDays": 0 };
}

// 宽松归一化：脏字段一律回落默认值，坏数据不让插件崩（与 plan-hook 同口径）
function normalizeAction(action) {
    if (!action || typeof action !== "object")
        return null;
    var type = String(action.type || "");
    if (type === "create") {
        return {
            "type": "create",
            "title": String(action.title || "").trim(),
            "status": STATUSES.indexOf(action.status) >= 0 ? action.status : "todo",
            "tags": String(action.tags || "").trim(),
            "note": String(action.note || ""),
            "dueOffsetDays": isFinite(action.dueOffsetDays) ? Math.max(0, Math.floor(Number(action.dueOffsetDays))) : 0
        };
    }
    if (type === "move") {
        var to = String(action.to || "").trim();
        return {
            "type": "move",
            "keyword": String(action.keyword || "").trim(),
            "tag": String(action.tag || "").trim(),
            "from": STATUSES.indexOf(action.from) >= 0 ? action.from : "",
            "to": STATUSES.indexOf(to) >= 0 ? to : ""
        };
    }
    if (type === "notify") {
        return {
            "type": "notify",
            "title": String(action.title || "").trim(),
            "body": String(action.body || "")
        };
    }
    return null;
}

function normalizeRule(rule) {
    var base = defaultRule();
    if (!rule || typeof rule !== "object")
        return base;
    var out = base;
    if (typeof rule.id === "string" && rule.id.length > 0)
        out.id = rule.id;
    out.enabled = rule.enabled !== false;
    out.label = String(rule.label || "").trim();
    var kind = String(rule.kind || "daily");
    out.kind = (kind === "weekly" || kind === "once") ? kind : "daily";
    var time = parseTime(rule.time);
    out.time = time.length > 0 ? time : base.time;
    var weekdays = [];
    if (Array.isArray(rule.weekdays)) {
        for (var i = 0; i < rule.weekdays.length; i++) {
            var d = Math.floor(Number(rule.weekdays[i]));
            if (d >= 1 && d <= 7 && weekdays.indexOf(d) < 0)
                weekdays.push(d);
        }
        weekdays.sort(function (a, b) { return a - b; });
    }
    out.weekdays = weekdays;
    out.onceAt = isFinite(rule.onceAt) ? Math.floor(Number(rule.onceAt)) : 0;
    var actions = [];
    if (Array.isArray(rule.actions)) {
        for (var j = 0; j < rule.actions.length; j++) {
            var normalized = normalizeAction(rule.actions[j]);
            if (normalized !== null)
                actions.push(normalized);
        }
    }
    out.actions = actions;
    return out;
}

function parseRules(raw) {
    if (typeof raw !== "string" || raw.length === 0)
        return [];
    var parsed = null;
    try {
        parsed = JSON.parse(raw);
    } catch (e) {
        return [];
    }
    if (!Array.isArray(parsed))
        return [];
    var rules = [];
    for (var i = 0; i < parsed.length; i++)
        rules.push(normalizeRule(parsed[i]));
    return rules;
}

function parseState(raw) {
    if (typeof raw !== "string" || raw.length === 0)
        return {};
    var parsed = null;
    try {
        parsed = JSON.parse(raw);
    } catch (e) {
        return {};
    }
    return (parsed && typeof parsed === "object" && !Array.isArray(parsed)) ? parsed : {};
}

function parseRecords(raw) {
    if (typeof raw !== "string" || raw.length === 0)
        return [];
    var parsed = null;
    try {
        parsed = JSON.parse(raw);
    } catch (e) {
        return [];
    }
    return Array.isArray(parsed) ? parsed : [];
}

// storage 读写统一入口：前后台共用同一份 plugin_kv（按 pluginId 隔离）
function readRules(host) {
    return parseRules(host.storageGet(RULES_KEY));
}

function writeRules(host, rules) {
    return host.storageSet(RULES_KEY, JSON.stringify(rules));
}

function readState(host) {
    return parseState(host.storageGet(STATE_KEY));
}

function writeState(host, state) {
    return host.storageSet(STATE_KEY, JSON.stringify(state));
}

function readRecords(host) {
    return parseRecords(host.storageGet(RECORDS_KEY));
}

// 执行记录头部插入并截断（同 plan-hook 的 records 上限做法）
function appendRecord(host, record) {
    var records = readRecords(host);
    records.unshift(record);
    while (records.length > RECORDS_LIMIT)
        records.pop();
    if (!host.storageSet(RECORDS_KEY, JSON.stringify(records)))
        host.log("scheduler: 执行记录写入 storage 失败");
}

// "HH:MM" → 归一化的 "HH:MM"，非法返回 ""
function parseTime(value) {
    var text = String(value || "").trim();
    var m = /^(\d{1,2}):(\d{2})$/.exec(text);
    if (m === null)
        return "";
    var h = parseInt(m[1], 10), min = parseInt(m[2], 10);
    if (h > 23 || min > 59)
        return "";
    return pad2(h) + ":" + pad2(min);
}

// "YYYY-MM-DD HH:MM"（秒可省）→ 本地时点毫秒数，非法返回 NaN
function parseLocalDateTime(text) {
    var m = /^(\d{4})-(\d{1,2})-(\d{1,2})[ T](\d{1,2}):(\d{2})(?::\d{2})?$/.exec(String(text || "").trim());
    if (m === null)
        return NaN;
    var ms = new Date(parseInt(m[1], 10), parseInt(m[2], 10) - 1, parseInt(m[3], 10),
                      parseInt(m[4], 10), parseInt(m[5], 10)).getTime();
    return isFinite(ms) ? ms : NaN;
}

function formatDate(d) {
    return d.getFullYear() + "-" + pad2(d.getMonth() + 1) + "-" + pad2(d.getDate());
}

function formatDateTime(ms) {
    var d = new Date(ms);
    return formatDate(d) + " " + pad2(d.getHours()) + ":" + pad2(d.getMinutes());
}

// 计划字段指纹：任一调度字段变化即视为新计划（状态侧据此重新起算，不追旧账）
function fingerprint(rule) {
    return [rule.kind, rule.time, (rule.weekdays || []).join(","), rule.onceAt].join("|");
}

function msAtDay(dateStr, time) {
    var p = dateStr.split("-");
    var t = parseTime(time) || "00:00";
    return new Date(parseInt(p[0], 10), parseInt(p[1], 10) - 1, parseInt(p[2], 10),
                    parseInt(t.split(":")[0], 10), parseInt(t.split(":")[1], 10)).getTime();
}

// QML getDay()：0=周日 … 6=周六；规则侧 1=周一 … 7=周日
function isMatchingWeekday(weekdays, jsDay) {
    var iso = (jsDay === 0) ? 7 : jsDay;
    return weekdays.indexOf(iso) >= 0;
}

// 最近一个已到期槽位键；daily / weekly 恒有值（最多退到前一天 / 上一个命中日），
// once 未到期返回 ""。调用方以「严格大于 lastSlot」判定是否触发。
function dueSlot(rule, now) {
    if (!rule.enabled)
        return "";
    if (rule.kind === "once")
        return (rule.onceAt > 0 && now.getTime() >= rule.onceAt) ? ("once:" + rule.onceAt) : "";
    if (rule.kind === "daily") {
        var today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
        var day = (now.getTime() >= msAtDay(formatDate(today), rule.time)) ? today
                : new Date(today.getTime() - 86400000);
        return "daily:" + formatDate(day);
    }
    // weekly：从今天往前找最近一个命中 weekday 且时点已过的日期。
    // 窗口 8 天：今天命中但时点未到时，要能回落到上周同一天
    for (var back = 0; back < 8; back++) {
        var day2 = new Date(now.getFullYear(), now.getMonth(), now.getDate() - back);
        if (!isMatchingWeekday(rule.weekdays, day2.getDay()))
            continue;
        if (now.getTime() >= msAtDay(formatDate(day2), rule.time))
            return "weekly:" + formatDate(day2);
    }
    return "";
}

// 规则的动作是否合法（保存前面板校验 / 执行侧兜底共用）
function ruleProblems(rule) {
    var problems = [];
    if (String(rule.label || "").trim().length === 0)
        problems.push("规则名不能为空");
    if (rule.kind === "weekly" && rule.weekdays.length === 0)
        problems.push("每周规则至少勾选一个星期");
    if (rule.kind === "once" && !(rule.onceAt > 0))
        problems.push("一次性规则需要一个有效时点");
    if (rule.actions.length === 0)
        problems.push("至少配置一个动作");
    for (var i = 0; i < rule.actions.length; i++) {
        var action = rule.actions[i];
        if (action.type === "create" && action.title.length === 0)
            problems.push("建卡动作缺少标题");
        if (action.type === "move" && action.to.length === 0)
            problems.push("流转动作缺少目标状态");
    }
    return problems;
}

// scheduleText / nextFireText / actionText 只服务面板展示，逻辑放这份实现里避免两处漂移
function scheduleText(rule) {
    if (rule.kind === "once")
        return "一次性 " + (rule.onceAt > 0 ? formatDateTime(rule.onceAt) : "未设置");
    if (rule.kind === "weekly") {
        var names = [];
        for (var i = 0; i < rule.weekdays.length; i++)
            names.push(WEEKDAY_NAMES[rule.weekdays[i]] || ("?" + rule.weekdays[i]));
        return "每周" + names.join("、") + " " + rule.time;
    }
    return "每天 " + rule.time;
}

function nextFireText(rule, now) {
    if (!rule.enabled)
        return "已停用";
    if (rule.kind === "once")
        return rule.onceAt > now.getTime() ? formatDateTime(rule.onceAt) : "已到点";
    if (rule.kind === "daily") {
        var today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
        if (now.getTime() < msAtDay(formatDate(today), rule.time))
            return "今天 " + rule.time;
        return "明天 " + rule.time;
    }
    for (var ahead = 0; ahead < 8; ahead++) {
        var day = new Date(now.getFullYear(), now.getMonth(), now.getDate() + ahead);
        if (!isMatchingWeekday(rule.weekdays, day.getDay()))
            continue;
        if (ahead === 0 && now.getTime() >= msAtDay(formatDate(day), rule.time))
            continue;
        var name = ahead === 0 ? "今天" : (ahead === 1 ? "明天" : WEEKDAY_NAMES[day.getDay()]);
        return name + " " + rule.time;
    }
    return "";
}

function actionText(action) {
    if (action.type === "create")
        return "建卡「" + action.title + "」→ " + (STATUS_NAMES[action.status] || action.status)
                + (action.dueOffsetDays > 0 ? "（" + action.dueOffsetDays + " 天后到期）" : "");
    if (action.type === "move") {
        var conds = [];
        if (action.keyword.length > 0)
            conds.push("标题含「" + action.keyword + "」");
        if (action.tag.length > 0)
            conds.push("标签「" + action.tag + "」");
        if (conds.length === 0 && action.from.length > 0)
            conds.push((STATUS_NAMES[action.from] || action.from) + "列全部");
        return "流转 " + conds.join(" 或 ") + " → " + (STATUS_NAMES[action.to] || action.to);
    }
    if (action.type === "notify")
        return "通知「" + (action.title || "定时任务") + "」";
    return "未知动作";
}

// move 动作的任务匹配：keyword 命中标题（不分大小写包含）或 tag 等于任务标签，
// 两者皆空时要求 from 非空（显式整列流转），from 非空时再叠加来源列过滤
function matchTasks(action, tasks) {
    var keyword = action.keyword.toLowerCase();
    var matched = [];
    var list = tasks || [];
    for (var i = 0; i < list.length; i++) {
        var task = list[i];
        if (!task)
            continue;
        if (action.from.length > 0 && task.status !== action.from)
            continue;
        var hit = (action.keyword.length === 0 && action.tag.length === 0);
        if (keyword.length > 0 && String(task.title || "").toLowerCase().indexOf(keyword) >= 0)
            hit = true;
        if (action.tag.length > 0 && String(task.tags || "") === action.tag)
            hit = true;
        if (hit)
            matched.push(task);
    }
    return matched;
}

// 执行一条规则的全部动作。前后台共用：后台到点触发与面板「立即执行」
// 走同一实现（Z5），失败逐条记入 errors（fail-soft，不中断后续动作）。
function runActions(rule, host) {
    var result = { "created": 0, "moved": 0, "notified": 0, "errors": [] };
    var tasksCache = null;
    for (var i = 0; i < rule.actions.length; i++) {
        var action = rule.actions[i];
        if (action.type === "create") {
            var id = host.addTask(action.title, action.status);
            if (!id || id.length === 0) {
                result.errors.push("建卡失败：" + action.title);
                continue;
            }
            var fields = null;
            if (action.tags.length > 0) {
                fields = fields || {};
                fields["tags"] = action.tags;
            }
            if (action.note.length > 0) {
                fields = fields || {};
                fields["note"] = action.note;
            }
            if (action.dueOffsetDays > 0) {
                fields = fields || {};
                fields["dueAt"] = Date.now() + action.dueOffsetDays * 86400000;
            }
            if (fields !== null && !host.updateTask(id, fields))
                result.errors.push("建卡补字段失败：" + action.title);
            result.created += 1;
        } else if (action.type === "move") {
            if (tasksCache === null)
                tasksCache = host.tasks() || [];
            var matched = matchTasks(action, tasksCache);
            for (var j = 0; j < matched.length; j++) {
                if (host.moveTaskToEnd(matched[j].id, action.to))
                    result.moved += 1;
                else
                    result.errors.push("流转失败：" + matched[j].title);
            }
        } else if (action.type === "notify") {
            if (!host.hasPermission("notifications")) {
                result.errors.push("缺少 notifications 权限");
                continue;
            }
            host.notify(action.title || rule.label || "定时任务", action.body);
            result.notified += 1;
        } else {
            result.errors.push("未知动作类型：" + action.type);
        }
    }
    return result;
}

function resultText(result) {
    var text = "建卡 " + result.created + " · 流转 " + result.moved + " · 通知 " + result.notified;
    if (result.errors.length > 0)
        text += " · 异常 " + result.errors.length + "（" + result.errors[0] + "）";
    return text;
}
