#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// 拖拽落点几何（R8-05）。
//
// 原先这套「鼠标 y 落在哪两张卡之间」的判断长在 BoardColumn.qml 的 resolveDrop() 里，
// 和 delegate 实例化、contentY 换算、指示线绘制拌在一起，谁都不敢改 —— 改错一行
// 就是「拖到 A 位置结果插到 B 位置」，而这种错误在界面上只表现为「手感怪」，
// 没有断言能抓住它。这里把它拆成纯函数：
//
//   输入只有三样：可见卡片的 (id, y, height)、整列完整顺序的 id、鼠标在内容区的 y。
//   输出：插到谁之前（beforeId）、指示线锚在谁身上（anchorId/上下）、指示线 y。
//
// QML 那侧仍然负责「把视口坐标换成内容坐标」和「画线」，因为那才是真正需要
// delegate 实例的知识；一旦换完坐标，剩下的算术全在这里，且可被 tst_dragfeel 覆盖。
class DragFeel : public QObject
{
    Q_OBJECT

public:
    // 可见卡片的一条：y / height 是 delegate 在内容坐标系里的位置与高度
    struct Slot
    {
        QString id;
        qreal y = 0;
        qreal height = 0;
    };

    // 落点结果
    struct Place
    {
        QString beforeId;    // 插到这张卡之前；空串 = 列尾
        QString anchorId;    // 指示线锚定的卡；空串 = 空列，没有线可画
        bool below = false;  // 指示线画在 anchor 下方（true）还是上方
        qreal lineY = -1;    // 指示线在内容坐标系里的 y；负数表示无线
        bool onLine = false; // 鼠标是否已经贴到这条线上（磁吸）
        bool valid = false;  // 是否算出了落点（空列也算有效：落到列尾）
    };

    explicit DragFeel(QObject *parent = nullptr);

    // 纯函数核心：单测直接调它，不经过 QML。
    static Place resolve(const QList<Slot> &visible, const QStringList &fullIds, qreal y,
                         qreal spacing, qreal snapDistance);

    // 落位动效预算：拖完必须立刻落库，不能让用户等动画。
    // reduced 时归零（只保留状态切换），常规 90ms —— 超过 100ms 就会觉得「拖完卡了一下」。
    static int settleMs(bool reducedMotion);

    // ---- QML 入口 ----
    // visible: [{ id, y, height }, ...]（按 y 升序，由 BoardColumn.visibleDelegates 给）
    Q_INVOKABLE QVariantMap resolveDrop(const QVariantList &visible, const QStringList &fullIds,
                                        qreal y, qreal spacing, qreal snapDistance) const;
    Q_INVOKABLE int settleDuration(bool reducedMotion) const;

private:
    static QList<Slot> slotsOf(const QVariantList &visible);
};
