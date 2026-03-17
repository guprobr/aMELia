#include "mainwindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QPixmap>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_busyFrames({QStringLiteral("◐"), QStringLiteral("◓"), QStringLiteral("◑"), QStringLiteral("◒")})
{
    setWindowTitle(QStringLiteral("Amelia Qt6"));
    setMinimumSize(1280, 820);

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    auto *memoryMenu = menuBar()->addMenu(QStringLiteral("&Memory"));
    auto *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));

    auto *newConversationAction = fileMenu->addAction(QStringLiteral("New conversation"));
    newConversationAction->setShortcut(QKeySequence::New);
    connect(newConversationAction, &QAction::triggered, this, &MainWindow::newConversationRequested);

    m_clearMemoriesAction = memoryMenu->addAction(QStringLiteral("Clear stored memories"));
    connect(m_clearMemoriesAction, &QAction::triggered, this, &MainWindow::onClearMemoriesTriggered);

    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(QStringLiteral("Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    m_aboutAmeliaAction = helpMenu->addAction(QStringLiteral("About Amelia"));
    m_aboutQtAction = helpMenu->addAction(QStringLiteral("About Qt"));
    connect(m_aboutAmeliaAction, &QAction::triggered, this, &MainWindow::showAboutAmelia);
    connect(m_aboutQtAction, &QAction::triggered, this, &MainWindow::showAboutQtDialog);

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);

    auto *splitter = new QSplitter(Qt::Horizontal, central);

    auto *sessionPane = new QWidget(splitter);
    auto *sessionLayout = new QVBoxLayout(sessionPane);
    auto *sessionTitle = new QLabel(QStringLiteral("Sessions"), sessionPane);
    sessionTitle->setStyleSheet(QStringLiteral("font-weight: 700;"));
    m_conversationsList = new QListWidget(sessionPane);
    m_conversationsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_newConversationButton = new QPushButton(QStringLiteral("New conversation"), sessionPane);
    sessionLayout->addWidget(sessionTitle);
    sessionLayout->addWidget(m_conversationsList, 1);
    sessionLayout->addWidget(m_newConversationButton);

    auto *chatPane = new QWidget(splitter);
    auto *chatLayout = new QVBoxLayout(chatPane);

    auto *titleRow = new QHBoxLayout();
    auto *title = new QLabel(QStringLiteral("Amelia Qt6 - Local coding and cloud assistant"), chatPane);
    title->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 18px;"));
    auto *logoLabel = new QLabel(chatPane);
    const QPixmap logoPixmap(QStringLiteral(":/branding/amelia_logo.svg"));
    logoLabel->setPixmap(logoPixmap.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setToolTip(QStringLiteral("Amelia logo"));
    titleRow->addWidget(title);
    titleRow->addWidget(logoLabel, 0, Qt::AlignVCenter);
    titleRow->addStretch(1);

    m_transcript = new QPlainTextEdit(chatPane);
    m_transcript->setReadOnly(true);
    m_transcript->setPlaceholderText(QStringLiteral("Conversation transcript..."));

    m_input = new QPlainTextEdit(chatPane);
    m_input->setPlaceholderText(QStringLiteral("Type your prompt here..."));
    m_input->setMaximumHeight(120);

    auto *toolbarLayout = new QHBoxLayout();
    m_externalSearchCheck = new QCheckBox(QStringLiteral("Allow sanitized external search"), chatPane);
    m_externalSearchCheck->setChecked(false);
    m_modelCombo = new QComboBox(chatPane);
    m_modelCombo->setEditable(false);
    m_modelCombo->setMinimumWidth(220);
    m_modelCombo->addItem(QStringLiteral("qwen2.5-coder:14b"));
    m_modelCombo->addItem(QStringLiteral("qwen2.5-coder:7b"));
    auto *modelLabel = new QLabel(QStringLiteral("Model:"), chatPane);
    toolbarLayout->addWidget(m_externalSearchCheck);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(modelLabel);
    toolbarLayout->addWidget(m_modelCombo);

    auto *controlsLayout = new QHBoxLayout();
    m_sendButton = new QPushButton(QStringLiteral("Send"), chatPane);
    m_stopButton = new QPushButton(QStringLiteral("Stop"), chatPane);
    m_reindexButton = new QPushButton(QStringLiteral("Reindex docs"), chatPane);
    m_testBackendButton = new QPushButton(QStringLiteral("Test Ollama"), chatPane);
    m_refreshModelsButton = new QPushButton(QStringLiteral("List models"), chatPane);
    m_rememberButton = new QPushButton(QStringLiteral("Remember input"), chatPane);
    m_importFilesButton = new QPushButton(QStringLiteral("Import files"), chatPane);
    m_importFolderButton = new QPushButton(QStringLiteral("Import folder"), chatPane);
    m_statusLabel = new QLabel(QStringLiteral("Ready."), chatPane);
    m_busyIndicatorLabel = new QLabel(chatPane);
    m_busyIndicatorLabel->setMinimumWidth(220);
    m_busyIndicatorLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_busyIndicatorLabel->setStyleSheet(QStringLiteral("font-weight: 700; color: palette(highlight);"));
    m_busyIndicatorLabel->hide();
    m_busyIndicatorTimer = new QTimer(this);
    m_busyIndicatorTimer->setInterval(100);

    m_stopButton->setEnabled(false);

    controlsLayout->addWidget(m_importFilesButton);
    controlsLayout->addWidget(m_importFolderButton);
    controlsLayout->addWidget(m_rememberButton);
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(m_testBackendButton);
    controlsLayout->addWidget(m_refreshModelsButton);
    controlsLayout->addWidget(m_reindexButton);
    controlsLayout->addWidget(m_stopButton);
    controlsLayout->addWidget(m_sendButton);

    chatLayout->addLayout(titleRow);
    chatLayout->addWidget(m_transcript, 1);
    chatLayout->addWidget(m_input);
    chatLayout->addLayout(toolbarLayout);
    chatLayout->addLayout(controlsLayout);

    auto *statusLayout = new QHBoxLayout();
    statusLayout->addWidget(m_statusLabel, 1);
    statusLayout->addWidget(m_busyIndicatorLabel, 0);
    chatLayout->addLayout(statusLayout);

    auto *rightPane = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPane);
    auto *contextTitle = new QLabel(QStringLiteral("Inspection Panels"), rightPane);
    contextTitle->setStyleSheet(QStringLiteral("font-weight: 700;"));

    auto *tabs = new QTabWidget(rightPane);

    m_privacyPreview = new QPlainTextEdit(tabs);
    m_privacyPreview->setReadOnly(true);
    tabs->addTab(m_privacyPreview, QStringLiteral("Privacy"));

    m_outlinePlan = new QPlainTextEdit(tabs);
    m_outlinePlan->setReadOnly(true);
    tabs->addTab(m_outlinePlan, QStringLiteral("Outline Plan"));

    m_localSources = new QPlainTextEdit(tabs);
    m_localSources->setReadOnly(true);
    tabs->addTab(m_localSources, QStringLiteral("Local Sources"));

    m_externalSources = new QPlainTextEdit(tabs);
    m_externalSources->setReadOnly(true);
    tabs->addTab(m_externalSources, QStringLiteral("External Sources"));

    m_sourceInventory = new QPlainTextEdit(tabs);
    m_sourceInventory->setReadOnly(true);
    tabs->addTab(m_sourceInventory, QStringLiteral("Knowledge Base"));

    m_memoriesView = new QPlainTextEdit(tabs);
    m_memoriesView->setReadOnly(true);
    tabs->addTab(m_memoriesView, QStringLiteral("Memory"));

    m_sessionSummary = new QPlainTextEdit(tabs);
    m_sessionSummary->setReadOnly(true);
    tabs->addTab(m_sessionSummary, QStringLiteral("Session Summary"));

    m_backendSummary = new QPlainTextEdit(tabs);
    m_backendSummary->setReadOnly(true);
    tabs->addTab(m_backendSummary, QStringLiteral("Backend"));

    m_diagnostics = new QPlainTextEdit(tabs);
    m_diagnostics->setReadOnly(true);
    tabs->addTab(m_diagnostics, QStringLiteral("Diagnostics"));

    rightLayout->addWidget(contextTitle);
    rightLayout->addWidget(tabs, 1);

    splitter->addWidget(sessionPane);
    splitter->addWidget(chatPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 2);

    rootLayout->addWidget(splitter, 1);
    setCentralWidget(central);
    resize(1700, 960);

    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopRequested);
    connect(m_reindexButton, &QPushButton::clicked, this, &MainWindow::reindexRequested);
    connect(m_testBackendButton, &QPushButton::clicked, this, &MainWindow::testBackendRequested);
    connect(m_refreshModelsButton, &QPushButton::clicked, this, &MainWindow::refreshModelsRequested);
    connect(m_newConversationButton, &QPushButton::clicked, this, &MainWindow::newConversationRequested);
    connect(m_rememberButton, &QPushButton::clicked, this, &MainWindow::onRememberClicked);
    connect(m_importFilesButton, &QPushButton::clicked, this, &MainWindow::onImportFilesClicked);
    connect(m_importFolderButton, &QPushButton::clicked, this, &MainWindow::onImportFolderClicked);
    connect(m_modelCombo, &QComboBox::currentTextChanged, this, &MainWindow::onModelSelectionChanged);
    connect(m_conversationsList, &QListWidget::currentItemChanged, this, &MainWindow::onConversationItemChanged);
    connect(m_busyIndicatorTimer, &QTimer::timeout, this, &MainWindow::updateBusyIndicator);
}

void MainWindow::appendUserMessage(const QString &text)
{
    m_streamingAssistant = false;
    m_transcript->appendPlainText(QStringLiteral("\nUSER> ") + text + QStringLiteral("\n"));
}

void MainWindow::appendAssistantChunk(const QString &text)
{
    QTextCursor cursor = m_transcript->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_transcript->setTextCursor(cursor);

    if (!m_streamingAssistant) {
        m_streamingAssistant = true;
        cursor.insertText(QStringLiteral("ASSISTANT> "));
    }

    cursor.insertText(text);
    m_transcript->setTextCursor(cursor);
    m_transcript->ensureCursorVisible();
}

void MainWindow::finalizeAssistantMessage(const QString &text)
{
    Q_UNUSED(text)
    if (m_streamingAssistant) {
        QTextCursor cursor = m_transcript->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QStringLiteral("\n"));
        m_transcript->setTextCursor(cursor);
    }
    m_streamingAssistant = false;
}

void MainWindow::appendSystemMessage(const QString &text)
{
    m_streamingAssistant = false;
    m_transcript->appendPlainText(QStringLiteral("[system] ") + text + QStringLiteral("\n"));
}

void MainWindow::setPrivacyPreview(const QString &text)
{
    m_privacyPreview->setPlainText(text);
}

void MainWindow::setLocalSources(const QString &text)
{
    m_localSources->setPlainText(text);
}

void MainWindow::setExternalSources(const QString &text)
{
    m_externalSources->setPlainText(text);
}

void MainWindow::setOutlinePlan(const QString &text)
{
    m_outlinePlan->setPlainText(text);
}

void MainWindow::setMemoriesView(const QString &text)
{
    m_memoriesView->setPlainText(text);
}

void MainWindow::setSessionSummary(const QString &text)
{
    m_sessionSummary->setPlainText(text);
}

void MainWindow::setTranscript(const QString &text)
{
    m_streamingAssistant = false;
    m_transcript->setPlainText(text);
    QTextCursor cursor = m_transcript->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_transcript->setTextCursor(cursor);
}

void MainWindow::setConversationList(const QStringList &ids,
                                     const QStringList &titles,
                                     const QString &currentId)
{
    m_updatingConversationList = true;
    m_conversationsList->clear();

    const int count = qMin(ids.size(), titles.size());
    for (int i = 0; i < count; ++i) {
        auto *item = new QListWidgetItem(titles.at(i), m_conversationsList);
        item->setData(Qt::UserRole, ids.at(i));
        if (ids.at(i) == currentId) {
            m_conversationsList->setCurrentItem(item);
        }
    }

    m_updatingConversationList = false;
}

void MainWindow::setStatusText(const QString &text)
{
    m_statusLabel->setText(text);
}

void MainWindow::setBusy(bool busy)
{
    m_sendButton->setEnabled(!busy);
    m_stopButton->setEnabled(busy);
    m_input->setReadOnly(busy);
    m_newConversationButton->setEnabled(!busy);
    m_importFilesButton->setEnabled(!busy);
    m_importFolderButton->setEnabled(!busy);

    if (busy) {
        m_busyFrameIndex = 0;
        updateBusyIndicator();
        m_busyIndicatorLabel->show();
        m_busyIndicatorTimer->start();
    } else {
        m_busyIndicatorTimer->stop();
        m_busyIndicatorLabel->hide();
        m_busyIndicatorLabel->clear();
    }
}

void MainWindow::setBackendSummary(const QString &text)
{
    m_backendSummary->setPlainText(text);
}

void MainWindow::setDiagnostics(const QString &text)
{
    m_diagnostics->setPlainText(text);
    QTextCursor cursor = m_diagnostics->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_diagnostics->setTextCursor(cursor);
}

void MainWindow::setSourceInventory(const QString &text)
{
    m_sourceInventory->setPlainText(text);
}

void MainWindow::setAvailableModels(const QStringList &models, const QString &currentModel)
{
    m_updatingModelList = true;
    QStringList uniqueModels = models;
    if (!currentModel.trimmed().isEmpty() && !uniqueModels.contains(currentModel)) {
        uniqueModels.prepend(currentModel);
    }
    if (uniqueModels.isEmpty()) {
        uniqueModels << (currentModel.trimmed().isEmpty() ? QStringLiteral("qwen2.5-coder:14b") : currentModel.trimmed());
    }

    m_modelCombo->clear();
    m_modelCombo->addItems(uniqueModels);
    const int index = m_modelCombo->findText(currentModel);
    m_modelCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_updatingModelList = false;
}

void MainWindow::setExternalSearchEnabledDefault(bool enabled)
{
    m_externalSearchCheck->setChecked(enabled);
}

void MainWindow::onSendClicked()
{
    const QString prompt = m_input->toPlainText().trimmed();
    if (prompt.isEmpty()) {
        return;
    }

    appendUserMessage(prompt);
    m_input->clear();
    emit promptSubmitted(prompt, m_externalSearchCheck->isChecked());
}

void MainWindow::onConversationItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous)
    if (m_updatingConversationList || current == nullptr) {
        return;
    }
    emit conversationSelected(current->data(Qt::UserRole).toString());
}

void MainWindow::onRememberClicked()
{
    const QString candidate = m_input->toPlainText().trimmed();
    if (candidate.isEmpty()) {
        return;
    }
    emit rememberRequested(candidate);
}

void MainWindow::onImportFilesClicked()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this,
                                                            QStringLiteral("Import files into Amelia knowledge base"),
                                                            QString(),
                                                            QStringLiteral("All Files (*.*)"));
    if (!paths.isEmpty()) {
        emit importPathsRequested(paths);
    }
}

void MainWindow::onImportFolderClicked()
{
    const QString folder = QFileDialog::getExistingDirectory(this,
                                                             QStringLiteral("Import folder into Amelia knowledge base"));
    if (!folder.isEmpty()) {
        emit importPathsRequested(QStringList() << folder);
    }
}

void MainWindow::onModelSelectionChanged(const QString &model)
{
    if (m_updatingModelList || model.trimmed().isEmpty()) {
        return;
    }
    emit backendModelSelected(model.trimmed());
}

void MainWindow::updateBusyIndicator()
{
    if (m_busyFrames.isEmpty()) {
        m_busyIndicatorLabel->setText(QStringLiteral("Thinking..."));
        return;
    }

    const QString &frame = m_busyFrames.at(m_busyFrameIndex % m_busyFrames.size());
    m_busyIndicatorLabel->setText(QStringLiteral("%1 Thinking / budgeting / retrieving / generating").arg(frame));
    ++m_busyFrameIndex;
}

void MainWindow::showAboutAmelia()
{
    QMessageBox::about(this,
                       QStringLiteral("About Amelia"),
                       QStringLiteral("<b>Amelia Qt6</b><br><br>"
                                      "A local offline coding and cloud assistant built with C++ and Qt6.<br><br>"
                                      "This build focuses on local Ollama inference, persistent knowledge, prompt budgeting, outline-first document generation, and operational diagnostics."));
}

void MainWindow::showAboutQtDialog()
{
    QApplication::aboutQt();
}

void MainWindow::onClearMemoriesTriggered()
{
    const auto answer = QMessageBox::question(this,
                                              QStringLiteral("Clear stored memories"),
                                              QStringLiteral("Delete all persisted memories from Amelia? This cannot be undone."),
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    emit clearMemoriesRequested();
}
