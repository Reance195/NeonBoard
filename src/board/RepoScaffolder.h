#pragma once

#include <QString>

// 无台账仓库脚手架（R18-03）：
// 把一个全新 git 仓库绑到项目时，程序替它生成最小 v3 快照
// （progress.json，items=[]）与空台账（docs/board-ledger.json），
// 面板随即可监听该仓库 —— 用户不需要手工搭任何脚手架。
//
// fail-closed 纪律（Z4）：
//   1. 路径校验不过（相对 / 不存在 / 非目录 / 非 git 仓库）→ 拒绝写入；
//   2. progress.json 或 docs/board-ledger.json 已存在 → 拒绝覆盖，返回原因；
//   3. 绝不碰目标仓库的 .gitignore / git 配置 / 钩子 —— 脚手架只新增两个文件。
//
// 只依赖 QtCore，纯文件操作，不碰数据库。
class RepoScaffolder
{
public:
    // "" = 路径可用（绝对、存在的目录、且是 git 仓库）；否则返回原因
    static QString validateRepoPath(const QString &repoPath);
    // 仓库是否已有台账脚手架（两个文件都在才算）
    static bool hasScaffold(const QString &repoPath);
    // 生成脚手架；"" = 成功，否则返回原因。既有文件一律不覆盖。
    static QString scaffold(const QString &repoPath);

private:
    static bool writeFileAtomic(const QString &filePath, const QByteArray &bytes, QString *error);
};
