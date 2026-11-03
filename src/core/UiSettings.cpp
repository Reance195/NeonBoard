#include "UiSettings.h"

// 注意：这里**不要**自己判断 AppPaths::isolated() 再指定 ini 文件名。
// main.cpp 已经把默认格式设成 IniFormat，并在沙箱下把 UserScope 路径指到 dataDir，
// 所以默认构造的 QSettings 天然跟着沙箱走。再单开一个 ui-settings.ini 会把
// 主题/画质从 NeonBoard.ini 里拆出去，老沙箱里的设置瞬间读成默认值。

UiSettings::UiSettings(QObject *parent)
    : QObject(parent)
    , m_settings()
    , m_themePreset(m_settings.value(QStringLiteral("appearance/themePreset"),
                                    QStringLiteral("neon")).toString())
    , m_quality(m_settings.value(QStringLiteral("appearance/quality"), 1).toInt())
    , m_reducedMotion(m_settings.value(QStringLiteral("appearance/reducedMotion"), false).toBool())
    , m_onboardingDone(m_settings.value(QStringLiteral("ui/onboardingDone"), false).toBool())
{
}

QString UiSettings::themePreset() const
{
    return m_themePreset;
}

void UiSettings::setThemePreset(const QString &preset)
{
    if (preset == m_themePreset)
        return;
    m_themePreset = preset;
    m_settings.setValue(QStringLiteral("appearance/themePreset"), preset);
    emit themePresetChanged();
}

int UiSettings::quality() const
{
    return m_quality;
}

void UiSettings::setQuality(int level)
{
    if (level < 0)
        level = 0;
    if (level > 2)
        level = 2;
    if (level == m_quality)
        return;
    m_quality = level;
    m_settings.setValue(QStringLiteral("appearance/quality"), level);
    emit qualityChanged();
}

bool UiSettings::reducedMotion() const
{
    return m_reducedMotion;
}

void UiSettings::setReducedMotion(bool enabled)
{
    if (enabled == m_reducedMotion)
        return;
    m_reducedMotion = enabled;
    m_settings.setValue(QStringLiteral("appearance/reducedMotion"), enabled);
    emit reducedMotionChanged();
}

bool UiSettings::onboardingDone() const
{
    return m_onboardingDone;
}

void UiSettings::setOnboardingDone(bool done)
{
    if (done == m_onboardingDone)
        return;
    m_onboardingDone = done;
    m_settings.setValue(QStringLiteral("ui/onboardingDone"), done);
    emit onboardingDoneChanged();
}

bool UiSettings::shouldOnboard(int taskCount) const
{
    // 两个条件必须同时成立：
    //   1) 从未完成/跳过过引导（跳过也置位，避免每次启动再弹）；
    //   2) 看板是空的 —— 老用户即使换机重装也不该被三步说明挡住，
    //      他们的看板一开始就有卡片。
    return !m_onboardingDone && taskCount == 0;
}
