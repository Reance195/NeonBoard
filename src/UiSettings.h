#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

#include <memory>

// 界面偏好持久化（主题预设 / 画质档 / 减弱动效）。
// 用 QSettings 落地为 ini，与窗口几何等既有设置同源。
class UiSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString themePreset READ themePreset WRITE setThemePreset NOTIFY themePresetChanged)
    Q_PROPERTY(int quality READ quality WRITE setQuality NOTIFY qualityChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
    Q_PROPERTY(bool onboardingDone READ onboardingDone WRITE setOnboardingDone NOTIFY onboardingDoneChanged)

public:
    explicit UiSettings(QObject *parent = nullptr);

    QString themePreset() const;
    void setThemePreset(const QString &preset);

    int quality() const;
    void setQuality(int level);

    bool reducedMotion() const;
    void setReducedMotion(bool enabled);

    // 首启动引导是否已完成（跳过也算完成，避免每次启动再弹）
    bool onboardingDone() const;
    void setOnboardingDone(bool done);

signals:
    void themePresetChanged();
    void qualityChanged();
    void reducedMotionChanged();
    void onboardingDoneChanged();

private:
    // 沙箱/开发运行（AppPaths::isolated）落到 <dataDir>/ui-settings.ini，
    // 正常用户仍走注册表，与既有行为一致。
    std::unique_ptr<QSettings> m_settings;
    QString m_themePreset;
    int m_quality;
    bool m_reducedMotion;
    bool m_onboardingDone;
};
