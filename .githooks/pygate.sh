# pygate.sh —— 钩子层「python 是否真可用」的唯一判据与统一处置文案（DX-147）。
#
# 背景：`command -v python` 会被 WindowsApps 应用执行别名占位（命令「存在」但一跑就死，
# 退出码 9009/49 且无输出），旧钩子据此把审计闸门降级成一行 ⚠ 甚至静默失败。
# 本文件用真实执行探测替代，pre-commit / commit-msg / pre-push 三条钩子共用同一判定与文案。
#
# 约束：钩子文件与本文件必须 LF 行尾、无 BOM（宪章 §三）。

NB_PY=""

nb_py_probe() {
  # 真可用 = 能执行且 print(1) 原样输出 1；stub 命中、解析失败一律判不可用。
  local cand
  for cand in python python3 py; do
    if command -v "$cand" >/dev/null 2>&1; then
      if "$cand" -c "print(1)" 2>/dev/null | grep -qx "1"; then
        NB_PY="$cand"
        return 0
      fi
    fi
  done
  return 1
}

nb_staged_is_exempt() {
  # 杂务豁免分流：只看路径，不看声明；与 task_gate.py 的白名单保持一致。
  # 唯一权威判定仍在 task_gate.py（pre-push 联网复核）；这里的 shell 分流只决定
  # 「python 不可用时放行还是拦」，判定拿不准的一侧永远取「拦」。
  local files f
  files=$(git diff --cached --name-only --diff-filter=ACMR)
  [ -z "$files" ] && return 0
  for f in $files; do
    case "$f" in
      docs/*|scripts/*|.githooks/*|.github/*|*.md|progress.json|.gitignore|.gitattributes|LICENSE) ;;
      *) return 1 ;;
    esac
  done
  return 0
}

nb_staged_has_ledger_change() {
  git diff --cached --name-only --diff-filter=ACMR | grep -qx "docs/board-ledger.json"
}

nb_in_progress_state() {
  # 与 pre-commit 的中间态放行同一判据：这些提交在首次落定时已过了闸门。
  local git_dir
  git_dir=$(git rev-parse --git-dir 2>/dev/null)
  [ -e "$git_dir/rebase-apply" ] || [ -e "$git_dir/rebase-merge" ] || \
  [ -e "$git_dir/CHERRY_PICK_HEAD" ] || [ -e "$git_dir/MERGE_HEAD" ]
}

nb_py_block() {
  # $1 = 本次要做的校验名。响亮失败：点名处置动作，绝不静默。
  echo ""
  echo "==================================================================="
  echo " ❌ python 不可用，无法执行「$1」，已阻止本次操作。"
  echo "==================================================================="
  echo ""
  echo " 任务闸门依赖真 python 解释器（WindowsApps 的 python/python3 占位别名不算）。"
  echo " 处置二选一，做完直接重试即可："
  echo "   1) 点源仓库环境：  . ./scripts/dev-env.ps1"
  echo "   2) 装 Python 并确认当前 PATH 里执行 python -c \"print(1)\" 能输出 1"
  echo "      （Windows 还需在「应用执行别名」里关掉 python.exe / python3.exe 占位）"
  echo ""
  echo " 若确属紧急绕行，仅限维护者本人使用 git commit --no-verify（宪章 §三，agent 禁用）。"
  echo ""
}
