#include "TrayController.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
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
    const QString title = m_instanceLabel.isEmpty() ? QStringLiteral("NeonBoard") : m_instanceLabel;
    if (m_tray)
        m_tray->setToolTip(title);
    if (m_showAction)
        m_showAction->setText(QStringLiteral("显示 %1").arg(title));
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
}
