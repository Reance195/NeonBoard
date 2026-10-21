import QtQuick
import "rules.js" as Rules

// 后台常驻入口：由宿主在子 QQmlContext 里加载，
// 注入的 pluginHost 就是本插件专属门面（权限天然隔离，不会拿到别的插件的权限）。
// 职责：订阅 hook.commit 事件 → 按规则把任务流转到目标状态。
QtObject {
    id: root

    // 最近一次事件摘要，供宿主日志 / 面板查看
    property string lastEvent: ""
    property int lastActionCount: 0
    property int handledCount: 0

    function readRules() {
        return Rules.parseRules(pluginHost.metaValue("plan.rules", ""));
    }

    // 把提交信息里的标记落到任务上，返回实际流转的任务数
    function applyText(text) {
        var rules = readRules();
        if (!rules.enabled)
            return 0;

        var actions = Rules.planActions(text, pluginHost.tasks(), rules);
        for (var i = 0; i < actions.length; i++)
            pluginHost.moveTaskToEnd(actions[i].id, actions[i].status);

        if (actions.length > 0) {
            pluginHost.log("plan-hook: " + actions.length + " task(s) moved, first="
                           + actions[0].title + " -> " + actions[0].status);
        }
        return actions.length;
    }

    function onHookCommit(payload) {
        var text = (payload && payload.text) ? String(payload.text) : "";
        if (text.length === 0)
            return;
        lastEvent = text;
        lastActionCount = applyText(text);
        handledCount += 1;
    }

    Component.onCompleted: {
        pluginHost.subscribe(["hook.commit"]);
        pluginHost.eventReceived.connect(function (name, payload) {
            if (name === "hook.commit")
                onHookCommit(payload);
        });
        pluginHost.log("plan-hook: background ready");
    }
}
