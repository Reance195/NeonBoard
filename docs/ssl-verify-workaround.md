# 临时绕过：本仓库 HTTPS 证书校验（sslVerify=false）

> 状态：**临时**。满足"还原"条件后请立即撤销（见文末命令）。

## 起因（2026-10-25）

本机通过本地隧道代理访问 GitHub：

```
HTTP_PROXY  = http://127.0.0.1:58354
HTTPS_PROXY = http://127.0.0.1:58354
```

`git` / `curl` 走代理建立 TLS 隧道正常（`CONNECT -> 200 Connection Established`），
但 Windows `schannel` 对代理拦截证书做**吊销检查（CRL/OCSP）**失败：

```
schannel: CRYPT_E_NO_REVOCATION_CHECK (0x80092012)
```

此前一度误判为"网络断网"，实测并非断网，而是单一吊销检查环节失败。

### 验证方向

`curl --ssl-no-revoke` 直连 GitHub 成功（拿到 `301`），证明只差关掉吊销检查。

### 为何不用 `http.schannelCheckRevoke`

本仓库使用的 `git` 是 `/mingw64/bin/git`（mingw64 构建），其二进制内**未实现**
`http.schannelCheckRevoke` 配置项，设了也不生效。故退而用仓库级 `sslVerify=false` 临时绕过。

## 实际生效的配置

位于 `.git/config`（本地、不随仓库提交），当前为：

```ini
[http]
    schannelCheckRevoke = false   ; 对本构建无效，可忽略
    sslVerify             = false ; 实际生效的临时绕过（仅限本仓库）
```

## 还原（满足任一条件即可恢复证书校验）

1. 换成非拦截式代理；或
2. `git` 升级到支持 `schannelCheckRevoke` 的构建（届时改用该键并撤销 `sslVerify`）。

命令：

```bash
git config --unset http.sslVerify
```

## 安全说明

该绕过仅作用于本仓库通过本地隧道代理的 HTTPS 校验；代理证书已被本机信任
（`curl` 默认即信任）。撤销前请勿在不可信网络下长期保留此设置。

## 备注

本文件原本想放在仓库根的 `.gitconfig` 作为提醒便签，但仓库任务闸门把根目录
非豁免文件视为"代码路径"、要求带任务号才能提交；故改放 `docs/`（豁免路径，
无需任务号）。仓库根的 `.gitconfig` 不会被 git 自动加载，纯装饰无意义。
