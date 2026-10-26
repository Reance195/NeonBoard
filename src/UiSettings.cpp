#include "UiSettings.h"

#include "AppPaths.h"

#include <QDir>

UiSettings::UiSettings(QObject *parent)
    : QObject(parent)
{
    if (AppPaths::isolated()) {
        QDir().mkpath(AppPaths::dataDir());
        m_settings = std::make_unique<QSettings>(AppPaths::dataDir() + QStringLiteral("/ui-settings.ini"),
                                                 QSettings::IniFormat);
    } else {
        m_settings = std::make_unique<QSettings>();
    }

    m_themePreset = m_settings->value(QStringLiteral("appearance/themePreset"),
                                      QStringLiteral("neon")).toString();
    m_quality = m_settings->value(QStringLiteral("appearance/quality"), 1).toInt();
    m_reducedMotion = m_settings->value(QStringLiteral("appearance/reducedMotion"), false).toBool();
    m_onboardingDone = m_settings->value(QStringLiteral("ui/onboardingDone"), false).toBool();
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
    m_settings->setValue(QStringLiteral("appearance/themePreset"), preset);
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
    m_settings->setValue(QStringLiteral("appearance/quality"), level);
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
    m_settings->setValue(QStringLiteral("appearance/reducedMotion"), enabled);
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
    m_settings->setValue(QStringLiteral("ui/onboardingDone"), done);
    emit onboardingDoneChanged();
}
