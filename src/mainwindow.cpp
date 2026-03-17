#include "mainwindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QPixmap>

namespace {
QColor transcriptPrefixColor(const QString &role)
{
    const QString lower = role.toLower();
    if (lower == QStringLiteral("user")) {
        return QColor(QStringLiteral("#4ea1ff"));
    }
    if (lower == QStringLiteral("assistant")) {
        return QColor(QStringLiteral("#34d399"));
    }
    if (lower == QStringLiteral("system")) {
        return QColor(QStringLiteral("#f59e0b"));
    }
    if (lower == QStringLiteral("status")) {
        return QColor(QStringLiteral("#a78bfa"));
    }
    return QColor(QStringLiteral("#d1d5db"));
}

QColor transcriptBodyColor(const QString &role)
{
    const QString lower = role.toLower();
    if (lower == QStringLiteral("system")) {
        return QColor(QStringLiteral("#f8c471"));
    }
    return QColor(QStringLiteral("#e5e7eb"));
}

QString transcriptPrefix(const QString &role)
{
    const QString lower = role.toLower();
    if (lower == QStringLiteral("user")) {
        return QStringLiteral("USER> ");
    }
    if (lower == QStringLiteral("assistant")) {
        return QStringLiteral("ASSISTANT> ");
    }
    if (lower == QStringLiteral("system")) {
        return QStringLiteral("[system] ");
    }
    if (lower == QStringLiteral("status")) {
        return QStringLiteral("[status] ");
    }
    return QStringLiteral("[") + role + QStringLiteral("] ");
}

QColor diagnosticCategoryColor(const QString &category)
{
    const QString lower = category.toLower();
    if (lower == QStringLiteral("backend")) {
        return QColor(QStringLiteral("#60a5fa"));
    }
    if (lower == QStringLiteral("search")) {
        return QColor(QStringLiteral("#22c55e"));
    }
    if (lower == QStringLiteral("rag")) {
        return QColor(QStringLiteral("#14b8a6"));
    }
    if (lower == QStringLiteral("memory")) {
        return QColor(QStringLiteral("#f97316"));
    }
    if (lower == QStringLiteral("planner")) {
        return QColor(QStringLiteral("#a78bfa"));
    }
    if (lower == QStringLiteral("guardrail")) {
        return QColor(QStringLiteral("#ef4444"));
    }
    if (lower == QStringLiteral("ingest")) {
        return QColor(QStringLiteral("#eab308"));
    }
    if (lower == QStringLiteral("startup")) {
        return QColor(QStringLiteral("#f472b6"));
    }
    if (lower == QStringLiteral("budget")) {
        return QColor(QStringLiteral("#38bdf8"));
    }
    if (lower == QStringLiteral("chat")) {
        return QColor(QStringLiteral("#c084fc"));
    }
    return QColor(QStringLiteral("#d1d5db"));
}

QStringList splitAssetPaths(const QString &raw)
{
    QString normalized = raw;
    normalized.replace(QLatin1Char('\n'), QLatin1Char(';'));
    normalized.replace(QLatin1Char(','), QLatin1Char(';'));
    const QStringList parts = normalized.split(QLatin1Char(';'), Qt::SkipEmptyParts);

    QStringList paths;
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            paths << trimmed;
        }
    }
    return paths;
}
}

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

    m_transcript = new QTextEdit(chatPane);
    m_transcript->setReadOnly(true);
    m_transcript->setPlaceholderText(QStringLiteral("Conversation transcript..."));
    m_transcript->setStyleSheet(QStringLiteral("QTextEdit { background: #0f172a; color: #e5e7eb; }"));

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

    m_diagnostics = new QTextEdit(tabs);
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setStyleSheet(QStringLiteral("QTextEdit { background: #0b1220; color: #e5e7eb; }"));
    tabs->addTab(m_diagnostics, QStringLiteral("Diagnostics"));

    auto *promptLabTab = new QWidget(tabs);
    auto *promptLabLayout = new QVBoxLayout(promptLabTab);
    auto *promptLabForm = new QFormLayout();
    m_promptLabPresetCombo = new QComboBox(promptLabTab);
    m_promptLabPresetCombo->addItems({
        QStringLiteral("General grounding"),
        QStringLiteral("Code patch"),
        QStringLiteral("Runbook / docs"),
        QStringLiteral("Incident investigation"),
        QStringLiteral("Dataset from assets")
    });
    m_promptLabGoal = new QLineEdit(promptLabTab);
    m_promptLabGoal->setPlaceholderText(QStringLiteral("What should Amelia learn or generate from these assets?"));
    m_promptLabAssets = new QLineEdit(promptLabTab);
    m_promptLabAssets->setPlaceholderText(QStringLiteral("/path/a;/path/b;~/notes.txt"));
    promptLabForm->addRow(QStringLiteral("Preset:"), m_promptLabPresetCombo);
    promptLabForm->addRow(QStringLiteral("Goal:"), m_promptLabGoal);
    promptLabForm->addRow(QStringLiteral("Assets:"), m_promptLabAssets);

    m_promptLabNotes = new QTextEdit(promptLabTab);
    m_promptLabNotes->setPlaceholderText(QStringLiteral("Optional notes, style constraints, schema hints, prompt fragments, expected labels, etc."));
    m_promptLabNotes->setMaximumHeight(130);

    auto *promptLabButtons = new QHBoxLayout();
    m_promptLabGenerateButton = new QPushButton(QStringLiteral("Compose recipe"), promptLabTab);
    m_promptLabUseButton = new QPushButton(QStringLiteral("Use in input"), promptLabTab);
    m_promptLabImportButton = new QPushButton(QStringLiteral("Import listed assets"), promptLabTab);
    promptLabButtons->addWidget(m_promptLabGenerateButton);
    promptLabButtons->addWidget(m_promptLabUseButton);
    promptLabButtons->addWidget(m_promptLabImportButton);

    m_promptLabPreview = new QTextEdit(promptLabTab);
    m_promptLabPreview->setReadOnly(true);
    m_promptLabPreview->setPlaceholderText(QStringLiteral("Composed prompt recipe and JSONL preview appear here."));

    promptLabLayout->addLayout(promptLabForm);
    promptLabLayout->addWidget(new QLabel(QStringLiteral("Notes / constraints"), promptLabTab));
    promptLabLayout->addWidget(m_promptLabNotes);
    promptLabLayout->addLayout(promptLabButtons);
    promptLabLayout->addWidget(m_promptLabPreview, 1);
    tabs->addTab(promptLabTab, QStringLiteral("Prompt Lab"));

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
    connect(m_promptLabGenerateButton, &QPushButton::clicked, this, &MainWindow::onPromptLabGenerateClicked);
    connect(m_promptLabUseButton, &QPushButton::clicked, this, &MainWindow::onPromptLabUseClicked);
    connect(m_promptLabImportButton, &QPushButton::clicked, this, &MainWindow::onPromptLabImportAssetsClicked);

    if (m_promptLabPreview != nullptr) {
        m_promptLabPreview->setPlainText(buildPromptLabRecipe());
    }
}


void MainWindow::appendTranscriptEntry(const QString &role, const QString &text)
{
    if (m_transcript == nullptr) {
        return;
    }

    QTextCursor cursor = m_transcript->textCursor();
    cursor.movePosition(QTextCursor::End);
    if (!m_transcript->document()->isEmpty()) {
        cursor.insertBlock();
    }

    QTextCharFormat prefixFormat;
    prefixFormat.setFontWeight(QFont::Bold);
    prefixFormat.setForeground(transcriptPrefixColor(role));

    QTextCharFormat bodyFormat;
    bodyFormat.setForeground(transcriptBodyColor(role));

    cursor.insertText(transcriptPrefix(role), prefixFormat);
    cursor.insertText(text, bodyFormat);
    cursor.insertBlock();

    m_transcript->setTextCursor(cursor);
    m_transcript->ensureCursorVisible();
}

void MainWindow::appendDiagnosticEntry(const QString &timestamp, const QString &category, const QString &message)
{
    if (m_diagnostics == nullptr) {
        return;
    }

    QTextCursor cursor = m_diagnostics->textCursor();
    cursor.movePosition(QTextCursor::End);
    if (!m_diagnostics->document()->isEmpty()) {
        cursor.insertBlock();
    }

    QTextCharFormat timeFormat;
    timeFormat.setForeground(QColor(QStringLiteral("#94a3b8")));

    QTextCharFormat categoryFormat;
    categoryFormat.setFontWeight(QFont::Bold);
    categoryFormat.setForeground(diagnosticCategoryColor(category));

    QTextCharFormat messageFormat;
    messageFormat.setForeground(QColor(QStringLiteral("#e5e7eb")));

    cursor.insertText(QStringLiteral("[") + timestamp + QStringLiteral("] "), timeFormat);
    cursor.insertText(QStringLiteral("[") + category + QStringLiteral("] "), categoryFormat);
    cursor.insertText(message, messageFormat);

    m_diagnostics->setTextCursor(cursor);
    m_diagnostics->ensureCursorVisible();
}

void MainWindow::appendUserMessage(const QString &text)
{
    m_streamingAssistant = false;
    appendTranscriptEntry(QStringLiteral("user"), text);
}

void MainWindow::appendAssistantChunk(const QString &text)
{
    QTextCursor cursor = m_transcript->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat prefixFormat;
    prefixFormat.setFontWeight(QFont::Bold);
    prefixFormat.setForeground(transcriptPrefixColor(QStringLiteral("assistant")));

    QTextCharFormat bodyFormat;
    bodyFormat.setForeground(transcriptBodyColor(QStringLiteral("assistant")));

    if (!m_streamingAssistant) {
        if (!m_transcript->document()->isEmpty()) {
            cursor.insertBlock();
        }
        cursor.insertText(transcriptPrefix(QStringLiteral("assistant")), prefixFormat);
        m_streamingAssistant = true;
    }

    cursor.insertText(text, bodyFormat);
    m_transcript->setTextCursor(cursor);
    m_transcript->ensureCursorVisible();
}

void MainWindow::finalizeAssistantMessage(const QString &text)
{
    Q_UNUSED(text)
    if (m_streamingAssistant) {
        QTextCursor cursor = m_transcript->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        m_transcript->setTextCursor(cursor);
    }
    m_streamingAssistant = false;
}

void MainWindow::appendSystemMessage(const QString &text)
{
    m_streamingAssistant = false;
    appendTranscriptEntry(QStringLiteral("system"), text);
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

void MainWindow::rebuildTranscriptFromPlainText(const QString &text)
{
    m_transcript->clear();
    m_streamingAssistant = false;

    const QStringList blocks = text.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        const QString trimmed = block.trimmed();
        if (trimmed.startsWith(QStringLiteral("USER> "))) {
            appendTranscriptEntry(QStringLiteral("user"), trimmed.mid(6));
        } else if (trimmed.startsWith(QStringLiteral("ASSISTANT> "))) {
            appendTranscriptEntry(QStringLiteral("assistant"), trimmed.mid(11));
        } else if (trimmed.startsWith(QStringLiteral("[system] "))) {
            appendTranscriptEntry(QStringLiteral("system"), trimmed.mid(9));
        } else if (!trimmed.isEmpty()) {
            appendTranscriptEntry(QStringLiteral("system"), trimmed);
        }
    }
}

void MainWindow::setTranscript(const QString &text)
{
    rebuildTranscriptFromPlainText(text);
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
    m_promptLabGenerateButton->setEnabled(!busy);
    m_promptLabUseButton->setEnabled(!busy);
    m_promptLabImportButton->setEnabled(!busy);

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

void MainWindow::rebuildDiagnosticsFromPlainText(const QString &text)
{
    m_diagnostics->clear();

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QRegularExpression regex(QStringLiteral(R"(^\[([^\]]+)\]\s+\[([^\]]+)\]\s+(.*)$)"));

    for (const QString &line : lines) {
        const QRegularExpressionMatch match = regex.match(line);
        if (match.hasMatch()) {
            appendDiagnosticEntry(match.captured(1), match.captured(2), match.captured(3));
        } else {
            appendDiagnosticEntry(QStringLiteral("log"), QStringLiteral("misc"), line);
        }
    }
}

void MainWindow::setDiagnostics(const QString &text)
{
    rebuildDiagnosticsFromPlainText(text);
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

QString MainWindow::buildPromptLabRecipe() const
{
    const QString preset = m_promptLabPresetCombo != nullptr ? m_promptLabPresetCombo->currentText() : QStringLiteral("General grounding");
    const QString goal = m_promptLabGoal != nullptr ? m_promptLabGoal->text().trimmed() : QString();
    const QString notes = m_promptLabNotes != nullptr ? m_promptLabNotes->toPlainText().trimmed() : QString();
    const QStringList assets = m_promptLabAssets != nullptr ? splitAssetPaths(m_promptLabAssets->text()) : QStringList();

    QStringList directives;
    if (preset == QStringLiteral("Code patch")) {
        directives << QStringLiteral("Study the imported assets before suggesting code changes.")
                   << QStringLiteral("Preserve existing naming and architecture.")
                   << QStringLiteral("Output paste-ready functions or files only when asked.");
    } else if (preset == QStringLiteral("Runbook / docs")) {
        directives << QStringLiteral("Extract grounded operational steps from the imported assets.")
                   << QStringLiteral("Prefer concise procedures, prerequisites, rollback, and validation.")
                   << QStringLiteral("Do not invent commands or environment details.");
    } else if (preset == QStringLiteral("Incident investigation")) {
        directives << QStringLiteral("Correlate clues from logs, configs, and notes in the imported assets.")
                   << QStringLiteral("Separate evidence, hypotheses, and proposed checks.")
                   << QStringLiteral("Highlight the smallest next diagnostic step.");
    } else if (preset == QStringLiteral("Dataset from assets")) {
        directives << QStringLiteral("Transform the imported assets into reusable supervision examples.")
                   << QStringLiteral("Produce compact input/output training pairs grounded in those assets.")
                   << QStringLiteral("Reject unsupported labels or invented metadata.");
    } else {
        directives << QStringLiteral("Ground every answer in the imported assets and the user request.")
                   << QStringLiteral("Prefer explicit citations to file names or sections when possible.")
                   << QStringLiteral("Say when information is missing instead of guessing.");
    }

    const QString displayGoal = goal.isEmpty() ? QStringLiteral("<describe the target outcome>") : goal;
    const QString displayAssets = assets.isEmpty() ? QStringLiteral("<none listed>") : assets.join(QStringLiteral("; "));
    QString escapedGoal = goal.isEmpty() ? QStringLiteral("<goal>") : goal;
    QString escapedAssets = assets.isEmpty() ? QStringLiteral("<none>") : assets.join(QStringLiteral("; "));
    escapedGoal.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    escapedAssets.replace(QStringLiteral("\""), QStringLiteral("\\\""));

    QStringList lines;
    lines << QStringLiteral("Prompt Lab recipe");
    lines << QStringLiteral("=================");
    lines << QStringLiteral("Preset: %1").arg(preset);
    lines << QStringLiteral("Goal: %1").arg(displayGoal);
    lines << QStringLiteral("Assets: %1").arg(displayAssets);
    lines << QString();
    lines << QStringLiteral("Use this with Amelia:");
    lines << QStringLiteral("You will study the imported assets and answer only from them.");
    for (const QString &directive : directives) {
        lines << QStringLiteral("- %1").arg(directive);
    }
    if (!notes.isEmpty()) {
        lines << QStringLiteral("- Extra constraints: %1").arg(notes);
    }
    lines << QString();
    lines << QStringLiteral("Suggested working prompt:");
    lines << QStringLiteral("Analyze the imported assets and help with this goal: %1").arg(escapedGoal);
    lines << QStringLiteral("Return a grounded answer and identify which imported materials were used.");
    lines << QString();
    lines << QStringLiteral("JSONL training sample preview:");
    lines << QStringLiteral("{\"messages\":[{\"role\":\"system\",\"content\":\"You are Amelia. Answer only from the imported assets.\"},{\"role\":\"user\",\"content\":\"Goal: %1\\nAssets: %2\"},{\"role\":\"assistant\",\"content\":\"Grounded response based on the imported assets.\"}]}")
                .arg(escapedGoal, escapedAssets);

    return lines.join(QStringLiteral("\n"));
}

void MainWindow::onPromptLabGenerateClicked()
{
    if (m_promptLabPreview != nullptr) {
        m_promptLabPreview->setPlainText(buildPromptLabRecipe());
    }
    m_statusLabel->setText(QStringLiteral("Prompt Lab recipe updated."));
}

void MainWindow::onPromptLabUseClicked()
{
    const QString recipe = buildPromptLabRecipe();
    m_promptLabPreview->setPlainText(recipe);
    m_input->setPlainText(recipe);
    m_input->setFocus();
    m_statusLabel->setText(QStringLiteral("Prompt Lab recipe copied to the main input."));
}

void MainWindow::onPromptLabImportAssetsClicked()
{
    const QStringList paths = splitAssetPaths(m_promptLabAssets != nullptr ? m_promptLabAssets->text() : QString());
    if (paths.isEmpty()) {
        QMessageBox::information(this,
                                 QStringLiteral("Prompt Lab"),
                                 QStringLiteral("Add one or more asset paths first. Separate them with ';' or ','."));
        return;
    }

    emit importPathsRequested(paths);
    m_statusLabel->setText(QStringLiteral("Prompt Lab requested asset import for %1 path(s).").arg(paths.size()));
}

void MainWindow::showAboutAmelia()
{
    QMessageBox::about(this,
                       QStringLiteral("About Amelia"),
                       QStringLiteral("<b>Amelia Qt6</b><br><br>"
                                      "A local offline coding and cloud assistant built with C++ and Qt6.<br><br>"
                                      "This build includes colored transcript/diagnostic views, local Ollama inference, persistent knowledge, prompt budgeting, outline-first document generation, Prompt Lab asset training helpers, and operational diagnostics."));
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
