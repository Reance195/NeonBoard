// R8-01：外部主题包的解析与扫描。
//
// 主题包是一堆用户会互相传的文件，验收标准不是「能解析对的」，而是
// **「坏的那几个不许把好的拖下水」**：坏 JSON / 缺令牌 / 非法色值三种包
// 都要被逐个跳过并给出原因，其余包照常加载、应用照常启动。

#include "ThemePack.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

class TestThemePack : public QObject
{
    Q_OBJECT

private slots:
    void parsesMinimalPack();
    void acceptsThreeDigitHex();
    void rejectsBrokenJson();
    void rejectsMissingToken();
    void rejectsInvalidColor();
    void rejectsBadIdentity();
    void loadsDirectoryAndSkipsBadOnes();
    void missingDirectoryIsNotAnError();
    void duplicateKeyLastFileWins();
    void reloadPicksUpNewFiles();
    void ensureDirCreatesIt();

private:
    static QByteArray packJson(const QString &id, const QString &name = QStringLiteral("极光"));
    static void writeFile(const QString &dir, const QString &name, const QByteArray &data);
};

QByteArray TestThemePack::packJson(const QString &id, const QString &name)
{
    QJsonObject tokens;
    tokens.insert(QStringLiteral("bg"), QStringLiteral("#0A0F1E"));
    tokens.insert(QStringLiteral("text"), QStringLiteral("#EAF2FF"));
    tokens.insert(QStringLiteral("muted"), QStringLiteral("#7C8BA1"));
    tokens.insert(QStringLiteral("cyan"), QStringLiteral("#5EEAD4"));
    tokens.insert(QStringLiteral("magenta"), QStringLiteral("#F472B6"));
    tokens.insert(QStringLiteral("violet"), QStringLiteral("#A78BFA"));
    tokens.insert(QStringLiteral("success"), QStringLiteral("#34D399"));
    tokens.insert(QStringLiteral("warning"), QStringLiteral("#FBBF24"));
    tokens.insert(QStringLiteral("danger"), QStringLiteral("#FB7185"));

    QJsonObject root;
    root.insert(QStringLiteral("id"), id);
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("desc"), QStringLiteral("  测试用  "));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("tokens"), tokens);
    return QJsonDocument(root).toJson();
}

void TestThemePack::writeFile(const QString &dir, const QString &name, const QByteArray &data)
{
    QFile file(QDir(dir).filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
    file.close();
}

void TestThemePack::parsesMinimalPack()
{
    QVariantMap pack;
    QString reason;
    QVERIFY2(ThemePack::parsePack(packJson(QStringLiteral("aurora")), &pack, &reason),
             qPrintable(reason));
    QCOMPARE(pack.value(QStringLiteral("key")).toString(), QStringLiteral("aurora"));
    QCOMPARE(pack.value(QStringLiteral("name")).toString(), QStringLiteral("极光"));
    // desc 去空白：面板里那行小字不该带用户误敲的空格
    QCOMPARE(pack.value(QStringLiteral("desc")).toString(), QStringLiteral("测试用"));
    QCOMPARE(pack.value(QStringLiteral("cyan")).toString(), QStringLiteral("#5eead4")); // 统一小写
    QCOMPARE(pack.value(QStringLiteral("source")).toString(), QStringLiteral("pack"));
}

void TestThemePack::acceptsThreeDigitHex()
{
    QByteArray data = packJson(QStringLiteral("short"));
    data.replace("#5EEAD4", "#5ED");
    QVariantMap pack;
    QString reason;
    QVERIFY2(ThemePack::parsePack(data, &pack, &reason), qPrintable(reason));
    QCOMPARE(pack.value(QStringLiteral("cyan")).toString(), QStringLiteral("#5ed"));
}

void TestThemePack::rejectsBrokenJson()
{
    QVariantMap pack;
    QString reason;
    QVERIFY(!ThemePack::parsePack("{\"id\": \"x\", ", &pack, &reason));
    QVERIFY2(reason.contains(QStringLiteral("JSON")), qPrintable(reason));

    // 顶层必须是对象：数组 / 裸字符串都不算主题包
    QVERIFY(!ThemePack::parsePack("[]", &pack, &reason));
    QVERIFY2(reason.contains(QStringLiteral("对象")), qPrintable(reason));
}

void TestThemePack::rejectsMissingToken()
{
    QByteArray data = packJson(QStringLiteral("incomplete"));
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    QJsonObject tokens = root.value(QStringLiteral("tokens")).toObject();
    tokens.remove(QStringLiteral("success"));
    root.insert(QStringLiteral("tokens"), tokens);
    data = QJsonDocument(root).toJson();

    QVariantMap pack;
    QString reason;
    QVERIFY(!ThemePack::parsePack(data, &pack, &reason));
    QVERIFY2(reason.contains(QStringLiteral("success")), qPrintable(reason));
}

void TestThemePack::rejectsInvalidColor()
{
    const QStringList bad = { QStringLiteral("red"), QStringLiteral("#12345"),
                              QStringLiteral("#GGGGGG"), QStringLiteral("rgb(1,2,3)"),
                              QString() };
    for (const QString &color : bad) {
        QByteArray data = packJson(QStringLiteral("badtone"));
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();
        QJsonObject tokens = root.value(QStringLiteral("tokens")).toObject();
        tokens.insert(QStringLiteral("bg"), color);
        root.insert(QStringLiteral("tokens"), tokens);
        data = QJsonDocument(root).toJson();

        QVariantMap pack;
        QString reason;
        QVERIFY2(!ThemePack::parsePack(data, &pack, &reason), qPrintable(color));
        QVERIFY2(reason.contains(QStringLiteral("bg")), qPrintable(reason));
    }
}

void TestThemePack::rejectsBadIdentity()
{
    QVariantMap pack;
    QString reason;
    // id 会被写进 QSettings：中文 / 大写 / 空格都不收
    QVERIFY(!ThemePack::parsePack(packJson(QStringLiteral("极光")), &pack, &reason));
    QVERIFY(!ThemePack::parsePack(packJson(QStringLiteral("Aurora")), &pack, &reason));
    QVERIFY(!ThemePack::parsePack(packJson(QStringLiteral("a b")), &pack, &reason));
    QVERIFY(!ThemePack::parsePack(packJson(QString()), &pack, &reason));
    // 没有 name 就没法在面板里选
    QVERIFY(!ThemePack::parsePack(packJson(QStringLiteral("noname"), QString()), &pack, &reason));
}

void TestThemePack::loadsDirectoryAndSkipsBadOnes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    writeFile(dir.path(), QStringLiteral("a-good.json"), packJson(QStringLiteral("aurora")));
    writeFile(dir.path(), QStringLiteral("b-broken.json"), "{ not json");
    writeFile(dir.path(), QStringLiteral("c-missing.json"),
              QByteArray("{\"id\":\"c\",\"name\":\"C\",\"tokens\":{}}"));
    writeFile(dir.path(), QStringLiteral("d-badcolor.json"),
              QByteArray(packJson(QStringLiteral("d")).replace("#5EEAD4", "#ZZZZZZ")));
    // 非 json 文件不进扫描范围
    writeFile(dir.path(), QStringLiteral("notes.txt"), "随便写点");

    ThemePack packs(dir.path());
    QCOMPARE(packs.count(), 1);
    QCOMPARE(packs.skippedCount(), 3);
    QCOMPARE(packs.packs().first().toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("aurora"));

    // 每个被跳过的包都要有文件名与原因，否则界面只能说「有几个没装上」
    for (const QVariant &item : packs.skipped()) {
        const QVariantMap info = item.toMap();
        QVERIFY(!info.value(QStringLiteral("file")).toString().isEmpty());
        QVERIFY(!info.value(QStringLiteral("reason")).toString().isEmpty());
    }
}

void TestThemePack::missingDirectoryIsNotAnError()
{
    // 从没装过主题包的用户：目录不存在，0 个包，也不该有「错误」记录
    ThemePack packs(QStringLiteral("Z:/definitely-not-here/themes"));
    QCOMPARE(packs.count(), 0);
    QCOMPARE(packs.skippedCount(), 0);
}

void TestThemePack::duplicateKeyLastFileWins()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path(), QStringLiteral("a.json"), packJson(QStringLiteral("same"),
                                                             QStringLiteral("先装的")));
    writeFile(dir.path(), QStringLiteral("b.json"), packJson(QStringLiteral("same"),
                                                             QStringLiteral("后装的")));

    ThemePack packs(dir.path());
    // 同一批里不许出现两份同 key：面板上两个「同名主题」谁是谁说不清
    QCOMPARE(packs.count(), 1);
    QCOMPARE(packs.packs().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("后装的"));
}

void TestThemePack::reloadPicksUpNewFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path(), QStringLiteral("a.json"), packJson(QStringLiteral("aurora")));

    ThemePack packs(dir.path());
    QCOMPARE(packs.count(), 1);

    // 用户点「重新扫描」：新丢进去的包要能出现，不需要重启
    writeFile(dir.path(), QStringLiteral("b.json"), packJson(QStringLiteral("ember")));
    packs.reload();
    QCOMPARE(packs.count(), 2);

    // 删掉一个再扫：少一个
    QVERIFY(QFile::remove(QDir(dir.path()).filePath(QStringLiteral("a.json"))));
    packs.reload();
    QCOMPARE(packs.count(), 1);
    QCOMPARE(packs.packs().first().toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("ember"));
}

void TestThemePack::ensureDirCreatesIt()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = QDir(dir.path()).filePath(QStringLiteral("themes"));
    ThemePack packs(target);
    QVERIFY(!QDir(target).exists());
    QCOMPARE(packs.ensureDir(), target);
    QVERIFY(QDir(target).exists());
}

QTEST_GUILESS_MAIN(TestThemePack)

#include "tst_themepack.moc"
