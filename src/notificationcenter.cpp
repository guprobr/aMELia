#include "notificationcenter.h"

#include "appconfig.h"

#include <QApplication>
#include <QIcon>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QWidget>

namespace {
QPointer<QSystemTrayIcon> s_sharedTrayIcon;
int s_trayRefCount = 0;
}

NotificationCenter::NotificationCenter(const AppConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    if (!m_config.enableDesktopNotifications) {
        return;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    if (s_sharedTrayIcon.isNull()) {
        s_sharedTrayIcon = new QSystemTrayIcon(QIcon(QStringLiteral(":/branding/amelia_logo.svg")), qApp);
        s_sharedTrayIcon->setToolTip(QStringLiteral("Amelia Qt6"));
        s_sharedTrayIcon->show();
        m_ownsTray = true;
    }

    ++s_trayRefCount;
    m_trayIcon = s_sharedTrayIcon.data();
}

NotificationCenter::~NotificationCenter()
{
    shutdown();

    m_trayIcon = nullptr;

    if (s_trayRefCount > 0) {
        --s_trayRefCount;
    }

    if (m_ownsTray && s_trayRefCount <= 0 && !s_sharedTrayIcon.isNull()) {
        s_sharedTrayIcon->hide();
        s_sharedTrayIcon->deleteLater();
        s_sharedTrayIcon.clear();
    }
}

void NotificationCenter::shutdown()
{
    if (m_trayIcon != nullptr) {
        m_trayIcon->hide();
    }
}

void NotificationCenter::setAlertWidget(QWidget *widget)
{
    m_alertWidget = widget;
}

bool NotificationCenter::isEnabled() const
{
    return m_config.enableDesktopNotifications;
}

bool NotificationCenter::isNativeTrayAvailable() const
{
    return m_trayIcon != nullptr && m_trayIcon->isVisible();
}

bool NotificationCenter::shouldEmitForSeverity(int severity) const
{
    if (!m_config.enableDesktopNotifications) {
        return false;
    }

    switch (severity) {
    case Success:
        return m_config.notifyOnTaskSuccess;
    case Warning:
    case Error:
        return m_config.notifyOnTaskFailure;
    case Info:
    default:
        return m_config.notifyOnTaskStart;
    }
}

void NotificationCenter::notify(const QString &title, const QString &message, int severity)
{
    if (!shouldEmitForSeverity(severity)) {
        return;
    }

    const QString normalizedTitle = title.trimmed().isEmpty() ? QStringLiteral("Amelia") : title.trimmed();
    const QString normalizedMessage = message.trimmed().isEmpty() ? QStringLiteral("Task update.") : message.trimmed();

    if (m_trayIcon != nullptr && m_trayIcon->supportsMessages()) {
        QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;
        switch (severity) {
        case Success:
            icon = QSystemTrayIcon::Information;
            break;
        case Warning:
            icon = QSystemTrayIcon::Warning;
            break;
        case Error:
            icon = QSystemTrayIcon::Critical;
            break;
        case Info:
        default:
            icon = QSystemTrayIcon::Information;
            break;
        }
        m_trayIcon->showMessage(normalizedTitle,
                                normalizedMessage,
                                icon,
                                m_config.desktopNotificationTimeoutMs);
        return;
    }

    if (m_alertWidget != nullptr) {
        QApplication::alert(m_alertWidget.data(), 0);
    }
}
