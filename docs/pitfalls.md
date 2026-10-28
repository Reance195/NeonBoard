# 问题与解决方案台账（pitfalls）

> 这份文档只干一件事：**把踩过的坑变成可复用的判据**，避免同一片坑被不同 agent 反复踩。
> 与 `AGENTS.md` 的分工：宪章写「必须怎么做」（规范），这里写「为什么会踩、怎么识别、怎么绕」（经验）。
> 已有专门文档的，这里只留一条索引并指过去，不重复展开。

## 维护约定

1. 新坑按分类追加，每条写 **现象 / 根因 / 解法 / 预防**，解法已进代码或脚本的就标出处（提交号或文件）。
2. **还没有解法的**进[第七节](#七尚无解决方案需人拍板)，写清「卡在哪、谁拍板」；不要混在已解决条目里。
3. 只写对**任何机器都成立**的经验。只对这台机器成立的运维便条（代理端口、杀软排除、证书开关）不进仓库。
4. 涉及「绕过闸门」的做法一律不写命令原文，只写「由维护者授权后手工处置」——闸门本身以 `AGENTS.md` 为准。
5. 与宪章的边界：`AGENTS.md` §五.8 已列的三条构建/测试坑（AutoMoc 原始字符串、旧 `*.obj` 造成 LNK2001、
   编辑 `.ps1` 静默丢 BOM）**以宪章为准**，本文只补「怎么识别」与出处，不复述规则原文。

---

## 一、Git 与多代理并行

### 1.1 锚点区长期落后 `origin/main`，越攒越多

- **现象**：agent 每次合并都跑过 `git-done.ps1`，但回到仓库根目录看到的还是几十个提交前的代码。
- **根因**：锚点快进的前提是「锚点区干净，或只有 `progress.json` 被抓脏」（`Sync-AnchorWorktree`）。
  桌面工具习惯把产物写进**调用方 CWD**（本仓库实际踩的是 Qoder 的画布与额度报告 `.qoder-credits/`），一旦这些目录既没入库也没被忽略，
  锚点区就**永久处于「有未提交改动」状态**，快进每次被跳过——跳过只是灰字提示，没人回头翻。
- **解法**：把工具产物目录写进 `.gitignore`（`.qoder-credits/`、`.workbuddy/` 同源），见 `5b245a8`。
- **预防**：判断标准是「这个目录是不是某个工具的输出」——是就进 `.gitignore` 并加一行注释说明它为什么会拖脏锚点。
  新增此类工具前先想清楚：落在锚点的未跟踪文件，等于给所有 agent 的收尾流程装了个隐形开关。

### 1.2 应用在跑，快照文件把锚点同步卡死

- **现象**：`.gitignore` 补好后锚点仍然不同步，日志停在「临时暂存 progress.json → 暂存失败，跳过快进」。
- **根因**：主线实例常驻运行，`RoadmapSync` 会随时写 `progress.json`；旧逻辑**无条件先 `git stash`**，
  而 stash 的 checkout 步骤要写这个文件，撞上应用占用就失败。
- **解法**：改成**先直接 `git merge --ff-only origin/main`**——上游没动 `progress.json` 时本地快照抓脏根本不挡快进；
  确实动了才临时暂存后重试，失败时提示「应用可能在运行」（`2b2538c`）。
- **预防**：任何「为了绕开脏文件而先写这个文件」的设计，都要先问一句：这次写会不会和常驻进程抢句柄。
  能用一次原子操作解决的，不要拆成 stash + merge + pop 三步。

### 1.3 stash pop 撞车，把冲突标记留在快照里

- **现象**：锚点 `progress.json` 里出现 `<<<<<<<`，下次应用写回会把标记打进快照文件，脏检查从此一直误判。
- **根因**：CI 每次回写都会动 `progress.json`，本地快照与主线快照必然冲突；失败的 `pop` 会把文件挂成 `UU` 并留下标记。
- **解法**：撞车时**主线版本优先**（`git checkout HEAD -- progress.json`），本地快照留在 stash 里并打印其提交号供取回（`9da6f06`）。
- **预防**：`progress.json` 是快照不是数据源，冲突时永远向主线让步；要保的是本地内容**可找回**，不是本地内容**覆盖上去**。

### 1.4 合并推送被 non-fast-forward 拒绝

- **现象**：`git push origin HEAD:main` 报 `cannot lock ref 'refs/heads/main': is at <新哈希> but expected <旧哈希>`。
- **根因**：多 agent 并发推主线，串行化本来就靠远端快进保护（`AGENTS.md` §7.3），这是**预期行为**不是故障。
- **解法**：重跑 `git-done.ps1`（会重新 fetch 并基于最新 `origin/main` 建临时分支）。
  若脚本已把当前 worktree 切到临时分支，先 `git switch <原分支>` 再 `git branch -D merge/<slug>`，
  否则会报 `cannot delete branch ... used by worktree`。
- **预防**：动手前 `git ls-remote origin` 现取基线，别拿几分钟前的快照当事实；重写历史类操作更需要现取现用。

### 1.5 功能分支直推被 pre-push 拦下

- **现象**：`git push origin feature/xxx` 被拒：「尚未合并进 main，禁止推送」。
- **根因**：钩子按设计拦未合并分支；推分支只为让 claim ref 指向的提交在 origin 可达（`AGENTS.md` §7.2.1）。
- **解法**：正常收尾一律走 `git-done.ps1`；确实需要先让远端能看到提交时，用**领用/闸门放行**的方式，而不是手工绕过。
- **预防**：分支存活期短、一次一事（条款 2），就不会出现「攒了一堆没合的分支」需要直推。

### 1.6 `git worktree remove` 报 Permission denied

- **现象**：`git worktree remove --force .worktrees/<agent>` 失败，看起来回收不掉。
- **根因**：调用方的当前目录就在那个 worktree 里（Windows 目录被进程占用）；元数据其实已摘除。
- **解法**：先 `cd` 出该目录，`git worktree prune`，再删空目录即可。
- **预防**：脚本与 agent 不要在准备回收的工作区内当 CWD 执行删除命令。

### 1.7 同一脚本被两个 agent 同时改写

- **现象**：`git-done` 合并阶段在 `scripts/git-done.ps1` 上冲突。
- **根因**：热点文件没有行段划归（`AGENTS.md` §7.4），两人改同一段。
- **解法**：按 §7.5 手工合并——**禁止 `--ours/--theirs` 盲解**；本仓库这条冲突的实际处置是「保留双方改进」，
  即直接快进（1.2）与 pop 撞车回退（1.3）两段逻辑合并共存（`2b2538c`）。
- **预防**：改 `Sync-AnchorWorktree` / `Release-ClaimRef` / 任务闸门这类共享函数前，先看 `git log -- <文件>` 最近有没有别人的手。
  `rerere` 现在由 `scripts/setup-git-hooks.ps1` 自动开启（§7.5，不再指望每个 agent 手工配），同类冲突第二次会复用上次解法。

### 1.8 台账里留着「分支已不存在」的过期领用登记

- **现象**：卡片早就合入了，派生器 / `--check` 一直报认领分支不存在。
- **根因**：过去只有「下一个人领卡时顺带清理」这条隐式路径，顺带不保证发生。
- **解法**：`git-done.ps1` 收尾时调用 `scripts/release-claims.py` 全量扫描，只删「本地与远端都没有认领分支」的条目，
  并作为独立的 `chore(board): 清理过期领用登记 <短号>` 提交快进补推；失败只回滚该提交，不阻断已完成的合并（`c8419b8`）。
- **预防**：清理走脚本，不要手删台账；`baseline` 仍归维护者所有（见 1.9）。

### 1.9 台账 baseline 落后于卡片交付说明

- **现象**：卡片 `note` 已写「交付（日期）+ 验收结果」，`baseline` 里同一条还标着 `todo`。
- **根因**：`baseline` 归维护者所有（agent 不得改），结项靠 `#短号=done` 提交标记 + `derive_board_status.py` 校正；
  其它会话合入的代码若没带结项标记，note 与 baseline 就会漂。
- **解法 / 预防**：结项必须带结项标记；发现漂移**不要顺手补 baseline**，这条已记在第七节 7.1。

### 1.10 锚点同步没生效时的三步定位

- **第一步看原因**：`Sync-AnchorWorktree` 只会打这四类提示——锚点不在 `main` / 有未提交改动 N 项 / 暂存失败 / 无法快进。
- **脏文件类**：`git -C <锚点> status --porcelain`。只有 `progress.json` 是被允许的；冒出未跟踪目录就按 1.1 补 `.gitignore`。
- **分叉类**：`git log --oneline origin/main..main` 有输出，说明锚点有本地提交（见 7.4），**不要自行变基或改写历史**。
- **占用类**：只有 `progress.json` 脏却暂存失败，是主线实例正占着该文件；1.2 之后这类已少见，重跑一次即可。

---

## 二、脚本与编码（PowerShell / Python）

### 2.1 `.ps1` 少了 UTF-8 BOM，整份脚本报「批处理式」语法错

- **现象**：Windows PowerShell 5.1 下中文字符串像被撕开，引号配对错乱；PowerShell 7 却正常。
- **根因**：`.ps1` 在 Windows 工作区检出为 CRLF，无 BOM 的 UTF-8 被 5.1 当 GBK 读，中文最后一个字节会与紧随其后的引号配对。
- **解法 / 预防**：新增脚本带 BOM 保存（`AGENTS.md` 第四节已定为硬约定）；读无 BOM 的数据文件必须显式 `-Encoding utf8`
  （`agent-init.ps1` 读台账就是栽在这，`c471a5d` 修）。注意**编辑工具会静默剥掉 BOM**（一晚上丢过两次），
  已由 `scripts/check_script_hygiene.py` 规则 C 在 CI 拦（§五.8 第三条）——改完 `.ps1` 不放心就跑一次卫生检查。

### 2.2 临时脚本里的中文注释把下一行吞掉

- **现象**：临时探针脚本莫名少一行、或字符串跨行。
- **根因**：同 2.1 的 GBK 误读，且临时脚本往往忘了带 BOM。
- **预防**：临时 `.ps1` 一律**纯 ASCII**，用完即删（`AGENTS.md` 第四节）。

### 2.3 原生命令的 stderr 把脚本拦腰打断

- **现象**：MSBuild / ISCC / git 明明成功，脚本却在这一步抛终止性错误。
- **根因**：`$ErrorActionPreference = 'Stop'` 下 `2>&1` 会把 stderr 的进度行升级成错误（NativeCommandError）。
- **解法 / 预防**：原生命令统一走带日志的调用封装（`git-done.ps1` 的 `Invoke-Cmd` / `Invoke-GitQuiet`），**只看退出码**判成败（§7.3）。
  另：`git-done.ps1` 用 pwsh 7 跑，5.1 在 `2>&1` 处更容易炸。

### 2.4 带空格的参数被拆词

- **现象**：钩子投递/CLI 传带空格的标题文本，结果只剩第一个词。
- **根因**：`Start-Process -ArgumentList "a b c"` 按空格拆词。
- **解法 / 预防**：带空格文本改走文件通道（`--emit-hook-file`，§8.4）。

### 2.5 本机安全策略拦住 `reg.exe` / `.NET Process.Start`

- **现象**：`vcvars64.bat` 拿不到 SDK 路径，`build.ps1` 失败；或 PowerShell 起进程被拒。
- **解法**：`scripts/dev-env.ps1` 点号装配环境后再构建（§一）；手工装配时 `PATH`/`LIB`/`INCLUDE` 必须
  是 **Windows 风格路径 + `;` 分隔**，用 `/c/...` 会 `LNK1104 无法打开 kernel32.lib`。
- **预防**：GUI 子系统 exe 的 stdout 在 PowerShell 里拿不到，验证一律改看应用日志（见 4.1）。

### 2.6 Python 脚本打印中文或符号时 GBK 崩

- **现象**：`check_scope.py` 报 `UnicodeEncodeError: 'gbk' codec can't encode character '✓'`——判定其实是对的，
  只是打印炸了，看起来像检查失败；临时靠 `PYTHONIOENCODING=utf-8` 顶着。
- **根因**：Windows 上 stdout 默认跟随 ANSI 代码页，输出被重定向进管道（CI、`Invoke-Cmd`）时尤其明显。
- **解法**：脚本开头对 `sys.stdout` / `sys.stderr` 做 `reconfigure(encoding="utf-8", errors="replace")`，
  已统一进 `check_scope.py` / `check_script_hygiene.py` / `task_gate.py`（`c8419b8`）。
- **预防**：新增 Python 工具凡要打印中文或符号就自带 reconfigure，**不要把编码要求推给调用方的环境变量**。

---

## 三、构建与 Qt / QML

### 3.1 `rc.exe not found` / `type_traits` 找不到

- **根因**：新机器或新 worktree 没装配 MSVC/SDK 环境，或直接手敲 cmake 跳过 `vcvars64`。
- **解法 / 预防**：永远走 `scripts/build.ps1`（自动探测），新 clone 先 `. ./scripts/dev-env.ps1`（§一、§7.7）；
  配置缓存坏了用 `-Reconfigure`。

### 3.2 `LNK1104` 无法打开 `NeonBoard.exe`

- **根因**：上一次沙箱实例还在跑，链接器写不进目标文件。
- **解法**：只结束**本 worktree** 的进程（按可执行路径过滤，例如路径含该 agent 名），不要按进程名一把杀——会误杀主线实例和同事的预览实例。
- **预防**：`dev-run.ps1` 起的沙箱实例用完就退，预览实例用 `scripts/preview.ps1 -Action Stop` 停干净；
  多实例本身是允许的（`NEONBOARD_INSTANCE` 加后缀），别为了「怕冲突」而全局杀进程。

### 3.3 AutoMoc 与 C++ 原始字符串：链接期才炸

- **现象**：测试类里用 `R"(...)"` 写 JSON fixture，编译一路过，链接报 `metaObject` / `qt_metacast` 未解析（LNK2001），
  生成的 moc 文件是空的；清 `*_autogen` 也不见得好。
- **根因**：moc 不认原始字符串——内容里有 `)"` 会提前截断（报 missing ')'），带花括号的 JSON 会让它干脆不生成元代码。
- **解法 / 预防**：测试数据里的 JSON / 正则一律用**转义字符串拼接**（`AGENTS.md` §五.8 第一条已定为约定）。

### 3.4 改了公共头文件签名，却报「无法解析的外部符号」

- **现象**：签名明明改对了，链接器说符号找不到，看起来像签名写错。
- **根因**：旧 `*.obj` / `*_autogen` 没重编，链接器拿的是改签名之前的目标文件。
- **解法**：删掉对应测试的产物（`*.obj` 连同它的 `*_autogen` 目录）再走 `scripts/build.ps1`，别急着回改签名（§五.8 第二条）。

### 3.5 新增 `qml/components/*.qml` 后 `Theme` / `Motion` ReferenceError

- **现象**：组件单独看没问题，一挂上去就报单例找不到，面板整块空白。
- **根因**：没在 `CMakeLists.txt` 里设 `QT_RESOURCE_ALIAS`，QML 文件脱离模块上下文。
- **预防**：新增组件必须同步 `set_source_files_properties(... QT_RESOURCE_ALIAS "<Name>.qml")`；
  界面令牌只能用 `Theme.xxx`，不写死颜色（§10.1）。

### 3.6 `ComboBox.textRole` 覆盖导致整块设置面板加载失败

- **现象**：改一处下拉框，整个 SettingsPanel 白屏，日志只有一行 QML 错误。
- **根因**：`textRole` 是 FINAL 属性，不能覆盖；`onActivated: { ... index ... }` 的信号参数注入也已弃用。
- **解法**：自定义显示走 `contentItem` + 从 `model[currentIndex][labelRole]` 自取标签（`displayText` 对 `QVariantList<map>` 会退化成空串），
  信号用显式形参 `onActivated: (index) => ...`。
- **预防**：Qt Quick Controls 的 FINAL 属性先查文档再覆盖；QML 报错要看**日志里最后一条**并顺带确认它前面是否有加载失败的兄弟节点。

### 3.7 控件状态「看起来不同步」多为误读

- **现象**：以为 `Switch` 没跟着 meta 值走。
- **结论**：Basic style 深色面板里**滑块靠右 = 选中**；`checked` 绑定 + `onToggled` 写回是正确用法。
- **预防**：判定界面状态要截图放大核对指针位置，或直接读 `meta` 表值，别凭缩略图印象改代码。

---

## 四、界面验收与自动化

### 4.1 GUI 子系统 exe 拿不到 stdout

- **现象**：`--selftest` / `--smoke` 在 PowerShell 里看不到输出，误判成没跑。
- **解法 / 预防**：看 `.sandbox/neonboard.log`；测试用 `-o out.txt,txt`；CLI 需要回传结果时走 stdout（`emitCliLine()`，§7.9）。
  日志断言取**最后一条**匹配。

### 4.2 `verify.ps1` 在 worktree 里找不到运行日志

- **现象**：验收脚本报「未生成运行日志」，可应用明明跑起来了。
- **根因**：脚本按 `%APPDATA%\com.lin\NeonBoard` 拼日志路径，而 worktree 里的 exe 被 `AppPaths` 护栏强制写到本工作区 `.sandbox`；
  更糟的是没设沙箱变量时，脚本会去动**真实实例正在占用**的文件（`Remove-Item` 直接被打断过）。
- **解法**：读应用落盘位置收口到唯一入口——PowerShell 点源 `scripts/nb-paths.ps1`（`Get-NbLogFile` / `Get-NbBoardDb`），
  Python 侧走 `scripts/claim-card.py` 读同一组环境变量；`verify.ps1` 未设 `NEONBOARD_DATA_DIR` 时自开
  `<BuildDir>\verify-sandbox`，路径口径随之正确（§7.6）。
- **预防**：**不要在第三个文件里再拼一次路径**——`scripts/check_script_hygiene.py` 会在 CI 拦下那个字面量。
  手工跑 exe 而不带沙箱变量时，日志仍在 `%APPDATA%`，这不算脚本坏，是环境变量没设。

### 4.3 Qt 的 ScrollView 不响应模拟滚轮

- **现象**：`PostMessageW` 发滚轮消息，界面不动，截图截不到面板下半部分。
- **解法**：用 `SendMessageW(WM_MOUSEWHEEL)` 且 `lParam` 传**屏幕坐标**；`PostMessage` 版本对 Qt 不可靠。
- **预防**：设置面板是固定尺寸上限（约 520×560 逻辑像素），**放大窗口不会放大面板**，长内容必须滚动后才能截全。

### 4.4 界面自动化不能抢焦点、还要过 DPI

- **解法**：`SetProcessDpiAwarenessContext` + `ShowWindow(SW_SHOWNOACTIVATE)` + `SetWindowPos(SWP_NOACTIVATE)`，
  取图用 `PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)` 走 GDI；点击用消息投递而不是移动真实光标。
- **预防**：验收点击要有**新鲜截图确认坐标**——坐标过期会点到别的控件（曾把 `exec.defaultAgent` 误改成别的 profile，
  事后要回读 `meta` 表核对并改回）。

### 4.5 `--smoke` 的 4 秒窗口不足以验证事件消费

- **根因**：`HookEventQueue` 一投出就 `markDelivered`，插件后台还没 ready 时事件被丢弃。
- **解法 / 预防**：验证事件链路必须用**常驻实例**（§11.3）；只跑 `--smoke` 通过不代表插件收到了事件。

---

## 五、网络与远程操作

### 5.1 GitHub HTTPS 443 时通时断

- **现象**：同一分钟内 `git fetch` 成功、紧接着 push 报 `Recv failure: Connection was reset` 或连不上 443（rc=128）。
- **解法**：带间隔的重试循环（3–5 次）；分支与本地合并提交不会因此丢。
  需要改走 SSH 时用**一次性环境变量**改写 URL，不动仓库或用户全局 git 配置：
  `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0='url.<ssh-url>.insteadOf' GIT_CONFIG_VALUE_0=<https-url>`。
- **预防**：故障链条与两台加速器互踩的分析见 [`handoff-network-issue.md`](handoff-network-issue.md)；
  凡「只对这台机器成立」的代理细节留本地，不进仓库。

### 5.2 领用与闸门需要联网

- **根因**：认领占位 `refs/claims/<短号>` 推 origin 才算数，本地存在同名 ref 不算（fail-closed，§7.2.1）。
- **预防**：离线时不要指望认领成功；本地孤儿 `refs/claims/*` 应清掉，不作为认领依据。

---

## 六、应用运行时与沙箱

### 6.1 沙箱实例把看板状态写回仓库快照

- **现象**：测试卡出现在仓库 `progress.json` 里。
- **根因**：路线图快照默认从 exe 目录向上探测仓库根，沙箱实例没设 `NEONBOARD_ROADMAP` 就会写回仓库文件。
- **解法 / 预防**：跑沙箱应用统一用 `scripts/dev-run.ps1`（固定快照路径到沙箱内并默认关闭自动写回）；
  验收完 `git status` 若发现 `progress.json` 被写脏，还原它（§7.6、§7.7）。

### 6.2 运行态数据与看板库混淆

- **预防**：钩子事件走独立库 `hooks.db`，不写 `board.db`（§8.2）；执行运行态在 `task_run` 侧表 + `runs/` 落数据目录，不进仓库（§12.2）。
  新增侧表/键位一律不升 `user_version`，且要断言它没变（`tst_databasemigration`）。

### 6.3 自动更新静默安装「悄悄放弃」

- **现象**：升级后注册表版本没变，安装器却没报错。
- **根因**：残留的 `NeonBoardHost.exe` 进程占着目标文件。
- **解法 / 预防**：升级前先清掉残留 Host 进程；链路验证用 `--update-apply`，可配 `--update-source` 指向本地清单（`docs/auto-update.md`）。

---

## 七、尚无解决方案（需人拍板）

### 7.1 已交付的卡片 `baseline` 还停在 `todo`

- **现状**：`docs/board-ledger.json` 里 R6-01 ~ R6-06 的代码都在主线、`note` 也写了交付，但 `baseline` 仍是 `todo`（R6-07 之后同理）。
- **风险**：`baseline=<短号> done` 是「拒绝重新领用」的判据之一（§条款 6）。它停在 `todo`，意味着
  **任何人都能把一张已交付的卡重新领一遍**，做出重复实现；派生器只校正 `progress.json` 的 `status`，不动台账 `baseline`。
- **卡在哪**：`baseline` 归维护者，agent 不得改（也不该「顺手补别人留下的缺口」）。
  要么维护者批量结卡，要么加一条闸门：合入带 `#短号=done` 的提交时同步台账 `baseline`，并明确这是授权代登记的一部分。

### 7.2 worktree 里有提交、但 origin 上没有对应分支时无人报警

- **现状**：宪章 §7.2.1 要求「认领分支必须推 origin」，否则 claim ref 指向的提交在远端不可达、CI 领用闸门无法校验。
  实际发生过：一个会话的 R5-04 实现只在 `.worktrees/<agent>` 本地存在、16 小时无活动（该成果最终在 `6c235f1` 合入，闭环靠的是人盯）。
- **缺口**：没有任何检查会主动列出「本地有提交、origin 无同名分支」的工作区，全靠维护者翻 `git worktree list`。
- **建议解法**：`agent-init.ps1` 开区时（现在已经在打印工作区数量与闲置天数）顺带列出这类工作区并显式警告；
  回收决定仍归人，不自动删（§7.10，自动清理极易端掉正在跑的任务）。

### 7.3 锚点区并发人工同步没有登记

- **现状**：`agent-workspace.ps1` 有 `-Action Claim -Scope root`，但 `git-done.ps1` 的锚点快进**不看这个登记**，
  于是可能出现两个会话同时在锚点区 stash / 快进 / 暂存快照（实际已遇到：快照的已暂存改动来自另一会话）。
- **卡在哪**：要不要让 `Sync-AnchorWorktree` 在 root 被占用时直接跳过，需要定；目前只能靠人错开。

### 7.4 锚点区落下「只在本地的提交」，快进同步从此长期失效

- **现状**：锚点 `main` 上出现过 `chore(release): v0.1.3 …` 这类只在本地、没在 `origin/main` 的提交。
  `Sync-AnchorWorktree` 只做快进，遇到分叉立即放弃（`fatal: Not possible to fast-forward`），
  于是锚点会越漂越远，而且**这个本地提交不在远端**——机器出问题它就没了。
- **为什么会发生**：发版/版本号提交天然发生在根目录（维护者在那儿跑 `release.ps1`），
  而宪章条款 1/5 又禁止在 `main` 上直接提交；两条规则在「发版」这个场景里没接上。
- **识别**：`git log --oneline origin/main..main` 有输出即分叉；`git rev-list --count main..origin/main` 看落后多少。
- **卡在哪**：处置要么把这个本地提交补推到远端（需要人决定用哪个分支承载、是否算合法主线内容），
  要么改发版流程（版本 bump 走独立分支 + `git-done` 合并，锚点不产生本地提交）。
  **需要维护者定发版归属**；agent 不得在锚点区提交、也不得自行变基或改写历史。

### 7.5 codex CLI 未实测，不内置 profile

- **现状**：内置只有 opencode / claude / cursor-agent（§12.1 禁止内置未实测 profile）。
- **需要**：实测后补命令行拼装、只读模式与输出格式，并更新 [`agent-profile.md`](agent-profile.md)；在那之前由用户自行新增 profile。

### 7.6 执行面板截图缺 GUI 手工补拍

- **现状**：R6-06 遗留 `docs/screenshots/exec-panel.png` 未出（自动化验收项不含截图）。
- **需要**：在沙箱里跑一次执行流程后补拍，并同步 `README.md` 的功能描述（§五 第 6 条）。

### 7.7 工具产物目录还会继续长

- **现状**：`.gitignore` 现在逐个点名（`.workbuddy/`、`.qoder-credits/`）。每接入一个新桌面工具，就可能冒出一个写 CWD 的目录，
  在补进 `.gitignore` 之前它会静默拖脏锚点（1.1）。
- **建议解法**：`git-done.ps1` 的锚点同步在跳过时把「脏文件清单」直接打出来（现在只打数量），让这类问题一眼可见；
  或约定 agent 会话产物一律写到仓库外的显式目录。

---

## 八、同专题的既有文档

本文不重复展开这些，遇到对应场景请直接翻：

| 文档 | 管什么 |
| --- | --- |
| [`git-workflow.md`](git-workflow.md) | 分支工作流、钩子拦截与排查（§四）、流程类 FAQ（§六） |
| [`board-sync.md`](board-sync.md) | 看板状态派生与一致性，含常见问题（§六） |
| [`handoff-network-issue.md`](handoff-network-issue.md) | 推送失败 / GitHub 连接问题的完整排查链条与结论 |
| [`handoff-r6-05.md`](handoff-r6-05.md) | R6-05 执行器交付上下文，含「踩过的坑（别再踩）」一节 |
| [`testing.md`](testing.md) | 测试与验收口径 |
| [`auto-update.md`](auto-update.md) | 升级链路与 `update.json` 清单 |
| [`adr/`](adr/README.md) | 架构决策记录（运行态与执行器安全边界见 ADR-0002） |
