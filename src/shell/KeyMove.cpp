#include "KeyMove.h"

#include <QAccessible>
#include <QGuiApplication>
#include <QWindow>

KeyMove::KeyMove(QObject *parent)
    : QObject(parent)
{
}

int KeyMove::step(int current, int delta, int count)
{
    if (count <= 0 || current < 0 || current >= count)
        return -1;
    if (delta == 0)
        return -1;
    const int target = current + delta;
    if (target < 0 || target >= count)
        return -1;
    return target;
}

int KeyMove::insertIndex(int index, int targetCount)
{
    if (targetCount <= 0)
        return 0;
    if (index < 0)
        return targetCount;          // 源列里找不到这张卡：退到列尾
    return qMin(index, targetCount); // 目标列更短就落列尾，否则保持原来的相对位置
}

int KeyMove::stepFor(int current, int delta, int count) const
{
    return step(current, delta, count);
}

int KeyMove::insertIndexFor(int index, int targetCount) const
{
    return insertIndex(index, targetCount);
}

void KeyMove::announce(const QString &text) const
{
    if (text.isEmpty())
        return;
    QWindow *window = QGuiApplication::focusWindow();
    if (!window) {
        const auto windows = QGuiApplication::topLevelWindows();
        if (windows.isEmpty())
            return;
        window = windows.first();
    }
    QAccessibleAnnouncementEvent event(window, text);
    QAccessible::updateAccessibility(&event);
}
