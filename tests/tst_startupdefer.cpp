// R0-13：Database 两阶段打开的竞态面。
// 覆盖：openDeferred 后的空态、finalizeSchema 的就绪信号、重复 finalize 幂等、
// 老库（v1）在两阶段打开后照常迁移与读取。
#include "Database.h"

#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

class TestStartupDefer : public QObject
{
    Q_OBJECT

private slots:
    void deferredIsEmptyUntilFinalized();
    void finalizeIsIdempotent();
    void legacyDatabaseSurvivesDeferredOpen();
};

static bool taskTableReadable(Database *db)
{
    QSqlDatabase conn = QSqlDatabase::database(db->connectionName());
    QSqlQuery query(conn);
    return query.exec(QStringLiteral("SELECT count(*) FROM task"));
}

void TestStartupDefer::deferredIsEmptyUntilFinalized()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    QVERIFY2(db.openDeferred(&error, tmp.filePath(QStringLiteral("board.db"))),
             qPrintable(error));
    QVERIFY(!db.schemaReady());
    QVERIFY(!taskTableReadable(&db));   // 空态：表尚未建立

    QSignalSpy spy(&db, &Database::schemaFinalized);
    QVERIFY2(db.finalizeSchema(&error), qPrintable(error));
    QVERIFY(db.schemaReady());
    QCOMPARE(spy.count(), 1);
    QVERIFY(taskTableReadable(&db));    // 就绪：任务表可读写
}

void TestStartupDefer::finalizeIsIdempotent()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    QVERIFY2(db.openDeferred(&error, tmp.filePath(QStringLiteral("board.db"))),
             qPrintable(error));
    QVERIFY2(db.finalizeSchema(&error), qPrintable(error));
    QVERIFY2(db.finalizeSchema(&error), qPrintable(error));  // 迁移幂等
    QVERIFY(db.schemaReady());
    QVERIFY(taskTableReadable(&db));
}

void TestStartupDefer::legacyDatabaseSurvivesDeferredOpen()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("legacy.db"));
    {
        QSqlDatabase legacy = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("tst_startupdefer_legacy"));
        legacy.setDatabaseName(path);
        QVERIFY(legacy.open());
        QSqlQuery query(legacy);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO meta (key, value) VALUES ('legacy', '1')")));
        legacy.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("tst_startupdefer_legacy"));

    Database db;
    QString error;
    QVERIFY2(db.openDeferred(&error, path), qPrintable(error));   // 老库照常打开
    QVERIFY2(db.finalizeSchema(&error), qPrintable(error));       // 迁移延后执行
    QVERIFY(db.schemaReady());
    QCOMPARE(db.metaValue(QStringLiteral("legacy")), QStringLiteral("1"));
    QCOMPARE(db.defaultBoardId(), QStringLiteral("board-default"));
    QVERIFY(taskTableReadable(&db));
}

QTEST_MAIN(TestStartupDefer)
#include "tst_startupdefer.moc"
