#include "PluginPackage.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

namespace {

QJsonObject demoManifest()
{
    QJsonObject manifest;
    manifest.insert(QStringLiteral("id"), QStringLiteral("com.test.demo"));
    manifest.insert(QStringLiteral("name"), QStringLiteral("演示插件"));
    manifest.insert(QStringLiteral("apiVersion"), 2);
    return manifest;
}

QJsonObject packageWithFiles(const QJsonObject &files)
{
    QJsonObject root;
    root.insert(QStringLiteral("manifest"), demoManifest());
    root.insert(QStringLiteral("files"), files);
    return root;
}

// 把给定 JSON 写成 .neon 包，返回文件路径
QString writePackage(const QString &dirPath, const QString &fileName, const QJsonObject &root)
{
    const QString path = QDir(dirPath).filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    return path;
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

// 写入任意原始字节，用于构造非法包
QString writeRaw(const QString &dirPath, const QString &fileName, const QByteArray &raw)
{
    const QString path = QDir(dirPath).filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    file.write(raw);
    file.close();
    return path;
}

} // namespace

class TestPluginPackage : public QObject
{
    Q_OBJECT

private slots:
    void dirNameForId_data();
    void dirNameForId();

    void neonPluginIdReadsManifest();

    void validateAcceptsWellFormedPackage();
    void validateRejectsBrokenPackages_data();
    void validateRejectsBrokenPackages();

    void installWritesManifestAndFiles();
    void installRejectsPathTraversal();
    void reinstallReplacesPreviousContent();
};

void TestPluginPackage::dirNameForId_data()
{
    QTest::addColumn<QString>("pluginId");
    QTest::addColumn<QString>("expected");

    QTest::newRow("三段 id") << QStringLiteral("com.lin.plan-hook")
                             << QStringLiteral("plan-hook");
    QTest::newRow("普通 id") << QStringLiteral("com.test.demo") << QStringLiteral("demo");
    QTest::newRow("无点号") << QStringLiteral("plain") << QStringLiteral("plain");
    // 末尾点号后为空：按实现保留原串，避免产生空目录名
    QTest::newRow("末尾点号") << QStringLiteral("com.example.") << QStringLiteral("com.example.");
    QTest::newRow("空串") << QString() << QString();
}

void TestPluginPackage::dirNameForId()
{
    QFETCH(QString, pluginId);
    QFETCH(QString, expected);

    QCOMPARE(PluginPackage::dirNameForId(pluginId), expected);
}

void TestPluginPackage::neonPluginIdReadsManifest()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = writePackage(
        dir.path(), QStringLiteral("demo.neon"),
        packageWithFiles(QJsonObject{{QStringLiteral("main.qml"), QStringLiteral("Item {}")}}));
    QVERIFY(!path.isEmpty());

    QCOMPARE(PluginPackage::neonPluginId(path), QStringLiteral("com.test.demo"));
    // 不存在的包读出空串，不抛异常
    QCOMPARE(PluginPackage::neonPluginId(QDir(dir.path()).filePath(QStringLiteral("nope.neon"))),
             QString());
}

void TestPluginPackage::validateAcceptsWellFormedPackage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = writePackage(
        dir.path(), QStringLiteral("demo.neon"),
        packageWithFiles(QJsonObject{{QStringLiteral("main.qml"), QStringLiteral("Item {}")}}));

    QString error;
    QVERIFY2(PluginPackage::validateNeonFile(path, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
}

void TestPluginPackage::validateRejectsBrokenPackages_data()
{
    QTest::addColumn<QByteArray>("raw");

    QTest::newRow("非法 JSON") << QByteArray("{oops");
    QTest::newRow("JSON 非对象") << QByteArray("[]");
    QTest::newRow("缺 manifest") << QByteArray("{\"files\":{}}");
    QTest::newRow("缺 files") << QByteArray("{\"manifest\":{\"id\":\"com.test.demo\"}}");
    QTest::newRow("id 为空") << QByteArray("{\"manifest\":{\"id\":\"  \"},\"files\":{}}");
}

void TestPluginPackage::validateRejectsBrokenPackages()
{
    QFETCH(QByteArray, raw);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = writeRaw(dir.path(), QStringLiteral("broken.neon"), raw);
    QVERIFY(!path.isEmpty());

    QString error;
    QVERIFY(!PluginPackage::validateNeonFile(path, &error));
    // 失败必须给出可读原因，否则上层无法定位坏包
    QVERIFY(!error.isEmpty());
}

void TestPluginPackage::installWritesManifestAndFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString mainQml = QStringLiteral("import QtQuick\nItem {}\n");
    const QString path = writePackage(
        dir.path(), QStringLiteral("demo.neon"),
        packageWithFiles(QJsonObject{{QStringLiteral("main.qml"), mainQml}}));
    QVERIFY(!path.isEmpty());

    const QString target = QDir(dir.path()).filePath(QStringLiteral("plugins/demo"));
    QString error;
    QVERIFY2(PluginPackage::installNeonFile(path, target, &error), qPrintable(error));

    // manifest 落成 plugin.json，且 id 可被再读出来
    const QString manifestPath = QDir(target).filePath(QStringLiteral("plugin.json"));
    QVERIFY(QFile::exists(manifestPath));
    const QJsonDocument manifest = QJsonDocument::fromJson(readText(manifestPath).toUtf8());
    QVERIFY(manifest.isObject());
    QCOMPARE(manifest.object().value(QStringLiteral("id")).toString(),
             QStringLiteral("com.test.demo"));

    // files 按原样落盘（引入 UTF-8 断言，防止编码被改写）
    QCOMPARE(readText(QDir(target).filePath(QStringLiteral("main.qml"))), mainQml);

    // 安装完成后不残留备份目录
    QVERIFY(!QDir(target + QStringLiteral(".backup")).exists());
}

void TestPluginPackage::installRejectsPathTraversal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = writePackage(
        dir.path(), QStringLiteral("evil.neon"),
        packageWithFiles(
            QJsonObject{{QStringLiteral("../evil.txt"), QStringLiteral("pwned")}}));
    QVERIFY(!path.isEmpty());

    const QString target = QDir(dir.path()).filePath(QStringLiteral("plugins/evil"));
    QString error;
    QVERIFY(!PluginPackage::installNeonFile(path, target, &error));
    QVERIFY(error.contains(QStringLiteral("非法路径")));

    // 关键安全断言：越界文件绝不能落到目标目录之外
    QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QStringLiteral("plugins/evil.txt"))));
    QVERIFY(!QDir(target).exists());
}

void TestPluginPackage::reinstallReplacesPreviousContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString target = QDir(dir.path()).filePath(QStringLiteral("plugins/demo"));

    const QString v1 = writePackage(
        dir.path(), QStringLiteral("v1.neon"),
        packageWithFiles(QJsonObject{{QStringLiteral("main.qml"), QStringLiteral("A")}}));
    const QString v2 = writePackage(
        dir.path(), QStringLiteral("v2.neon"),
        packageWithFiles(QJsonObject{{QStringLiteral("main.qml"), QStringLiteral("B")}}));
    QVERIFY(!v1.isEmpty());
    QVERIFY(!v2.isEmpty());

    QString error;
    QVERIFY2(PluginPackage::installNeonFile(v1, target, &error), qPrintable(error));
    QCOMPARE(readText(QDir(target).filePath(QStringLiteral("main.qml"))), QStringLiteral("A"));

    // 覆盖安装：内容必须整体替换为 v2，不能残留旧文件
    error.clear();
    QVERIFY2(PluginPackage::installNeonFile(v2, target, &error), qPrintable(error));
    QCOMPARE(readText(QDir(target).filePath(QStringLiteral("main.qml"))), QStringLiteral("B"));
    QVERIFY(!QDir(target + QStringLiteral(".backup")).exists());
}

QTEST_GUILESS_MAIN(TestPluginPackage)

#include "tst_pluginpackage.moc"
