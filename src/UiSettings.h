#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

// 界面偏好持久化（主题预设 / 画质档 / 减弱动效）。
// 用 QSettings 落地为 ini，与窗口几何等既有设置同源。
class UiSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString themePreset READ themePreset WRITE setThemePreset NOTIFY themePresetChanged)
    Q_PROPERTY(int quality READ quality WRITE setQuality NOTIFY qualityChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)

public:
    explicit UiSettings(QObject *parent = nullptr);

    QString themePreset() const;
    void setThemePreset(const QString &preset);

    int quality() const;
    void setQuality(int level);

    bool reducedMotion() const;
    void setReducedMotion(bool enabled);

signals:
    void themePresetChanged();
    void qualityChanged();
    void reducedMotionChanged();

private:
    QSettings m_settings;
    QString m_themePreset;
    int m_quality;
    bool m_reducedMotion;
};
