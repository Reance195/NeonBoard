import QtQuick
import "rules.js" as Rules

// 后台常驻入口：由宿主在子 QQmlContext 里加载，
// 注入的 pluginHost 就是本插件专属门面（权限天然隔离，不会拿到别的插件的权限）。
// 职责：30 秒巡检到期规则 → 执行动作 → 推进槽位状态并落执行记录（幂等见 rules.js 头注）。
QtObject {
    id: root

    // 最近一次自动触发摘要，供宿主日志 / 面板查看
    property string lastSummary: ""
    property int handledCount: 0

    // QtObject 没有可写子对象的默认属性（与顶层 Connections 必须改 JS 连接同源），
    // Timer 以属性值形式挂在根上，不能写成 child 声明
    property Timer ticker: Timer {
        interval: 30000
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: root.checkDue()
    }

    function checkDue() {
        var now = new Date();
        var rules = Rules.readRules(pluginHost);
        var state = Rules.readState(pluginHost);
        var rulesById = {};
        var changed = false;

        for (var i = 0; i < rules.length; i++) {
            var rule = rules[i];
            rulesById[rule.id] = true;
            if (!rule.enabled)
                continue;
            var fp = Rules.fingerprint(rule);
            var slot = Rules.dueSlot(rule, now);
            var st = state[rule.id];
            var fresh = (!st || st.fp !== fp);
            // 首次见到或计划被修改：从当前时点起生效，不追改前的历史槽位
            //（否则新建「每天 09:00」会在当天 09:00 已过时立刻补发一次）。
            // 例外：一次性规则指向过去时点 = 显式的补执行诉求，首次即触发——
            // 否则「保存时已过期」的一次性规则永远死在手上。
            if (fresh && !(rule.kind === "once" && slot.length > 0)) {
                state[rule.id] = { "lastSlot": (slot.length > 0 ? slot : "_"), "fp": fp };
                changed = true;
                continue;
            }
            if (slot.length > 0 && (fresh || slot > st.lastSlot)) {
                var result = Rules.runActions(rule, pluginHost);
                // fresh 时 st 尚不存在，统一整对象写回，别在 st 上补字段
                state[rule.id] = { "lastSlot": slot, "fp": fp };
                changed = true;
                handledCount += 1;
                lastSummary = rule.label + " · " + Rules.resultText(result);
                var record = {
                    "time": Rules.formatDateTime(now.getTime()),
                    "label": rule.label,
                    "source": "auto",
                    "created": result.created,
                    "moved": result.moved,
                    "notified": result.notified,
                    "error": result.errors.length > 0 ? result.errors[0] : ""
                };
                Rules.appendRecord(pluginHost, record);
                pluginHost.log("scheduler: " + lastSummary);
            }
        }

        // 清扫已删规则的残留状态（同 agent-bridge 的失效映射清扫思路）
        for (var key in state) {
            if (Object.prototype.hasOwnProperty.call(state, key) && !rulesById[key]) {
                delete state[key];
                changed = true;
            }
        }
        if (changed && !Rules.writeState(pluginHost, state))
            pluginHost.log("scheduler: 槽位状态写回 storage 失败，同一槽位可能重复触发");
    }

    Component.onCompleted: {
        pluginHost.log("scheduler: background ready");
    }
}
