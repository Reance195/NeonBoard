# NeonBoard 软件自动更新

> R5-01。客户端只读一份「更新清单」，下载后先验 SHA-256 再安装；安装形态不同，
> 落地方式也不同 —— 这条分叉是整个功能里唯一需要小心的地方。

## 1. 客户端在做什么

启动后延迟 8 秒检查一次（`UpdateManager::startupCheck`），**最长每 20 小时才真的发一次请求**，
避免每次开程序都打网络、也避免打扰离线用户。除此之外：

| 入口 | 行为 |
| --- | --- |
| 设置面板「软件更新」 | 手动检查、立即更新、跳过此版本、自动检查开关、打开下载页 |
| 命令面板 `Ctrl+K` | 「检查软件更新」→ 打开设置并立即查一次 |
| 托盘 | 发现新版本时提示一次（不弹窗骚扰） |
| `--check-update` | 命令行检查，结果同时进 stdout 与日志 |
| `--update-apply` | 命令行把「检查 → 下载 → 校验 → 静默安装」一次跑完，无人值守升级 |

检查到新版本后，界面显示版本号、发布日期、包体积与更新日志（日志超 6 行截断）。

## 2. 清单：update.json

默认从仓库 raw 读取，**Gitee 优先、GitHub 兜底**（与插件市场同一套取舍，国内可达性优先）：

```text
https://gitee.com/Reance195/NeonBoard/raw/main/update.json
https://raw.githubusercontent.com/Reance195/NeonBoard/main/update.json
https://api.github.com/repos/Reance195/NeonBoard/releases/latest
```

三个候选源**串行**尝试，每个都有自己的 6 秒看门狗（`kSourceTimeoutMs`），
一次检查的总预算 25 秒（`UpdateManager::kDefaultCheckTimeoutMs`）。

这个设计是被真实故障逼出来的：`raw.githubusercontent.com` 在国内常年「只挂不答」，
早期单个源给 10 秒、总预算只有 15 秒时，它一个人就吃光配额，
排最后的 GitHub API 兜底源永远轮不到 —— 表现为**每次检查都报超时**，
哪怕仓库、Release、清单全都正常。所以：

> 单源超时 × 候选源个数必须明显小于总预算，否则兜底形同不存在。

单源超时到点由看门狗 `abort()`，客户端按网络错误处理并顺延到下一个源；
界面上会显示「连接超时（6 秒无响应）」而不是 Qt 的 `Operation canceled`。

格式：

```json
{
  "version": 1,
  "channel": "stable",
  "latest": "0.2.0",
  "publishedAt": "2026-10-26",
  "notes": "更新日志",
  "htmlUrl": "https://github.com/Reance195/NeonBoard/releases/latest",
  "artifacts": [
    { "kind": "setup",    "url": "NeonBoard-Setup-0.2.0.exe",     "sha256": "…", "size": 12345 },
    { "kind": "portable", "url": "NeonBoard-0.2.0-portable.zip",  "sha256": "…", "size": 67890 },
    { "kind": "delta",    "url": "delta-0.1.3-0.2.0.zip",         "sha256": "…", "size": 1234, "fromVersion": "0.1.3" }
  ]
}
```

- `url` 写文件名也可以：客户端会按清单所在地址补全（与插件市场同约定）。
- `size` 只用于界面显示，不参与校验。
- `kind` 有 `setup` / `portable` / `delta` 三种，前两种对应安装形态，`delta` 是增量包（见下节）。
- `delta` 条目必须带 `fromVersion`：增量包只从该版本升到 `latest`，客户端当前版本
  **恰好等于** `fromVersion` 才会走增量，否则自动回退全量。缺 `fromVersion` 的 delta
  条目视为不可用（宁走全量，不赌版本）。

### 兜底：GitHub Releases API

清单拉不到时回退 `https://api.github.com/repos/Reance195/NeonBoard/releases/latest`，
按 `tag_name` / `body` / `assets` 解析，资产按文件名推断形态（含 `Setup` → setup，`.zip` → portable）。

**该接口不提供资产的 SHA-256**，所以发布流水线会把这两行写进 **Release 正文**
（`release.yml` 生成 `dist/RELEASE-NOTES.md` 并以 `body_path` 提交），客户端从正文里抓回校验值：

```text
setup-sha256: <64 位十六进制>
portable-sha256: <64 位十六进制>
```

注意校验值的落点是**正文**，不是 `update.json` 的 `notes`：
回退路径只拿得到 `tag_name` / `body` / `assets`，拿不到 `update.json`，
所以写进清单的 `notes` 对这条路径没有任何用处 —— 那正是之前的一个设计错位，
`notes` 里塞 `sha256:` 两行只会让设置面板把它当更新日志显示。

也就是说：**两条路都做校验**。校验值缺失时客户端中止安装并明确报错，不会静默放行。

## 3. 安装形态与落地方式

形态由注册表里有没有 Inno Setup 的卸载项决定（AppId `{8E3B6A2C-9F4D-4C1A-9B7E-2D5F6A8C1E30}`）：

| 形态 | 判定 | 落地 |
| --- | --- | --- |
| `installed` | HKCU / HKLM 卸载项存在 | 下载 Setup → 校验 → **先 `taskkill` 托盘宿主** → `/VERYSILENT /NORESTART /SUPPRESSMSGBOXES` 静默升级，装完由安装包的 `[Run]` 段拉起 |
| `portable` | 卸载项不存在 | 下载 zip → 校验 → 解压到临时目录 → 生成自更新脚本 → **主程序退出后**覆盖文件 → 重新拉起 |

便携版的两条硬约束：

1. **正在运行的 exe 不能被替换**，所以覆盖动作必须发生在本进程退出之后。
   脚本先 `taskkill` 托盘宿主（常驻但无状态，强制结束安全），再等主进程自己退出。
2. **脚本内容恒为 ASCII**（注释也是英文）。cmd 按本地代码页解析 bat，
   中文注释或中文路径在 GBK 机器上会直接语法错误。

清单里没有对应形态的产物时，客户端只提示「暂未提供当前安装方式的更新包」，不会装错包。

### 全量更新前必须退宿主（R5-09）

`installed` 形态的静默升级曾经**整单悄悄失败**，2026-11-04 真机实测取证如下：

- 界面进程发起更新后自己退出，但 `NeonBoardHost.exe` 还活着 —— 它攥着安装目录里的
  **自身 exe 与全部 Qt DLL**，且 `checkIdle` 看门狗会把被关掉的界面进程复活
  （`src/host/HostMain.cpp:100-103`）。
- Inno 的 `CloseApplications`（Restart Manager）因此永远关不净：`/VERYSILENT` 下整单静默放弃，
  交互模式下逐文件弹 `DeleteFile failed; code 5`，「忽略」一次不够（每个被占文件都要点一遍）。
- 结果是**新旧混装树**：`NeonBoard.exe` 已是 0.1.4、`NeonBoardHost.exe` 仍是 0.1.3。
  按已发布 `delta-0.1.3-0.1.4` 清单逐文件复核，混装树的**二进制证据只有 `NeonBoardHost.exe`
  这一条** —— Qt DLL 在 0.1.3 与 0.1.4 之间字节相同（不在增量清单的 Modified 里），
  本就不该换，把它们「没换」当证据是错的（本文件早先这么写过，2026-11-05 订正）。
  增量路径没有这个毛病 ——
  它的自更新脚本本来就 `taskkill` 宿主（`UpdateManager.cpp` 的 `buildDeltaUpdateScript`）。

三处收口，缺一不可：

| 位置 | 动作 |
| --- | --- |
| `UpdateManager::installWithSetup` | 拉起安装器**之前**调 `terminateHostForUpdate()`（`taskkill /F /IM NeonBoardHost.exe` + `CREATE_NO_WINDOW`）；宿主缺席时静默返回，不阻断安装 |
| 安装器命令行 | **不再带 `/CLOSEAPPLICATIONS`** —— 命令行开关会覆盖 iss 里的设置，把已关掉的 Restart Manager 路径重新打开 |
| `installer/neonboard.iss` | `CloseApplications=no`；`[Code] PrepareToInstall` 再 `taskkill` 一次宿主与界面进程作兜底（用户手动双击安装包、或从别的进程触发时，客户端那一刀不会执行） |

### 安装回执：判出「上次没装完」

安装器在 `CurStepChanged(ssPostInstall)` 写 `{app}\install-receipt.json`：

```json
{"version": "0.1.5", "writtenAt": "2026-11-04 22:45:59", "commit": "unknown", "source": "inno"}
```

`commit` 试图取自同目录 `BUILD-INFO.txt`（发布闸门第 5 条产出），**但今天野外装出来的回执里
它恒为 `unknown`**：CI 的「写溯源信息」把 `BUILD-INFO.txt` 写在 `dist\`，而 iss 的 `[Files]`
只打包 `dist\NeonBoard\*`，且那一步排在「编译安装包」**之后** —— `{app}` 里根本没有这个文件。
判「上次没装完」只靠 `version`，不受影响；要让 `commit` 真有值，得把 BUILD-INFO.txt 纳入安装树
并前移到编译安装包之前（待立卡）。

回执代码只用 `LoadStringsFromFile` / `SaveStringsToFile`（`TArrayOfString`），不用
`LoadStringFromFile` / `SaveStringToFile`：Inno Setup 6 只有 Unicode 版，后两者的字符串参数是
**AnsiString**，拿 `String` 变量去传会在 ISCC **编译期**报 `Type mismatch`。而 iss 的 `[Code]`
段只有 ISCC 会编译 —— `verify.ps1` 不跑它、`tst_updatemanager` 只 grep 文本，所以这类错误
本地全绿、直到推 tag 才炸（v0.1.5 第一次发布就是这样废掉的，run 41870082136）。
**改 iss 的 `[Code]` 段必须真编译一次**，别把发布流水线当编译器。

`UpdateManager` 构造时读回执（`receiptVersionMismatch`）：

- 回执**缺失**（0.1.5 之前的安装目录）或**版本相符** → 放行，不打扰用户；
- 回执版本与自身编译版本**不符** → `qWarning`「上次更新未完成…」，`lastInstallIncomplete()` 为真。

这正是混装树的机器判据：文件替换被打断时，回执要么没写、要么停在目标版本，
而运行中的 exe 还是旧版 —— 两者一比就露出来，不用等用户发现界面行为怪异。

**告警不止写日志（R5-12）**：`lastInstallIncomplete` 以 `Q_PROPERTY` 暴露给 QML，
设置面板「软件更新」区在它为真时显示一条可点的 `Theme.warning` 警示条（文案含「上次更新未完成」，
点击即重新检查更新），为假时 `visible=false` 不占位。日志文件没人翻 —— 判据再准，
用户看不到就等于没有；界面这一层才是「上次没装完」真正被消费的入口。

### 覆盖安装不清理旧文件，得靠 `[InstallDelete]`（R5-11）

Inno 的 `[Files]` 是「新包里有什么就写什么」，**不会删掉新包里没有的旧文件**。
0.1.4 起（`8e52812`：windeployqt 只留 Basic 样式）不再打包 Qt 自带的其它控件样式，
于是每个从 0.1.3 覆盖升上来的用户都会永久背着 1136 个死文件 —— 2026-11-04 真机取证：
装完后按已发布 `delta-0.1.3-0.1.4` 清单逐文件复核，Modified 2/2、Added 15/15 全对，
Deleted 的 1136 条一个都没少（`qml/QtQuick/Controls/` 下 FluentWinUI3 / Material / Fusion /
Universal / Imagine / Windows 六套共 1126 个 qml 与 style plugin dll，加 root 级 10 个
`Qt6QuickControls2*Style*.dll`）。

功能上无害（应用只用 Basic 样式，这些插件不会被加载），但白占空间、也让「安装树等于发布物」
这个前提不成立。`installer/neonboard.iss` 的 `[InstallDelete]` 因此逐条列出这 6 个目录与
10 个 dll（清单以那份增量清单的 Deleted 条目为事实源，脚本核对过 1136/1136 覆盖、
对真实 0.1.4 树 0 误伤）。

> **不要用 `Qt6QuickControls2*Style*.dll` 这类通配**：0.1.4 仍在用的
> `Qt6QuickControls2BasicStyleImpl.dll` 会被一并删掉，样式直接加载失败。
> 以后再瘦身 windeployqt 产物，同样要在 `[InstallDelete]` 里补显式条目。

### 增量更新（R5-06）

便携全量包 69–71 MB（v0.1.3 实测 70.6 MB），增量包通常只有几 MB。清单里给了 `delta` 条目且当前版本恰好等于
`fromVersion` 时，「立即更新」优先走增量；**任一步失败自动回退全量**，用户无感。

增量包（`delta.zip`）布局：

```text
delta.json              变更清单（fromVersion / toVersion / changes[]）
files/<相对路径>         新增 / 修改的资源文件（QML、图片、配置等，全量内容）
patches/<相对路径>.patch 修改的二进制文件（exe/dll）的 bsdiff patch
```

客户端流程（`UpdateManager::downloadAndApplyDelta`）：

1. 下载 `delta.zip`，校验 SHA-256（校验值来自清单）；
2. 解压后**预校验**：安装目录里 Modified/Deleted 的旧文件 SHA-256 必须与清单一致
   （不一致 = 用户实际版本与 `fromVersion` 不符，直接放弃增量）；
3. 备份将被修改/删除的文件 → 应用变更：资源直接覆盖安装目录，二进制用 bsdiff
   合并到**暂存目录**（正在运行的 exe/dll 不能被替换）→ 逐文件校验；
4. 任一步失败：恢复备份、删除新增文件，回退全量；
5. 成功：生成自更新脚本（与便携版同构：杀宿主 → 等主进程退出 → robocopy 暂存
   二进制进安装目录 → 重新拉起），应用退出后完成替换。

发布侧：`scripts/build-delta.ps1` 调 `neonboard-delta-tool`（与主程序共用
`DeltaUpdate` / `BsdiffWrapper` 源码）对比新旧版本目录生成上述布局，再打 zip、
算 SHA-256，产出 `delta-manifest-entry.json`（可直接并入 `update.json` 的
`artifacts`）。`release.ps1` 支持 `-DeltaFromDir <旧版目录> -DeltaFromVersion <旧版本>`
在出包时顺带生成。

## 4. 发布

**版本号只有一个来源：`CMakeLists.txt` 的 `project(NeonBoard VERSION x.y.z)`。**
发版就改这一处，然后打同名标签：

- 程序自报版本（`QCoreApplication::applicationVersion`，更新检查的比较基准）→ 编译期常量
  `NEONBOARD_VERSION_STRING`，由 CMake 注入
- 安装包版本（`installer/neonboard.iss` 的 `MyAppVersion`）→ `scripts/release.ps1` 与
  `release.yml` 解析 CMakeLists 后以 `/DMyAppVersion=<版本>` 传入
- `release.yml` 第一步就校验「标签版本 == 源码版本」，不一致直接失败

历史上这五处（CMake / main.cpp / HostMain.cpp / SplashWindow.cpp / .iss）各自手写，
漏改 `main.cpp` 的后果是程序永远自报旧版本 → 更新检查判定「已是最新」→ 用户再也收不到更新，
而且现象极难复现，因此收敛到单点 + 构建期闸门。

打 `v*` 标签触发 `.github/workflows/release.yml`，一条流水线依次产出：

1. 便携目录打 zip：`NeonBoard-<版本>-portable.zip`
2. 增量包（自动，首次发布没有上一版时整步跳过）：流水线查 GitHub Releases API 取
   **上一个非 draft / 非 prerelease** 的 release，下载它自带的
   `NeonBoard-<prev>-portable.zip` 解压成 `dist/NeonBoard-prev`，调
   `scripts/build-delta.ps1` 与本次便携目录做 diff，产出 `dist/delta.zip`。
   上一版包只存在于 Release 资产上 —— fresh runner 的 `dist/` 里从来没有它，
   必须回下载，不能在工作区里找
3. 生成清单：`scripts/build-update-manifest.ps1`（算 SHA-256 与体积；有 `dist/delta.zip`
   时带 `-DeltaZip` / `-DeltaFromVersion`，把 `kind=delta` 条目写进 `artifacts`）。
   清单 schema 只有这一个实现，workflow 里不再手写 JSON
4. 把每个产物一行的 `*-sha256:` 写进 Release 正文（`dist/RELEASE-NOTES.md` → `body_path`）——
   GitHub API 回退路径唯一的校验值来源
5. 上传：`delta.zip` 必须与 setup / portable 一起挂到 Release 上，否则清单里的 `url`
   指过去就是 404。首次发布没有 `delta.zip`，该步单独用 `if` 跳过，
   不让 `if-no-files-found: error` 把第一次出包红掉
6. **把 `update.json` 提交回仓库** —— 客户端默认读 raw，只挂在 Release 资产上客户端找不到

本机手工出清单：

```powershell
pwsh -File scripts/build-update-manifest.ps1 -Version 0.2.0 `
    -Setup dist\NeonBoard-Setup-0.2.0.exe `
    -Portable dist\NeonBoard-0.2.0-portable.zip `
    -DownloadBase https://github.com/Reance195/NeonBoard/releases/download/v0.2.0
```

`-DownloadBase` 留空则 `url` 只写文件名，适合自建源或本地测试。

发布前想把整条增量链路先串一遍（不落 `dist/`，产物全在 `.sandbox/delta-test/`）：

```powershell
pwsh -File scripts/test-delta.ps1
```

它用当前构建目录当「新版」，合成一个少文件 + 改文件的「旧版」目录，依次跑
`build-delta.ps1` 与 `build-update-manifest.ps1`，断言清单里确有 `kind=delta`、
`fromVersion` 与 zip 的 SHA-256 对得上。

## 5. 配置

| 项 | 位置 | 默认 |
| --- | --- | --- |
| 自动检查 | 设置面板开关 / QSettings `updates/autoCheck` | 开 |
| 跳过版本 | 面板「跳过此版本」/ `updates/skipVersion` | 无 |
| 上次检查时间 | `updates/lastCheckAt` | 0 |
| 清单源 | QSettings `updates/sources` | 仓库简写（Gitee → GitHub） |

命令行：

```text
--update-source <url|owner/repo|本地路径>   设置清单源
--update-sources                           列出当前源
--check-update                             检查一次并打印结果
--update-apply                             检查→下载→校验→静默安装，跑完即退出
```

四者都属于 `utilityMode`：应用正在运行时也能执行，不会撞单实例锁。
本地路径（存在的 `update.json`）可直接作为源，用于离线验证。

网络代理：`UpdateManager` 构造时开 `QNetworkProxyFactory::setUseSystemConfiguration(true)`，
即**跟随系统代理设置**（此前整个 `src/` 里没有任何代理支持，系统代理开着的机器上
直连 GitHub 会被 RST，日志只留一条 `QIODevice::read (QNetworkReplyHttpImpl): device not open`，
看着像下载器坏了）。没有单独的代理配置项，也不读环境变量代理。

`--update-apply` 的退出码：`0` 已把安装器拉起来（安装本身由 Inno Setup 的
`/VERYSILENT /NORESTART /SUPPRESSMSGBOXES` 完成，进程随即退出），`0` + `OK uptodate` 表示本来就最新，
`2` 表示检查失败 / 下载校验失败 / 10 分钟超时。它只负责「调用同一条安装路径」，不改任何安装语义。

## 6. 排障

- 清单缓存：`%APPDATA%\com.lin\NeonBoard\update-cache.json`（沙箱下落在数据目录内），TTL 6 小时；
  全部候选地址都失败时用它兜底，界面会标注「离线：使用 … 缓存的更新信息」。
- 日志关键词：`Update:`（版本与形态）、`CHECK-UPDATE`、`UPDATE-APPLY`、`UPDATE-SOURCE`。
- 便携版更新的执行日志：`%TEMP%\neonboard-update.log`（脚本自己写的）。
- 增量更新的执行日志：`%TEMP%\neonboard-delta-update.log`（自更新脚本自己写的）；
  增量失败回退全量时应用日志里会有一条 `增量更新失败，回退全量下载：…` 的警告。
- 想完整走一遍流程又不想真装：把源指到本地 `update.json`，检查与下载校验都能验证；
  **别在常用目录里触发「立即更新」**，便携版会真的覆盖 exe。
- 下载卡在 0% 不动：产物下载有「60 秒没有任何字节就中止」的判定（`kDownloadStallMs`），
  超时会报「下载超时（60 秒没有收到数据）」。如果检查能通过、下载必挂，先用
  `--update-source <本地路径>` 换传输路径验证一遍，多半是本机代理/证书链的问题
  （实测 GitHub 资产地址在开启代理时可能连上即静默，而 API 与清单地址正常）。
- 静默安装「跑完没效果」：安装器是 detached 启动的，它结束后才替换文件。若此时仍有
  残留的 `NeonBoardHost.exe` 占着安装目录，Inno 关不掉它，`/SUPPRESSMSGBOXES` 又会把
  「文件被占用」的询问压掉，表现为安装静默放弃。R5-09 起客户端会先杀宿主、iss 里也有
  `PrepareToInstall` 兜底；排障时先看应用日志有没有 `UPDATE host-kill rc=`，
  再看注册表 `DisplayVersion` 与 `{app}\install-receipt.json` 是否都变成了目标版本。
- **2026-11-04 真机实测记录（R5-08 ⑤ / R5-09 的证据来源）**：维护者机器上实装已发布的
  `Setup-0.1.3.exe`（SHA-256 `45a5cfa6…821cb`，55,807,822 B），再从 0.1.3 界面触发
  「立即更新」升到 0.1.4：
  1. 下载到 100% 后界面自动关闭，重新打开**版本仍是 0.1.3** —— 静默安装整单失败，
     应用日志里只有 `Update: version 0.1.4 kind installed`，没有任何失败记录；
  2. 直接双击 `Setup-0.1.4.exe`（SHA-256 `4c8d5b39…33ef`，53,788,233 B）→ 逐文件
     `DeleteFile failed; code 5`，「忽略」要点很多次（每个被占文件一次）；
  3. 事后按发布增量包的 `old` 哈希逐文件校验安装树：1137 个未改动文件**全部匹配**、
     0 个哈希不符，只有 `NeonBoard.exe` 尺寸对不上（已是 0.1.4 的 4,993,536 B），
     而 `NeonBoardHost.exe` 仍是 0.1.3 的原字节 —— 混装树坐实，根因是宿主占用。
     **混装树的二进制证据只有宿主这一条**：Qt DLL 在 0.1.3 与 0.1.4 之间字节相同
     （不在增量清单的 Modified 里），「DLL 没换」不构成证据，别拿它归因；
  4. 同期发现客户端**完全不支持代理**：本机系统代理为 `127.0.0.1:7078` 时直连 GitHub
     被 RST，日志 `QIODevice::read (QNetworkReplyHttpImpl): device not open`。
  另：已发布的 0.1.3 **不含增量客户端**（首个具备增量能力的构建是 0.1.4），
  所以 0.1.3 → 0.1.4 只能走全量；增量的受益区间从 0.1.4 → 0.1.5 起算。
