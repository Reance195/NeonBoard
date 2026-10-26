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

检查到新版本后，界面显示版本号、发布日期、包体积与更新日志（日志超 6 行截断）。

## 2. 清单：update.json

默认从仓库 raw 读取，**Gitee 优先、GitHub 兜底**（与插件市场同一套取舍，国内可达性优先）：

```text
https://gitee.com/Reance195/NeonBoard/raw/main/update.json
https://raw.githubusercontent.com/Reance195/NeonBoard/main/update.json
```

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
    { "kind": "portable", "url": "NeonBoard-0.2.0-portable.zip",  "sha256": "…", "size": 67890 }
  ]
}
```

- `url` 写文件名也可以：客户端会按清单所在地址补全（与插件市场同约定）。
- `size` 只用于界面显示，不参与校验。
- `kind` 只有 `setup` / `portable` 两种，对应下面的安装形态。

### 兜底：GitHub Releases API

清单拉不到时回退 `https://api.github.com/repos/Reance195/NeonBoard/releases/latest`，
按 `tag_name` / `body` / `assets` 解析，资产按文件名推断形态（含 `Setup` → setup，`.zip` → portable）。

**该接口不提供资产的 SHA-256**，所以发布脚本会把这两行写进更新说明，客户端从说明里抓回校验值：

```text
setup-sha256: <64 位十六进制>
portable-sha256: <64 位十六进制>
```

也就是说：**两条路都做校验**。校验值缺失时客户端中止安装并明确报错，不会静默放行。

## 3. 安装形态与落地方式

形态由注册表里有没有 Inno Setup 的卸载项决定（AppId `{8E3B6A2C-9F4D-4C1A-9B7E-2D5F6A8C1E30}`）：

| 形态 | 判定 | 落地 |
| --- | --- | --- |
| `installed` | HKCU / HKLM 卸载项存在 | 下载 Setup → 校验 → `/VERYSILENT /NORESTART /SUPPRESSMSGBOXES /CLOSEAPPLICATIONS` 静默升级，装完由安装包的 `[Run]` 段拉起 |
| `portable` | 卸载项不存在 | 下载 zip → 校验 → 解压到临时目录 → 生成自更新脚本 → **主程序退出后**覆盖文件 → 重新拉起 |

便携版的两条硬约束：

1. **正在运行的 exe 不能被替换**，所以覆盖动作必须发生在本进程退出之后。
   脚本先 `taskkill` 托盘宿主（常驻但无状态，强制结束安全），再等主进程自己退出。
2. **脚本内容恒为 ASCII**（注释也是英文）。cmd 按本地代码页解析 bat，
   中文注释或中文路径在 GBK 机器上会直接语法错误。

清单里没有对应形态的产物时，客户端只提示「暂未提供当前安装方式的更新包」，不会装错包。

## 4. 发布

打 `v*` 标签触发 `.github/workflows/release.yml`，在原有安装包之外新增：

1. 便携目录打 zip：`NeonBoard-<版本>-portable.zip`
2. 生成清单：`scripts/build-update-manifest.ps1`（算 SHA-256 与体积）
3. 随 Release 上传 zip 与 `update.json`
4. **把 `update.json` 提交回仓库** —— 客户端默认读 raw，只挂在 Release 资产上客户端找不到

本机手工出清单：

```powershell
pwsh -File scripts/build-update-manifest.ps1 -Version 0.2.0 `
    -Setup dist\NeonBoard-Setup-0.2.0.exe `
    -Portable dist\NeonBoard-0.2.0-portable.zip `
    -DownloadBase https://github.com/Reance195/NeonBoard/releases/download/v0.2.0
```

`-DownloadBase` 留空则 `url` 只写文件名，适合自建源或本地测试。

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
```

三者都属于 `utilityMode`：应用正在运行时也能执行，不会撞单实例锁。
本地路径（存在的 `update.json`）可直接作为源，用于离线验证。

## 6. 排障

- 清单缓存：`%APPDATA%\com.lin\NeonBoard\update-cache.json`（沙箱下落在数据目录内），TTL 6 小时；
  全部候选地址都失败时用它兜底，界面会标注「离线：使用 … 缓存的更新信息」。
- 日志关键词：`Update:`（版本与形态）、`CHECK-UPDATE`、`UPDATE-SOURCE`。
- 便携版更新的执行日志：`%TEMP%\neonboard-update.log`（脚本自己写的）。
- 想完整走一遍流程又不想真装：把源指到本地 `update.json`，检查与下载校验都能验证；
  **别在常用目录里触发「立即更新」**，便携版会真的覆盖 exe。
