#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

// 界面偏好持久化（主题预设 / 画质档 / 减弱动效 / 首启动引导标记）。
// 用 QSettings 落地为 ini，与窗口几何等既有设置同源（沙箱路径由 main.cpp 统一改）。
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

    // 首启动引导是否已完成（「跳过」也算完成，避免每次启动再弹）
    bool onboardingDone() const;
    void setOnboardingDone(bool done);

    // 是否该弹引导：未完成过 且 看板为空。判定放 C++ 而不是 QML，
    // 是为了让「线上到底该不该弹」有单测可覆盖，而不是只能靠肉眼点。
    Q_INVOKABLE bool shouldOnboard(int taskCount) const;

signals:
    void themePresetChanged();
    void qualityChanged();
    void reducedMotionChanged();
    void onboardingDoneChanged();

private:
    QSettings m_settings;
    QString m_themePreset;
    int m_quality;
    bool m_reducedMotion;
    bool m_onboardingDone;
};
