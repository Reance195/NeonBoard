#pragma once

#include <QList>
#include <QString>
#include <functional>

#include "MenuSkin.h"

// 宿主进程的托盘右键菜单：自绘的 Win32 分层窗口（R17-01）。
//
// 为什么不用 HMENU：CreatePopupMenu / TrackPopupMenu 画的是 Windows 系统菜单 ——
// 白底方框、字号字体全由系统决定，QSS 改不动，也接不进 NeonBoard 的霓虹主题。
// 这里是整块自己画：一张带 alpha 的 32 位位图，用 UpdateLayeredWindow 贴到屏幕上，
// 于是圆角、外辉光、半透明底、悬停高亮都能按主题画出来。
//
// 渲染用 GDI+（gdiplus.dll，Windows 自带）：分层窗口要求逐像素 alpha，
// 而纯 GDI 往位图里画文字不会写 alpha 通道 —— 字会整块消失。
// GDI+ 的 PixelFormat32bppPARGB 位图带预乘 alpha，UpdateLayeredWindow 正好吃这一套。
//
// Win32 / GDI+ 的类型全部收在 .cpp 的 Impl 里：这个头只给宿主 main 用，
// 不该把 windows.h 拖进任何可能进 Qt 世界的编译单元。
class TrayMenu
{
public:
    struct Item
    {
        enum Kind { Action, Separator };
        Kind kind = Action;
        QString text;
        bool checkable = false;
        bool checked = false;
        bool danger = false; // 「退出」这类危险项：悬停用 danger 色
        std::function<void()> onTriggered;
    };

    TrayMenu();
    ~TrayMenu();

    void setItems(const QList<Item> &items);

    // 调色板随看板主题走（宿主读的是主程序那份界面设置）。
    void setPalette(const MenuSkin::Palette &palette);

    // 在鼠标位置弹出；贴到屏幕边缘时自动翻转。
    void popupAtCursor();

    // 离屏渲染成 PNG（验收截图用，不弹窗）。hoverIndex < 0 画静止态。
    bool renderPreview(const QString &pngPath, int hoverIndex = -1) const;

private:
    struct Impl;
    Impl *m_impl = nullptr;

    // 兜底：GDI+ 或窗口创建失败时退回系统菜单 —— 「点得到」优先于「好看」。
    void showNativeFallback() const;
};
