#pragma once

#include "appconfig.h"

#include <QObject>
#include <QPointer>

class QTimer;
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
    void showInAppToast(const QString &title, const QString &message, int severity);

    AppConfig m_config;
    QPointer<QWidget> m_alertWidget;
    QPointer<QWidget> m_toastWidget;
    QPointer<QTimer> m_toastTimer;
    QSystemTrayIcon *m_trayIcon = nullptr;
    bool m_ownsTray = false;
};
