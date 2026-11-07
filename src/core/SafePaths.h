#pragma once

#include <QString>

/**
 * 安全路径判定（单一实现，Z5）：插件 .neon 落盘（DX-132）与更新包路径（DX-136）共用。
 * 判定凭「内容」而不是「清单上出现过的攻击面」——盘符、备用数据流、反斜杠、
 * 点段全部按字符拒绝，不依赖 QDir 的平台相关语义。
 */
namespace SafePaths {

/** 防目录穿越：只允许相对路径（纯文件名 + 单层子目录），拒绝盘符/UNC/反斜杠/冒号/点段 */
bool isSafeRelativePath(const QString &path);

/** 单段目录名：非空且不含路径分隔符与冒号（用于 id → 目录名派生后的防御） */
bool isSafeSingleSegment(const QString &name);

} // namespace SafePaths
