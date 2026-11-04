#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QAction;
class QMenu;
class QPixmap;
class QSystemTrayIcon;

namespace MenuSkin {
struct Palette;
}

class TrayController : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(bool headless = false, QObject *parent = nullptr);
    ~TrayController() override;

    void updateStats(int todo, int doing, int doneToday);
    void showMessage(const QString &title, const QString &body);
    // 实例标签（多预览实例并行时用于区分托盘项）
    void setInstanceLabel(const QString &label);
    // 专注倒计时：秒数 <= 0 表示结束，恢复基础 tooltip
    Q_INVOKABLE void setFocusRemaining(int seconds);

    Q_INVOKABLE bool isAutostart() const;
    Q_INVOKABLE void setAutostart(bool enabled);

    // 看板主题变了，托盘菜单跟着换霓虹主色（R17-01）。
    // 不传就用当前界面设置里的预设（菜单弹出前自己读）。
    void setThemePreset(const QString &presetKey);

    // 离屏把菜单抓成 PNG（验收截图用，不弹到屏幕上）。
    bool saveMenuPreview(const QString &pngPath) const;

    // 命令行出图：--menu-preview <png>。
    // 返回值约定：-1 = 没有这个命令（调用方继续正常启动）；>= 0 = 已处理，
    // 调用方按它退出（0 成功 / 2 失败）。
    // 为什么放在这里而不是 main.cpp：main.cpp 有行数棘轮（只许减不许增），
    // 命令行分支的解析逻辑一律住在自己模块里，main.cpp 只留一行调用。
    int handleMenuPreview(const QStringList &arguments);

signals:
    void showRequested();
    void hideRequested();
    void newTaskRequested();
    void settingsRequested();
    void quitRequested();

private:
    void refreshAutostartAction();
    void applyTooltip();
    void applySkin();
    QPixmap checkMark(const MenuSkin::Palette &palette) const;
    // 界面设置里的主题预设（与主程序同一份 ini）。
    QString currentThemePreset() const;

    bool m_headless = false;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_hideAction = nullptr;
    QAction *m_statsAction = nullptr;
    QAction *m_autostartAction = nullptr;
    QString m_instanceLabel;
    QString m_baseTitle = QStringLiteral("NeonBoard");
    QString m_themePreset = QStringLiteral("neon");
    int m_focusRemaining = 0;
};
