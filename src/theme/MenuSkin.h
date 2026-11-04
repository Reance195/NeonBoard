#pragma once

#include <QString>

// 菜单皮肤令牌（R17-01）。
//
// 为什么单独一份：托盘右键菜单有两处实现 —— 常驻宿主进程 NeonBoardHost.exe 用自绘的
// Win32 分层窗口（纯 GDI+，不引 Qt Widgets），主程序用 Qt 的 QMenu。两边各写一份颜色
// 与度量，用户会看到「同一个托盘、两种皮肤」。这里把调色板与度量收敛成唯一事实源，
// 两边都从这里取，Qt 侧再据此生成 QSS。
//
// 依赖刻意只留 QtCore：宿主进程只链 Core + Network，多引一个 QtGui 就是多一份常驻内存。
// 颜色一律用 "#RRGGBB" 字面量描述，由 rgba() / argb() 两种出口转成各自需要的格式。
namespace MenuSkin {

// 一套菜单的调色板。
struct Palette
{
    QString bg;      // 弹窗底色
    QString text;    // 条目文字
    QString muted;   // 禁用条目文字
    QString accent;  // 霓虹主色：描边 / 悬停 / 勾选
    QString accent2; // 辅助霓虹：分隔线微光
    QString danger;  // 危险项（退出）悬停
};

// 一套菜单的度量（逻辑像素，96 DPI 基准；渲染时按缩放系数换算）。
struct Metrics
{
    int radius = 12;     // 弹窗圆角
    int itemRadius = 8;  // 条目圆角
    int itemHeight = 34; // 条目高度
    int padX = 12;       // 弹窗左右内边距
    int padY = 8;        // 弹窗上下内边距
    int sepGap = 8;      // 分隔线上下留白（与 QSS 的 separator margin 对齐）
    int fontSize = 14;   // 字号
    int checkWidth = 18; // 勾选槽宽度
    int borderWidth = 1; // 描边宽度
    int glowLayers = 10;  // 外辉光层数（自绘侧用；0 = 不画辉光）
};

// 按主题预设取调色板。未知 key（外部主题包自定义的）退回第一套内置预设（neon），
// 菜单是系统级 UI，不该因为看板换了个主题包就变成另一种颜色。
Palette paletteForPreset(const QString &presetKey);

// 菜单度量（当前只有一套，留成函数是为了以后调密度时只改一处）。
Metrics metrics();

// "#RRGGBB" → "rgba(r,g,b,a)"（Qt QSS 用；alpha 取 0~1）。
QString rgba(const QString &hex, double alpha);

// "#RRGGBB" → 0xAARRGGBB（GDI+ 的 ARGB 用；alpha 取 0~255）。
unsigned int argb(const QString &hex, int alpha);

// Qt 侧 QMenu 的样式表（由同一套调色板与度量生成）。
QString styleSheet(const Palette &palette, const Metrics &metrics);

} // namespace MenuSkin
