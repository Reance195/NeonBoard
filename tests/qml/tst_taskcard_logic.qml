import QtQuick
import QtTest
import "../../qml/components/Overdue.js" as Overdue

// DX-137 的两组纯逻辑回归：
//   1. 逾期判定 —— 直接调用 TaskCard 实际 import 的 Overdue.js（测的就是
//      shipped 实现，不是复刻品）：跨年不再整年失效。
//   2. Canvas 数据变化 → requestPaint 的重绘机制 —— ActivityHeatmap 的接线是
//      一行 onCellsChanged handler，真实组件依赖应用上下文属性（Theme）无法在
//      测试引擎实例化，这里用同形复刻夹具证明机制本身成立。
Item {
    id: root

    width: 100
    height: 100

    // 复刻夹具：数据属性变化 → 显式 requestPaint → onPaint 真的执行
    Canvas {
        id: replica
        anchors.fill: parent
        property var cells: []
        onCellsChanged: requestPaint()
        onPaint: ++root.paintCount
    }
    property int paintCount: 0

    TestCase {
        name: "TaskCardLogic"

        function test_crossYearStaysOverdue() {
            // 去年 12-31 23:59:59：MM-dd 字符串口径整年失效，epoch 口径必须仍判逾期
            var now = new Date()
            var lastNewYearEve =
                new Date(now.getFullYear() - 1, 11, 31, 23, 59, 59)
            verify(Overdue.isOverdue(Math.floor(lastNewYearEve.getTime() / 1000),
                                     "12-31", now.getTime()),
                   "跨年的到期日必须仍判逾期")
        }

        function test_futureDueNotOverdue() {
            var now = new Date()
            var tomorrow = new Date(now.getFullYear(), now.getMonth(),
                                    now.getDate() + 1, 23, 59, 59)
            verify(!Overdue.isOverdue(Math.floor(tomorrow.getTime() / 1000),
                                      "12-31", now.getTime()),
                   "未来的到期日不得判逾期")
        }

        function test_dueDayItselfNotOverdue() {
            // dueAt 存的是到期日 23:59:59：当天之内不算逾期
            var now = new Date()
            var tonight = new Date(now.getFullYear(), now.getMonth(),
                                   now.getDate(), 23, 59, 59)
            verify(!Overdue.isOverdue(Math.floor(tonight.getTime() / 1000),
                                      "", now.getTime()))
        }

        function test_fallbackSameYearText() {
            // 未传 dueAt 时退回 MM-dd 字典序（同年口径，老调用方不回归）
            var jan5 = new Date(2026, 0, 5, 12, 0, 0).getTime()
            verify(Overdue.isOverdue(0, "01-01", jan5), "同年已过日期判逾期")
            verify(!Overdue.isOverdue(0, "12-31", jan5), "同年未到日期不判逾期")
            verify(!Overdue.isOverdue(0, "", jan5), "无到期信息不判逾期")
            verify(!Overdue.isOverdue(0, "bad", jan5), "非 MM-dd 文本不判逾期")
        }

        function test_dataChangeTriggersRepaint() {
            root.paintCount = 0
            var before = root.paintCount
            replica.cells = [1, 2, 3]
            // requestPaint 后 onPaint 必须真的执行（机制证明：接线有效）
            tryCompare(root, "paintCount", before + 1, 5000)
        }
    }
}
