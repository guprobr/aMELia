#pragma once

#include <QDialog>
#include <QPixmap>

class QLabel;
class QPlainTextEdit;
class QTimer;

class BootstrapDialog : public QDialog {
    Q_OBJECT
public:
    explicit BootstrapDialog(QWidget *parent = nullptr);

    void appendLog(const QString &text);
    void setStatusText(const QString &text);

private slots:
    void advanceSpinner();

private:
    QLabel *m_logoLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QTimer *m_spinnerTimer = nullptr;
    QPixmap m_baseLogo;
    int m_rotationDegrees = 0;
};
