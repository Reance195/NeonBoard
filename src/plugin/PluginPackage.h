#pragma once

#include <QString>
#include <QStringList>

/**
 * 插件包（.neon）格式：一个 JSON 文件，内容是
 * {
 *   "manifest": { "id": "com.example.x", "name": "示例", "apiVersion": 2, ... },
 *   "files": { "main.qml": "<源码文本>", "rules.js": "<源码文本>" }
 * }
 *
 * manifest 部分会被写成插件目录下的 plugin.json，files 部分按原样落盘。
 * 选 JSON 而不是 zip，是为了零第三方依赖、跨平台、便于人工审阅。
 */
class PluginPackage
{
public:
    /** 从 .neon 文件解包并安装到 targetDir（已存在则先备份再原子替换） */
    static bool installNeonFile(const QString &neonPath, const QString &targetDir, QString *error);

    /** 把 sourceDir 整个目录复制到 targetDir（用于从官方源/示例源安装） */
    static bool installDirectory(const QString &sourceDir, const QString &targetDir, QString *error);

    /** 校验 .neon 文件结构合法（不解包） */
    static bool validateNeonFile(const QString &neonPath, QString *error);

    /** 读取 .neon 包里的 manifest.id（读不出返回空串） */
    static QString neonPluginId(const QString &neonPath);

    /** 插件 id → 插件目录名（取最后一段点号后的部分，如 com.lin.plan-hook → plan-hook） */
    static QString dirNameForId(const QString &pluginId);
};
