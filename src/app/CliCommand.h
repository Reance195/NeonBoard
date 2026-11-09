#pragma once

#include <QStringList>
#include <vector>

// 命令表是 utilityMode 的唯一事实源：新增无头命令只改 CliCommands.cpp 一行，
// 不必回到 AppMain.cpp 补 || （main.cpp 行数棘轮只许减不许增）。
enum class CliMode { Gui, NonGui };

struct CliCommand {
    const char *flag;
    const char *summary;
    CliMode mode;
    int arity;        // 0 = 纯开关，1 = 后接一个值
    bool repeatable;
};

const std::vector<CliCommand> &allCommands();
bool isUtilityMode(const QStringList &arguments);
