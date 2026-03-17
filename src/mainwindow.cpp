#include "mainwindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
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
#include <QPoint>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QPixmap>

#ifndef AMELIA_VERSION_STRING
#define AMELIA_VERSION_STRING "6.5"
#endif

namespace {
QString appVersionString()
{
    return QStringLiteral(AMELIA_VERSION_STRING);
}

QString appDisplayName()
{
    return QStringLiteral("Amelia Qt6 v%1").arg(appVersionString());
}

QColor transcriptPrefixColor(const QString &role)
{
    const QString lower = role.toLower();
    if (lower == QStringLiteral("user")) {
        return QColor(QStringLiteral("#60a5fa"));
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
        return QColor(QStringLiteral("#fde68a"));
    }
    if (lower == QStringLiteral("assistant")) {
        return QColor(QStringLiteral("#e5e7eb"));
    }
    return QColor(QStringLiteral("#dbe4f0"));
}

QString transcriptPrefix(const QString &role)
{
    const QString lower = role.toLower();
    if (lower == QStringLiteral("user")) {
        return QStringLiteral("USER");
    }
    if (lower == QStringLiteral("assistant")) {
        return QStringLiteral("ASSISTANT");
    }
    if (lower == QStringLiteral("system")) {
        return QStringLiteral("SYSTEM");
    }
    if (lower == QStringLiteral("status")) {
        return QStringLiteral("STATUS");
    }
    return role.toUpper();
}

QString transcriptRoleBadgeBackground(const QString &role)
{
    const QString lower = role.toLower();
    if (lower == QStringLiteral("user")) {
        return QStringLiteral("rgba(37, 99, 235, 0.22)");
    }
    if (lower == QStringLiteral("assistant")) {
        return QStringLiteral("rgba(5, 150, 105, 0.22)");
    }
    if (lower == QStringLiteral("system")) {
        return QStringLiteral("rgba(217, 119, 6, 0.22)");
    }
    return QStringLiteral("rgba(148, 163, 184, 0.18)");
}

QString transcriptCardBackground(const QString &role)
{
    const QString lower = role.toLower();
    if (lower == QStringLiteral("assistant")) {
        return QStringLiteral("#111827");
    }
    if (lower == QStringLiteral("user")) {
        return QStringLiteral("#0f172a");
    }
    if (lower == QStringLiteral("system")) {
        return QStringLiteral("#1f2937");
    }
    return QStringLiteral("#111827");
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
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    normalized.replace(QLatin1Char(','), QLatin1Char(';'));
    normalized.replace(QLatin1Char('\n'), QLatin1Char(';'));
    const QStringList parts = normalized.split(QLatin1Char(';'), Qt::SkipEmptyParts);

    QStringList values;
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty() && !values.contains(trimmed)) {
            values << trimmed;
        }
    }
    return values;
}

QStringList extractInventoryFilePaths(const QString &inventoryText)
{
    QStringList files;
    const QStringList lines = inventoryText.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("File: "))) {
            const QString path = line.mid(6).trimmed();
            if (!path.isEmpty() && !files.contains(path)) {
                files << path;
            }
        }
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

QString extractBodyFromHtmlDocument(const QString &html)
{
    static const QRegularExpression bodyRegex(QStringLiteral(R"(<body[^>]*>(.*)</body>)"),
                                              QRegularExpression::DotMatchesEverythingOption
                                              | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = bodyRegex.match(html);
    if (match.hasMatch()) {
        return match.captured(1);
    }
    return html;
}

QString escapedPre(const QString &text)
{
    QString value = text;
    while (value.endsWith(QLatin1Char('\n'))) {
        value.chop(1);
    }
    return value.toHtmlEscaped();
}

QString summarizeCodeBlock(const QString &code)
{
    const QStringList lines = code.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            QString summary = trimmed;
            if (summary.size() > 46) {
                summary = summary.left(43) + QStringLiteral("...");
            }
            return summary;
        }
    }
    return QStringLiteral("empty block");
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_busyFrames({QStringLiteral("◐"), QStringLiteral("◓"), QStringLiteral("◑"), QStringLiteral("◒")})
{
    setWindowTitle(appDisplayName());
    setMinimumSize(1380, 860);

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
    auto *sessionTitle = new QLabel(QStringLiteral("Conversations"), sessionPane);
    sessionTitle->setStyleSheet(QStringLiteral("font-weight: 700;"));
    m_newConversationButton = new QPushButton(QStringLiteral("New conversation"), sessionPane);
    m_conversationsList = new QListWidget(sessionPane);
    m_conversationsList->setSelectionMode(QAbstractItemView::SingleSelection);
    sessionLayout->addWidget(sessionTitle);
    sessionLayout->addWidget(m_newConversationButton);
    sessionLayout->addWidget(m_conversationsList, 1);

    auto *chatPane = new QWidget(splitter);
    auto *chatLayout = new QVBoxLayout(chatPane);

    auto *titleRow = new QHBoxLayout();
    auto *title = new QLabel(appDisplayName(), chatPane);
    title->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 18px;"));
    auto *logoLabel = new QLabel(chatPane);
    const QPixmap logoPixmap(QStringLiteral(":/branding/amelia_logo.svg"));
    logoLabel->setPixmap(logoPixmap.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setToolTip(QStringLiteral("Amelia logo"));
    titleRow->addWidget(title);
    titleRow->addWidget(logoLabel, 0, Qt::AlignVCenter);
    titleRow->addStretch(1);

    m_transcript = new QTextBrowser(chatPane);
    m_transcript->setOpenExternalLinks(false);
    m_transcript->setOpenLinks(false);
    m_transcript->setReadOnly(true);
    m_transcript->setContextMenuPolicy(Qt::CustomContextMenu);
    m_transcript->setPlaceholderText(QStringLiteral("Conversation transcript..."));
    m_transcript->setStyleSheet(QStringLiteral(
        "QTextBrowser { background: #020617; color: #e5e7eb; border: 1px solid #1e293b; border-radius: 10px; padding: 6px; }"));

    auto *transcriptTools = new QHBoxLayout();
    m_copyLastAnswerButton = new QPushButton(QStringLiteral("Copy last answer"), chatPane);
    m_copyTranscriptButton = new QPushButton(QStringLiteral("Copy transcript"), chatPane);
    m_codeBlockCombo = new QComboBox(chatPane);
    m_codeBlockCombo->setMinimumWidth(320);
    m_codeBlockCombo->addItem(QStringLiteral("No code blocks yet"));
    m_copyCodeBlockButton = new QPushButton(QStringLiteral("Copy code block"), chatPane);
    transcriptTools->addWidget(m_copyLastAnswerButton);
    transcriptTools->addWidget(m_copyTranscriptButton);
    transcriptTools->addStretch(1);
    transcriptTools->addWidget(new QLabel(QStringLiteral("Blocks:"), chatPane));
    transcriptTools->addWidget(m_codeBlockCombo);
    transcriptTools->addWidget(m_copyCodeBlockButton);

    m_input = new QPlainTextEdit(chatPane);
    m_input->setPlaceholderText(QStringLiteral("Type your prompt here..."));
    m_input->setMaximumHeight(140);

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
    chatLayout->addLayout(transcriptTools);
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
        QStringLiteral("Dataset from assets"),
        QStringLiteral("Executive summary"),
        QStringLiteral("Knowledge extraction"),
        QStringLiteral("KB-only analysis"),
        QStringLiteral("Migration / refactor plan")
    });
    m_promptLabGoal = new QLineEdit(promptLabTab);
    m_promptLabGoal->setPlaceholderText(QStringLiteral("What should Amelia learn, extract, summarize, or generate?"));
    promptLabForm->addRow(QStringLiteral("Preset:"), m_promptLabPresetCombo);
    promptLabForm->addRow(QStringLiteral("Goal:"), m_promptLabGoal);

    m_promptLabImportAssetsEdit = new QPlainTextEdit(promptLabTab);
    m_promptLabImportAssetsEdit->setPlaceholderText(
        QStringLiteral("List filesystem assets to import, one per line or separated by ';' or ','\n"
                       "/path/to/file.md\n/path/to/folder"));
    m_promptLabImportAssetsEdit->setMaximumHeight(110);

    auto *importButtons = new QHBoxLayout();
    m_promptLabBrowseAssetsButton = new QPushButton(QStringLiteral("Select files"), promptLabTab);
    m_promptLabBrowseFolderButton = new QPushButton(QStringLiteral("Select folder"), promptLabTab);
    m_promptLabImportButton = new QPushButton(QStringLiteral("Import listed assets"), promptLabTab);
    m_promptLabClearAssetsButton = new QPushButton(QStringLiteral("Clear asset inputs"), promptLabTab);
    importButtons->addWidget(m_promptLabBrowseAssetsButton);
    importButtons->addWidget(m_promptLabBrowseFolderButton);
    importButtons->addWidget(m_promptLabImportButton);
    importButtons->addWidget(m_promptLabClearAssetsButton);

    m_promptLabKbFilter = new QLineEdit(promptLabTab);
    m_promptLabKbFilter->setPlaceholderText(QStringLiteral("Filter knowledge-base assets by file name or path"));

    m_promptLabKbAssetsList = new QListWidget(promptLabTab);
    m_promptLabKbAssetsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_promptLabKbAssetsList->setAlternatingRowColors(true);

    auto *kbButtons = new QHBoxLayout();
    m_promptLabAddSelectedKbButton = new QPushButton(QStringLiteral("Add selected KB assets"), promptLabTab);
    kbButtons->addWidget(m_promptLabAddSelectedKbButton);
    kbButtons->addStretch(1);

    m_promptLabKbManualEdit = new QLineEdit(promptLabTab);
    m_promptLabKbManualEdit->setPlaceholderText(
        QStringLiteral("Extra KB asset identifiers already indexed (paths, names, tags) separated by ';'"));

    m_promptLabNotes = new QTextEdit(promptLabTab);
    m_promptLabNotes->setPlaceholderText(QStringLiteral("Optional notes, style constraints, schema hints, prompt fragments, expected labels, or coverage requirements."));
    m_promptLabNotes->setMaximumHeight(130);

    auto *promptLabButtons = new QHBoxLayout();
    m_promptLabGenerateButton = new QPushButton(QStringLiteral("Compose recipe"), promptLabTab);
    m_promptLabUseButton = new QPushButton(QStringLiteral("Use in input"), promptLabTab);
    m_promptLabCopyRecipeButton = new QPushButton(QStringLiteral("Copy recipe"), promptLabTab);
    promptLabButtons->addWidget(m_promptLabGenerateButton);
    promptLabButtons->addWidget(m_promptLabUseButton);
    promptLabButtons->addWidget(m_promptLabCopyRecipeButton);

    m_promptLabPreview = new QTextEdit(promptLabTab);
    m_promptLabPreview->setReadOnly(true);
    m_promptLabPreview->setPlaceholderText(QStringLiteral("Composed prompt recipe and JSONL preview appear here."));

    promptLabLayout->addLayout(promptLabForm);
    promptLabLayout->addWidget(new QLabel(QStringLiteral("Filesystem assets to import"), promptLabTab));
    promptLabLayout->addWidget(m_promptLabImportAssetsEdit);
    promptLabLayout->addLayout(importButtons);
    promptLabLayout->addWidget(new QLabel(QStringLiteral("Already indexed knowledge-base assets"), promptLabTab));
    promptLabLayout->addWidget(m_promptLabKbFilter);
    promptLabLayout->addWidget(m_promptLabKbAssetsList, 1);
    promptLabLayout->addLayout(kbButtons);
    promptLabLayout->addWidget(new QLabel(QStringLiteral("Manual KB references"), promptLabTab));
    promptLabLayout->addWidget(m_promptLabKbManualEdit);
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
    resize(1780, 980);

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
    connect(m_promptLabBrowseAssetsButton, &QPushButton::clicked, this, &MainWindow::onPromptLabBrowseAssetsClicked);
    connect(m_promptLabBrowseFolderButton, &QPushButton::clicked, this, &MainWindow::onPromptLabBrowseFolderClicked);
    connect(m_promptLabClearAssetsButton, &QPushButton::clicked, this, &MainWindow::onPromptLabClearAssetsClicked);
    connect(m_promptLabCopyRecipeButton, &QPushButton::clicked, this, &MainWindow::onPromptLabCopyRecipeClicked);
    connect(m_promptLabAddSelectedKbButton, &QPushButton::clicked, this, &MainWindow::onPromptLabAddSelectedKbAssetsClicked);
    connect(m_promptLabKbFilter, &QLineEdit::textChanged, this, &MainWindow::onPromptLabKbFilterChanged);
    connect(m_copyLastAnswerButton, &QPushButton::clicked, this, &MainWindow::onCopyLastAnswerClicked);
    connect(m_copyTranscriptButton, &QPushButton::clicked, this, &MainWindow::onCopyTranscriptClicked);
    connect(m_copyCodeBlockButton, &QPushButton::clicked, this, &MainWindow::onCopySelectedCodeBlockClicked);
    connect(m_transcript, &QTextBrowser::customContextMenuRequested, this, &MainWindow::onTranscriptContextMenuRequested);

    if (m_promptLabPreview != nullptr) {
        m_promptLabPreview->setPlainText(buildPromptLabRecipe());
    }
    refreshCodeBlockSelector();
    renderTranscript();
}

void MainWindow::appendTranscriptEntry(const QString &role, const QString &text)
{
    m_transcriptEntries.push_back({role, text});
    if (role == QStringLiteral("assistant")) {
        m_lastAssistantMessage = text;
    }
    renderTranscript();
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

QString MainWindow::markdownFragmentToHtml(const QString &markdown) const
{
    if (markdown.trimmed().isEmpty()) {
        return QString();
    }

    QTextDocument document;
    document.setMarkdown(markdown);
    return extractBodyFromHtmlDocument(document.toHtml());
}

QString MainWindow::renderMessageBodyHtml(const QString &text,
                                          const QString &role,
                                          int assistantIndex,
                                          int &globalCodeIndex,
                                          int &perMessageCodeIndex)
{
    QString html;
    static const QRegularExpression fenceRegex(
        QStringLiteral(R"(```([^\n`]*)\n(.*?)```)") ,
        QRegularExpression::DotMatchesEverythingOption);

    int searchOffset = 0;
    auto match = fenceRegex.match(text, searchOffset);
    while (match.hasMatch()) {
        const int start = match.capturedStart();
        const int end = match.capturedEnd();
        const QString before = text.mid(searchOffset, start - searchOffset);
        html += markdownFragmentToHtml(before);

        QString language = match.captured(1).trimmed();
        QString code = match.captured(2);
        if (code.endsWith(QLatin1Char('\n'))) {
            code.chop(1);
        }

        ++globalCodeIndex;
        ++perMessageCodeIndex;

        const QString label = role == QStringLiteral("assistant")
                ? QStringLiteral("Answer %1 · block %2").arg(assistantIndex).arg(perMessageCodeIndex)
                : QStringLiteral("%1 · block %2").arg(transcriptPrefix(role)).arg(perMessageCodeIndex);
        const QString languageBadge = language.isEmpty() ? QStringLiteral("plain text") : language.toHtmlEscaped();
        const QString summary = summarizeCodeBlock(code).toHtmlEscaped();

        m_codeBlocks.push_back({QStringLiteral("%1 · %2 · %3").arg(label, language.isEmpty() ? QStringLiteral("plain") : language, summary), code});

        html += QStringLiteral(
                    "<div class='code-card'>"
                    "<div class='code-meta'><span class='code-label'>%1</span><span class='code-lang'>%2</span></div>"
                    "<pre>%3</pre>"
                    "</div>")
                    .arg(label.toHtmlEscaped(), languageBadge, escapedPre(code));

        searchOffset = end;
        match = fenceRegex.match(text, searchOffset);
    }

    html += markdownFragmentToHtml(text.mid(searchOffset));
    if (html.trimmed().isEmpty()) {
        html = QStringLiteral("<p>%1</p>").arg(text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    }
    return html;
}

void MainWindow::renderTranscript()
{
    if (m_transcript == nullptr) {
        return;
    }

    m_codeBlocks.clear();
    m_lastAssistantMessage.clear();

    QString html;
    html += QStringLiteral(
        "<html><head><style>"
        "body{background:#020617;color:#e5e7eb;font-family:'Noto Sans','DejaVu Sans',sans-serif;}"
        ".entry{margin:0 0 14px 0;padding:12px 14px;border:1px solid #1f2937;border-radius:12px;}"
        ".entry-header{margin-bottom:8px;}"
        ".role-badge{display:inline-block;padding:3px 9px;border-radius:999px;font-weight:700;letter-spacing:0.04em;font-size:11px;}"
        ".message{font-size:13px;line-height:1.45;}"
        ".message p{margin:0 0 10px 0;}"
        ".message ul,.message ol{margin-top:4px;margin-bottom:10px;}"
        ".message h1,.message h2,.message h3,.message h4{margin:10px 0 6px 0;color:#f8fafc;}"
        ".message blockquote{margin:8px 0;padding-left:10px;border-left:3px solid #334155;color:#cbd5e1;}"
        ".message code{background:#111827;color:#dbeafe;padding:2px 5px;border-radius:6px;font-family:'DejaVu Sans Mono','Noto Sans Mono',monospace;}"
        ".code-card{margin:8px 0 12px 0;border:1px solid #334155;border-radius:10px;overflow:hidden;background:#0b1220;}"
        ".code-meta{display:flex;justify-content:space-between;gap:12px;padding:7px 10px;background:#111827;border-bottom:1px solid #334155;color:#cbd5e1;font-size:11px;font-weight:700;}"
        ".code-lang{color:#93c5fd;}"
        ".code-label{color:#c4b5fd;}"
        ".code-card pre{margin:0;padding:12px;white-space:pre-wrap;font-family:'DejaVu Sans Mono','Noto Sans Mono',monospace;font-size:12px;color:#e5e7eb;background:#020617;}"
        "</style></head><body>");

    int assistantIndex = 0;
    int globalCodeIndex = 0;
    for (const TranscriptEntry &entry : m_transcriptEntries) {
        if (entry.role == QStringLiteral("assistant")) {
            ++assistantIndex;
            m_lastAssistantMessage = entry.text;
        }

        int perMessageCodeIndex = 0;
        const QString bodyHtml = renderMessageBodyHtml(entry.text,
                                                       entry.role,
                                                       assistantIndex <= 0 ? 1 : assistantIndex,
                                                       globalCodeIndex,
                                                       perMessageCodeIndex);
        const QString prefixColor = transcriptPrefixColor(entry.role).name();
        const QString bodyColor = transcriptBodyColor(entry.role).name();
        html += QStringLiteral(
                    "<div class='entry' style='background:%1;color:%2;'>"
                    "<div class='entry-header'><span class='role-badge' style='color:%3;background:%4;'>%5</span></div>"
                    "<div class='message' style='color:%2;'>%6</div>"
                    "</div>")
                    .arg(transcriptCardBackground(entry.role),
                         bodyColor,
                         prefixColor,
                         transcriptRoleBadgeBackground(entry.role),
                         transcriptPrefix(entry.role).toHtmlEscaped(),
                         bodyHtml);
    }

    if (m_transcriptEntries.isEmpty()) {
        html += QStringLiteral("<p style='color:#94a3b8;'>Conversation transcript...</p>");
    }

    html += QStringLiteral("</body></html>");
    m_transcript->setHtml(html);
    if (m_transcript->verticalScrollBar() != nullptr) {
        m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
    }
    refreshCodeBlockSelector();
}

void MainWindow::appendUserMessage(const QString &text)
{
    m_streamingAssistant = false;
    appendTranscriptEntry(QStringLiteral("user"), text);
}

void MainWindow::appendAssistantChunk(const QString &text)
{
    if (!m_streamingAssistant || m_transcriptEntries.isEmpty() || m_transcriptEntries.last().role != QStringLiteral("assistant")) {
        m_transcriptEntries.push_back({QStringLiteral("assistant"), text});
        m_streamingAssistant = true;
    } else {
        m_transcriptEntries.last().text += text;
    }
    m_lastAssistantMessage = m_transcriptEntries.last().text;
    renderTranscript();
}

void MainWindow::finalizeAssistantMessage(const QString &text)
{
    if (m_streamingAssistant && !m_transcriptEntries.isEmpty() && m_transcriptEntries.last().role == QStringLiteral("assistant")) {
        if (!text.isEmpty()) {
            m_transcriptEntries.last().text = text;
        }
        m_lastAssistantMessage = m_transcriptEntries.last().text;
    }
    m_streamingAssistant = false;
    renderTranscript();
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
    m_transcriptEntries.clear();
    m_streamingAssistant = false;
    m_lastAssistantMessage.clear();

    const QString normalized = text;
    const QStringList lines = normalized.split(QLatin1Char('\n'));

    QString currentRole;
    QStringList currentBuffer;
    auto flushCurrent = [this, &currentRole, &currentBuffer]() {
        if (currentRole.isEmpty()) {
            return;
        }
        QString content = currentBuffer.join(QStringLiteral("\n"));
        while (content.startsWith(QLatin1Char('\n'))) {
            content.remove(0, 1);
        }
        while (content.endsWith(QLatin1Char('\n'))) {
            content.chop(1);
        }
        m_transcriptEntries.push_back({currentRole, content});
        if (currentRole == QStringLiteral("assistant")) {
            m_lastAssistantMessage = content;
        }
        currentRole.clear();
        currentBuffer.clear();
    };

    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("USER> "))) {
            flushCurrent();
            currentRole = QStringLiteral("user");
            currentBuffer << line.mid(6);
            continue;
        }
        if (line.startsWith(QStringLiteral("ASSISTANT> "))) {
            flushCurrent();
            currentRole = QStringLiteral("assistant");
            currentBuffer << line.mid(11);
            continue;
        }
        if (line.startsWith(QStringLiteral("[system] "))) {
            flushCurrent();
            currentRole = QStringLiteral("system");
            currentBuffer << line.mid(9);
            continue;
        }
        if (line.startsWith(QStringLiteral("[status] "))) {
            flushCurrent();
            currentRole = QStringLiteral("status");
            currentBuffer << line.mid(9);
            continue;
        }

        if (currentRole.isEmpty()) {
            if (!line.trimmed().isEmpty()) {
                currentRole = QStringLiteral("system");
                currentBuffer << line;
            }
        } else {
            currentBuffer << line;
        }
    }

    flushCurrent();
    renderTranscript();
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
    m_promptLabBrowseAssetsButton->setEnabled(!busy);
    m_promptLabBrowseFolderButton->setEnabled(!busy);
    m_promptLabCopyRecipeButton->setEnabled(!busy);

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

void MainWindow::refreshKbAssetList(const QString &filterText)
{
    if (m_promptLabKbAssetsList == nullptr) {
        return;
    }

    const QString currentFilter = filterText.trimmed();
    QStringList selectedPaths;
    const auto selectedItems = m_promptLabKbAssetsList->selectedItems();
    for (QListWidgetItem *item : selectedItems) {
        if (item != nullptr) {
            selectedPaths << item->data(Qt::UserRole).toString();
        }
    }

    m_promptLabKbAssetsList->clear();
    for (const QString &path : m_knownKbAssets) {
        if (!currentFilter.isEmpty() && !path.contains(currentFilter, Qt::CaseInsensitive)) {
            continue;
        }
        auto *item = new QListWidgetItem(path, m_promptLabKbAssetsList);
        item->setData(Qt::UserRole, path);
        if (selectedPaths.contains(path)) {
            item->setSelected(true);
        }
    }

    if (m_promptLabKbAssetsList->count() == 0) {
        auto *item = new QListWidgetItem(currentFilter.isEmpty()
                                             ? QStringLiteral("No indexed KB assets yet")
                                             : QStringLiteral("No KB assets match this filter"),
                                         m_promptLabKbAssetsList);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
    }
}

void MainWindow::setSourceInventory(const QString &text)
{
    m_sourceInventory->setPlainText(text);
    m_knownKbAssets = extractInventoryFilePaths(text);
    refreshKbAssetList(m_promptLabKbFilter != nullptr ? m_promptLabKbFilter->text() : QString());
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

QStringList MainWindow::promptLabImportAssets() const
{
    return splitAssetPaths(m_promptLabImportAssetsEdit != nullptr ? m_promptLabImportAssetsEdit->toPlainText() : QString());
}

QStringList MainWindow::promptLabSelectedKbAssets() const
{
    QStringList assets;
    if (m_promptLabKbAssetsList == nullptr) {
        return assets;
    }
    const auto items = m_promptLabKbAssetsList->selectedItems();
    for (QListWidgetItem *item : items) {
        if (item == nullptr) {
            continue;
        }
        const QString value = item->data(Qt::UserRole).toString().trimmed();
        if (!value.isEmpty() && !assets.contains(value)) {
            assets << value;
        }
    }
    return assets;
}

QStringList MainWindow::promptLabManualKbAssets() const
{
    return splitAssetPaths(m_promptLabKbManualEdit != nullptr ? m_promptLabKbManualEdit->text() : QString());
}

QString MainWindow::buildPromptLabRecipe() const
{
    const QString preset = m_promptLabPresetCombo != nullptr ? m_promptLabPresetCombo->currentText() : QStringLiteral("General grounding");
    const QString goal = m_promptLabGoal != nullptr ? m_promptLabGoal->text().trimmed() : QString();
    const QString notes = m_promptLabNotes != nullptr ? m_promptLabNotes->toPlainText().trimmed() : QString();
    const QStringList importAssets = promptLabImportAssets();
    QStringList kbAssets = promptLabSelectedKbAssets();
    for (const QString &manual : promptLabManualKbAssets()) {
        if (!kbAssets.contains(manual)) {
            kbAssets << manual;
        }
    }

    QStringList directives;
    if (preset == QStringLiteral("Code patch")) {
        directives << QStringLiteral("Study the listed assets before suggesting code changes.")
                   << QStringLiteral("Preserve existing naming, code style, and architecture.")
                   << QStringLiteral("Return paste-ready complete functions or files when patching code.");
    } else if (preset == QStringLiteral("Runbook / docs")) {
        directives << QStringLiteral("Extract grounded operational steps from the listed materials.")
                   << QStringLiteral("Prefer concise procedures, prerequisites, rollback, and validation.")
                   << QStringLiteral("Do not invent commands, hosts, or environment details.");
    } else if (preset == QStringLiteral("Incident investigation")) {
        directives << QStringLiteral("Correlate clues from logs, configs, and notes in the listed materials.")
                   << QStringLiteral("Separate evidence, hypotheses, and proposed checks.")
                   << QStringLiteral("Highlight the smallest next diagnostic step.");
    } else if (preset == QStringLiteral("Dataset from assets")) {
        directives << QStringLiteral("Transform the listed assets into reusable supervision examples.")
                   << QStringLiteral("Produce compact input/output training pairs grounded in those assets.")
                   << QStringLiteral("Reject unsupported labels or invented metadata.");
    } else if (preset == QStringLiteral("Executive summary")) {
        directives << QStringLiteral("Summarize only what is supported by the listed materials.")
                   << QStringLiteral("Surface risks, decisions, and open questions for an executive audience.")
                   << QStringLiteral("Keep the language direct and non-speculative.");
    } else if (preset == QStringLiteral("Knowledge extraction")) {
        directives << QStringLiteral("Extract reusable facts, concepts, and relationships from the listed materials.")
                   << QStringLiteral("Organize the result for future retrieval or KB curation.")
                   << QStringLiteral("Flag gaps or duplicated entries explicitly.");
    } else if (preset == QStringLiteral("KB-only analysis")) {
        directives << QStringLiteral("Use only already indexed KB assets listed below.")
                   << QStringLiteral("Do not rely on non-indexed files unless the user imports them explicitly.")
                   << QStringLiteral("Mention which KB assets were actually used in the answer.");
    } else if (preset == QStringLiteral("Migration / refactor plan")) {
        directives << QStringLiteral("Inspect the listed materials and design an ordered migration or refactor plan.")
                   << QStringLiteral("List impact areas, sequencing, validation, and rollback points.")
                   << QStringLiteral("Keep the plan grounded in the current implementation or docs.");
    } else {
        directives << QStringLiteral("Ground every answer in the listed materials and the user request.")
                   << QStringLiteral("Prefer explicit references to asset names, files, or sections when possible.")
                   << QStringLiteral("Say when information is missing instead of guessing.");
    }

    const QString displayGoal = goal.isEmpty() ? QStringLiteral("<describe the target outcome>") : goal;
    const QString displayImportAssets = importAssets.isEmpty() ? QStringLiteral("<none listed>") : importAssets.join(QStringLiteral("; "));
    const QString displayKbAssets = kbAssets.isEmpty() ? QStringLiteral("<none listed>") : kbAssets.join(QStringLiteral("; "));

    QString escapedGoal = goal.isEmpty() ? QStringLiteral("<goal>") : goal;
    QString escapedImportAssets = importAssets.isEmpty() ? QStringLiteral("<none>") : importAssets.join(QStringLiteral("; "));
    QString escapedKbAssets = kbAssets.isEmpty() ? QStringLiteral("<none>") : kbAssets.join(QStringLiteral("; "));
    escapedGoal.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    escapedImportAssets.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    escapedKbAssets.replace(QStringLiteral("\""), QStringLiteral("\\\""));

    QStringList lines;
    lines << QStringLiteral("Prompt Lab recipe");
    lines << QStringLiteral("=================");
    lines << QStringLiteral("Preset: %1").arg(preset);
    lines << QStringLiteral("Goal: %1").arg(displayGoal);
    lines << QStringLiteral("Filesystem assets to import: %1").arg(displayImportAssets);
    lines << QStringLiteral("Existing KB assets to use: %1").arg(displayKbAssets);
    lines << QString();
    lines << QStringLiteral("Use this with Amelia:");
    lines << QStringLiteral("Study the requested materials before answering.");
    for (const QString &directive : directives) {
        lines << QStringLiteral("- %1").arg(directive);
    }
    if (!notes.isEmpty()) {
        lines << QStringLiteral("- Extra constraints: %1").arg(notes);
    }
    lines << QString();
    lines << QStringLiteral("Suggested working prompt:");
    lines << QStringLiteral("Analyze the requested materials and help with this goal: %1").arg(escapedGoal);
    lines << QStringLiteral("Imported asset paths: %1").arg(escapedImportAssets);
    lines << QStringLiteral("Indexed KB assets: %1").arg(escapedKbAssets);
    lines << QStringLiteral("Return a grounded answer and identify which materials were actually used.");
    lines << QString();
    lines << QStringLiteral("JSONL training sample preview:");
    lines << QStringLiteral("{\"messages\":[{\"role\":\"system\",\"content\":\"You are Amelia. Answer only from the requested materials.\"},{\"role\":\"user\",\"content\":\"Goal: %1\\nImport assets: %2\\nKB assets: %3\"},{\"role\":\"assistant\",\"content\":\"Grounded response based on the requested materials.\"}]}")
                .arg(escapedGoal, escapedImportAssets, escapedKbAssets);

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
    const QStringList paths = promptLabImportAssets();
    if (paths.isEmpty()) {
        QMessageBox::information(this,
                                 QStringLiteral("Prompt Lab"),
                                 QStringLiteral("Add one or more filesystem asset paths first. Separate them with ';', ',' or new lines."));
        return;
    }

    emit importPathsRequested(paths);
    m_statusLabel->setText(QStringLiteral("Prompt Lab requested asset import for %1 path(s).").arg(paths.size()));
}

void MainWindow::onPromptLabBrowseAssetsClicked()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this,
                                                            QStringLiteral("Select Prompt Lab assets"),
                                                            QString(),
                                                            QStringLiteral("All Files (*.*)"));
    if (paths.isEmpty()) {
        return;
    }

    QStringList merged = promptLabImportAssets();
    for (const QString &path : paths) {
        if (!merged.contains(path)) {
            merged << path;
        }
    }
    m_promptLabImportAssetsEdit->setPlainText(merged.join(QStringLiteral("\n")));
    onPromptLabGenerateClicked();
}

void MainWindow::onPromptLabBrowseFolderClicked()
{
    const QString folder = QFileDialog::getExistingDirectory(this,
                                                             QStringLiteral("Select Prompt Lab asset folder"));
    if (folder.isEmpty()) {
        return;
    }

    QStringList merged = promptLabImportAssets();
    if (!merged.contains(folder)) {
        merged << folder;
    }
    m_promptLabImportAssetsEdit->setPlainText(merged.join(QStringLiteral("\n")));
    onPromptLabGenerateClicked();
}

void MainWindow::onPromptLabClearAssetsClicked()
{
    if (m_promptLabImportAssetsEdit != nullptr) {
        m_promptLabImportAssetsEdit->clear();
    }
    if (m_promptLabKbManualEdit != nullptr) {
        m_promptLabKbManualEdit->clear();
    }
    if (m_promptLabKbFilter != nullptr) {
        m_promptLabKbFilter->clear();
    }
    if (m_promptLabKbAssetsList != nullptr) {
        m_promptLabKbAssetsList->clearSelection();
    }
    refreshKbAssetList();
    onPromptLabGenerateClicked();
    m_statusLabel->setText(QStringLiteral("Prompt Lab asset fields cleared."));
}

void MainWindow::onPromptLabCopyRecipeClicked()
{
    copyTextToClipboard(buildPromptLabRecipe(), QStringLiteral("Prompt Lab recipe copied to the clipboard."));
    if (m_promptLabPreview != nullptr) {
        m_promptLabPreview->setPlainText(buildPromptLabRecipe());
    }
}

void MainWindow::onPromptLabAddSelectedKbAssetsClicked()
{
    const QStringList selected = promptLabSelectedKbAssets();
    if (selected.isEmpty()) {
        QMessageBox::information(this,
                                 QStringLiteral("Prompt Lab"),
                                 QStringLiteral("Select one or more indexed KB assets first."));
        return;
    }

    QStringList merged = promptLabManualKbAssets();
    for (const QString &item : selected) {
        if (!merged.contains(item)) {
            merged << item;
        }
    }
    m_promptLabKbManualEdit->setText(merged.join(QStringLiteral("; ")));
    onPromptLabGenerateClicked();
    m_statusLabel->setText(QStringLiteral("Added %1 KB asset reference(s) to the recipe.").arg(selected.size()));
}

void MainWindow::onPromptLabKbFilterChanged(const QString &text)
{
    refreshKbAssetList(text);
}

void MainWindow::copyTextToClipboard(const QString &text, const QString &statusMessage)
{
    if (QApplication::clipboard() != nullptr) {
        QApplication::clipboard()->setText(text);
    }
    if (m_statusLabel != nullptr) {
        m_statusLabel->setText(statusMessage);
    }
}

void MainWindow::refreshCodeBlockSelector()
{
    if (m_codeBlockCombo == nullptr || m_copyCodeBlockButton == nullptr) {
        return;
    }

    m_codeBlockCombo->clear();
    if (m_codeBlocks.isEmpty()) {
        m_codeBlockCombo->addItem(QStringLiteral("No code blocks yet"));
        m_codeBlockCombo->setEnabled(false);
        m_copyCodeBlockButton->setEnabled(false);
        return;
    }

    m_codeBlockCombo->setEnabled(true);
    m_copyCodeBlockButton->setEnabled(true);
    for (int i = 0; i < m_codeBlocks.size(); ++i) {
        m_codeBlockCombo->addItem(m_codeBlocks.at(i).label, i);
    }
}

void MainWindow::onCopyLastAnswerClicked()
{
    if (m_lastAssistantMessage.trimmed().isEmpty()) {
        m_statusLabel->setText(QStringLiteral("There is no assistant answer to copy yet."));
        return;
    }
    copyTextToClipboard(m_lastAssistantMessage, QStringLiteral("Last assistant answer copied to the clipboard."));
}

void MainWindow::onCopyTranscriptClicked()
{
    QStringList lines;
    for (const TranscriptEntry &entry : m_transcriptEntries) {
        lines << QStringLiteral("%1> %2").arg(transcriptPrefix(entry.role), entry.text);
        lines << QString();
    }
    copyTextToClipboard(lines.join(QStringLiteral("\n")).trimmed(), QStringLiteral("Transcript copied to the clipboard."));
}

void MainWindow::onCopySelectedCodeBlockClicked()
{
    const int index = m_codeBlockCombo != nullptr ? m_codeBlockCombo->currentData().toInt() : -1;
    if (index < 0 || index >= m_codeBlocks.size()) {
        m_statusLabel->setText(QStringLiteral("There is no code block to copy."));
        return;
    }
    copyTextToClipboard(m_codeBlocks.at(index).code,
                        QStringLiteral("Copied %1 to the clipboard.").arg(m_codeBlocks.at(index).label));
}

void MainWindow::onTranscriptContextMenuRequested(const QPoint &pos)
{
    if (m_transcript == nullptr) {
        return;
    }

    QMenu menu(this);
    auto *copySelectionAction = menu.addAction(QStringLiteral("Copy selection"));
    copySelectionAction->setEnabled(!m_transcript->textCursor().selectedText().isEmpty());
    auto *copyLastAnswerAction = menu.addAction(QStringLiteral("Copy last answer"));
    copyLastAnswerAction->setEnabled(!m_lastAssistantMessage.trimmed().isEmpty());
    auto *copyTranscriptAction = menu.addAction(QStringLiteral("Copy transcript"));
    auto *copyCodeBlockAction = menu.addAction(QStringLiteral("Copy selected code block"));
    copyCodeBlockAction->setEnabled(!m_codeBlocks.isEmpty());

    QAction *chosen = menu.exec(m_transcript->viewport()->mapToGlobal(pos));
    if (chosen == copySelectionAction) {
        m_transcript->copy();
    } else if (chosen == copyLastAnswerAction) {
        onCopyLastAnswerClicked();
    } else if (chosen == copyTranscriptAction) {
        onCopyTranscriptClicked();
    } else if (chosen == copyCodeBlockAction) {
        onCopySelectedCodeBlockClicked();
    }
}

void MainWindow::showAboutAmelia()
{
    QMessageBox::about(this,
                       QStringLiteral("About Amelia"),
                       QStringLiteral("<b>Amelia Qt6 v%1</b><br><br>"
                                      "A local offline coding and knowledge assistant built with C++ and Qt6.<br><br>"
                                      "This 6.5 build adds a richer formatted transcript, code-block copying, whole-answer copying, a stronger Prompt Lab with KB-aware asset selection, persistent knowledge, prompt budgeting, outline-first document generation, and operational diagnostics.")
                           .arg(appVersionString()));
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
