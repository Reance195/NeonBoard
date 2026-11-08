// DX-146：基于真实 qml/components/TaskCard.qml 的交互回归。
//
// 为什么是 C++ harness（而不是 qmltestrunner 纯 QML）：TaskCard.qml 依赖
// projectStore/taskTree/taskExtra/boardSync/taskRuns/Theme/Motion 这些**引擎级
// context property**，QML 侧没有 setContextProperty 的 API，自包含复刻夹具
// （tests/qml/tst_taskcard_actions.qml）又测不到真组件。这里用 QQmlEngine
// 注入 mock context，加载真实文件，再用 QTest 合成点击/双击/拖拽并断言信号。
//
// 所有 mock 门面共用一个实例（6 个 context 名指向同一个 QObject），因为测试
// 只关心事件路由，不关心各门面的返回值差异（Z1）。

#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QColor>
#include <QUrl>
#include <QVariant>

#ifndef NB_QML_TASKCARD_PATH
#define NB_QML_TASKCARD_PATH "qml/components/TaskCard.qml"
#endif

// ---- Theme：颜色/尺寸共用 getter，属性名与 getter 名不必相同 ----
#define NB_COLOR_PROP(n) Q_PROPERTY(QColor n READ color CONSTANT)
#define NB_NUM_PROP(n)   Q_PROPERTY(qreal n READ number CONSTANT)

class ThemeMock : public QObject
{
    Q_OBJECT
    NB_COLOR_PROP(violet)
    NB_COLOR_PROP(cyan)
    NB_COLOR_PROP(danger)
    NB_COLOR_PROP(magenta)
    NB_COLOR_PROP(success)
    NB_COLOR_PROP(muted)
    NB_COLOR_PROP(text)
    NB_COLOR_PROP(card)
    NB_COLOR_PROP(cardHover)
    NB_COLOR_PROP(border)
    NB_COLOR_PROP(borderStrong)
    NB_COLOR_PROP(accent)
    NB_COLOR_PROP(accent3)
    NB_COLOR_PROP(glow)
    NB_NUM_PROP(radiusCard)
    NB_NUM_PROP(radiusControl)
    NB_NUM_PROP(spacingSm)
    NB_NUM_PROP(spacingXs)
    NB_NUM_PROP(fontTitle)
    NB_NUM_PROP(fontCaption)
    NB_NUM_PROP(fontMicro)
public:
    QColor color() const { return QColor(128, 128, 128); }
    qreal number() const { return 8.0; }
    Q_INVOKABLE QColor alpha(const QColor &c, qreal a) const
    {
        QColor r(c);
        r.setAlphaF(a);
        return r;
    }
};

class MotionMock : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int fast READ number CONSTANT)
    Q_PROPERTY(int enter READ number CONSTANT)
public:
    int number() const { return 100; }
};

// 6 个门面共用一个实例：revision 属性 + 全部 Q_INVOKABLE 方法
class FacadeMock : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision CONSTANT)
public:
    int revision() const { return 0; }
    Q_INVOKABLE QVariant taskProject(const QString &) { return QVariantMap{}; }
    Q_INVOKABLE QString parentTitle(const QString &) { return QString(); }
    Q_INVOKABLE QVariant childStats(const QString &)
    { return QVariantMap{{QStringLiteral("total"), 0}}; }
    Q_INVOKABLE QVariant extraOf(const QString &)
    { return QVariantMap{{QStringLiteral("rrule"), QString()}}; }
    Q_INVOKABLE QVariant claimForTask(const QString &) { return QVariantMap{}; }
    Q_INVOKABLE QVariant latestRun(const QString &) { return QVariantMap{}; }
    Q_INVOKABLE QVariant costSummary(const QString &) { return QVariantMap{}; }
};

class tst_TaskCardReal : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void loadsRealComponent();
    void doubleClickRequestsEdit();
    void clickRequestsSelection();
    void dragRoutesSignals();

private:
    ThemeMock m_theme;
    MotionMock m_motion;
    FacadeMock m_facade;
    QQmlEngine m_engine;
    QQuickWindow m_window;
    QQuickItem *m_card = nullptr;
};

void tst_TaskCardReal::initTestCase()
{
    auto *ctx = m_engine.rootContext();
    ctx->setContextProperty(QStringLiteral("Theme"), &m_theme);
    ctx->setContextProperty(QStringLiteral("Motion"), &m_motion);
    for (const char *name : {"projectStore", "taskTree", "taskExtra",
                             "boardSync", "taskRuns"}) {
        ctx->setContextProperty(QString::fromLatin1(name), &m_facade);
    }

    const QString path = QStringLiteral(NB_QML_TASKCARD_PATH);
    QVERIFY2(QFile::exists(path), qPrintable("找不到真实组件：" + path));

    QQmlComponent comp(&m_engine, QUrl::fromLocalFile(path));
    if (comp.isError()) {
        qWarning() << "QML 组件错误：" << comp.errorString();
    }
    QObject *obj = comp.create();
    QVERIFY2(obj, qPrintable("真实 TaskCard.qml 创建失败：" + comp.errorString()));
    m_card = qobject_cast<QQuickItem *>(obj);
    QVERIFY(m_card);
    m_card->setWidth(320);
    m_card->setHeight(82);

    m_window.resize(400, 200);
    m_card->setParentItem(m_window.contentItem());
    // dragSpace 为 null 时 mapToItem(null, ...) 会抛错，拖拽测试需要有效 Item
    m_card->setProperty("dragSpace",
                        QVariant::fromValue<QObject *>(m_window.contentItem()));
    m_window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&m_window));
    QVERIFY(m_card->window());
}

void tst_TaskCardReal::loadsRealComponent()
{
    QVERIFY(m_card);
    QCOMPARE(m_card->objectName(), QString());
    // 真实组件的属性都在（自包含夹具没有这些）
    QVERIFY(m_card->property("taskId").isValid());
    QVERIFY(m_card->property("title").isValid());
    QVERIFY(m_card->property("taskStatus").isValid());
    QVERIFY(m_card->property("dragSpace").isValid());
}

void tst_TaskCardReal::doubleClickRequestsEdit()
{
    QSignalSpy spy(m_card, SIGNAL(editRequested()));
    QVERIFY(spy.isValid());
    const QPoint pos(50, 30);
    QTest::mouseDClick(&m_window, Qt::LeftButton, Qt::NoModifier, pos);
    QCOMPARE(spy.count(), 1);
}

void tst_TaskCardReal::clickRequestsSelection()
{
    QSignalSpy spy(m_card, SIGNAL(selectionRequested()));
    QVERIFY(spy.isValid());
    const QPoint pos(50, 30);
    QTest::mouseClick(&m_window, Qt::LeftButton, Qt::NoModifier, pos);
    QCOMPARE(spy.count(), 1);
}

void tst_TaskCardReal::dragRoutesSignals()
{
    QSignalSpy started(m_card, SIGNAL(dragStarted(QString,QString,QString,qreal,qreal)));
    QSignalSpy moved(m_card, SIGNAL(dragMoved(qreal,qreal)));
    QSignalSpy ended(m_card, SIGNAL(dragEnded(qreal,qreal)));
    QVERIFY(started.isValid());
    QVERIFY(moved.isValid());
    QVERIFY(ended.isValid());

    const QPoint from(50, 30);
    const QPoint to(90, 60);
    QTest::mousePress(&m_window, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&m_window, to);
    QTest::mouseRelease(&m_window, Qt::LeftButton, Qt::NoModifier, to);

    QCOMPARE(started.count(), 1);
    QVERIFY(moved.count() >= 1);
    QCOMPARE(ended.count(), 1);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    tst_TaskCardReal tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_taskcard_real.moc"
