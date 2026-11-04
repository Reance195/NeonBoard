#include "TrayController.h"

#include "MenuSkin.h"

#include <QAction>
#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QSettings>
#include <QSystemTrayIcon>

TrayController::TrayController(bool headless, QObject *parent)
    : QObject(parent)
    , m_headless(headless)
{
    if (m_headless)
        return;

    QIcon icon(QStringLiteral(":/icons/neonboard.svg"));
    if (icon.isNull()) {
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#22D3EE")), 2.5));
        painter.setBrush(QColor(QStringLiteral("#05060A")));
        painter.drawRoundedRect(QRectF(2, 2, 28, 28), 8, 8);
        painter.drawLine(QPointF(10, 23), QPointF(10, 9));
        painter.drawLine(QPointF(10, 9), QPointF(22, 23));
        painter.drawLine(QPointF(22, 23), QPointF(22, 9));
        painter.end();
        icon = QIcon(pixmap);
    }

    m_tray = new QSystemTrayIcon(icon, this);
    m_tray->setToolTip(QStringLiteral("NeonBoard"));

    m_menu = new QMenu();
    // 菜单皮（R17-01）：Qt 6 的弹出菜单由 Qt 自己绘制（Windows 的系统菜单没有
    // 「让 QSS 接管」的接口，那正是截图里那个白底方框的来源），所以这里只需要
    // 给窗口开透明底，border-radius 才有圆角。配色与宿主自绘弹窗共用 MenuSkin
    // 一份令牌，两处菜单看起来是同一套皮肤。
    m_menu->setAttribute(Qt::WA_TranslucentBackground, true);
    m_showAction = m_menu->addAction(QStringLiteral("显示 NeonBoard"));
    m_hideAction = m_menu->addAction(QStringLiteral("隐藏到托盘"));
    m_menu->addSeparator();
    QAction *newTask = m_menu->addAction(QStringLiteral("新建任务"));
    m_statsAction = m_menu->addAction(QStringLiteral("待办 0 · 进行中 0 · 今日完成 0"));
    m_statsAction->setEnabled(false);
    m_menu->addSeparator();
    m_autostartAction = m_menu->addAction(QStringLiteral("开机自启"));
    m_autostartAction->setCheckable(true);
    m_autostartAction->setChecked(isAutostart());
    QAction *settings = m_menu->addAction(QStringLiteral("设置…"));
    m_menu->addSeparator();
    QAction *quitAction = m_menu->addAction(QStringLiteral("退出"));
    applySkin();
    // 弹出前刷新一次皮肤：换主题不用重启，菜单也不会挂着旧配色。
    connect(m_menu, &QMenu::aboutToShow, this, [this]() { applySkin(); });

    connect(m_showAction, &QAction::triggered, this, &TrayController::showRequested);
    connect(m_hideAction, &QAction::triggered, this, &TrayController::hideRequested);
    connect(newTask, &QAction::triggered, this, &TrayController::newTaskRequested);
    connect(settings, &QAction::triggered, this, &TrayController::settingsRequested);
    connect(quitAction, &QAction::triggered, this, &TrayController::quitRequested);
    connect(m_autostartAction, &QAction::toggled, this, &TrayController::setAutostart);

    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    emit showRequested();
            });

    m_tray->show();
}

TrayController::~TrayController()
{
    if (m_tray)
        m_tray->hide();
    delete m_menu;
}

void TrayController::updateStats(int todo, int doing, int doneToday)
{
    if (!m_statsAction)
        return;
    m_statsAction->setText(QStringLiteral("待办 %1 · 进行中 %2 · 今日完成 %3")
                               .arg(todo)
                               .arg(doing)
                               .arg(doneToday));
}

void TrayController::showMessage(const QString &title, const QString &body)
{
    if (m_tray)
        m_tray->showMessage(title, body, QSystemTrayIcon::Information, 3000);
}

void TrayController::setInstanceLabel(const QString &label)
{
    m_instanceLabel = label.trimmed();
    m_baseTitle = m_instanceLabel.isEmpty() ? QStringLiteral("NeonBoard") : m_instanceLabel;
    applyTooltip();
    if (m_showAction)
        m_showAction->setText(QStringLiteral("显示 %1").arg(m_baseTitle));
}

void TrayController::setFocusRemaining(int seconds)
{
    m_focusRemaining = qMax(0, seconds);
    applyTooltip();
}

void TrayController::applyTooltip()
{
    if (!m_tray)
        return;
    QString tip = m_baseTitle;
    if (m_focusRemaining > 0) {
        tip += QStringLiteral(" · 专注 %1:%2")
                   .arg(m_focusRemaining / 60, 2, 10, QChar('0'))
                   .arg(m_focusRemaining % 60, 2, 10, QChar('0'));
    }
    m_tray->setToolTip(tip);
}

bool TrayController::isAutostart() const
{
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    return run.contains(QStringLiteral("NeonBoard"));
}

void TrayController::setAutostart(bool enabled)
{
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    if (enabled) {
        QString exePath = QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("NeonBoardHost.exe"));
        if (!QFileInfo::exists(exePath))
            exePath = QCoreApplication::applicationFilePath();
        const QString command = QStringLiteral("\"%1\"")
                                    .arg(QDir::toNativeSeparators(exePath));
        run.setValue(QStringLiteral("NeonBoard"), command);
    } else {
        run.remove(QStringLiteral("NeonBoard"));
    }
    refreshAutostartAction();
}

void TrayController::refreshAutostartAction()
{
    if (m_autostartAction)
        m_autostartAction->setChecked(isAutostart());
    applySkin();
}

void TrayController::setThemePreset(const QString &presetKey)
{
    if (m_themePreset == presetKey)
        return;
    m_themePreset = presetKey;
    applySkin();
}

void TrayController::applySkin()
{
    if (!m_menu)
        return;
    // 主题预设每次弹菜单前重新读一次：主程序那边改主题写的是同一份界面设置
    // （ini 的 appearance/themePreset），托盘菜单不必知道谁在改，跟着变就行。
    // 这样也不必让 main.cpp 往这里接信号 —— main.cpp 有行数棘轮。
    const QString preset = currentThemePreset();
    if (preset != m_themePreset) {
        m_themePreset = preset;
    }
    const MenuSkin::Palette palette = MenuSkin::paletteForPreset(m_themePreset);
    m_menu->setStyleSheet(MenuSkin::styleSheet(palette, MenuSkin::metrics()));
    if (!m_autostartAction)
        return;
    // 非原生菜单不画系统勾选框，自己画一个霓虹对勾。
    // 未勾选时给一张同尺寸的空图：图标列宽是全菜单统一的，空图能免得文字左右跳。
    QPixmap blank(16, 16);
    blank.fill(Qt::transparent);
    m_autostartAction->setIcon(QIcon(m_autostartAction->isChecked() ? checkMark(palette) : blank));
}

QString TrayController::currentThemePreset() const
{
    // 与主程序读同一份界面设置：org/app 与 ini 路径都由 QSettings 全局状态决定，
    // 沙箱下 main.cpp 已把 ini 路径指到数据目录，这里自然跟着走。
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("com.lin"),
                       QStringLiteral("NeonBoard"));
    return settings.value(QStringLiteral("appearance/themePreset"), QStringLiteral("neon")).toString();
}

QPixmap TrayController::checkMark(const MenuSkin::Palette &palette) const
{
    QPixmap mark(16, 16);
    mark.fill(Qt::transparent);
    QPainter painter(&mark);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(palette.accent), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    QPolygonF poly;
    poly << QPointF(3, 8.5) << QPointF(6.5, 12) << QPointF(13, 4.5);
    painter.drawPolyline(poly);
    return mark;
}

bool TrayController::saveMenuPreview(const QString &pngPath) const
{
    if (!m_menu)
        return false;
    // 不真弹到屏幕上：WA_DontShowOnScreen 只布局不显示，抓完立即收。
    m_menu->setAttribute(Qt::WA_DontShowOnScreen, true);
    m_menu->show();
    const QPixmap shot = m_menu->grab();
    m_menu->hide();
    m_menu->setAttribute(Qt::WA_DontShowOnScreen, false);
    return !shot.isNull() && shot.save(pngPath, "PNG");
}

int TrayController::handleMenuPreview(const QStringList &arguments)
{
    const int index = arguments.indexOf(QStringLiteral("--menu-preview"));
    if (index < 0)
        return -1;
    const QString output = index + 1 < arguments.size() ? arguments.at(index + 1).trimmed()
                                                         : QString();
    if (output.isEmpty()) {
        qWarning() << "MENU-PREVIEW FAIL missing output path";
        return 2;
    }
    const bool ok = saveMenuPreview(output);
    qInfo().noquote() << "MENU-PREVIEW" << output << (ok ? "OK" : "FAIL");
    return ok ? 0 : 2;
}
