# 贡献指南（CONTRIBUTING）

欢迎参与 NeonBoard。本文件说明如何把改动提交进本仓库。

完整的 Git 操作步骤见 [`docs/git-workflow.md`](docs/git-workflow.md)，
仓库级强制条款见 [`AGENTS.md`](AGENTS.md)。

---

## 一、核心约定（TL;DR）

```text
1. git switch main
2. git switch -c feature/xxx      # 拉分支
3. 改代码 + git commit             # 在分支上提交
4. git switch main
5. git merge --no-ff feature/xxx   # 合并回主分支
6. git branch -d feature/xxx       # 删除分支
```

**在 `main` 上直接提交会被钩子拦截。**

---

## 二、提交前自检

代码改动提交前，请在本地跑通：

```powershell
# 1. 编译
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release

# 2. 数据库自检
.\build\Release\NeonBoard.exe --selftest

# 3. 启动冒烟
.\build\Release\NeonBoard.exe --smoke
```

- 编译无错误、无新增警告
- `--selftest` 全部通过
- 涉及界面行为的改动，已同步更新 `README.md` 的功能项 / 快捷键 / 已知限制

---

## 三、提交信息规范

格式：`<type>(<scope>): <中文简述>`

```text
feat(board): 增加卡片按标签筛选
fix(model): 修复删除后统计芯片未刷新
docs(plugin): 补充插件权限清单说明
refactor(db): 抽取查询构造辅助函数
perf(list): 列表批量刷新合并为一次更新
chore(git): 初始化分支工作流与钩子
```

`type` 取值：`feat` `fix` `chore` `docs` `refactor` `perf` `test` `build` `ci` `style` `revert`

**scope 写模块名**，本项目常用：`board` `model` `db` `plugin` `tray` `hotkey` `window` `qml` `ui` `build` `git`

---

## 四、代码风格

### C++（`src/`）

- C++20，Qt 6 风格；类成员用 `m_` 前缀，方法用小驼峰
- 信号槽用 Qt 新语法（`&Class::method`）
- 数据持久化统一走 `Database`，不要在别处直接写 SQL
- 换行 LF，缩进 4 空格，文件末尾保留换行

### QML（`qml/`）

- 缩进 4 空格，属性名用小驼峰，对象名用大驼峰
- 颜色、间距、动画时长统一从 `qml/theme/Theme.qml` 取，不要硬编码
- 可复用组件放 `qml/components/`

### 禁止事项

- 不要提交 `build/`、`dist/`、`*.log`、`*.exe` 等产物（`.gitignore` 已排除）
- 不要提交密钥、令牌、`.env`、`*.pem`、`*.key`
- 不要在 `main` 分支上直接提交
- 不要用 `git push --force` 推主分支

---

## 五、合并请求自查清单

- [ ] 改动在独立分支上完成，分支名带规范前缀
- [ ] 编译通过，`--selftest` 无报错
- [ ] 提交信息符合 `<type>(<scope>): <简述>`
- [ ] 已用 `git merge --no-ff` 合并回 `main`
- [ ] 已删除已合并的本地分支
- [ ] 界面行为有变化时，`README.md` 已同步

---

## 六、遇到问题

- 提交被拦截 → 看终端提示，通常已写明正确命令
- 钩子细节 → [`docs/git-workflow.md` 的「钩子拦截与排查」](docs/git-workflow.md#三钩子拦截与排查)
- 想了解项目本身 → [`README.md`](README.md)
