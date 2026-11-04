#include <QtTest>

#include "MenuSkin.h"

// R17-01：菜单皮肤令牌的护栏测试。
//
// 托盘右键菜单有两处实现 —— 宿主进程的自绘弹窗与主程序的 QMenu —— 两者共用
// MenuSkin 这一份令牌。分叉的症状是「同一个托盘、两种皮肤」，肉眼很难稳定复现，
// 所以把「同一预设两边取到同一套颜色」「未知预设有确定回退」钉成断言。
class TestMenuSkin : public QObject
{
    Q_OBJECT

private slots:
    void knownPresetsHavePalette();
    void unknownPresetFallsBackToNeon();
    void styleSheetCarriesSkin();
    void rgbaAndArgbAgree();
};

void TestMenuSkin::knownPresetsHavePalette()
{
    const QStringList keys = { QStringLiteral("neon"),  QStringLiteral("violet"),
                               QStringLiteral("sunset"), QStringLiteral("abyss"),
                               QStringLiteral("forest"), QStringLiteral("mono") };
    QSet<QString> accents;
    for (const QString &key : keys) {
        const MenuSkin::Palette palette = MenuSkin::paletteForPreset(key);
        QVERIFY2(!palette.bg.isEmpty(), qPrintable(key + " 缺底色"));
        QVERIFY2(!palette.text.isEmpty(), qPrintable(key + " 缺文字色"));
        QVERIFY2(!palette.accent.isEmpty(), qPrintable(key + " 缺霓虹主色"));
        QVERIFY2(palette.accent.startsWith(QLatin1Char('#')), qPrintable(key + " 主色格式不对"));
        QVERIFY2(palette.muted != palette.text, qPrintable(key + " 禁用色与文字色不能同色"));
        accents.insert(palette.accent);
    }
    // 六套预设的霓虹主色必须彼此不同：撞车就等于「换主题在菜单上看不出来」
    QCOMPARE(accents.size(), keys.size());
}

void TestMenuSkin::unknownPresetFallsBackToNeon()
{
    // 外部主题包可以自定义看板配色，但托盘菜单是系统级 UI，不跟着主题包走：
    // 认不出的 key 必须有确定回退，而不是「随机取一个」。
    const MenuSkin::Palette fallback = MenuSkin::paletteForPreset(QStringLiteral("no-such-preset"));
    const MenuSkin::Palette neon = MenuSkin::paletteForPreset(QStringLiteral("neon"));
    QCOMPARE(fallback.accent, neon.accent);
    QCOMPARE(fallback.bg, neon.bg);
    QCOMPARE(fallback.text, neon.text);
}

void TestMenuSkin::styleSheetCarriesSkin()
{
    const MenuSkin::Palette palette = MenuSkin::paletteForPreset(QStringLiteral("neon"));
    const MenuSkin::Metrics metrics = MenuSkin::metrics();
    const QString css = MenuSkin::styleSheet(palette, metrics);
    QVERIFY(css.contains(QStringLiteral("QMenu")));
    QVERIFY(css.contains(QStringLiteral("border-radius")));
    QVERIFY(css.contains(QStringLiteral("QMenu::separator")));
    // 描边与悬停底色必须来自调色板，不能是写死的颜色
    QVERIFY(css.contains(MenuSkin::rgba(palette.accent, 0.38)));
    QVERIFY(css.contains(MenuSkin::rgba(palette.accent, 0.16)));
    QVERIFY(css.contains(QString::number(metrics.radius)));
    QVERIFY(css.contains(QString::number(metrics.itemHeight / 4)));
}

void TestMenuSkin::rgbaAndArgbAgree()
{
    const QString hex = QStringLiteral("#22D3EE");
    QCOMPARE(MenuSkin::rgba(hex, 1.0), QStringLiteral("rgba(34,211,238,1)"));
    QCOMPARE(MenuSkin::rgba(hex, 0.0), QStringLiteral("rgba(34,211,238,0)"));
    // GDI+ 的 ARGB：0xAARRGGBB
    QCOMPARE(MenuSkin::argb(hex, 255), 0xFF22D3EEu);
    QCOMPARE(MenuSkin::argb(hex, 0), 0x0022D3EEu);
    // 越界要夹住，别把 alpha 溢出到相邻通道
    QCOMPARE(MenuSkin::argb(hex, 300), 0xFF22D3EEu);
    QCOMPARE(MenuSkin::argb(hex, -5), 0x0022D3EEu);
}

QTEST_GUILESS_MAIN(TestMenuSkin)

#include "tst_menuskint.moc"
