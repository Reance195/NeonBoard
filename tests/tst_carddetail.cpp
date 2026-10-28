// R8-03：卡片详情抽屉的后端契约。
//
// 详情面板唯一值得担心的数据风险不是「改错了」，而是「没改却写了一次库」——
// 点开详情按 Esc、来回切预览/编辑、重复点保存，都不该产生写入，也不该污染撤销栈。
// 这里用真实 sqlite 临时库验证，不 mock：到底有没有写库，要看 TaskModel 的内存副本
// 与 updated_at 有没有动。

#include "CardDetail.h"
#include "Database.h"
#include "TaskExtra.h"
#include "TaskModel.h"
#include "TaskUndo.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestCardDetail : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void identicalNoteIsNotDirty();
    void lineEndingAndTrailingSpaceAreIgnored();
    void changedNoteIsDirty();
    void saveWithoutChangeDoesNotWrite();
    void savePersistsNoteOnly();
    void saveThenUndoRestoresOriginal();
    void unknownCardSavesNothing();

    // 全字段补丁（R8-03 的就地编辑）
    void patchWithSameValuesDoesNotWrite();
    void patchWritesOnlyChangedFields();
    void patchTouchesReminderAndRrule();
    void patchRefusesEmptyTitle();
    void patchTagsCompareAsListNotAsText();
    void dueAndReminderCompareAtUiGranularity();

private:
    // 建一张带完整字段的卡，返回 id
    QString seedCard(const QString &title);

    QTemporaryDir m_dir;
    Database *m_db = nullptr;
    TaskModel *m_model = nullptr;
    TaskExtra *m_extra = nullptr;
    TaskUndo *m_undo = nullptr;
    CardDetail *m_detail = nullptr;
};

void TestCardDetail::init()
{
    QVERIFY(m_dir.isValid());
    const QString path = m_dir.filePath(QString::fromLatin1(QTest::currentTestFunction())
                                        + QStringLiteral(".db"));
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));

    m_db = new Database;
    QString error;
    QVERIFY2(m_db->open(&error, path), qPrintable(error));
    m_model = new TaskModel(m_db);
    m_model->load();
    m_extra = new TaskExtra(m_db, m_model);
    m_undo = new TaskUndo(m_model);
    m_undo->setTaskExtra(m_extra);
    m_detail = new CardDetail(m_model, m_extra, m_undo);
}

void TestCardDetail::cleanup()
{
    delete m_detail;
    m_detail = nullptr;
    delete m_undo;
    m_undo = nullptr;
    delete m_extra;
    m_extra = nullptr;
    delete m_model;
    m_model = nullptr;
    delete m_db;
    m_db = nullptr;
}

QString TestCardDetail::seedCard(const QString &title)
{
    return m_model->addTask(title, QStringLiteral("doing"), 3,
                            QStringLiteral("R8"), 1893456000);
}

void TestCardDetail::identicalNoteIsNotDirty()
{
    QVERIFY(!m_detail->isNoteDirty(QStringLiteral("同一段备注"),
                                   QStringLiteral("同一段备注")));
    QVERIFY(!m_detail->isNoteDirty(QString(), QString()));
}

void TestCardDetail::lineEndingAndTrailingSpaceAreIgnored()
{
    // 换个系统编辑备注不该变成「有未保存改动」
    QVERIFY(!m_detail->isNoteDirty(QStringLiteral("第一行\n第二行"),
                                   QStringLiteral("第一行\r\n第二行")));
    // 末尾多敲几个空行同理
    QVERIFY(!m_detail->isNoteDirty(QStringLiteral("正文"),
                                   QStringLiteral("正文\n\n  \n")));
}

void TestCardDetail::changedNoteIsDirty()
{
    QVERIFY(m_detail->isNoteDirty(QStringLiteral("旧文案"),
                                  QStringLiteral("新文案")));
    QVERIFY(m_detail->isNoteDirty(QString(), QStringLiteral("写了点东西")));
}

void TestCardDetail::saveWithoutChangeDoesNotWrite()
{
    const QString id = seedCard(QStringLiteral("写周报"));
    QVERIFY(!id.isEmpty());

    const QString original = QStringLiteral("- [x] 数据\n- [ ] 结论");
    QVERIFY(m_detail->saveNote(id, QString(), original));
    m_undo->clear();

    const QVariantMap before = m_model->taskById(id);
    QCOMPARE(before.value(QStringLiteral("note")).toString(), original);

    // 抽屉再开关一次、点了保存但内容没变：不该写库，也不该进撤销栈
    QVERIFY(!m_detail->saveNote(id, original, original));
    QVERIFY(!m_undo->canUndo());

    const QVariantMap after = m_model->taskById(id);
    QCOMPARE(after.value(QStringLiteral("note")).toString(), original);
    QCOMPARE(after.value(QStringLiteral("updatedAt")).toLongLong(),
             before.value(QStringLiteral("updatedAt")).toLongLong());
}

void TestCardDetail::savePersistsNoteOnly()
{
    const QString id = seedCard(QStringLiteral("看板详情乱码排查"));
    QVERIFY(!id.isEmpty());
    const QVariantMap before = m_model->taskById(id);

    const QString note = QStringLiteral("# 结论\n\n先看脏检查，再看渲染。");
    QVERIFY(m_detail->saveNote(id, before.value(QStringLiteral("note")).toString(), note));

    const QVariantMap after = m_model->taskById(id);
    QCOMPARE(after.value(QStringLiteral("note")).toString(), note);
    // 详情面板只发 note 这一个字段：其余字段不能被补丁吃掉
    QCOMPARE(after.value(QStringLiteral("title")).toString(), QStringLiteral("看板详情乱码排查"));
    QCOMPARE(after.value(QStringLiteral("priority")).toInt(), 3);
    QCOMPARE(after.value(QStringLiteral("status")).toString(), QStringLiteral("doing"));
    QCOMPARE(after.value(QStringLiteral("tags")).toString(), QStringLiteral("R8"));
    QCOMPARE(after.value(QStringLiteral("dueAt")).toLongLong(),
             before.value(QStringLiteral("dueAt")).toLongLong());
    QCOMPARE(after.value(QStringLiteral("columnId")).toString(),
             before.value(QStringLiteral("columnId")).toString());
    QVERIFY(after.value(QStringLiteral("updatedAt")).toLongLong()
            >= before.value(QStringLiteral("updatedAt")).toLongLong());
}

void TestCardDetail::saveThenUndoRestoresOriginal()
{
    const QString id = seedCard(QStringLiteral("整理素材"));
    QVERIFY(!id.isEmpty());

    const QString original = QStringLiteral("原始备注");
    QVERIFY(m_detail->saveNote(id, QString(), original));
    QVERIFY(m_detail->saveNote(id, original, QStringLiteral("改过一次的备注")));
    QCOMPARE(m_model->taskById(id).value(QStringLiteral("note")).toString(),
             QStringLiteral("改过一次的备注"));

    QVERIFY(m_undo->undo());
    QCOMPARE(m_model->taskById(id).value(QStringLiteral("note")).toString(), original);
}

void TestCardDetail::unknownCardSavesNothing()
{
    QVERIFY(!m_detail->saveNote(QStringLiteral("no-such-card"), QString(),
                                QStringLiteral("写了也白写")));
    QVERIFY(!m_undo->canUndo());
}

void TestCardDetail::patchWithSameValuesDoesNotWrite()
{
    const QString id = seedCard(QStringLiteral("补丁空转"));
    QVERIFY(!id.isEmpty());
    const QVariantMap before = m_model->taskById(id);

    // 把库里现值原样塞回补丁：一个字段都不该判成改动。
    // 注意 tags 故意给 JSON 原文而不是逗号串 —— 比较必须按列表，不按字面量。
    QVariantMap patch;
    patch.insert(QStringLiteral("title"), before.value(QStringLiteral("title")));
    patch.insert(QStringLiteral("priority"), before.value(QStringLiteral("priority")));
    patch.insert(QStringLiteral("tags"), before.value(QStringLiteral("tagsFull")));
    patch.insert(QStringLiteral("dueAt"), before.value(QStringLiteral("dueAt")));
    patch.insert(QStringLiteral("note"), before.value(QStringLiteral("note")));
    patch.insert(QStringLiteral("remindAt"), 0);
    patch.insert(QStringLiteral("rrule"), QString());

    QVERIFY(!m_detail->hasChanges(id, patch));
    QVERIFY(!m_detail->saveFields(id, patch));
    QVERIFY(!m_undo->canUndo());
    QCOMPARE(m_model->taskById(id).value(QStringLiteral("updatedAt")).toLongLong(),
             before.value(QStringLiteral("updatedAt")).toLongLong());
}

void TestCardDetail::patchWritesOnlyChangedFields()
{
    const QString id = seedCard(QStringLiteral("改优先级"));
    QVERIFY(!id.isEmpty());
    const QVariantMap before = m_model->taskById(id);

    QVariantMap patch;
    patch.insert(QStringLiteral("title"), before.value(QStringLiteral("title")));
    patch.insert(QStringLiteral("note"), before.value(QStringLiteral("note")));
    patch.insert(QStringLiteral("tags"), before.value(QStringLiteral("tagsFull")));
    patch.insert(QStringLiteral("dueAt"), before.value(QStringLiteral("dueAt")));
    patch.insert(QStringLiteral("priority"), 1);

    QVERIFY(m_detail->hasChanges(id, patch));
    QVERIFY(m_detail->saveFields(id, patch));

    const QVariantMap after = m_model->taskById(id);
    QCOMPARE(after.value(QStringLiteral("priority")).toInt(), 1);
    // 同一张补丁里没变的字段不能被顺手改掉
    QCOMPARE(after.value(QStringLiteral("title")).toString(),
             before.value(QStringLiteral("title")).toString());
    QCOMPARE(after.value(QStringLiteral("note")).toString(),
             before.value(QStringLiteral("note")).toString());
    QCOMPARE(after.value(QStringLiteral("dueAt")).toLongLong(),
             before.value(QStringLiteral("dueAt")).toLongLong());
    QCOMPARE(after.value(QStringLiteral("status")).toString(),
             before.value(QStringLiteral("status")).toString());
}

void TestCardDetail::patchTouchesReminderAndRrule()
{
    const QString id = seedCard(QStringLiteral("提醒与重复"));
    QVERIFY(!id.isEmpty());

    QVariantMap patch;
    patch.insert(QStringLiteral("remindAt"), 1893500000LL);
    patch.insert(QStringLiteral("rrule"), QStringLiteral("FREQ=WEEKLY;INTERVAL=2"));

    QVERIFY(m_detail->hasChanges(id, patch));
    QVERIFY(m_detail->saveFields(id, patch));

    QVariantMap extra = m_extra->extraOf(id);
    QCOMPARE(extra.value(QStringLiteral("remindAt")).toLongLong(), 1893500000LL);
    QCOMPARE(extra.value(QStringLiteral("rrule")).toString(),
             QStringLiteral("FREQ=WEEKLY;INTERVAL=2"));

    // 同样的值再存一次：零写入
    QVERIFY(!m_detail->hasChanges(id, patch));
    QVERIFY(!m_detail->saveFields(id, patch));

    // 清空重复规则也是改动
    QVariantMap clear;
    clear.insert(QStringLiteral("rrule"), QString());
    QVERIFY(m_detail->saveFields(id, clear));
    QCOMPARE(m_extra->extraOf(id).value(QStringLiteral("rrule")).toString(), QString());

    // 侧表改动不进撤销栈：撤销栈里不该因此多出一条
    QVERIFY(!m_undo->canUndo());
}

void TestCardDetail::patchRefusesEmptyTitle()
{
    const QString id = seedCard(QStringLiteral("标题不能空"));
    QVERIFY(!id.isEmpty());
    const QVariantMap before = m_model->taskById(id);

    // 标题被清空 = 非法补丁：整单拒绝，连带同一补丁里的优先级也不能落库
    // （否则用户只看到「标题没变」却不知道优先级被写了一半）
    QVariantMap patch;
    patch.insert(QStringLiteral("title"), QStringLiteral("   "));
    patch.insert(QStringLiteral("priority"), 0);

    QVERIFY(!m_detail->hasChanges(id, patch));
    QVERIFY(!m_detail->saveFields(id, patch));

    const QVariantMap after = m_model->taskById(id);
    QCOMPARE(after.value(QStringLiteral("title")).toString(),
             before.value(QStringLiteral("title")).toString());
    QCOMPARE(after.value(QStringLiteral("priority")).toInt(),
             before.value(QStringLiteral("priority")).toInt());
    QVERIFY(!m_undo->canUndo());
}

void TestCardDetail::patchTagsCompareAsListNotAsText()
{
    const QString id = seedCard(QStringLiteral("标签形态"));
    QVERIFY(!id.isEmpty());

    // 逗号串（带空格）→ 落库成 JSON 数组
    QVariantMap patch;
    patch.insert(QStringLiteral("tags"), QStringLiteral(" R8 , 详情 "));
    QVERIFY(m_detail->hasChanges(id, patch));
    QVERIFY(m_detail->saveFields(id, patch));

    const QVariantMap after = m_model->taskById(id);
    QCOMPARE(after.value(QStringLiteral("tagsFull")).toString(),
             QStringLiteral("[\"R8\",\"详情\"]"));

    // 同一组标签换个写法（JSON 原文）不该再判成改动
    QVariantMap same;
    same.insert(QStringLiteral("tags"), after.value(QStringLiteral("tagsFull")));
    QVERIFY(!m_detail->hasChanges(id, same));
}

void TestCardDetail::dueAndReminderCompareAtUiGranularity()
{
    const QString id = seedCard(QStringLiteral("同一天不改到期"));
    QVERIFY(!id.isEmpty());

    const qint64 dueAt = m_model->taskById(id).value(QStringLiteral("dueAt")).toLongLong();
    QVERIFY(dueAt > 0);

    // 界面只给到「日」：同一天换个时刻不该算改动，否则一进编辑态就显示「有未保存改动」，
    // 用户只改个标题也会顺带把到期的具体时刻改成 23:59:59。
    QVariantMap patch;
    patch.insert(QStringLiteral("dueAt"), dueAt + 3600);

    // 提醒同理：界面精确到分钟，库里那半分钟的秒差不算改动
    QVERIFY(m_extra->setReminder(id, 1893500030));
    patch.insert(QStringLiteral("remindAt"), 1893500000LL);

    QVERIFY(!m_detail->hasChanges(id, patch));
    QVERIFY(!m_detail->saveFields(id, patch));
}

QTEST_MAIN(TestCardDetail)
#include "tst_carddetail.moc"
