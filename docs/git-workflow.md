# Git 工作流手册

> 本仓库的 Git 使用规范。核心一句话：**改动在分支上做，做完必须合并回 `main`。**
>
> 规范条款见 [`../AGENTS.md`](../AGENTS.md)，本文给出逐步命令与排错方法。

---

## 一、核心原则

```text
        ┌──────────────┐
        │     main     │  ← 受保护，禁止直接提交
        └──────┬───────┘
               │  git switch -c feature/xxx
        ┌──────▼───────┐
        │ feature/xxx  │  ← 所有改动在这里提交
        └──────┬───────┘
               │  git merge --no-ff
        ┌──────▼───────┐
        │     main     │  ← 合并后删除功能分支
        └──────────────┘
```

| 原则 | 说明 |
| --- | --- |
| 主分支受保护 | `main` 上不能直接提交代码，只能接收合并 |
| 一次改动一个分支 | 分支存活期间不混入无关改动 |
| 合并用 `--no-ff` | 保留分支合并痕迹，历史可回溯 |
| 合完即删 | 合并后立即 `git branch -d`，保持分支列表干净 |

---

## 二、日常流程

### 1. 开工：拉分支

```powershell
git switch main
git status                      # 确认工作区干净
git switch -c feature/task-tag-filter
```

分支名格式：`<前缀>/<英文小写短横线描述>`。允许前缀：

`feature` `fix` `hotfix` `chore` `docs` `refactor` `perf` `test` `build` `ci` `style` `revert`

### 2. 干活：改代码并提交

```powershell
# 改代码……
git add -A
git status                      # 复核暂存内容
git commit -m "feat(board): 增加卡片按标签筛选"
```

提交信息格式：`<type>(<scope>): <中文简述>`

| type | 用途 | 示例 |
| --- | --- | --- |
| `feat` | 新功能 | `feat(board): 增加卡片按标签筛选` |
| `fix` | 缺陷修复 | `fix(model): 修复删除后统计芯片未刷新` |
| `chore` | 构建/脚本/依赖 | `chore(git): 初始化分支工作流与钩子` |
| `docs` | 文档 | `docs(plugin): 补充插件权限清单说明` |
| `refactor` | 重构不改行为 | `refactor(db): 抽取查询构造辅助函数` |
| `perf` | 性能优化 | `perf(list): 列表批量刷新合并为一次更新` |
| `test` | 测试 | `test(db): 增加任务排序用例` |
| `build` | 构建系统 | `build(cmake): 切换到 Ninja 生成器` |
| `style` | 纯格式 | `style(qml): 统一缩进为四空格` |
| `revert` | 回滚 | `revert(feat): 撤销卡片拖拽功能` |

`scope` 可省略，但不允许写成空括号。

### 3. 收尾：合并回主分支

推荐一键脚本：

```powershell
pwsh -File scripts/git-done.ps1
```

它会自动完成：检查工作区干净 → 切回 `main` → `git merge --no-ff` → 删除功能分支。

手工等价步骤：

```powershell
git switch main
git merge --no-ff feature/task-tag-filter    # 必须 --no-ff
git branch -d feature/task-tag-filter        # 删除已合并分支
```

合并后看一眼历史，确认分支痕迹保留：

```powershell
git log --oneline --graph -10
```

### 4.（可选）推送

当前仓库为纯本地仓库，尚未配置远程。将来接入远程后：

```powershell
git remote add origin <仓库地址>
git push -u origin main
```

推送同样受 `pre-push` 钩子约束：功能分支必须已合并进 `main` 才允许推送。

---

## 三、钩子拦截与排查

仓库钩子位于 `.githooks/`，通过 `core.hooksPath` 启用。共 4 个：

| 钩子 | 触发时机 | 拦截内容 |
| --- | --- | --- |
| `pre-commit` | `git commit` | 主分支直接提交；分支名缺前缀 / 前缀非法 |
| `commit-msg` | `git commit` | 提交信息不符合 `<type>(<scope>): <subject>` |
| `pre-push` | `git push` | 功能分支未合并进 `main`；主分支非快进推送 |
| `post-merge` | `git merge` | 不拦截，仅提示可删除的已合并分支 |

### 常见拦截与处理

**① 报错「禁止在受保护分支 'main' 上直接提交」**

```powershell
git switch -c feature/<简述>    # 换到分支上再提交
```

若当前改动已经在 `main` 上被 `git add` 过，`git switch -c` 会带着暂存内容一起带走，无需处理。

**② 报错「分支名 'xxx' 缺少类型前缀」**

分支名必须形如 `feature/xxx`。用 `git branch -m <新名>` 改名。

**③ 报错「提交信息格式不符合规范」**

改成 `<type>(<scope>): <说明>`，例如 `fix(model): 修复删除后统计芯片未刷新`。

**④ 报错「分支 'xxx' 尚未合并进 'main'，禁止推送」**

先合并再推：

```powershell
pwsh -File scripts/git-done.ps1 -Branch xxx
git push origin main
```

### 紧急绕过

```powershell
git commit --no-verify
```

仅在热修复等特殊场景使用，且事后必须补一次规范合并。

### 钩子不生效？

```powershell
git config core.hooksPath    # 应输出 .githooks
```

Windows 上钩子文件必须是 **LF 换行 + 无 BOM**，否则 `sh` 无法解析。仓库已通过
`.gitattributes` 固定：`.githooks/* text eol=lf`。

---

## 四、常用命令速查

```powershell
# 看当前状态
git status
git branch -vv

# 看图形化历史
git log --oneline --graph --decorate -20

# 看某个文件改了什么
git diff
git diff --cached
git diff main...HEAD            # 本分支相对 main 的全部差异

# 撤销
git restore <file>              # 丢弃工作区改动
git restore --staged <file>     # 取消暂存
git reset --soft HEAD~1         # 撤销上一次提交但保留改动
git revert <commit>             # 用新提交撤销旧提交（已推送后用这个）

# 分支管理
git branch -d <branch>          # 删除已合并分支
git branch -D <branch>          # 强制删除（谨慎）
git switch -c <branch>          # 新建并切换
git merge --no-ff <branch>      # 合并（保留合并痕迹）

# 临时存放
git stash push -m "说明"
git stash list
git stash pop
```

---

## 五、FAQ

**Q：为什么强制 `--no-ff`？**

快进合并会把分支提交直接平铺到 `main` 上，历史里看不出「这批改动来自哪个分支」，
出问题无法整体回退。非快进合并会生成一个合并节点，保留了「分支 → 合并」的来源关系。

**Q：能不能在 `main` 上直接改小东西？**

不行。哪怕只改一行 README，`pre-commit` 也会拦截。走 `docs/xxx` 分支，
改完合并回来，流程一致。

**Q：钩子失败了怎么查？**

```powershell
git commit -m "..."      # 看提示信息
```

钩子脚本本身在 `.githooks/` 下，可直接阅读；需要临时手工执行验证：

```powershell
sh .githooks/pre-commit
```

**Q：仓库没有远程，但想用 `git push` 做备份？**

可以先建一个本地裸仓库当"远程"：

```powershell
git init --bare E:\mine\neonboard-backup.git
git remote add backup E:/mine/neonboard-backup.git
git push -u backup main
```

**Q：如何确认钩子已生效？**

```powershell
git switch main
git commit -m "test" --allow-empty    # 应当被 pre-commit 拦截
```
