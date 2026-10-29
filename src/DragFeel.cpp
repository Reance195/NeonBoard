#include "DragFeel.h"

#include <QVariantMap>
#include <QtMath>

DragFeel::DragFeel(QObject *parent)
    : QObject(parent)
{
}

QList<DragFeel::Slot> DragFeel::slotsOf(const QVariantList &visible)
{
    QList<Slot> out;
    out.reserve(visible.size());
    for (const QVariant &item : visible) {
        const QVariantMap map = item.toMap();
        Slot slot;
        slot.id = map.value(QStringLiteral("id")).toString();
        slot.y = map.value(QStringLiteral("y")).toDouble();
        slot.height = map.value(QStringLiteral("height")).toDouble();
        if (!slot.id.isEmpty())
            out.append(slot);
    }
    return out;
}

DragFeel::Place DragFeel::resolve(const QList<Slot> &visible, const QStringList &fullIds, qreal y,
                                  qreal spacing, qreal snapDistance)
{
    Place place;
    place.valid = true;

    // 空列：没有任何卡可锚，落到列尾。这里不算失败 —— 「拖进空列」是最常见的一次拖拽。
    if (visible.isEmpty())
        return place;

    // 插入下标：第一张「鼠标落在其上半区」的卡之前；都没落中就是列尾。
    int insertAt = visible.size();
    for (int i = 0; i < visible.size(); ++i) {
        if (y < visible[i].y + visible[i].height / 2.0) {
            insertAt = i;
            break;
        }
    }

    const Slot &last = visible.last();
    if (insertAt < visible.size()) {
        const Slot &target = visible[insertAt];
        place.beforeId = target.id;
        place.anchorId = target.id;
        place.below = false;
        place.lineY = target.y - spacing / 2.0;
    } else {
        // 落在最后一张可见卡的下半区：插到它后面那张之前。
        // 「后面那张」必须从完整顺序里取 —— 列表滚动时最后一张可见卡下面还有没实例化的卡，
        // 只按可见集合算会把它当成列尾，结果拖到中间却插到了最末。
        const int lastIndex = fullIds.indexOf(last.id);
        place.beforeId = (lastIndex >= 0 && lastIndex + 1 < fullIds.size())
                ? fullIds.at(lastIndex + 1)
                : QString();
        place.anchorId = last.id;
        place.below = true;
        place.lineY = last.y + last.height + spacing / 2.0;
    }

    place.onLine = snapDistance > 0 && qAbs(y - place.lineY) <= snapDistance;
    return place;
}

int DragFeel::settleMs(bool reducedMotion)
{
    return reducedMotion ? 0 : 90;
}

QVariantMap DragFeel::resolveDrop(const QVariantList &visible, const QStringList &fullIds, qreal y,
                                  qreal spacing, qreal snapDistance) const
{
    const Place place = resolve(slotsOf(visible), fullIds, y, spacing, snapDistance);
    QVariantMap out;
    out.insert(QStringLiteral("beforeId"), place.beforeId);
    out.insert(QStringLiteral("anchorId"), place.anchorId);
    out.insert(QStringLiteral("below"), place.below);
    out.insert(QStringLiteral("lineY"), place.lineY);
    out.insert(QStringLiteral("onLine"), place.onLine);
    out.insert(QStringLiteral("valid"), place.valid);
    return out;
}

int DragFeel::settleDuration(bool reducedMotion) const
{
    return settleMs(reducedMotion);
}
