#include "CliCommand.h"

#include <QString>

namespace {
const std::vector<CliCommand> kCommands = {
    {"--smoke", "smoke", CliMode::NonGui, 0, false},
    {"--sync-roadmap", "sync-roadmap", CliMode::NonGui, 1, false},
    {"--export-roadmap", "export-roadmap", CliMode::NonGui, 1, false},
    {"--export-tasks", "export-tasks", CliMode::NonGui, 1, false},
    {"--import-tasks", "import-tasks", CliMode::NonGui, 1, false},
    {"--backup", "backup", CliMode::NonGui, 1, false},
    {"--selftest", "selftest", CliMode::NonGui, 0, false},
    {"--emit-hook", "emit-hook", CliMode::NonGui, 0, false},
    {"--emit-hook-file", "emit-hook-file", CliMode::NonGui, 1, false},
    {"--install-plugin-file", "install-plugin-file", CliMode::NonGui, 1, false},
    {"--market-refresh", "market-refresh", CliMode::NonGui, 0, false},
    {"--market-source", "market-source", CliMode::NonGui, 1, false},
    {"--market-sources", "market-sources", CliMode::NonGui, 0, false},
    {"--market-install", "market-install", CliMode::NonGui, 1, false},
    {"--install-plugin-url", "install-plugin-url", CliMode::NonGui, 1, false},
    {"--preview-list", "preview-list", CliMode::NonGui, 0, false},
    {"--preview-build", "preview-build", CliMode::NonGui, 1, false},
    {"--preview-package", "preview-package", CliMode::NonGui, 1, false},
    {"--preview-start", "preview-start", CliMode::NonGui, 1, false},
    {"--preview-stop", "preview-stop", CliMode::NonGui, 1, false},
    {"--plugin-report", "plugin-report", CliMode::NonGui, 0, false},
    {"--register-file-assoc", "register-file-assoc", CliMode::NonGui, 0, false},
    {"--unregister-file-assoc", "unregister-file-assoc", CliMode::NonGui, 0, false},
    {"--assoc-status", "assoc-status", CliMode::NonGui, 0, false},
    {"--check-update", "check-update", CliMode::NonGui, 0, false},
    {"--update-apply", "update-apply", CliMode::NonGui, 0, false},
    {"--update-source", "update-source", CliMode::NonGui, 1, false},
    {"--update-sources", "update-sources", CliMode::NonGui, 0, false},
    {"--run-summary", "run-summary", CliMode::NonGui, 1, false},
    {"--menu-preview", "menu-preview", CliMode::NonGui, 0, false},
    {"--import-ledger", "import-ledger", CliMode::NonGui, 1, false},
};
}

const std::vector<CliCommand> &allCommands()
{
    return kCommands;
}

bool isUtilityMode(const QStringList &arguments)
{
    for (const CliCommand &c : kCommands) {
        if (c.mode == CliMode::NonGui
            && arguments.contains(QString::fromLatin1(c.flag)))
            return true;
    }
    return false;
}
