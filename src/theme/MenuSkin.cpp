#include "MenuSkin.h"

#include <QStringList>

namespace {

struct Preset
{
    const char *key;
    const char *bg;
    const char *text;
    const char *muted;
    const char *accent;
    const char *accent2;
    const char *danger;
};

// 与 qml/theme/Theme.qml 的内置预设一一对应（同名 key、同样的主色）。
// 这里只抄菜单用得到的那几个字段 —— 看板的完整令牌仍由 Theme.qml 单源持有。
// 外部主题包（themes/themepack.json）可以自定义看板配色，但托盘菜单是系统级 UI，
// 不跟着主题包走：未知 key 一律退回下面第一套（neon）。
const Preset kPresets[] = {
    { "neon",   "#070B14", "#E6F1FF", "#7C8BA1", "#22D3EE", "#F472B6", "#FB7185" },
    { "violet", "#0B0718", "#EFE9FF", "#8B81A8", "#B794F6", "#FF6FB5", "#FB7185" },
    { "sunset", "#150B06", "#FFEFE2", "#A08070", "#FFB454", "#FF6B6B", "#F87171" },
    { "abyss",  "#04101C", "#E0F2FE", "#6B8399", "#38BDF8", "#2DD4BF", "#FB7185" },
    { "forest", "#05130E", "#E6FFF4", "#6E9384", "#34D399", "#FB7185", "#FB7185" },
    { "mono",   "#0C0F13", "#E5E7EB", "#8A9199", "#9CA3AF", "#D1D5DB", "#EF4444" },
};

bool parseHex(const QString &hex, int *r, int *g, int *b)
{
    QString text = hex.trimmed();
    if (text.startsWith(QLatin1Char('#')))
        text = text.mid(1);
    if (text.size() != 6)
        return false;
    bool ok = false;
    const int value = text.toInt(&ok, 16);
    if (!ok)
        return false;
    *r = (value >> 16) & 0xFF;
    *g = (value >> 8) & 0xFF;
    *b = value & 0xFF;
    return true;
}

} // namespace

namespace MenuSkin {

Palette paletteForPreset(const QString &presetKey)
{
    const Preset *chosen = &kPresets[0];
    for (const Preset &preset : kPresets) {
        if (presetKey.compare(QLatin1String(preset.key), Qt::CaseInsensitive) == 0) {
            chosen = &preset;
            break;
        }
    }
    Palette palette;
    palette.bg = QString::fromLatin1(chosen->bg);
    palette.text = QString::fromLatin1(chosen->text);
    palette.muted = QString::fromLatin1(chosen->muted);
    palette.accent = QString::fromLatin1(chosen->accent);
    palette.accent2 = QString::fromLatin1(chosen->accent2);
    palette.danger = QString::fromLatin1(chosen->danger);
    return palette;
}

Metrics metrics()
{
    return Metrics();
}

QString rgba(const QString &hex, double alpha)
{
    int r = 0;
    int g = 0;
    int b = 0;
    if (!parseHex(hex, &r, &g, &b))
        return QStringLiteral("rgba(0,0,0,0)");
    const double clamped = alpha < 0.0 ? 0.0 : (alpha > 1.0 ? 1.0 : alpha);
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(r)
        .arg(g)
        .arg(b)
        .arg(QString::number(clamped, 'g', 3));
}

unsigned int argb(const QString &hex, int alpha)
{
    int r = 0;
    int g = 0;
    int b = 0;
    if (!parseHex(hex, &r, &g, &b))
        return 0;
    const int clamped = alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha);
    return (static_cast<unsigned int>(clamped) << 24)
        | (static_cast<unsigned int>(r) << 16)
        | (static_cast<unsigned int>(g) << 8)
        | static_cast<unsigned int>(b);
}

QString styleSheet(const Palette &palette, const Metrics &metrics)
{
    QStringList css;
    css << QStringLiteral("QMenu {")
        << QStringLiteral("    background-color: %1;").arg(rgba(palette.bg, 0.97))
        << QStringLiteral("    border: %1px solid %2;")
               .arg(metrics.borderWidth)
               .arg(rgba(palette.accent, 0.38))
        << QStringLiteral("    border-radius: %1px;").arg(metrics.radius)
        << QStringLiteral("    padding: %1px;").arg(metrics.padY)
        << QStringLiteral("}");
    css << QStringLiteral("QMenu::item {")
        << QStringLiteral("    color: %1;").arg(rgba(palette.text, 1.0))
        << QStringLiteral("    background: transparent;")
        << QStringLiteral("    padding: %1px 26px %1px %2px;")
               .arg(metrics.itemHeight / 4)
               .arg(metrics.padX)
        << QStringLiteral("    border-radius: %1px;").arg(metrics.itemRadius)
        << QStringLiteral("}");
    css << QStringLiteral("QMenu::item:selected {")
        << QStringLiteral("    background-color: %1;").arg(rgba(palette.accent, 0.16))
        << QStringLiteral("    color: rgba(255,255,255,1);")
        << QStringLiteral("}");
    css << QStringLiteral("QMenu::item:disabled {")
        << QStringLiteral("    color: %1;").arg(rgba(palette.muted, 1.0))
        << QStringLiteral("}");
    css << QStringLiteral("QMenu::separator {")
        << QStringLiteral("    height: 1px;")
        << QStringLiteral("    background: %1;").arg(rgba(palette.text, 0.10))
        << QStringLiteral("    margin: %1px %2px;").arg(metrics.sepGap).arg(metrics.padX)
        << QStringLiteral("}");
    return css.join(QLatin1Char('\n'));
}

} // namespace MenuSkin
