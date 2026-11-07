#include "SafePaths.h"

#include <QStringList>

namespace SafePaths {

bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty())
        return false;
    if (path.contains(QLatin1Char('\\')))
        return false;
    // 冒号一并拒绝：覆盖盘符绝对（C:/x）、盘符相对（C:x）与 NTFS 备用数据流（file:stream）
    if (path.contains(QLatin1Char(':')))
        return false;
    if (path.startsWith(QLatin1Char('/')))
        return false;
    if (path.contains(QLatin1String("..")))
        return false;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;
    for (const QString &part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    }
    return true;
}

bool isSafeSingleSegment(const QString &name)
{
    if (name.isEmpty())
        return false;
    if (name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return false;
    if (name.contains(QLatin1Char(':')))
        return false;
    return true;
}

} // namespace SafePaths
