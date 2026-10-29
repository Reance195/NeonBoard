// R8-05：拖拽落点几何。
//
// 这段算术原先埋在 BoardColumn.qml 的 resolveDrop() 里，混着 delegate 实例化与
// contentY 换算，改错只表现为「拖到 A 位置插到 B」，没有任何断言能抓住。
// 抽成纯函数后，这里把「落在哪两行之间」「滚动时最后一张可见卡下面还有卡」
// 「磁吸半径」这些容易错的分支逐条钉住。

#include "DragFeel.h"

#include <QTest>

class TestDragFeel : public QObject
{
    Q_OBJECT

private slots:
    void emptyColumnFallsToTail();
    void aboveFirstMidpointGoesFirst();
    void belowFirstMidpointGoesSecond();
    void belowLastVisibleUsesFullOrder();
    void tailWhenLastVisibleIsLastOfFullOrder();
    void singleCardSplitsAtMidpoint();
    void lineYMatchesInsertionGap();
    void magneticSnapWithinRadius();
    void magneticSnapIgnoresZeroRadius();
    void unknownVisibleCardDoesNotCrash();
    void visibleListOrderDoesNotMatterForTail();
    void settleBudgetIsUnderOneHundredMs();
    void qmlWrapperMirrorsCore();

private:
    // 三张等高的卡：0~80 / 88~168 / 176~256（间距 8）
    static QList<DragFeel::Slot> threeCards();
    static QStringList idsOf(const QList<DragFeel::Slot> &slots);
};

QList<DragFeel::Slot> TestDragFeel::threeCards()
{
    return { { QStringLiteral("a"), 0, 80 },
             { QStringLiteral("b"), 88, 80 },
             { QStringLiteral("c"), 176, 80 } };
}

QStringList TestDragFeel::idsOf(const QList<DragFeel::Slot> &cards)
{
    QStringList ids;
    for (const auto &card : cards)
        ids << card.id;
    return ids;
}

void TestDragFeel::emptyColumnFallsToTail()
{
    const DragFeel::Place place =
            DragFeel::resolve({}, {}, 500, 8, 6);
    // 空列也要算「有效落点」：拖进空列是最常见的一次拖拽，不是失败
    QVERIFY(place.valid);
    QVERIFY(place.beforeId.isEmpty());
    QVERIFY(place.anchorId.isEmpty());
    QVERIFY(!place.onLine);
    QVERIFY(place.lineY < 0);
}

void TestDragFeel::aboveFirstMidpointGoesFirst()
{
    const auto cards = threeCards();
    const DragFeel::Place place = DragFeel::resolve(cards, idsOf(cards), 10, 8, 6);
    QCOMPARE(place.beforeId, QStringLiteral("a"));
    QCOMPARE(place.anchorId, QStringLiteral("a"));
    QVERIFY(!place.below);
}

void TestDragFeel::belowFirstMidpointGoesSecond()
{
    const auto cards = threeCards();
    // 第 60px 在第一张（0~80）的下半区
    const DragFeel::Place place = DragFeel::resolve(cards, idsOf(cards), 60, 8, 6);
    QCOMPARE(place.beforeId, QStringLiteral("b"));
    QCOMPARE(place.anchorId, QStringLiteral("b"));
    QVERIFY(!place.below);
}

void TestDragFeel::belowLastVisibleUsesFullOrder()
{
    const auto cards = threeCards();
    // 列表滚到只剩前两张可见、鼠标落在第二张下半区：后面还有 c 没实例化，
    // 必须插到 c 之前，而不是当成列尾
    const QList<DragFeel::Slot> visible = { cards[0], cards[1] };
    const DragFeel::Place place = DragFeel::resolve(visible, idsOf(cards), 160, 8, 6);
    QCOMPARE(place.beforeId, QStringLiteral("c"));
    QCOMPARE(place.anchorId, QStringLiteral("b"));
    QVERIFY(place.below);
}

void TestDragFeel::tailWhenLastVisibleIsLastOfFullOrder()
{
    const auto cards = threeCards();
    const DragFeel::Place place = DragFeel::resolve(cards, idsOf(cards), 400, 8, 6);
    QVERIFY(place.beforeId.isEmpty());
    QCOMPARE(place.anchorId, QStringLiteral("c"));
    QVERIFY(place.below);
}

void TestDragFeel::singleCardSplitsAtMidpoint()
{
    const QList<DragFeel::Slot> one = { { QStringLiteral("only"), 0, 80 } };
    const DragFeel::Place top = DragFeel::resolve(one, { QStringLiteral("only") }, 30, 8, 6);
    QCOMPARE(top.beforeId, QStringLiteral("only"));
    QVERIFY(!top.below);

    const DragFeel::Place bottom = DragFeel::resolve(one, { QStringLiteral("only") }, 50, 8, 6);
    QVERIFY(bottom.beforeId.isEmpty());
    QVERIFY(bottom.below);
}

void TestDragFeel::lineYMatchesInsertionGap()
{
    const auto cards = threeCards();
    // 插到 b 之前：线画在两卡之间的空隙中线（b.y - spacing/2 = 84）
    const DragFeel::Place before = DragFeel::resolve(cards, idsOf(cards), 70, 8, 6);
    QCOMPARE(before.lineY, 84.0);

    // 列尾：线画在最后一张下方（176+80+4 = 260）
    const DragFeel::Place tail = DragFeel::resolve(cards, idsOf(cards), 1000, 8, 6);
    QCOMPARE(tail.lineY, 260.0);
}

void TestDragFeel::magneticSnapWithinRadius()
{
    const auto cards = threeCards();
    // 线在 84：y=80 距离 4 < 6，算吸附
    const DragFeel::Place near = DragFeel::resolve(cards, idsOf(cards), 80, 8, 6);
    QVERIFY(near.onLine);
    // 正好 6px 也算（闭区间）：y=78
    const DragFeel::Place edge = DragFeel::resolve(cards, idsOf(cards), 78, 8, 6);
    QVERIFY(edge.onLine);
    // 7px 就不算：y=77
    const DragFeel::Place far = DragFeel::resolve(cards, idsOf(cards), 77, 8, 6);
    QVERIFY(!far.onLine);
}

void TestDragFeel::magneticSnapIgnoresZeroRadius()
{
    const auto cards = threeCards();
    // 半径 0 表示关掉磁吸（低画质档），此时永远不该报吸附
    const DragFeel::Place place = DragFeel::resolve(cards, idsOf(cards), 84, 8, 0);
    QVERIFY(!place.onLine);
}

void TestDragFeel::unknownVisibleCardDoesNotCrash()
{
    // visible 里有张卡不在 fullIds 里（刚插入还没同步 / 过滤态）：按列尾处理，不许崩
    const QList<DragFeel::Slot> visible = { { QStringLiteral("ghost"), 0, 80 } };
    const DragFeel::Place place =
            DragFeel::resolve(visible, { QStringLiteral("x") }, 999, 8, 6);
    QVERIFY(place.beforeId.isEmpty());
    QCOMPARE(place.anchorId, QStringLiteral("ghost"));
    QVERIFY(place.below);
}

void TestDragFeel::visibleListOrderDoesNotMatterForTail()
{
    // 最后一张可见卡决定列尾：换个顺序传入，行首插位的判定就该换人
    const auto cards = threeCards();
    const QList<DragFeel::Slot> reversed = { cards[2], cards[0], cards[1] };
    const DragFeel::Place place =
            DragFeel::resolve(reversed, idsOf(cards), 1000, 8, 6);
    // 契约：调用方按 y 升序传入（BoardColumn.visibleDelegates 已排序）；
    // 这里只保证「取最后一项」的口径是明确的，不会因为内部排序而漂移。
    // 最后一项是 b，完整顺序里 b 后面还有 c —— 所以是「插到 c 之前」，不是列尾。
    QCOMPARE(place.anchorId, QStringLiteral("b"));
    QCOMPARE(place.beforeId, QStringLiteral("c"));
}

void TestDragFeel::settleBudgetIsUnderOneHundredMs()
{
    // 拖完必须立刻落库，视觉收尾不许超过 100ms：超过就会觉得「拖完卡了一下」
    QCOMPARE(DragFeel::settleMs(true), 0);
    QVERIFY(DragFeel::settleMs(false) > 0);
    QVERIFY(DragFeel::settleMs(false) <= 100);
}

void TestDragFeel::qmlWrapperMirrorsCore()
{
    DragFeel feel;
    const QVariantList visible = {
        QVariantMap{ { QStringLiteral("id"), QStringLiteral("a") },
                     { QStringLiteral("y"), 0 },
                     { QStringLiteral("height"), 80 } },
        QVariantMap{ { QStringLiteral("id"), QStringLiteral("b") },
                     { QStringLiteral("y"), 88 },
                     { QStringLiteral("height"), 80 } },
    };
    const QVariantMap drop = feel.resolveDrop(visible,
                                              { QStringLiteral("a"), QStringLiteral("b") },
                                              60, 8, 6);
    QCOMPARE(drop.value(QStringLiteral("beforeId")).toString(), QStringLiteral("b"));
    QCOMPARE(drop.value(QStringLiteral("anchorId")).toString(), QStringLiteral("b"));
    QCOMPARE(drop.value(QStringLiteral("below")).toBool(), false);
    QCOMPARE(drop.value(QStringLiteral("lineY")).toDouble(), 84.0);
    QVERIFY(drop.value(QStringLiteral("valid")).toBool());

    // QVariant 里缺字段 / 类型不对时跳过该条，不产生空 id 的落点
    const QVariantList broken = { QVariantMap{ { QStringLiteral("y"), 10 } } };
    const QVariantMap fallback = feel.resolveDrop(broken, {}, 10, 8, 6);
    QVERIFY(fallback.value(QStringLiteral("beforeId")).toString().isEmpty());
    QVERIFY(fallback.value(QStringLiteral("valid")).toBool());
}

QTEST_GUILESS_MAIN(TestDragFeel)

#include "tst_dragfeel.moc"
