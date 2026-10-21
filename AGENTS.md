# NeonBoard 仓库宪章（AGENTS.md）

> 本文件是 NeonBoard 仓库的最高约束。任何人（人或 AI 代理）在本仓库中的所有改动，
> 都必须先读懂并遵守本文件。违反条款的操作会被 Git 钩子直接拦截。

---

## 一、项目速览

NeonBoard 是赛博霓虹风格的三列个人任务看板。

- 技术栈：Qt 6.8.3（MSVC 2022 x64）+ QML + C++20，本地 SQLite 持久化
- 目录约定：

  | 目录 | 内容 |
  | --- | --- |
  | `src/` | C++ 源码：数据库、模型、托盘、热键、窗口、插件宿主 |
  | `qml/` | QML 界面：主界面、霓虹背景、组件、主题 |
  | `shaders/` | GLSL 霓虹着色器 |
  | `plugins-samples/` | 示例插件（QML） |
  | `installer/` | Inno Setup 打包脚本 |
  | `scripts/` | 构建部署脚本与 Git 助手脚本 |
  | `docs/` | 设计文档、截图、插件 API 文档 |

- 构建命令：

  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
  cmake --build build --config Release
  ```

- 自检参数：`NeonBoard.exe --selftest`（数据库增删改查自检）、`--smoke`（启动 4 秒输出指标）。

---

## 二、Git 宪章（强制条款，不可绕过）

### 条款 1 —— 改动必须先拉分支

**禁止在 `main`（及 `master` / `develop`）分支上直接修改并提交。**
每一次功能改动、缺陷修复、文档调整，都必须从 `main` 拉出一个独立分支。

```powershell
git switch main
git pull                       # 有远程时先同步
git switch -c feature/<简述>
```

### 条款 2 —— 分支命名必须带类型前缀

允许的前缀：

| 前缀 | 用途 |
| --- | --- |
| `feature/` | 新功能 |
| `fix/` | 缺陷修复 |
| `hotfix/` | 紧急修复 |
| `chore/` | 构建、脚本、依赖等杂务 |
| `docs/` | 纯文档改动 |
| `refactor/` | 重构，不改行为 |
| `perf/` | 性能优化 |
| `test/` | 仅测试相关 |
| `build/` | 构建系统 |
| `ci/` | 流水线 |
| `style/` | 纯格式 |
| `revert/` | 回滚 |

命名格式：`<前缀>/<简短的英文小写短横线描述>`，例如 `feature/task-tag-filter`。
一次改动只对应一个分支，分支存活期间不要混入无关改动。

### 条款 3 —— 提交信息必须规范

格式：`<type>(<scope>): <中文简述>`

- `type` 必须取自：feat / fix / chore / docs / refactor / perf / test / build / ci / style / revert
- `scope` 可省略，但不得为空括号
- 主题句不超过 50 字

示例：

```text
feat(board): 增加卡片按标签筛选
fix(model): 修复删除后统计芯片未刷新
docs(plugin): 补充插件权限清单说明
chore(git): 初始化分支工作流与钩子
```

### 条款 4 —— 改动完成后必须合并回主分支

**功能分支上的成果不得长期滞留。** 改完必须合并回 `main`，并删除已合并的本地分支。

推荐用助手脚本一步完成：

```powershell
pwsh -File scripts/git-done.ps1
```

等价的手工步骤：

```powershell
git switch main
git merge --no-ff feature/<简述>     # 必须 --no-ff，保留分支合并痕迹
git branch -d feature/<简述>
```

> 禁止 fast-forward 合并（`--ff-only`），否则主分支历史会退化成分支线性堆积，
> 事后无法追溯「哪次改动来自哪个分支」。

### 条款 5 —— 主分支只接受合并提交

`main` 上只允许出现合并提交或文档/构建类的合并提交，不允许直接改代码提交。
主分支推送必须是快进，禁止 `--force`。

---

## 三、钩子强制说明

仓库通过 `core.hooksPath = .githooks` 启用以下钩子，条款落地情况如下：

| 钩子 | 拦截的行为 | 绕过方式 |
| --- | --- | --- |
| `pre-commit` | 在 `main`/`master`/`develop` 上提交；分支名缺少规范前缀 | `git commit --no-verify`（仅限紧急情况） |
| `commit-msg` | 提交信息不符合 `<type>(<scope>): <subject>` 格式 | `git commit --no-verify` |
| `pre-push` | 推送尚未合并进 `main` 的分支；主分支非快进推送 | 需先合并回 `main` |
| `post-merge` | ——（仅提示可清理的已合并分支） | —— |

`pre-commit` 在**合并冲突解决**、**rebase 续行**、**cherry-pick 续行**等中间态会自动放行，
不会阻断正常合并流程。

---

## 四、工作区卫生

- `build/`、`dist/`、`*.log`、`*.exe` 等编译产物与日志已在 `.gitignore` 中排除，不要手工 `git add -f`。
- 密钥、令牌、本地私有配置（`.env`、`*.pem`、`*.key`）**严禁入库**。
- 行尾统一：仓库内文本用 LF；`.ps1` / `.bat` / `.cmd` / `.iss` 在 Windows 工作区检出为 CRLF（见 `.gitattributes`）。
- 临时脚本用完即删，不要留在 `scripts/` 里。

---

## 五、代理执行约定

AI 代理在本仓库干活时：

1. **先读 `README.md` 与本文件**，再动代码。
2. **先拉分支再改代码**，不要在 `main` 上直接编辑（即便只是改文档）。
3. 改动完成后**必须合并回 `main`**，并删除分支，否则任务不算完成。
4. 合并前自检：`cmake --build build --config Release` 通过，且 `NeonBoard.exe --selftest` 无报错。
5. 不擅自提交 `build/`、`dist/`、日志；不擅自改动用户全局 Git 配置。
6. 涉及界面行为的改动，同步更新 `README.md` 中对应描述（如功能项、快捷键、已知限制）。

---

## 六、相关文档

- 完整操作手册（含命令示例与常见问题）：[`docs/git-workflow.md`](docs/git-workflow.md)
- 贡献者约定：[`CONTRIBUTING.md`](CONTRIBUTING.md)
- 项目说明与构建：[`README.md`](README.md)
