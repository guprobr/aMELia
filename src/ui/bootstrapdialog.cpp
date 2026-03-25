#include "ui/bootstrapdialog.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QPixmap>
#include <QTransform>
#include <QApplication>
#include <QScreen>

BootstrapDialog::BootstrapDialog(QWidget *parent)
    : QDialog(parent)
    , m_logoLabel(new QLabel(this))
    , m_statusLabel(new QLabel(this))
    , m_logView(new QPlainTextEdit(this))
    , m_spinnerTimer(new QTimer(this))
    , m_baseLogo(QStringLiteral(":/branding/amelia_logo.svg"))
{
    setWindowTitle(QStringLiteral("Starting Amelia..."));
    setModal(false);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(640, 420);

    auto *layout = new QVBoxLayout(this);
    m_logoLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setText(QStringLiteral("Bootstrapping Amelia..."));
    QFont statusFont = m_statusLabel->font();
    statusFont.setBold(true);
    statusFont.setPointSize(statusFont.pointSize() + 1);
    m_statusLabel->setFont(statusFont);

    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(200);
    m_logView->setPlaceholderText(QStringLiteral("Bootstrap log..."));

    layout->addWidget(m_logoLabel, 0, Qt::AlignCenter);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_logView, 1);

    if (!m_baseLogo.isNull()) {
        m_logoLabel->setPixmap(m_baseLogo.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    m_spinnerTimer->setInterval(80);
    connect(m_spinnerTimer, &QTimer::timeout, this, &BootstrapDialog::advanceSpinner);
    m_spinnerTimer->start();

    const QRect available = qApp->primaryScreen()->availableGeometry();
    move(available.center() - rect().center());
}

void BootstrapDialog::appendLog(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }
    m_logView->appendPlainText(text);
}

void BootstrapDialog::setStatusText(const QString &text)
{
    if (!text.trimmed().isEmpty()) {
        m_statusLabel->setText(text);
    }
}

void BootstrapDialog::advanceSpinner()
{
    if (m_baseLogo.isNull()) {
        return;
    }

    m_rotationDegrees = (m_rotationDegrees + 20) % 360;
    QTransform transform;
    transform.rotate(m_rotationDegrees);
    const QPixmap rotated = m_baseLogo.transformed(transform, Qt::SmoothTransformation);
    m_logoLabel->setPixmap(rotated.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
