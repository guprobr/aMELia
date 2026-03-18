#pragma once

#include <QObject>
#include <QPointer>

class QWidget;
class QSystemTrayIcon;

struct AppConfig;

class NotificationCenter : public QObject {
    Q_OBJECT
public:
    enum Severity {
        Info = 0,
        Success = 1,
        Warning = 2,
        Error = 3
    };

    explicit NotificationCenter(const AppConfig &config, QObject *parent = nullptr);
    ~NotificationCenter() override;

    void setAlertWidget(QWidget *widget);
    bool isEnabled() const;
    bool isNativeTrayAvailable() const;

public slots:
    void notify(const QString &title, const QString &message, int severity = Info);
    void shutdown();

private:
    bool shouldEmitForSeverity(int severity) const;

    const AppConfig &m_config;
    QPointer<QWidget> m_alertWidget;
    QSystemTrayIcon *m_trayIcon = nullptr;
    bool m_ownsTray = false;
};
