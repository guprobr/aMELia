#pragma once

#include "core/appconfig.h"

#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>

class QAction;
class QMenu;
class QTimer;
class QWidget;

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

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void showMainWindow();
    void hideMainWindow();

private:
    bool shouldEmitForSeverity(int severity) const;
    void showInAppToast(const QString &title, const QString &message, int severity);
    void ensureTrayMenu();
    void updateTrayActions();

    AppConfig m_config;
    QPointer<QWidget> m_alertWidget;
    QPointer<QWidget> m_toastWidget;
    QPointer<QTimer> m_toastTimer;
    QPointer<QMenu> m_trayMenu;
    QPointer<QAction> m_showAction;
    QPointer<QAction> m_hideAction;
    QPointer<QAction> m_exitAction;
    QSystemTrayIcon *m_trayIcon = nullptr;
    bool m_ownsTray = false;
};
