#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

class Hotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged)
    Q_PROPERTY(QString error READ error NOTIFY registeredChanged)

public:
    explicit Hotkey(QObject *parent = nullptr);
    ~Hotkey() override;

    bool install(QObject *window);
    void uninstall();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    bool registered() const { return m_registered; }
    QString error() const { return m_error; }

signals:
    void activated();
    void registeredChanged();

private:
    static constexpr int kHotkeyId = 0x4E42;
    void *m_hwnd = nullptr;
    bool m_registered = false;
    QString m_error;
};
