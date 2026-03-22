#include "ui/notificationcenter.h"

#include "core/appconfig.h"

#include <QAction>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QApplication>
#include <QCoreApplication>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {
QPointer<QSystemTrayIcon> s_sharedTrayIcon;
int s_trayRefCount = 0;

QString severityAccent(int severity)
{
    switch (severity) {
    case NotificationCenter::Success:
        return QStringLiteral("#2ecc71");
    case NotificationCenter::Warning:
        return QStringLiteral("#f39c12");
    case NotificationCenter::Error:
        return QStringLiteral("#e74c3c");
    case NotificationCenter::Info:
    default:
        return QStringLiteral("#4da3ff");
    }
}

QSystemTrayIcon::MessageIcon trayIconForSeverity(int severity)
{
    switch (severity) {
    case NotificationCenter::Warning:
        return QSystemTrayIcon::Warning;
    case NotificationCenter::Error:
        return QSystemTrayIcon::Critical;
    case NotificationCenter::Success:
    case NotificationCenter::Info:
    default:
        return QSystemTrayIcon::Information;
    }
}

bool shouldPersistInHistory(int severity)
{
    return severity >= NotificationCenter::Warning;
}

uchar urgencyForSeverity(int severity)
{
    switch (severity) {
    case NotificationCenter::Error:
        return 2;
    case NotificationCenter::Warning:
        return 1;
    case NotificationCenter::Success:
    case NotificationCenter::Info:
    default:
        return 0;
    }
}
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
    ensureTrayMenu();
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &NotificationCenter::onTrayActivated, Qt::UniqueConnection);
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
    if (m_toastTimer != nullptr) {
        m_toastTimer->stop();
        m_toastTimer->deleteLater();
        m_toastTimer = nullptr;
    }

    if (m_toastWidget != nullptr) {
        m_toastWidget->hide();
        m_toastWidget->deleteLater();
        m_toastWidget = nullptr;
    }

    if (m_trayMenu != nullptr) {
        m_trayMenu->deleteLater();
        m_trayMenu = nullptr;
    }

    m_showAction = nullptr;
    m_hideAction = nullptr;
    m_exitAction = nullptr;

    if (m_trayIcon != nullptr) {
        m_trayIcon->setContextMenu(nullptr);
        m_trayIcon->hide();
    }
}

void NotificationCenter::setAlertWidget(QWidget *widget)
{
    m_alertWidget = widget;
    updateTrayActions();
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

    showNativeDesktopNotification(normalizedTitle, normalizedMessage, severity);
    showInAppToast(normalizedTitle, normalizedMessage, severity);

    if (m_alertWidget != nullptr) {
        QApplication::alert(m_alertWidget.data(), 0);
    }
}

bool NotificationCenter::showNativeDesktopNotification(const QString &title,
                                                       const QString &message,
                                                       int severity) const
{
#ifdef Q_OS_LINUX
    if (QDBusConnection::sessionBus().isConnected()) {
        QDBusInterface notifications(QStringLiteral("org.freedesktop.Notifications"),
                                     QStringLiteral("/org/freedesktop/Notifications"),
                                     QStringLiteral("org.freedesktop.Notifications"),
                                     QDBusConnection::sessionBus());
        if (notifications.isValid()) {
            QVariantMap hints;
            hints.insert(QStringLiteral("urgency"), QVariant::fromValue(urgencyForSeverity(severity)));
            hints.insert(QStringLiteral("transient"), !shouldPersistInHistory(severity));
            hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("amelia_qt6"));

            const QDBusMessage reply = notifications.call(QStringLiteral("Notify"),
                                                          QStringLiteral("amelia_qt6"),
                                                          static_cast<uint>(0),
                                                          QStringLiteral("amelia_qt6"),
                                                          title,
                                                          message,
                                                          QStringList(),
                                                          hints,
                                                          m_config.desktopNotificationTimeoutMs);
            if (reply.type() != QDBusMessage::ErrorMessage) {
                return true;
            }
        }
    }
#endif

    if (m_trayIcon != nullptr && m_trayIcon->supportsMessages()) {
        m_trayIcon->showMessage(title,
                                message,
                                trayIconForSeverity(severity),
                                m_config.desktopNotificationTimeoutMs);
        return true;
    }

    return false;
}

void NotificationCenter::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        if (m_alertWidget != nullptr && m_alertWidget->isVisible()) {
            hideMainWindow();
        } else {
            showMainWindow();
        }
    }
}

void NotificationCenter::showMainWindow()
{
    QWidget *widget = m_alertWidget.data();
    if (widget == nullptr) {
        return;
    }

    widget->showNormal();
    widget->raise();
    widget->activateWindow();
    updateTrayActions();
}

void NotificationCenter::hideMainWindow()
{
    QWidget *widget = m_alertWidget.data();
    if (widget == nullptr) {
        return;
    }

    widget->hide();
    updateTrayActions();
}

void NotificationCenter::ensureTrayMenu()
{
    if (m_trayIcon == nullptr || m_trayMenu != nullptr) {
        updateTrayActions();
        return;
    }

    m_trayMenu = new QMenu();
    m_showAction = m_trayMenu->addAction(QStringLiteral("Show Amelia"));
    m_hideAction = m_trayMenu->addAction(QStringLiteral("Hide Amelia"));
    m_trayMenu->addSeparator();
    m_exitAction = m_trayMenu->addAction(QStringLiteral("Exit"));

    connect(m_showAction, &QAction::triggered, this, &NotificationCenter::showMainWindow);
    connect(m_hideAction, &QAction::triggered, this, &NotificationCenter::hideMainWindow);
    connect(m_exitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_trayIcon->setContextMenu(m_trayMenu.data());
    updateTrayActions();
}

void NotificationCenter::updateTrayActions()
{
    const bool visible = m_alertWidget != nullptr && m_alertWidget->isVisible();
    if (m_showAction != nullptr) {
        m_showAction->setEnabled(!visible);
    }
    if (m_hideAction != nullptr) {
        m_hideAction->setEnabled(visible);
    }
}

void NotificationCenter::showInAppToast(const QString &title, const QString &message, int severity)
{
    QWidget *anchor = m_alertWidget.data();
    if (anchor == nullptr) {
        anchor = QApplication::activeWindow();
    }

    if (anchor == nullptr && !QApplication::topLevelWidgets().isEmpty()) {
        anchor = QApplication::topLevelWidgets().constFirst();
    }

    if (anchor == nullptr) {
        return;
    }

    if (m_toastWidget == nullptr) {
        auto *toast = new QFrame(anchor, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        toast->setObjectName(QStringLiteral("ameliaNotificationToast"));
        toast->setAttribute(Qt::WA_ShowWithoutActivating, true);
        toast->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        auto *layout = new QHBoxLayout(toast);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto *accent = new QFrame(toast);
        accent->setObjectName(QStringLiteral("accent"));
        accent->setFixedWidth(6);

        auto *body = new QWidget(toast);
        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(14, 10, 14, 10);
        bodyLayout->setSpacing(4);

        auto *titleLabel = new QLabel(body);
        titleLabel->setObjectName(QStringLiteral("title"));
        titleLabel->setWordWrap(true);

        auto *messageLabel = new QLabel(body);
        messageLabel->setObjectName(QStringLiteral("message"));
        messageLabel->setWordWrap(true);

        bodyLayout->addWidget(titleLabel);
        bodyLayout->addWidget(messageLabel);
        layout->addWidget(accent);
        layout->addWidget(body);

        toast->setStyleSheet(QStringLiteral(
            "#ameliaNotificationToast {"
            " background-color: rgba(18, 20, 26, 236);"
            " color: #f4f7fb;"
            " border: 1px solid rgba(255, 255, 255, 32);"
            " border-radius: 12px;"
            "}"
            "#ameliaNotificationToast QLabel#title {"
            " font-weight: 700;"
            " font-size: 13px;"
            " color: #ffffff;"
            "}"
            "#ameliaNotificationToast QLabel#message {"
            " font-size: 12px;"
            " color: rgba(244, 247, 251, 220);"
            "}"
        ));

        m_toastWidget = toast;
    }

    auto *titleLabel = m_toastWidget->findChild<QLabel *>(QStringLiteral("title"));
    auto *messageLabel = m_toastWidget->findChild<QLabel *>(QStringLiteral("message"));
    auto *accent = m_toastWidget->findChild<QFrame *>(QStringLiteral("accent"));
    if (titleLabel == nullptr || messageLabel == nullptr || accent == nullptr) {
        return;
    }

    titleLabel->setText(title);
    messageLabel->setText(message);
    accent->setStyleSheet(QStringLiteral("background-color: %1; border-top-left-radius: 12px; border-bottom-left-radius: 12px;")
                              .arg(severityAccent(severity)));

    const int maxWidth = anchor->isVisible() ? qMax(300, anchor->width() / 3) : 420;
    m_toastWidget->setMaximumWidth(maxWidth);
    m_toastWidget->adjustSize();

    QRect targetGeometry;
    if (anchor->isVisible()) {
        const QPoint topLeft = anchor->mapToGlobal(QPoint(0, 0));
        const QRect anchorRect(topLeft, anchor->size());
        const QSize toastSize = m_toastWidget->sizeHint();
        targetGeometry = QRect(anchorRect.right() - toastSize.width() - 24,
                               anchorRect.bottom() - toastSize.height() - 24,
                               toastSize.width(),
                               toastSize.height());
    } else {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen == nullptr) {
            return;
        }
        const QRect available = screen->availableGeometry();
        const QSize toastSize = m_toastWidget->sizeHint();
        targetGeometry = QRect(available.right() - toastSize.width() - 24,
                               available.bottom() - toastSize.height() - 24,
                               toastSize.width(),
                               toastSize.height());
    }

    m_toastWidget->setGeometry(targetGeometry);
    m_toastWidget->show();
    m_toastWidget->raise();

    if (m_toastTimer == nullptr) {
        m_toastTimer = new QTimer(this);
        m_toastTimer->setSingleShot(true);
        connect(m_toastTimer, &QTimer::timeout, this, [this]() {
            if (m_toastWidget != nullptr) {
                m_toastWidget->hide();
            }
        });
    }

    const int timeoutMs = qMax(1500, m_config.desktopNotificationTimeoutMs > 0 ? m_config.desktopNotificationTimeoutMs : 4500);
    m_toastTimer->start(timeoutMs);
}
