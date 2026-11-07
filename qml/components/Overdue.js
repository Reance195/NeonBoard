// 逾期判定（DX-137）：带年份的 dueAt 优先 —— MM-dd 字符串比较跨年会整年失效
// （"12-31" 在次年任何日期都判不逾期）。纯函数单独成文件：TaskCard 与
// tests/qml 的用例共用同一份实现，测试测的就是 shipped 逻辑（Z5）。
//
// dueAtSeconds：到期时刻（epoch 秒，存的是到期日 23:59:59）；
// dueText：MM-dd 显示文本（老调用方未传 dueAt 时的退路口径，同年字典序）；
// nowMs：当前时刻（epoch 毫秒），由调用方注入以便测试钉死时间。
.pragma library

function isOverdue(dueAtSeconds, dueText, nowMs) {
    if (dueAtSeconds > 0)
        return nowMs > dueAtSeconds * 1000
    if (!dueText || dueText.length !== 5)
        return false
    var now = new Date(nowMs)
    var today = ("0" + (now.getMonth() + 1)).slice(-2) + "-" + ("0" + now.getDate()).slice(-2)
    return dueText < today
}
