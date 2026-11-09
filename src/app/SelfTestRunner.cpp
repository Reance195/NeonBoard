#include "AppMain.h"
#include "AppPaths.h"
#include "AppActions.h"
#include "AgentProfile.h"
#include "Database.h"
#include "HookEventQueue.h"
#include "Hotkey.h"
#include "Metrics.h"
#include "PluginEventBus.h"
#include "PluginHost.h"
#include "PluginLibrary.h"
#include "PluginLoader.h"
#include "PluginMarket.h"
#include "PluginPackage.h"
#include "PluginRuntime.h"
#include "PreviewHub.h"
#include "BoardSync.h"
#include "RoadmapSync.h"
#include "ProjectMap.h"
#include "ProjectStore.h"
#include "DataPort.h"
#include "SingleInstance.h"
#include "SplashWindow.h"
#include "WindowsIntegration.h"
#include "BoardColumns.h"
#include "DragFeel.h"
#include "KeyMove.h"
#include "TaskFilterModel.h"
#include "TaskModel.h"
#include "TaskTree.h"
#include "TaskStats.h"
#include "TaskAudit.h"
#include "TaskExtra.h"
#include "ThemeMarket.h"
#include "ThemePack.h"
#include "TaskExec.h"
#include "TaskRunner.h"
#include "TaskRunStore.h"
#include "Scheduler.h"
#include "WorkflowRunner.h"
#include "WorkflowPresets.h"
#include "AcceptanceRunner.h"
#include "ScopeMatcher.h"
#include "TriageRunner.h"
#include "LedgerImport.h"
#include "TaskUndo.h"
#include "CardDetail.h"
#include "ReminderScheduler.h"
#include "TrayController.h"
#include "UiSettings.h"
#include "UpdateManager.h"
#include "WindowController.h"

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <memory>
#include <mutex>
#include "StartupDefer.h"

static void neonboardLogHandler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    // 闪屏的渲染线程也会打日志（DX-39 起），而 QFile 不是线程安全的：
    // 两个线程同时写同一个文件会撕行，甚至踩坏 QTextStream 的内部缓冲。
    // 所以这里上把锁 —— 锁的范围只包住「写一行」，不影响任何业务路径。
    static std::mutex logMutex;
    static QFile file;
    static bool opened = false;
    std::lock_guard<std::mutex> lock(logMutex);
    if (!opened) {
        opened = true;
        file.setFileName(AppPaths::logFilePath());
        file.open(QIODevice::Append | QIODevice::Text);
    }
    if (file.isOpen()) {
        QTextStream stream(&file);
        stream << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz "))
               << message << '\n';
        stream.flush();
    }
}

// 命令行结果直接写 stdout：exe 是 GUI 子系统，qInfo 只进日志文件，
// 脚本要读结果必须走标准输出
static void emitCliLine(const QString &line)
{
    const QByteArray bytes = line.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}


#include "SelfTestRunner.h"

int runSelfTest(const SelfTestDeps &deps)
{
    Database &database = *deps.database;
    TaskModel &taskStore = *deps.taskStore;
    TaskUndo &taskUndo = *deps.taskUndo;
    TaskTree &taskTree = *deps.taskTree;
    TaskExtra &taskExtra = *deps.taskExtra;
    TaskExec &taskExec = *deps.taskExec;
    TaskRunStore &taskRuns = *deps.taskRuns;
    WorkflowPresets &workflowPresets = *deps.workflowPresets;
    TriageRunner &triageRunner = *deps.triageRunner;
    RoadmapSync &roadmap = *deps.roadmap;
    BoardColumns &boardColumns = *deps.boardColumns;

        const QString id = taskStore.addTask(QStringLiteral("selftest"), QStringLiteral("todo"), 2, QStringLiteral("test"), 0);
        const QVariantMap created = taskStore.taskById(id);
        qInfo() << "SELFTEST add" << id << created.value("title").toString() << "count" << taskStore.count();
        bool ok = !id.isEmpty() && created.value("title").toString() == QStringLiteral("selftest");
        ok = taskStore.updateTask(id, {{QStringLiteral("title"), QStringLiteral("selftest-updated")}, {QStringLiteral("priority"), 3}}) && ok;
        const QVariantMap updated = taskStore.taskById(id);
        qInfo() << "SELFTEST update" << updated.value("title").toString() << updated.value("priority").toInt();
        ok = updated.value("title").toString() == QStringLiteral("selftest-updated") && updated.value("priority").toInt() == 3 && ok;
        ok = taskStore.moveTask(id, QStringLiteral("doing"), -1) && ok;
        qInfo() << "SELFTEST move" << taskStore.taskById(id).value("status").toString();
        ok = taskStore.taskById(id).value("status").toString() == QStringLiteral("doing") && ok;
        // 精确插位：moveTaskBefore 插入到指定卡片之前，且落点顺序可被 taskIdsInStatus 读回
        const QString insA = taskStore.addTask(QStringLiteral("selftest-a"), QStringLiteral("todo"), 1, QString(), 0);
        const QString insB = taskStore.addTask(QStringLiteral("selftest-b"), QStringLiteral("todo"), 1, QString(), 0);
        const QString insC = taskStore.addTask(QStringLiteral("selftest-c"), QStringLiteral("todo"), 1, QString(), 0);
        ok = taskStore.moveTaskBefore(insC, QStringLiteral("todo"), insA) && ok;
        const QStringList todoIds = taskStore.taskIdsInStatus(QStringLiteral("todo"));
        qInfo() << "SELFTEST insert" << todoIds.join(QLatin1Char(','));
        ok = todoIds.indexOf(insC) >= 0 && todoIds.indexOf(insC) < todoIds.indexOf(insA) && ok;
        ok = taskStore.moveTaskBefore(insC, QStringLiteral("todo"), QString()) && ok;
        const QStringList tailIds = taskStore.taskIdsInStatus(QStringLiteral("todo"));
        ok = tailIds.last() == insC && ok;
        // 主/子任务关联：建边 → 父卡统计 → 成环拒绝 → 删子卡清边
        const QString linkParent = taskStore.addTask(QStringLiteral("selftest-parent"), QStringLiteral("todo"), 2, QString(), 0);
        const QString linkChild = taskStore.addTask(QStringLiteral("selftest-child"), QStringLiteral("todo"), 2, QString(), 0);
        ok = taskTree.setParent(linkChild, linkParent) && ok;
        const QVariantMap linkStats = taskTree.childStats(linkParent);
        qInfo() << "SELFTEST link" << linkStats.value("total").toInt() << linkStats.value("done").toInt();
        ok = linkStats.value("total").toInt() == 1 && ok;
        ok = !taskTree.setParent(linkParent, linkChild) && ok;   // 成环必须被拒
        taskStore.deleteTask(linkChild);
        ok = taskTree.parentId(linkChild).isEmpty() && ok;       // 删子卡要清边
        taskStore.deleteTask(linkParent);
        taskStore.deleteTask(insA);
        taskStore.deleteTask(insB);
        taskStore.deleteTask(insC);
        // 快速录入解析：日期/时间/标签/优先级标记被拆出来，标题只留正文
        const QVariantMap p1 = taskStore.previewInput(QStringLiteral("明天 15:00 #工作 !3 评审方案"));
        const QDateTime due1 = QDateTime::fromSecsSinceEpoch(p1.value("dueAt").toLongLong());
        ok = p1.value("title").toString() == QStringLiteral("评审方案") && ok;
        ok = p1.value("priority").toInt() == 3 && ok;
        ok = p1.value("tags").toStringList().contains(QStringLiteral("工作")) && ok;
        ok = due1.date() == QDate::currentDate().addDays(1)
             && due1.time().hour() == 15 && due1.time().minute() == 0 && ok;
        const QVariantMap p2 = taskStore.previewInput(QStringLiteral("周五 18:30 复盘"));
        const QDateTime due2 = QDateTime::fromSecsSinceEpoch(p2.value("dueAt").toLongLong());
        ok = p2.value("title").toString() == QStringLiteral("复盘") && ok;
        ok = due2.time().hour() == 18 && due2.time().minute() == 30 && ok;
        const QVariantMap p3 = taskStore.previewInput(QStringLiteral("普通任务"));
        ok = p3.value("title").toString() == QStringLiteral("普通任务")
             && !p3.value("hasDue").toBool() && p3.value("priority").toInt() < 0 && ok;
        const QVariantMap p4 = taskStore.previewInput(QStringLiteral("下周三 上午9点 汇报"));
        const QDateTime due4 = QDateTime::fromSecsSinceEpoch(p4.value("dueAt").toLongLong());
        ok = p4.value("title").toString() == QStringLiteral("汇报") && ok;
        ok = due4.date().dayOfWeek() == 3 && due4.time().hour() == 9 && ok;
        qInfo() << "SELFTEST parse" << p1.value("title").toString() << p1.value("dueText").toString()
                << p2.value("dueText").toString() << p4.value("dueText").toString();
        // 解析结果真正落库：addTask 走同一套解析
        const QString parsedId = taskStore.addTask(QStringLiteral("解析入库 明天 10:00 #测试 !2"),
                                                   QStringLiteral("todo"), 1, QString(), 0);
        const QVariantMap parsedTask = taskStore.taskById(parsedId);
        ok = parsedTask.value("title").toString() == QStringLiteral("解析入库") && ok;
        ok = parsedTask.value("priority").toInt() == 2 && ok;
        ok = parsedTask.value("tagsFull").toString().contains(QStringLiteral("测试")) && ok;
        ok = parsedTask.value("dueAt").toLongLong() > 0 && ok;
        taskStore.deleteTask(parsedId);
        // 通用撤销：删除 / 移动 / 编辑 三类操作都能回退到操作前的状态
        const QString undoId = taskStore.addTask(QStringLiteral("selftest-undo"), QStringLiteral("todo"), 1,
                                                 QStringLiteral("undo"), 0);
        ok = taskUndo.deleteTask(undoId) && ok;
        ok = taskStore.taskById(undoId).isEmpty() && ok;
        ok = taskUndo.undo() && ok;
        ok = taskStore.taskById(undoId).value("title").toString() == QStringLiteral("selftest-undo") && ok;
        ok = taskUndo.moveTask(undoId, QStringLiteral("doing"), -1) && ok;
        ok = taskStore.taskById(undoId).value("status").toString() == QStringLiteral("doing") && ok;
        ok = taskUndo.undo() && ok;
        ok = taskStore.taskById(undoId).value("status").toString() == QStringLiteral("todo") && ok;
        ok = taskUndo.updateTask(undoId, {{QStringLiteral("title"), QStringLiteral("selftest-undo-edited")},
                                          {QStringLiteral("priority"), 3}}) && ok;
        ok = taskStore.taskById(undoId).value("title").toString() == QStringLiteral("selftest-undo-edited") && ok;
        ok = taskUndo.undo() && ok;
        ok = taskStore.taskById(undoId).value("title").toString() == QStringLiteral("selftest-undo")
             && taskStore.taskById(undoId).value("priority").toInt() == 1 && ok;
        qInfo() << "SELFTEST undo" << taskUndo.canUndo();
        taskStore.deleteTask(undoId);
        const QVariantMap removed = taskStore.removeTask(id);
        ok = !removed.isEmpty() && ok;
        const QString restoredId = taskStore.restoreTask(removed);
        qInfo() << "SELFTEST restore" << restoredId << taskStore.taskById(id).value("status").toString();
        ok = restoredId == id
             && taskStore.taskById(id).value("status").toString() == QStringLiteral("doing") && ok;
        // 任务附加属性：设提醒 → 读回 → 标记已通知 → 清提醒 → 删任务清行
        const QString extraId = taskStore.addTask(QStringLiteral("selftest-extra"), QStringLiteral("todo"), 1, QString(), 0);
        ok = taskExtra.setReminder(extraId, QDateTime::currentSecsSinceEpoch() + 3600) && ok;
        {
            const QVariantMap extra = taskExtra.extraOf(extraId);
            ok = extra.value("remindAt").toLongLong() > 0
                 && extra.value("notifiedAt").toLongLong() == 0 && ok;
        }
        ok = taskExtra.markNotified(extraId) && ok;
        ok = taskExtra.extraOf(extraId).value("notifiedAt").toLongLong() > 0 && ok;
        ok = taskExtra.setReminder(extraId, 0) && ok;
        ok = taskExtra.extraOf(extraId).value("remindAt").toLongLong() == 0 && ok;
        taskStore.deleteTask(extraId);
        ok = taskExtra.extraOf(extraId).value("remindAt").toLongLong() == 0 && ok;  // 删任务要清行
        qInfo() << "SELFTEST extra" << taskExtra.revision();

        // 执行定义侧表：写入 / 往返 / 校验 / 删任务清行（M0）
        {
            const QString execId = taskStore.addTask(QStringLiteral("selftest-exec"));
            ok = !execId.isEmpty() && ok;
            QVariantMap execFields;
            execFields.insert(QStringLiteral("goal"), QStringLiteral("自检目标"));
            execFields.insert(QStringLiteral("acceptance"),
                              QVariantList{QStringLiteral("--selftest")});
            execFields.insert(QStringLiteral("scope"),
                              QVariantList{QStringLiteral("src/**")});
            execFields.insert(QStringLiteral("agent"), QStringLiteral("opencode"));
            execFields.insert(QStringLiteral("cwd"), QDir::tempPath());
            execFields.insert(QStringLiteral("workflow"), QStringLiteral("fix"));
            execFields.insert(QStringLiteral("timeoutMin"), 30);
            execFields.insert(QStringLiteral("maxAttempts"), 2);
            ok = taskExec.setExec(execId, execFields) && ok;
            const QVariantMap read = taskExec.execOf(execId);
            ok = read.value(QStringLiteral("goal")).toString() == QStringLiteral("自检目标") && ok;
            ok = read.value(QStringLiteral("acceptance")).toList().size() == 1 && ok;
            ok = read.value(QStringLiteral("workflow")).toString() == QStringLiteral("fix") && ok;
            // 非法 workflow 必须被拒
            QVariantMap bad;
            bad.insert(QStringLiteral("workflow"), QStringLiteral("落选流程"));
            ok = !taskExec.setExec(execId, bad) && ok;
            // 删任务要清行
            taskStore.deleteTask(execId);
            ok = taskExec.execOf(execId).value(QStringLiteral("goal")).toString().isEmpty() && ok;
            qInfo() << "SELFTEST exec" << taskExec.revision();
        }

        // Triage 只读分析（R6-07）：纯逻辑校验 + 草案存取
        {
            // 带解释性文字的输出里抠出 JSON
            const QJsonDocument doc = TriageRunner::extractJson(
                QStringLiteral("分析如下：\n```json\n{\"understanding\":\"x\"}\n```\n完毕"));
            ok = !doc.isNull()
                 && doc.object().value(QStringLiteral("understanding")).toString()
                        == QStringLiteral("x")
                 && ok;
            ok = TriageRunner::extractJson(QStringLiteral("完全没有 JSON")).isNull() && ok;
            // glob 合法性：相对路径可，绝对路径 / 反斜杠 / .. 不可
            ok = TriageRunner::isValidGlob(QStringLiteral("src/**")) && ok;
            ok = !TriageRunner::isValidGlob(QStringLiteral("C:/abs/path")) && ok;
            ok = !TriageRunner::isValidGlob(QStringLiteral("src\\a.cpp")) && ok;
            ok = !TriageRunner::isValidGlob(QStringLiteral("../evil")) && ok;
            // 草案校验：缺字段报 missing；齐全时 questions 截断、非法 glob 丢弃、
            // acceptance 空补默认、confidence<0.5 不可执行
            const QVariantMap missing = TriageRunner::validateDraft(
                {{QStringLiteral("understanding"), QStringLiteral("u")}});
            ok = !missing.value(QStringLiteral("ok")).toBool()
                 && missing.value(QStringLiteral("missing")).toList().size() >= 5 && ok;
            QVariantList questions;
            for (int i = 0; i < 5; ++i)
                questions.append(QStringLiteral("问题%1").arg(i));
            const QVariantMap full = TriageRunner::validateDraft({
                {QStringLiteral("understanding"), QStringLiteral("u")},
                {QStringLiteral("affected"), QVariantMap{{QStringLiteral("risk"), QStringLiteral("低")}}},
                {QStringLiteral("scope_draft"), QVariantList{QStringLiteral("src/**"),
                                                             QStringLiteral("bad\\path")}},
                {QStringLiteral("acceptance_draft"), QVariantList{}},
                {QStringLiteral("questions"), questions},
                {QStringLiteral("estimate"), QVariantMap{{QStringLiteral("size"), QStringLiteral("S")}}},
                {QStringLiteral("confidence"), 0.3},
            });
            ok = full.value(QStringLiteral("ok")).toBool() && ok;
            const QVariantMap draft = full.value(QStringLiteral("draft")).toMap();
            ok = draft.value(QStringLiteral("questions")).toList().size() == 3 && ok;
            ok = draft.value(QStringLiteral("scope_draft")).toList().size() == 1 && ok;
            ok = draft.value(QStringLiteral("acceptance_draft")).toList().size() == 1 && ok;
            ok = !draft.value(QStringLiteral("executable")).toBool() && ok;
            // prompt 模板：骨架 + fix 专项段
            const QString prompt = TriageRunner::buildPrompt(
                QStringLiteral("修按钮"), QStringLiteral("点了没反应"), QStringLiteral("fix"));
            ok = prompt.contains(QStringLiteral("只读分析器"))
                 && prompt.contains(QStringLiteral("修按钮"))
                 && prompt.contains(QStringLiteral("复现路径")) && ok;
            // 草案存 meta 表：写 → 读回 → 清除（不升 user_version、不加表）
            const QString triageId = taskStore.addTask(QStringLiteral("selftest-triage"));
            QVariantMap record{{QStringLiteral("status"), QStringLiteral("ok")},
                               {QStringLiteral("draft"), draft}};
            ok = database.setMetaValue(QStringLiteral("exec.triage.draft.%1").arg(triageId),
                                       QStringLiteral("{\"status\":\"ok\"}"))
                 && ok;
            ok = triageRunner.draftOf(triageId).value(QStringLiteral("status")).toString()
                        == QStringLiteral("ok")
                 && ok;
            ok = triageRunner.clearDraft(triageId) && triageRunner.draftOf(triageId).isEmpty()
                 && ok;
            Q_UNUSED(record);
            taskStore.deleteTask(triageId);
            qInfo() << "SELFTEST triage" << triageRunner.revision();
        }

        // 运行记录侧表：取锁 / 二次取锁被拒 / 心跳 / 结束 / 释放后可再取（R6-02）
        {
            const QString runTaskId = taskStore.addTask(QStringLiteral("selftest-run"));
            ok = !runTaskId.isEmpty() && ok;
            const QVariantMap runFirst = taskRuns.acquire(runTaskId);
            const QString runId = runFirst.value(QStringLiteral("runId")).toString();
            ok = runFirst.value(QStringLiteral("ok")).toBool() && !runId.isEmpty() && ok;
            const QVariantMap runBusy = taskRuns.acquire(runTaskId);
            ok = !runBusy.value(QStringLiteral("ok")).toBool()
                 && runBusy.value(QStringLiteral("code")).toString() == QStringLiteral("busy")
                 && ok;
            ok = taskRuns.heartbeat(runId) && ok;
            ok = taskRuns.release(runId, QStringLiteral("passed")) && ok;
            const QVariantMap runAgain = taskRuns.acquire(runTaskId);
            ok = runAgain.value(QStringLiteral("ok")).toBool() && ok;
            ok = taskRuns.release(runAgain.value(QStringLiteral("runId")).toString(),
                                  QStringLiteral("cancelled")) && ok;
            taskStore.deleteTask(runTaskId);
            const QSqlDatabase runConn = QSqlDatabase::database(database.connectionName());
            QSqlQuery runSchema(runConn);
            ok = runSchema.exec(QStringLiteral(
                     "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='task_run'"))
                 && runSchema.next() && runSchema.value(0).toInt() == 1 && ok;
            ok = runSchema.exec(QStringLiteral(
                     "SELECT COUNT(*) FROM sqlite_master WHERE type='index' "
                     "AND name='idx_task_run_active' AND sql LIKE '%UNIQUE%'"))
                 && runSchema.next() && runSchema.value(0).toInt() == 1 && ok;
            QSqlQuery runVersion(runConn);
            ok = runVersion.exec(QStringLiteral("PRAGMA user_version")) && runVersion.next()
                 && runVersion.value(0).toInt() == Database::kSchemaVersion && ok;
            qInfo() << "SELFTEST runs" << taskRuns.revision();
        }

        // 执行队列与调度器（R7-01）：新列存在 / 入队即排队 / 同卡防重 / 调序 / 跳过 /
        // 并发上限钳制 / 队列不跨会话恢复 / user_version 仍不变
        {
            const QSqlDatabase queueConn = QSqlDatabase::database(database.connectionName());
            // 新列必须都在（老库靠 ensureColumn 补，这里断言最终形态）
            const QStringList requiredColumns = {
                QStringLiteral("queue_status"), QStringLiteral("position"),
                QStringLiteral("parent_run_id"), QStringLiteral("step_index"),
                QStringLiteral("role"), QStringLiteral("handoff_path"),
                QStringLiteral("cost_ms"),
            };
            QSqlQuery columnQuery(queueConn);
            ok = columnQuery.exec(QStringLiteral("PRAGMA table_info(task_run)")) && ok;
            QStringList actualColumns;
            while (columnQuery.next())
                actualColumns.append(columnQuery.value(1).toString());
            for (const QString &column : requiredColumns)
                ok = actualColumns.contains(column) && ok;

            const QString queueTaskId = taskStore.addTask(QStringLiteral("selftest-queue"));
            ok = !queueTaskId.isEmpty() && ok;
            const QVariantMap queued = taskRuns.enqueue(queueTaskId);
            const QString queuedRunId = queued.value(QStringLiteral("runId")).toString();
            ok = queued.value(QStringLiteral("ok")).toBool() && !queuedRunId.isEmpty() && ok;
            ok = queued.value(QStringLiteral("position")).toInt() > 0 && ok;
            // 入队后该行是 queued/waiting，没有 started_at
            const QVariantMap queuedRow = taskRuns.run(queuedRunId);
            ok = queuedRow.value(QStringLiteral("status")).toString() == QStringLiteral("queued")
                 && queuedRow.value(QStringLiteral("queueStatus")).toString()
                        == QStringLiteral("waiting") && ok;
            ok = queuedRow.value(QStringLiteral("startedAt")).isNull() && ok;
            // 同卡再入队必须 busy（含排队中的自己）
            const QVariantMap queueBusy = taskRuns.enqueue(queueTaskId);
            ok = !queueBusy.value(QStringLiteral("ok")).toBool()
                 && queueBusy.value(QStringLiteral("code")).toString() == QStringLiteral("busy")
                 && ok;

            // 加第二条排队，验证调序
            const QString queueTaskId2 = taskStore.addTask(QStringLiteral("selftest-queue-2"));
            const QVariantMap queued2 = taskRuns.enqueue(queueTaskId2);
            const QString queuedRunId2 = queued2.value(QStringLiteral("runId")).toString();
            ok = queued2.value(QStringLiteral("ok")).toBool() && ok;
            ok = queued2.value(QStringLiteral("position")).toInt()
                     > queued.value(QStringLiteral("position")).toInt() && ok;
            // 第二条上移一次 → position 应排到第一条前面
            ok = taskRuns.reorder(queuedRunId2, -1) && ok;
            QSqlQuery orderQuery(queueConn);
            orderQuery.prepare(QStringLiteral(
                "SELECT id FROM task_run WHERE queue_status = 'waiting' "
                "ORDER BY position ASC LIMIT 1"));
            ok = orderQuery.exec() && orderQuery.next()
                 && orderQuery.value(0).toString() == queuedRunId2 && ok;
            // 只有两条时再上移无效（没有更早的 waiting）
            ok = !taskRuns.reorder(queuedRunId2, -1) && ok;

            // 跳过：终态 cancelled、queue_status=done、cost_ms=0，且不再出现在队列里
            ok = taskRuns.skip(queuedRunId2) && ok;
            const QVariantMap skipped = taskRuns.run(queuedRunId2);
            ok = skipped.value(QStringLiteral("status")).toString() == QStringLiteral("cancelled")
                 && skipped.value(QStringLiteral("queueStatus")).toString()
                        == QStringLiteral("done") && ok;
            ok = skipped.value(QStringLiteral("costMs")).toLongLong() == 0 && ok;
            bool stillQueued = false;
            for (const QVariant &row : taskRuns.waitingRuns())
                stillQueued = stillQueued
                              || row.toMap().value(QStringLiteral("id")).toString() == queuedRunId2;
            ok = !stillQueued && ok;

            // 派发：waiting → dispatching → acquire 提升为 running（不新建行）
            ok = taskRuns.beginDispatch(queuedRunId) && ok;
            const QVariantMap promoted = taskRuns.acquire(queueTaskId);
            ok = promoted.value(QStringLiteral("ok")).toBool() && ok;
            // 提升的是同一行，不是新 runId
            ok = promoted.value(QStringLiteral("runId")).toString() == queuedRunId && ok;
            const QVariantMap runningRow = taskRuns.run(queuedRunId);
            ok = runningRow.value(QStringLiteral("status")).toString() == QStringLiteral("running")
                 && runningRow.value(QStringLiteral("queueStatus")).toString()
                        == QStringLiteral("running") && ok;
            ok = !runningRow.value(QStringLiteral("startedAt")).isNull() && ok;
            // 结算：queue_status=done，cost_ms 由 running 时长补（≥0）
            ok = taskRuns.release(queuedRunId, QStringLiteral("passed")) && ok;
            const QVariantMap finished = taskRuns.run(queuedRunId);
            ok = finished.value(QStringLiteral("queueStatus")).toString()
                        == QStringLiteral("done")
                 && finished.value(QStringLiteral("costMs")).toLongLong() >= 0 && ok;

            // 成本账（R7-06）：本卡累计聚合 + 全库累计（纯读聚合，不改状态）
            ok = taskRuns.costSummary(queueTaskId).value(QStringLiteral("runs")).toInt() >= 1 && ok;
            ok = taskRuns.costSummary(queueTaskId).value(QStringLiteral("totalCostMs")).toLongLong()
                     >= 0
                 && taskRuns.totalCostAll() >= 0 && ok;

            // 并发上限钳制：默认 1，设置只接受 1..2
            ok = taskRuns.concurrency() == 1 && ok;
            ok = taskRuns.setConcurrency(9) && taskRuns.concurrency() == TaskRunStore::kMaxConcurrency
                 && ok;
            ok = taskRuns.setConcurrency(2) && taskRuns.concurrency() == 2 && ok;
            ok = taskRuns.setConcurrency(0) && taskRuns.concurrency() == 1 && ok;
            // 暂停 / 恢复落 meta
            ok = taskRuns.setQueuePaused(true) && taskRuns.queuePaused() && ok;
            ok = taskRuns.setQueuePaused(false) && !taskRuns.queuePaused() && ok;

            // 队列不跨会话恢复：重开 store 后残留的 waiting 行要变 cancelled
            const QVariantMap leftover = taskRuns.enqueue(queueTaskId);
            ok = leftover.value(QStringLiteral("ok")).toBool() && ok;
            {
                TaskRunStore reopened(&database);
                const QVariantMap cancelledRow =
                    reopened.run(leftover.value(QStringLiteral("runId")).toString());
                ok = cancelledRow.value(QStringLiteral("status")).toString()
                        == QStringLiteral("cancelled")
                     && cancelledRow.value(QStringLiteral("queueStatus")).toString()
                            == QStringLiteral("done") && ok;
            }
            // 恢复默认并发，别把自检的副作用留给真实实例
            taskRuns.setConcurrency(1);
            taskStore.deleteTask(queueTaskId);
            taskStore.deleteTask(queueTaskId2);

            QSqlQuery queueVersion(queueConn);
            ok = queueVersion.exec(QStringLiteral("PRAGMA user_version")) && queueVersion.next()
                 && queueVersion.value(0).toInt() == Database::kSchemaVersion && ok;
            qInfo() << "SELFTEST queue" << taskRuns.revision();
        }

        // 预设 workflow 与 orchestrator 选型（R7-04）：内置六条 / 规模选型 / 跳过规则
        // （qa 永不裁）/ 预设可编辑（存 meta，不升 user_version）/ 收口 summary 拼接
        {
            ok = WorkflowPresets::builtinPresets().size() == 6 && ok;
            // 选型：S→fix / M→feature / L→delivery / XL→delivery；未知 → 卡片兜底 fix
            ok = workflowPresets.routeFor(QStringLiteral("S")) == QStringLiteral("fix") && ok;
            ok = workflowPresets.routeFor(QStringLiteral("M")) == QStringLiteral("feature") && ok;
            ok = workflowPresets.routeFor(QStringLiteral("L")) == QStringLiteral("delivery") && ok;
            ok = workflowPresets.routeFor(QStringLiteral("4")) == QStringLiteral("delivery") && ok;
            ok = workflowPresets.routeFor(QString()) == QStringLiteral("fix") && ok;
            // 跳过规则：feature 小任务可跳 pm/architect/retro（qa 永不出现）；
            // feature@S 展开 = engineer,qa（小任务不跑复盘），M 档保留 retro（R19-01）
            const QVariantList skips = workflowPresets.skippableSteps(QStringLiteral("feature"),
                                                                     QStringLiteral("S"));
            ok = skips.size() == 3 && ok;
            for (const QVariant &role : skips)
                ok = role.toString() != QStringLiteral("qa")
                     && !WorkflowPresets::isProtectedRole(role.toString()) && ok;
            const QVariantList smallFeature =
                workflowPresets.stepsFor(QStringLiteral("feature"), QStringLiteral("S"));
            ok = smallFeature.size() == 2 && ok;
            const QVariantList midFeature =
                workflowPresets.stepsFor(QStringLiteral("feature"), QStringLiteral("M"));
            ok = midFeature.size() == 5
                 && midFeature.at(4).toMap().value(QStringLiteral("role")).toString()
                        == QStringLiteral("retro") && ok;
            // 预设可编辑：非法（skippable 含 qa）被拒；合法覆盖写 meta 后可见，reset 复位
            QVariantMap badPreset;
            badPreset.insert(QStringLiteral("key"), QStringLiteral("bad"));
            badPreset.insert(QStringLiteral("steps"), QVariantList{QStringLiteral("engineer")});
            badPreset.insert(QStringLiteral("skippable"), QVariantList{QStringLiteral("qa")});
            ok = !workflowPresets.savePreset(badPreset) && ok;
            QVariantMap goodPreset;
            goodPreset.insert(QStringLiteral("key"), QStringLiteral("myfix"));
            goodPreset.insert(QStringLiteral("label"), QStringLiteral("自定义修复"));
            goodPreset.insert(QStringLiteral("steps"),
                              QVariantList{QStringLiteral("engineer"), QStringLiteral("qa")});
            ok = workflowPresets.savePreset(goodPreset) && ok;
            ok = !workflowPresets.presetOf(QStringLiteral("myfix")).isEmpty() && ok;
            ok = workflowPresets.resetPresets()
                 && workflowPresets.presetOf(QStringLiteral("myfix")).isEmpty() && ok;
            // 收口 summary：含任务号 / workflow / 步骤角色 / 建议下一步
            const QVariantList summarySteps{
                QVariantMap{{QStringLiteral("role"), QStringLiteral("engineer")},
                            {QStringLiteral("status"), QStringLiteral("passed")}},
                QVariantMap{{QStringLiteral("role"), QStringLiteral("qa")},
                            {QStringLiteral("status"), QStringLiteral("passed")}},
            };
            const QString summary = WorkflowPresets::buildSummary(
                QStringLiteral("card-1"), QStringLiteral("fix"), summarySteps,
                QStringLiteral("修按钮"));
            ok = summary.contains(QStringLiteral("card-1"))
                 && summary.contains(QStringLiteral("fix"))
                 && summary.contains(QStringLiteral("engineer"))
                 && summary.contains(QStringLiteral("建议下一步")) && ok;
            qInfo() << "SELFTEST workflow-presets" << workflowPresets.revision();
        }

        // RRULE 下一期：DAILY / INTERVAL / WEEKLY+BYDAY / 月末钳制 / 非法规则
        ok = TaskExtra::nextOccurrence(QStringLiteral("FREQ=DAILY"),
                                        QDateTime(QDate(2026, 10, 23), QTime(15, 0)))
               .date() == QDate(2026, 10, 24) && ok;
        ok = TaskExtra::nextOccurrence(QStringLiteral("FREQ=DAILY;INTERVAL=7"),
                                        QDateTime(QDate(2026, 10, 23), QTime(9, 0)))
               .date() == QDate(2026, 10, 30) && ok;
        // 2026-10-23 是周五；BYDAY=MO → 下周一 10-26
        ok = TaskExtra::nextOccurrence(QStringLiteral("FREQ=WEEKLY;BYDAY=MO"),
                                        QDateTime(QDate(2026, 10, 23), QTime(9, 0)))
               .date() == QDate(2026, 10, 26) && ok;
        // 月末钳制：2027-01-31 + 1月 = 02-28（2027 非闰年）
        ok = TaskExtra::nextOccurrence(QStringLiteral("FREQ=MONTHLY"),
                                        QDateTime(QDate(2027, 1, 31), QTime(9, 0)))
               .date() == QDate(2027, 2, 28) && ok;
        ok = TaskExtra::nextOccurrence(QStringLiteral("FREQ=FOO"),
                                        QDateTime(QDate(2026, 10, 23), QTime(9, 0))).isNull() && ok;
        qInfo() << "SELFTEST rrule";
        // 完成重复任务派生下一期；撤销完成连带删除派生卡
        const QString recId = taskStore.addTask(QStringLiteral("selftest-recurring"),
                                                 QStringLiteral("todo"), 1, QString(), 0);
        ok = taskExtra.setRrule(recId, QStringLiteral("FREQ=DAILY")) && ok;
        const int beforeSpawn = taskStore.count();
        ok = taskUndo.moveTask(recId, QStringLiteral("done"), -1) && ok;
        ok = taskStore.count() == beforeSpawn + 1 && ok;
        // 找派生卡：同标题的新卡（不消费 takeSpawned 映射，留给撤销路径）
        QString spawnedId;
        for (const QVariant &value : taskStore.allTasks()) {
            const QVariantMap t = value.toMap();
            if (t.value(QStringLiteral("title")).toString() == QStringLiteral("selftest-recurring")
                && t.value(QStringLiteral("id")).toString() != recId)
                spawnedId = t.value(QStringLiteral("id")).toString();
        }
        ok = !spawnedId.isEmpty() && ok;
        if (!spawnedId.isEmpty()) {
            const QVariantMap spawned = taskStore.taskById(spawnedId);
            ok = spawned.value("status").toString() == QStringLiteral("todo")
                 && spawned.value("dueAt").toLongLong() > 0 && ok;
            ok = taskExtra.extraOf(spawnedId).value("rrule").toString() == QStringLiteral("FREQ=DAILY") && ok;
        }
        ok = taskUndo.undo() && ok;
        ok = taskStore.taskById(recId).value("status").toString() == QStringLiteral("todo") && ok;
        ok = spawnedId.isEmpty() || taskStore.taskById(spawnedId).isEmpty() && ok;  // 派生卡已删
        qInfo() << "SELFTEST recurring" << spawnedId;
        taskStore.deleteTask(recId);
        // 快照写回防降级：本程序不认识的字段（根级 / 条目级）必须原样保留。
        // 历史事故：旧版按「已知字段重建」把 v3 的 code 整片抹掉。
        {
            const QString snapPath = QDir(QDir::tempPath())
                                         .filePath(QStringLiteral("neonboard-selftest-snapshot.json"));
            const QString keepId = taskStore.addTask(QStringLiteral("selftest-snapshot"),
                                                     QStringLiteral("todo"), 1, QString(), 0);
            const QJsonObject seedItem{
                {QStringLiteral("id"), keepId},
                {QStringLiteral("title"), QStringLiteral("selftest-snapshot")},
                {QStringLiteral("status"), QStringLiteral("todo")},
                {QStringLiteral("priority"), 1},
                {QStringLiteral("unknownCustomField"), QStringLiteral("keep-me")},
            };
            const QJsonObject seedRoot{
                {QStringLiteral("version"), 3},
                {QStringLiteral("updated"), QStringLiteral("2026-01-01T00:00:00")},
                {QStringLiteral("signature"), QStringLiteral("keep-root-signature")},
                {QStringLiteral("items"), QJsonArray{seedItem}},
            };
            QFile seed(snapPath);
            if (seed.open(QIODevice::WriteOnly)) {
                seed.write(QJsonDocument(seedRoot).toJson(QJsonDocument::Indented));
                seed.close();
            }
            ok = roadmap.exportTo(snapPath) && ok;
            QJsonObject outRoot;
            QFile snapshot(snapPath);
            if (snapshot.open(QIODevice::ReadOnly)) {
                outRoot = QJsonDocument::fromJson(snapshot.readAll()).object();
                snapshot.close();
            }
            QJsonObject outItem;
            const QJsonArray outItems = outRoot.value(QStringLiteral("items")).toArray();
            for (const QJsonValue &value : outItems) {
                if (value.toObject().value(QStringLiteral("id")).toString() == keepId) {
                    outItem = value.toObject();
                    break;
                }
            }
            const QString keptRoot = outRoot.value(QStringLiteral("signature")).toString();
            const QString keptItem = outItem.value(QStringLiteral("unknownCustomField")).toString();
            ok = keptRoot == QStringLiteral("keep-root-signature") && ok;
            ok = keptItem == QStringLiteral("keep-me") && ok;
            qInfo() << "SELFTEST snapshot-preserve" << keptRoot << keptItem;
            QFile::remove(snapPath);
            taskStore.deleteTask(keepId);
        }
        // 事件队列不丢事件：没有订阅者时不得标记已投递，应保留待重投。
        {
            const QString hookPath = QDir(QDir::tempPath())
                                         .filePath(QStringLiteral("neonboard-selftest-hooks.db"));
            QFile::remove(hookPath);
            int pending = -1;
            {
                HookEventQueue probe(hookPath);
                QString hookError;
                ok = probe.open(&hookError) && ok;
                PluginEventBus emptyBus;
                probe.setEventBus(&emptyBus);
                ok = probe.enqueue(QStringLiteral("agent.task"),
                                   {{QStringLiteral("text"), QStringLiteral("selftest")}}, &hookError)
                     && ok;
                probe.start(50);
                probe.stop();
                pending = probe.pendingCount();
            }
            QFile::remove(hookPath);
            QFile::remove(hookPath + QStringLiteral("-wal"));
            QFile::remove(hookPath + QStringLiteral("-shm"));
            qInfo() << "SELFTEST hook-retry pending" << pending;
            ok = pending == 1 && ok;
        }
        ok = taskStore.deleteTask(id) && ok;
        // 数据可靠性：WAL 生效 + quick_check + 旧库迁移 + 损坏库自动备份重建
        {
            QSqlDatabase conn = QSqlDatabase::database(database.connectionName());
            QSqlQuery walQuery(conn);
            ok = walQuery.exec(QStringLiteral("PRAGMA journal_mode")) && walQuery.next()
                 && walQuery.value(0).toString().compare(QStringLiteral("wal"), Qt::CaseInsensitive) == 0
                 && ok;
            QSqlQuery checkQuery(conn);
            ok = checkQuery.exec(QStringLiteral("PRAGMA quick_check")) && checkQuery.next()
                 && checkQuery.value(0).toString().compare(QStringLiteral("ok"), Qt::CaseInsensitive) == 0
                 && ok;
            qInfo() << "SELFTEST integrity" << (ok ? "OK" : "FAIL");
        }
        {
            QTemporaryDir tmp;
            ok = tmp.isValid() && ok;
            if (tmp.isValid()) {
                // 旧库迁移：早期版本（无 task 表 / user_version=0）的库要能一路升到当前
                // schema 版本，已有 meta 数据保留，且看板/列被自动补齐
                const QString legacyPath = tmp.filePath(QStringLiteral("legacy.db"));
                {
                    QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                                    QStringLiteral("selftest_legacy"));
                    legacy.setDatabaseName(legacyPath);
                    ok = legacy.open() && ok;
                    QSqlQuery query(legacy);
                    ok = query.exec(QStringLiteral(
                             "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)")) && ok;
                    ok = query.exec(QStringLiteral(
                             "INSERT INTO meta (key, value) VALUES ('legacy', '1')")) && ok;
                    legacy.close();
                }
                QSqlDatabase::removeDatabase(QStringLiteral("selftest_legacy"));

                Database migrated;
                QString migrateError;
                ok = migrated.open(&migrateError, legacyPath) && ok;
                ok = migrated.metaValue(QStringLiteral("legacy")) == QStringLiteral("1") && ok;
                ok = migrated.setMetaValue(QStringLiteral("after"), QStringLiteral("ok"))
                     && migrated.metaValue(QStringLiteral("after")) == QStringLiteral("ok") && ok;
                // schema v2：默认板与三列必须补齐（老库没有 board 表）
                ok = migrated.defaultBoardId() == QStringLiteral("board-default") && ok;
                ok = migrated.boardColumns(migrated.defaultBoardId()).size() == 3 && ok;
                qInfo() << "SELFTEST migrate" << (ok ? "OK" : "FAIL") << migrateError;
            }
        }
        {
            QTemporaryDir tmp;
            ok = tmp.isValid() && ok;
            if (tmp.isValid()) {
                const QString badPath = tmp.filePath(QStringLiteral("bad.db"));
                QFile bad(badPath);
                ok = bad.open(QIODevice::WriteOnly) && ok;
                bad.write(QByteArray(4096, '\x5A'));
                bad.close();

                Database recovered;
                QString recoverError;
                ok = recovered.open(&recoverError, badPath) && ok;
                ok = recovered.recoveredFromCorruption() && ok;
                ok = QFileInfo::exists(recovered.recoveryBackup()) && ok;
                ok = !recovered.recoveryReason().isEmpty() && ok;
                ok = recovered.setMetaValue(QStringLiteral("probe"), QStringLiteral("1"))
                     && recovered.metaValue(QStringLiteral("probe")) == QStringLiteral("1") && ok;
                qInfo() << "SELFTEST recover" << (ok ? "OK" : "FAIL")
                        << recovered.recoveryBackup() << recoverError;
            }
        }
        qInfo() << "SELFTEST result" << (ok ? "OK" : "FAIL") << "remaining" << taskStore.count();
        return ok ? 0 : 2;
}
