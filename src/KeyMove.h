#pragma once

#include <QObject>

// 键盘跨列位移（R8-06）。
//
// 两件事被抽出来，因为它们是「纯算术 + 边界」的组合，错一次就是「按了右键卡不见了」
// 或者「在最后一列按右键，卡跑到第一列去了」：
//
//   step()        当前在第几列、按了几次方向键 → 目标列下标，越界返回 -1（不是夹紧）
//   insertIndex() 在源列的第几张 → 落到目标列的第几位（目标列更短就落到列尾）
//
// 「越界返回 -1 而不是夹紧」是刻意的：夹紧会让调用方以为移动成功了，
// 于是白白压一条「什么都没变」的撤销记录，Ctrl+Z 按一次像丢了反应。
class KeyMove : public QObject
{
    Q_OBJECT

public:
    explicit KeyMove(QObject *parent = nullptr);

    // delta 只取 ±1；返回 -1 表示没有可去的相邻列
    static int step(int current, int delta, int count);
    // 目标列只有 targetCount 张卡时，源列第 index 张应该落在第几位
    static int insertIndex(int index, int targetCount);

    Q_INVOKABLE int stepFor(int current, int delta, int count) const;
    Q_INVOKABLE int insertIndexFor(int index, int targetCount) const;

    // 读屏播报：键盘移动没有视觉焦点跳转可依赖，结果必须主动念出来。
    // 用 Qt 6.8 的 QAccessibleAnnouncementEvent（无障碍公告事件），
    // 没有可播报的窗口时静默返回 —— 单测与无 GUI 环境直接跳过。
    Q_INVOKABLE void announce(const QString &text) const;
};
