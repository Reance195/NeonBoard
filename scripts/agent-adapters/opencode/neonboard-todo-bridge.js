// opencode —— NeonBoard 入站适配器
//
// 装到 ~/.config/opencode/plugins/neonboard-todo-bridge.js（install-agent-adapters.ps1 负责）。
// 契约见 docs/agent-task-event.md：监听 todowrite 工具调用，把全量清单交给
// scripts/emit-agent-task.ps1，由它组装 envelope 投递到 agent.task 事件域。
//
// 本文件只做「抽取 → 调投递器」，不做解析规则；解析与建卡归 plugins/agent-bridge。
// 全程 fail-open：任何异常都吞掉，看板只是旁路可视化，绝不能拖慢 agent 主流程。

import { spawn } from "node:child_process";
import { writeFileSync, readFileSync, unlinkSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const AGENT = "opencode";
const TOOL = "todowrite";

// 上游 todo 状态 → 协议状态。opencode 的 todowrite 用 pending/in_progress/completed，
// 与协议同名，这里只做兜底归一。
function normalizeStatus(value) {
  const s = String(value || "").toLowerCase();
  if (s === "pending" || s === "in_progress" || s === "completed") return s;
  if (s === "todo" || s === "doing" || s === "done") return s;
  return "pending";
}

// opencode 的 todo 没有稳定 id（只有 content），用内容短哈希造一个。
// 同一条 todo 多轮投递 id 稳定，是 agent-bridge 幂等对账的前提。
import { createHash } from "node:crypto";
function stableId(content) {
  return "o" + createHash("md5").update(content, "utf8").digest("hex").slice(0, 8);
}

function itemsFrom(todos) {
  if (!Array.isArray(todos)) return [];
  return todos
    .map((t, i) => {
      // 形状兼容：{content,status} / {title,status} / 裸字符串
      const content =
        typeof t === "string"
          ? t
          : String(t?.content ?? t?.title ?? t?.text ?? "");
      if (!content.trim()) return null;
      return {
        id: stableId(content),
        title: content,
        status: normalizeStatus(t?.status),
      };
    })
    .filter(Boolean);
}

// 投递器路径的解析顺序：环境变量 → 本插件同级的配置文件 → 放弃。
// 为什么需要配置文件：插件被复制到 ~/.config/opencode/plugins/ 之后，与源码树不再有
// 相对位置关系，光靠相对路径找不到 emit-agent-task.ps1。安装脚本负责写这个同级 json。
function resolveEmitter() {
  if (process.env.NEONBOARD_EMITTER) return process.env.NEONBOARD_EMITTER;
  try {
    const cfgPath = join(__dirname, "neonboard-todo-bridge.json");
    const cfg = JSON.parse(readFileSync(cfgPath, "utf8"));
    if (cfg && cfg.emitter) return cfg.emitter;
  } catch {}
  return null;
}

function emit(sessionId, items) {
  try {
    const emitter = resolveEmitter();
    if (!emitter) return;

    const tmp = join(tmpdir(), `neon-opencode-${Date.now()}-${Math.random().toString(36).slice(2)}.json`);
    writeFileSync(tmp, JSON.stringify(items), "utf8");

    // 走 -ItemsFile 而不是把 JSON 塞进 argv：Windows 上带引号的 JSON 经 argv 会被
    // 层层转义撕碎（投递器里实测过「无效的 JSON 基元」）。
    const args = [
      "-NoProfile",
      "-ExecutionPolicy",
      "Bypass",
      "-File",
      emitter,
      "-Agent",
      AGENT,
      "-SessionId",
      String(sessionId || ""),
      "-ItemsFile",
      tmp,
    ];
    const child = spawn("powershell.exe", args, { detached: false, stdio: "ignore" });
    child.on("error", () => {});
    // 投递器自己是幂等的，落库很快；超时兜底避免僵尸进程
    setTimeout(() => {
      try {
        child.kill();
      } catch {}
      try {
        unlinkSync(tmp);
      } catch {}
    }, 8000);
  } catch {
    // 旁路：吞掉
  }
}

// 跨两次 hook 传递：before 拿到清单，after 才投递，需要一个进程内暂存。
// 用 Map 而不是模块级变量，避免多会话互相覆盖。
const pending = new Map();

export const NeonBoardTodoBridge = async () => {
  return {
    // before 抓 args（此时能拿到完整清单），after 确认执行成功再投递 ——
    // 工具失败了还投递，看板上会出现一张并不存在的「已完成」卡片。
    "tool.execute.before": async (input) => {
      try {
        if (input?.tool !== TOOL) return;
        const todos = input?.args?.todos ?? input?.args?.items ?? [];
        const items = itemsFrom(todos);
        if (items.length === 0) return;
        const sid = input?.sessionID ?? input?.sessionId ?? "";
        pending.set(String(sid), items);
      } catch {}
    },

    "tool.execute.after": async (input, output) => {
      try {
        if (input?.tool !== TOOL) return;
        // output 里带 error 表示工具没跑成功，不投递
        if (output && (output.error || output.isError)) return;
        const sid = String(input?.sessionID ?? input?.sessionId ?? "");
        const items = pending.get(sid);
        if (!items || items.length === 0) return;
        emit(sid, items);
        pending.delete(sid);
      } catch {}
    },
  };
};

export default NeonBoardTodoBridge;
