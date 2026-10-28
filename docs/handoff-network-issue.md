# 提示词：排查 git 推送失败与 GitHub 连接问题

> 用途：新会话排查「git fetch/push 失败、github.com 连不上」。
> 本文 §一 可直接粘贴给新会话；§二 是已收集的证据（别重复测）；§三 是根因假设与修复方案。

---

## 一、提示词正文（复制这段）

```
帮我排查这台 Windows 机器上 git 连 GitHub 失败的问题。

【环境】
- 机器：Windows 11，用户目录 C:\Users\13706
- git 2.48.1.windows.1，gh CLI 2.102.0
- 主仓库：E:\projects\NeonBoard（remote = https://github.com/Reance195/NeonBoard.git）

【症状】
git fetch / git push 间歇性失败，典型报错：
  fatal: unable to access 'https://github.com/Reance195/NeonBoard.git/':
  Failed to connect to github.com port 443 after 21xxx ms: Could not connect to server
偶尔是：
  Recv failure: Connection was reset
特征：同一个命令重试几次有时能成，但成功率很低；而 gh CLI、浏览器访问 GitHub 正常。

【已知线索（勿重复验证，直接在此基础上推进）】
1. 系统级代理是开着的：HKCU Internet Settings 里
   ProxyEnable=1，ProxyServer=127.0.0.1:7078，
   对应进程 MonoCloud_Mix.bin（E:\Program Files\MonoCloud_V1.3.8\）
2. 但 git 完全没配代理：
   http.proxy / https.proxy / 全局 http.proxy 全为空，
   环境变量 HTTP_PROXY / HTTPS_PROXY / NO_PROXY 也全空
3. 实测对比（curl）：
   - 直连 github.com：10 次全失败（http_code=000）
   - 走代理 127.0.0.1:7078：5 次全成功（http_code=200）
   - git 加 -c http.proxy=http://127.0.0.1:7078 后，ls-remote 与 fetch 连续多次成功
4. 域名差异：
   - github.com 解析到 20.205.243.166，该 IP 直连不通
   - api.github.com / codeload.github.com 直连可达
   - 换 github.com 的其他 GitHub IP（140.82.112.3 / 140.82.113.3 / 4.237.22.38 /
     20.200.245.247）直连多数可达，但不稳定
5. 曾经用 git config http.curloptResolve 把 github.com 钉到备用 IP，能临时绕过，但
   仍会间歇 reset —— 说明问题不是单纯 DNS 解析错误，而是直连链路本身不稳。

【请做】
1. 验证「git 没走系统代理」是不是根因：
   - 解释为什么 gh CLI 能用而 git 不能用（gh 读系统代理？git 只认 http.proxy？）
   - 给出证据（抓包 / curl 对比 / 进程网络连接皆可）
2. 给出修复方案，并说明取舍：
   - 方案 A：给 git 配代理（http.proxy / https.proxy，全局 or 仓库级）
   - 方案 B：配置 proxy 例外，让内网/本地地址绕过代理
   - 方案 C：修 DNS（换 DoH / 指定 hosts），仅解决解析问题
   - 判断哪个能根治，为什么
3. 配之前先确认：
   - 代理是否稳定常驻（MonoCloud 关闭后 git 是否会全部失败）
   - 代理是否支持 git 所需的 HTTP/1.1 长连接与大 POST
     （全局配置里有 http.postbuffer=524288000 / http.version=HTTP/1.1）
4. 修完做验收：
   - git ls-remote origin 连续 5 次全成功
   - git fetch origin 连续 3 次全成功
   - 在 E:\projects\NeonBoard 跑一次真实推送（可推一个空提交到测试分支再删）
   - 记录修复前后成功率对比

【约束】
- 改全局 git 配置前先备份现有配置（git config --global --list）
- 不要用 http.sslVerify=false 这类降低安全性的做法
- 不要留下临时 curloptResolve 残留（用完必须 unset）
- 每一步给命令 + 实际输出，不要只给结论
```

---

## 二、已收集的证据（2026-10-28 03:09 实测）

### 连通性对比

| 目标 | 直连 | 走代理 127.0.0.1:7078 |
|---|---|---|
| `github.com` | **0/10 成功**（全部 `http_code=000`，单次约 20 s 超时） | **5/5 成功**（HTTP 200，约 3.8 s） |
| `api.github.com` | 200 | 403（代理侧拒绝，不影响 git） |
| `codeload.github.com` | 301 | 301 |
| `objects.githubusercontent.com` | 404 | 404 |

### git 实测

| 操作 | 结果 |
|---|---|
| `git ls-remote origin`（直连） | ❌ `Failed to connect to github.com port 443` |
| `git ls-remote origin`（`-c http.proxy=…7078`） | ✅ 退出码 0，正常返回 `refs/heads/main` |
| `git fetch origin`（走代理，连试 3 次） | ✅ 3/3 成功 |
| `gh api user` | ✅ 正常返回 `Reance195` |

### 配置现状

```
系统代理（HKCU Internet Settings）
  ProxyEnable   = 1
  ProxyServer   = 127.0.0.1:7078
  AutoConfigURL = (空)
  ProxyOverride = localhost;127.*;10.*;172.16-32.*;192.168.*;<local>
  → 绕过列表里没有 github.com，所以浏览器/gh 走代理

WinHTTP 代理
  Direct access (no proxy server)      ← 注意：与 WinINET 不一致

git 配置（全局）
  http.version        = HTTP/1.1
  http.postbuffer     = 524288000
  http.lowspeedlimit  = 0
  http.lowspeedtime   = 999999
  http.schannelcheckrevoke = false
  http.sslbackend     = schannel
  http.proxy / https.proxy = (空)        ← 关键：没有代理

环境变量
  HTTP_PROXY / HTTPS_PROXY / NO_PROXY = 全空

DNS
  github.com → 20.205.243.166（该 IP 直连不通）
  WLAN DNS = 223.5.5.5, 119.29.29.29（阿里/腾讯公共 DNS）
  hosts 文件无 github 相关自定义条目
```

### 代理软件

```
进程：MonoCloud_Mix.bin  PID 14260
路径：E:\Program Files\MonoCloud_V1.3.8\MonoCloud_Mix.bin
监听：127.0.0.1:7078
```

---

## 三、根因分析（我的判断，待新会话复核）

**根因不是「网络断了」，而是「git 没有走那台已经能用通的代理」。**

证据链：

1. 直连 `github.com` 10 次全败，走代理 5 次全成 —— 链路本身没问题，是**直连路径**有问题
2. `github.com` 解析到 `20.205.243.166`，该 IP 直连不通；而 `api.github.com`
   等解析到别的 IP 就通 —— 是**特定 IP 可达性**问题，不是普遍断网
3. `gh` CLI 能用、浏览器能用、`git` 不能 —— 因为 `gh` 和浏览器读 WinINET 系统代理，
   而 **git（libcurl）不读 Windows 系统代理**，只认 `http.proxy` 配置或 `HTTP_PROXY` 环境变量
4. git 的 `http.proxy` 是空的，`HTTP_PROXY` 也是空的 → git 只能直连 → 撞上不通的 IP

**之前用的 `http.curloptResolve` 属于对症不对因**：它把域名钉到某个备用 IP，
能绕开那个不通的 IP，但备用 IP 的可达性本身也在波动，所以仍会间歇 reset。

### 推荐修复（优先级从高到低）

**方案 A：给 git 配代理（推荐，直接消除症状）**
```powershell
# 仓库级（只影响 NeonBoard，最保守）
git -C E:\projects\NeonBoard config http.proxy http://127.0.0.1:7078
git -C E:\projects\NeonBoard config https.proxy http://127.0.0.1:7078

# 或全局（影响所有仓库，需确认代理常驻）
git config --global http.proxy http://127.0.0.1:7078
git config --global https.proxy http://127.0.0.1:7078
```
需要配套确认：MonoCloud 若退出（开机未自启 / 崩溃），git 会**全部**失败。
可加一条例外让本地地址直连，避免连内网被代理拖慢：
```powershell
git config --global http.http://localhost.proxy ""
```

**方案 B：修 DNS（只解决解析，不解决该 IP 不通）**
换 DoH 或往 hosts 里写 GitHub 的可用 IP。但如果目标是「特定 IP 不可达」而非「解析错」，
换 DNS 拿到的还是同一个不可达 IP，收益有限。

**方案 C：继续用 curloptResolve 逐 IP 试探（不推荐，属于临时手段）**
优点是不依赖代理软件；缺点是要手工维护可用 IP 列表，且 IP 会变、可达性会波动。
若采用，必须用完 unset，别留仓库配置残留。

### 待新会话确认的关键问题

1. **为什么 WinHTTP 是 Direct 而 WinINET 有代理？** 这会影响 `git`（用 WinHTTP 还是 libcurl 自己的栈）
   以及后续用 `netsh winhttp` 配代理的做法是否有效。
2. **代理是游戏加速器还是全局代理？** MonoCloud 从名字看像加速器，可能只对特定域名生效，
   需确认它对 `github.com` 的 git 流量（大 POST、git-receive-pack）是否完整支持。
3. **`http.postbuffer=524288000` 是否是为大推送调过的？** 若是历史遗留为了绕开推送中断，
   说明推送问题早于本次故障就已存在，需要区分「连接失败」和「推送体积导致中断」两类症状。


---

## 四、排查结论与修复记录（2026-10-28 03:15–03:45 实测）

> 本节由新会话实测补充，含**两条更正**：
> ① 「`gh` 能用是因为它读了系统代理」不成立；
> ② 这台机器上装了**两个**加速器，它们接管 GitHub 流量的方式完全不同（之前把它们当成同一个 Watt 内核是错的）。

### 0. 关键前提：两个加速器，分工完全不同

| 对比项 | **Watt Toolkit 3.1**（进程 `Steam++.exe`） | **MonoCloud 1.3.8**（`E:\Program Files\MonoCloud_V1.3.8`） |
|---|---|---|
| 接管方式 | 改 **hosts**：31 条 GitHub 域名 → `127.0.0.1`，并在本机 `0.0.0.0:443` 自建 MITM（证书 `SteamTools Certificate`，已装进 `CurrentUser\Root` + `LocalMachine\Root`） | 设 **WinINET 系统代理** `127.0.0.1:7078`（Clash 内核 + PAC），**不改 hosts** |
| 覆盖浏览器 | ✅ | ✅ |
| 覆盖 `gh` | ✅ | ✅（但 gh 实际走 `api.github.com` 直连） |
| **覆盖 `git`** | ✅ **是**：git 读 hosts → 连 `127.0.0.1:443`，schannel 又读 Windows 证书库 → 信任 MITM 证书 | ❌ **否**：git/libcurl 不读 WinINET，只能直连 |
| 当前状态 | 运行中（`Steam++.exe` 03:18:25 启动，hosts 03:18:56 写入） | 未运行（`ProxyEnable=0`，7078 无监听） |
| 开机自启 | ❌ 无 | ❌ 无 |

> 结论：**能覆盖 git 的只有 Watt Toolkit 的 hosts 模式**。MonoCloud 怎么配都不会自动管到 git。

### 1. 故障链条（03:09 那次直连 0/10 成功率）

当时 MonoCloud 在跑（`ProxyEnable=1`、7078 监听中），Watt 没跑、hosts 里没有 github 段：

| 客户端 | 路由 | 结果 |
|---|---|---|
| 浏览器 | WinINET → `127.0.0.1:7078` | ✅ |
| `gh` | 不读 WinINET → 直连 `api.github.com` | ✅ 恰好可达 |
| `git` | 不读 WinINET、也没 `http.proxy` → 直连 `20.205.243.166:443` | ❌ TCP 被黑洞，21 s 超时 |

**更正 §三**：`gh`（Go `net/http` + `ProxyFromEnvironment`）和 `git`（libcurl）**都不读 WinINET**；
唯一差别是 `gh` 访问的 `api.github.com` 恰好可达，而 `github.com` 那个 IP 不可达。

### 2. 直连这条路本身不可依赖（本次实测）

- `20.205.243.166:443`：一次连测 3 次全部 21 s 超时；另一次裸 TCP 5 次全在 0.07 s 内连上，但 TLS 拿不到 GitHub 真证书
- 03:18:25 启动 Watt 之后，用 Node 直连 `github.com:443` 抓到的对端证书是
  `subject=github.com / issuer=SteamTools Certificate / remoteAddress=127.0.0.1`
  → 证明流量已被本机 443 的加速器接走，而不是连去 `20.205.243.166`

### 3. git 侧的正确做法（已验证：零配置）

**不要给 git 配 `http.proxy`** —— 那只在 MonoCloud 模式下才有意义，且 7078 一关 git 就从"超时"变成"硬失败"。
只要 Watt Toolkit 的 hosts 模式在跑，git 什么都不用配。

本次实际动作：
1. 备份全局配置 → `~/.gitconfig.bak-20261028-031657`（同内容另存于会话工作区 `backup/`）
2. 中途按方案 A 写入 `http.https://github.com/.proxy=http://127.0.0.1:7078`，随即实测到该端口已关、
   git 直接 `Failed to connect to 127.0.0.1 port 7078` → **已撤销**
3. `~/.gitconfig` 与备份逐行 diff = **完全一致（无残留）**；仓库配置无 `curloptResolve` / `proxy` 残留
4. 全程未使用 `http.sslVerify=false`

### 4. 两个加速器会互相踩（间歇故障的温床）

Watt 走 hosts + 本机 443，MonoCloud 走 WinINET + 7078，两者都想"接管 github.com"：

- **同时开**：浏览器请求先给 MonoCloud 代理 → 它解析 github.com 时又命中 hosts → `127.0.0.1` → 撞进 Watt 的 MITM（双跳，任何一方抖动就断）
- **Watt 退出但 hosts 残留**：`127.0.0.1:443` 无人监听 → 所有 GitHub 域名立刻 `connection refused`
- **MonoCloud 退出但 `ProxyEnable` 残留 1**：浏览器全断，git 照旧连不上

建议：**GitHub 只交给一个加速器**（推荐 Watt Toolkit 的「本地加速」），另一个要用就先关掉它的系统代理。

### 5. 验收（Watt hosts 模式，本次实测全过）

| 验收项 | 结果 |
|---|---|
| `git ls-remote origin` ×5 | **5/5 成功**，0.90 – 1.29 s |
| `git fetch origin` ×3 | **3/3 成功**，≈1.0 s |
| 真实 `git push`（新分支 `net-probe`） | ✅ 1.7 s，GitHub 返回 `* [new branch] HEAD -> net-probe` |
| 删除远端测试分支 | ✅ 1.55 s，`- [deleted] net-probe` |
| 事后远端 refs 复核 | 只剩 `refs/heads/main`，无残留 ✅ |
| 真实 `git clone` | ✅ 4.0 s / 225 文件 |

> 验证命令额外加了 `-c http.sslBackend=openssl -c http.sslCAInfo=<SteamTools CA>`，
> 仅仅因为排查所用 agent 进程是低完整性受限令牌、schannel 会报 `SEC_E_NO_CREDENTIALS`；
> 你本机 git 用 schannel 直接读 Windows 证书库，走的是等价且更顺的路径。

### 6. 一眼看出"现在谁在管"

```powershell
$h=Get-Content C:\Windows\System32\drivers\etc\hosts
$w=[bool]($h -match 'Steam\+\+ Start')
$p=(Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings').ProxyEnable
$l443=[bool](netstat -ano | Select-String ':443\s.*LISTENING')
$l7078=[bool](netstat -ano | Select-String ':7078\s.*LISTENING')
"hosts段=$w 443监听=$l443 | ProxyEnable=$p 7078监听=$l7078"
```

| 状态 | 谁在管 | git 需要什么 |
|---|---|---|
| hosts段=True、443=True、ProxyEnable=0 | Watt 本地加速 | 什么都不用配 ✅ |
| ProxyEnable=1、7078=True、hosts段=False | MonoCloud 系统代理 | `git config --global http.https://github.com/.proxy http://127.0.0.1:7078`（用完 unset） |
| 两边都没有 | 无加速 | git 直连，大概率 21 s 超时 ❌ |
| hosts段=True 但 443=False | Watt 残留 | 启动 Watt，或删掉 hosts 段 |

### 7. 建议

1. GitHub 只留 Watt Toolkit 负责（它能覆盖 git）；MonoCloud 要用时关掉它的系统代理，别两边同时接管。
2. 给常用的那个加速器开**开机自启**（现在两个都没有，重启后必然全断）。
   > 2026-10-28 09:45 复核确认：`Steam++.exe` 正在运行（PID 6456/15200，exe 在
   > `C:\Program Files\Steam++\`），hosts 里 27 行 github 映射在位 —— 但
   > HKCU\Run、HKLM\Run、两处启动菜单（用户级与公共级 Startup）、计划任务
   > **四处全无 Watt/Steam++ 条目**。结论：重启后 hosts 残留 + 443 无监听 =
   > 所有 GitHub 域名 connection refused（§4 表末行那种状态）。
   > 处置：在 Watt 设置里勾选开机自启（它自己的开关，不代改注册表/快捷方式），
   > 或手动往 Startup 放快捷方式；做完重启一次，用 §6 的一眼命令复核
   > `hosts段=True 443监听=True`。
3. `http.postbuffer=524288000`（500 MB）不是本次故障原因，但会让推送在内存里攒大包；
   若无「大文件推送被截断」的历史需求，可 `git config --global --unset http.postbuffer`。
4. 永远不要用 `http.sslVerify=false`：本机 MITM 证书已在系统信任库，关校验只会放大中间人风险。
