#include "mainwindow.h"
#include "appversion.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QColor>
#include <QUrl>
#include <QTextDocument>
#include <QTextBrowser>
#include <QDesktopServices>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QPoint>
#include <QPushButton>
#include <QProgressBar>
#include <QStatusBar>
#include <QRegularExpression>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBlockFormat>
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
    if (lower == QStringLiteral("reasoning")) {
        return QColor(QStringLiteral("#f43f5e"));
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


struct TranscriptSegment {
    bool isCode = false;
    QString language;
    QString text;
};

QVector<TranscriptSegment> splitTranscriptSegments(const QString &text)
{
    QVector<TranscriptSegment> segments;
    const QString fence = QStringLiteral("```");
    int pos = 0;
    bool inCode = false;
    QString currentLanguage;
    while (pos < text.size()) {
        const int fencePos = text.indexOf(fence, pos);
        if (fencePos < 0) {
            TranscriptSegment seg;
            seg.isCode = inCode;
            seg.language = currentLanguage;
            seg.text = text.mid(pos);
            if (!seg.text.isEmpty()) {
                segments.push_back(seg);
            }
            break;
        }
        if (fencePos > pos) {
            TranscriptSegment seg;
            seg.isCode = inCode;
            seg.language = currentLanguage;
            seg.text = text.mid(pos, fencePos - pos);
            if (!seg.text.isEmpty()) {
                segments.push_back(seg);
            }
        }
        const int afterFence = fencePos + fence.size();
        const int lineEnd = text.indexOf(QLatin1Char('\n'), afterFence);
        if (!inCode) {
            if (lineEnd < 0) {
                break;
            }
            currentLanguage = text.mid(afterFence, lineEnd - afterFence).trimmed();
            inCode = true;
            pos = lineEnd + 1;
        } else {
            inCode = false;
            currentLanguage.clear();
            pos = lineEnd < 0 ? afterFence : lineEnd + 1;
        }
    }
    return segments;
}

QString extractCodeBlocks(const QString &text)
{
    QStringList blocks;
    const QVector<TranscriptSegment> segments = splitTranscriptSegments(text);
    for (const TranscriptSegment &segment : segments) {
        if (segment.isCode && !segment.text.trimmed().isEmpty()) {
            blocks << segment.text.trimmed();
        }
    }
    return blocks.join(QStringLiteral("\n\n---\n\n"));
}


QString bodyFragmentFromDocument(const QTextDocument &doc)
{
    QString html = doc.toHtml();
    const int bodyStart = html.indexOf(QStringLiteral("<body"));
    if (bodyStart >= 0) {
        const int fragmentStart = html.indexOf(QLatin1Char('>'), bodyStart);
        const int bodyEnd = html.indexOf(QStringLiteral("</body>"), fragmentStart);
        if (fragmentStart >= 0 && bodyEnd > fragmentStart) {
            return html.mid(fragmentStart + 1, bodyEnd - fragmentStart - 1);
        }
    }
    return html;
}

QString markdownFragmentToHtml(const QString &markdown)
{
    const QString trimmed = markdown.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    QTextDocument doc;
    doc.setDocumentMargin(0.0);
    doc.setMarkdown(trimmed);
    QString html = bodyFragmentFromDocument(doc);

    html.replace(QStringLiteral("<pre"), QStringLiteral("<pre style=\"background:#0b1220;color:#e5e7eb;padding:12px;border-radius:10px;border:1px solid #243043;overflow:auto;\""));
    html.replace(QStringLiteral("<code"), QStringLiteral("<code style=\"background:#111827;color:#f8fafc;padding:2px 5px;border-radius:4px;\""));
    html.replace(QStringLiteral("<blockquote"), QStringLiteral("<blockquote style=\"border-left:4px solid #475569;margin:10px 0;padding:6px 12px;color:#cbd5e1;background:#0f172a;border-radius:6px;\""));
    html.replace(QStringLiteral("<table"), QStringLiteral("<table style=\"border-collapse:collapse;width:100%;margin:10px 0;\""));
    html.replace(QStringLiteral("<th"), QStringLiteral("<th style=\"border:1px solid #334155;padding:6px 8px;background:#111827;color:#f8fafc;text-align:left;\""));
    html.replace(QStringLiteral("<td"), QStringLiteral("<td style=\"border:1px solid #334155;padding:6px 8px;color:#e5e7eb;\""));
    html.replace(QStringLiteral("<a href="), QStringLiteral("<a style=\"color:#93c5fd;\" href="));
    return html;
}

QString messageToRichHtml(const QString &role, const QString &text, QStringList *codeBlocks)
{
    const QString rolePrefix = transcriptPrefix(role).toHtmlEscaped();
    const QString prefixColor = transcriptPrefixColor(role).name();
    const QString bodyColor = transcriptBodyColor(role).name();
    QStringList bodyParts;

    const QVector<TranscriptSegment> segments = splitTranscriptSegments(text);
    for (const TranscriptSegment &segment : segments) {
        if (segment.isCode) {
            const QString code = segment.text.trimmed();
            if (code.isEmpty()) {
                continue;
            }
            const int codeIndex = codeBlocks != nullptr ? codeBlocks->size() : 0;
            if (codeBlocks != nullptr) {
                codeBlocks->push_back(code);
            }
            const QString languageBadge = segment.language.trimmed().isEmpty()
                    ? QStringLiteral("code")
                    : segment.language.trimmed().toHtmlEscaped();
            bodyParts << QStringLiteral(
                "<div style=\"margin:10px 0 14px 0;\">"
                "<div style=\"display:flex;justify-content:space-between;align-items:center;margin:0 0 6px 0;\">"
                "<span style=\"font-size:11px;font-weight:700;color:#93c5fd;text-transform:uppercase;letter-spacing:0.08em;\">%1</span>"
                "<a href=\"copycode:%2\" style=\"font-size:12px;color:#93c5fd;text-decoration:none;background:#0f172a;padding:4px 8px;border-radius:6px;border:1px solid #334155;\">Copy code</a>"
                "</div>"
                "<pre style=\"margin:0;background:#020617;color:#e2e8f0;padding:12px;border-radius:10px;border:1px solid #334155;overflow:auto;white-space:pre-wrap;\"><code>%3</code></pre>"
                "</div>")
                .arg(languageBadge)
                .arg(codeIndex)
                .arg(code.toHtmlEscaped());
        } else {
            const QString html = markdownFragmentToHtml(segment.text);
            if (!html.trimmed().isEmpty()) {
                bodyParts << html;
            }
        }
    }

    if (bodyParts.isEmpty()) {
        bodyParts << QStringLiteral("<p>%1</p>").arg(text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
    }

    return QStringLiteral(
        "<div style=\"margin:8px 0 14px 0;padding:10px 12px;border-radius:12px;background:#111827;border:1px solid #1f2937;\">"
        "<div style=\"font-weight:700;color:%1;margin:0 0 8px 0;\">%2</div>"
        "<div style=\"color:%3;\">%4</div>"
        "</div>")
        .arg(prefixColor, rolePrefix, bodyColor, bodyParts.join(QStringLiteral("\n")));
}

void appendPathsToLineEdit(QLineEdit *lineEdit, const QStringList &paths)
{
    if (lineEdit == nullptr || paths.isEmpty()) {
        return;
    }
    QStringList merged = splitAssetPaths(lineEdit->text());
    for (const QString &path : paths) {
        if (!merged.contains(path)) {
            merged << path;
        }
    }
    lineEdit->setText(merged.join(QStringLiteral("; ")));
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_busyFrames({QStringLiteral("◐"), QStringLiteral("◓"), QStringLiteral("◑"), QStringLiteral("◒")})
{
    setWindowTitle(QStringLiteral("Amelia Qt6 v%1").arg(QLatin1StringView(AmeliaVersion::kDisplayVersion)));
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
    m_deleteConversationButton = new QPushButton(QStringLiteral("Delete selected"), sessionPane);
    sessionLayout->addWidget(sessionTitle);
    sessionLayout->addWidget(m_conversationsList, 1);
    sessionLayout->addWidget(m_newConversationButton);
    sessionLayout->addWidget(m_deleteConversationButton);

    auto *chatPane = new QWidget(splitter);
    auto *chatLayout = new QVBoxLayout(chatPane);

    auto *titleRow = new QHBoxLayout();
    auto *title = new QLabel(QStringLiteral("aMELia Qt6 v%1 ").arg(QLatin1StringView(AmeliaVersion::kDisplayVersion)), chatPane);
    title->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 18px;"));
    auto *logoLabel = new QLabel(chatPane);
    const QPixmap logoPixmap(QStringLiteral(":/branding/amelia_logo.svg"));
    logoLabel->setPixmap(logoPixmap.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setToolTip(QStringLiteral("Amelia logo"));
    titleRow->addWidget(title);
    titleRow->addWidget(logoLabel, 0, Qt::AlignVCenter);
    titleRow->addStretch(1);

    auto *transcriptBrowser = new QTextBrowser(chatPane);
    transcriptBrowser->setOpenLinks(false);
    transcriptBrowser->setOpenExternalLinks(false);
    m_transcript = transcriptBrowser;
    m_transcript->setReadOnly(true);
    m_transcript->setPlaceholderText(QStringLiteral("Conversation transcript..."));
    m_transcript->setStyleSheet(QStringLiteral("QTextEdit { background: #0f172a; color: #e5e7eb; }"));
    connect(transcriptBrowser, &QTextBrowser::anchorClicked, this, &MainWindow::onTranscriptAnchorClicked);

    m_input = new QPlainTextEdit(chatPane);
    m_input->setPlaceholderText(QStringLiteral("Type your prompt here..."));
    m_input->setMaximumHeight(120);

    auto *priorityPanel = new QWidget(chatPane);
    auto *priorityLayout = new QVBoxLayout(priorityPanel);
    priorityLayout->setContentsMargins(0, 0, 0, 0);
    priorityLayout->setSpacing(4);
    auto *priorityHeader = new QHBoxLayout();
    auto *priorityTitle = new QLabel(QStringLiteral("Prioritized KB assets"), priorityPanel);
    priorityTitle->setStyleSheet(QStringLiteral("font-weight: 700; color: #cbd5e1;"));
    m_prioritizedAssetsStatus = new QLabel(QStringLiteral("No prioritized KB assets"), priorityPanel);
    m_prioritizedAssetsStatus->setStyleSheet(QStringLiteral("color: #94a3b8;"));
    priorityHeader->addWidget(priorityTitle);
    priorityHeader->addStretch(1);
    priorityHeader->addWidget(m_prioritizedAssetsStatus);
    m_prioritizedAssetsList = new QListWidget(priorityPanel);
    m_prioritizedAssetsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_prioritizedAssetsList->setMaximumHeight(100);
    m_prioritizedAssetsList->setToolTip(QStringLiteral("Assets selected from the Knowledge Base and prioritized for retrieval."));
    auto *priorityButtons = new QHBoxLayout();
    m_removePrioritizedAssetButton = new QPushButton(QStringLiteral("Remove selected"), priorityPanel);
    m_clearPrioritizedAssetsButton = new QPushButton(QStringLiteral("Clear priorities"), priorityPanel);
    priorityButtons->addWidget(m_removePrioritizedAssetButton);
    priorityButtons->addWidget(m_clearPrioritizedAssetsButton);
    priorityButtons->addStretch(1);
    priorityLayout->addLayout(priorityHeader);
    priorityLayout->addWidget(m_prioritizedAssetsList);
    priorityLayout->addLayout(priorityButtons);

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
    m_testBackendButton = nullptr;
    m_refreshModelsButton = new QPushButton(QStringLiteral("List models"), chatPane);
    m_rememberButton = new QPushButton(QStringLiteral("Remember input"), chatPane);
    m_copyLastAnswerButton = new QPushButton(QStringLiteral("Copy answer"), chatPane);
    m_copyCodeBlocksButton = nullptr;
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
    controlsLayout->addWidget(m_copyLastAnswerButton);
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(m_refreshModelsButton);
    controlsLayout->addWidget(m_reindexButton);
    controlsLayout->addWidget(m_stopButton);
    controlsLayout->addWidget(m_sendButton);

    chatLayout->addLayout(titleRow);
    chatLayout->addWidget(m_transcript, 1);
    chatLayout->addLayout(toolbarLayout);
    chatLayout->addWidget(priorityPanel);
    chatLayout->addWidget(m_input);
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

    auto *diagnosticsTab = new QWidget(tabs);
    auto *diagnosticsLayout = new QVBoxLayout(diagnosticsTab);
    auto *diagnosticsHeader = new QHBoxLayout();
    m_reasoningTraceToggleButton = new QPushButton(QStringLiteral("Capture reasoning trace: OFF"), diagnosticsTab);
    m_reasoningTraceToggleButton->setCheckable(true);
    m_reasoningTraceInfoLabel = new QLabel(QStringLiteral("Off by default. When enabled, Amelia requests backend thinking streams when available and logs them here, along with any explicit model-authored reasoning notes."), diagnosticsTab);
    m_reasoningTraceInfoLabel->setWordWrap(true);
    m_reasoningTraceInfoLabel->setStyleSheet(QStringLiteral("color: #94a3b8;"));
    diagnosticsHeader->addWidget(m_reasoningTraceToggleButton, 0);
    diagnosticsHeader->addWidget(m_reasoningTraceInfoLabel, 1);
    m_diagnostics = new QTextEdit(diagnosticsTab);
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setStyleSheet(QStringLiteral("QTextEdit { background: #0b1220; color: #e5e7eb; }"));
    diagnosticsLayout->addLayout(diagnosticsHeader);
    diagnosticsLayout->addWidget(m_diagnostics, 1);
    tabs->addTab(diagnosticsTab, QStringLiteral("Diagnostics"));

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
        QStringLiteral("KB-only tutoring"),
        QStringLiteral("Summarize new KB assets"),
        QStringLiteral("Compare assets / revisions"),
        QStringLiteral("Flashcards / Q&A")
    });
    m_promptLabGoal = new QLineEdit(promptLabTab);
    m_promptLabGoal->setPlaceholderText(QStringLiteral("What should Amelia learn, generate, compare, or teach?"));

    m_promptLabAssets = new QLineEdit(promptLabTab);
    m_promptLabAssets->setPlaceholderText(QStringLiteral("/path/a; /path/b; ~/notes.txt"));
    m_promptLabBrowseFilesButton = new QPushButton(QStringLiteral("Browse files"), promptLabTab);
    m_promptLabBrowseFolderButton = new QPushButton(QStringLiteral("Browse folder"), promptLabTab);
    auto *localAssetsRow = new QWidget(promptLabTab);
    auto *localAssetsLayout = new QHBoxLayout(localAssetsRow);
    localAssetsLayout->setContentsMargins(0, 0, 0, 0);
    localAssetsLayout->addWidget(m_promptLabAssets, 1);
    localAssetsLayout->addWidget(m_promptLabBrowseFilesButton);
    localAssetsLayout->addWidget(m_promptLabBrowseFolderButton);

    m_promptLabKbAssets = new QLineEdit(promptLabTab);
    m_promptLabKbAssets->setPlaceholderText(QStringLiteral("Already in KB: file names, paths, tags, topics, PDFs, manuals..."));

    promptLabForm->addRow(QStringLiteral("Preset:"), m_promptLabPresetCombo);
    promptLabForm->addRow(QStringLiteral("Goal:"), m_promptLabGoal);
    promptLabForm->addRow(QStringLiteral("Local assets:"), localAssetsRow);
    promptLabForm->addRow(QStringLiteral("KB assets:"), m_promptLabKbAssets);

    m_promptLabNotes = new QTextEdit(promptLabTab);
    m_promptLabNotes->setPlaceholderText(QStringLiteral("Optional notes, style constraints, schema hints, expected output shape, audience, etc."));
    m_promptLabNotes->setMaximumHeight(140);

    auto *promptLabButtons = new QHBoxLayout();
    m_promptLabGenerateButton = new QPushButton(QStringLiteral("Compose recipe"), promptLabTab);
    m_promptLabUseButton = new QPushButton(QStringLiteral("Use in input"), promptLabTab);
    m_promptLabCopyRecipeButton = new QPushButton(QStringLiteral("Copy recipe"), promptLabTab);
    m_promptLabImportButton = new QPushButton(QStringLiteral("Import local assets"), promptLabTab);
    promptLabButtons->addWidget(m_promptLabGenerateButton);
    promptLabButtons->addWidget(m_promptLabUseButton);
    promptLabButtons->addWidget(m_promptLabCopyRecipeButton);
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

    m_backendSummary = new QPlainTextEdit(tabs);
    m_backendSummary->setReadOnly(true);
    tabs->addTab(m_backendSummary, QStringLiteral("Backend"));

    auto *kbTab = new QWidget(tabs);
    auto *kbLayout = new QVBoxLayout(kbTab);
    m_sourceInventory = new QPlainTextEdit(kbTab);
    m_sourceInventory->setReadOnly(true);
    m_sourceInventory->setMaximumHeight(120);

    auto *kbFilterLayout = new QHBoxLayout();
    m_sourceInventoryFilter = new QLineEdit(kbTab);
    m_sourceInventoryFilter->setClearButtonEnabled(true);
    m_sourceInventoryFilter->setPlaceholderText(QStringLiteral("Search indexed file names or paths..."));
    m_sourceInventoryFilterStatus = new QLabel(QStringLiteral("0 / 0 shown"), kbTab);
    kbFilterLayout->addWidget(new QLabel(QStringLiteral("Search:"), kbTab));
    kbFilterLayout->addWidget(m_sourceInventoryFilter, 1);
    kbFilterLayout->addWidget(m_sourceInventoryFilterStatus);

    m_sourceInventoryList = new QListWidget(kbTab);
    m_sourceInventoryList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto *kbButtons = new QHBoxLayout();
    m_prioritizeSelectedAssetButton = new QPushButton(QStringLiteral("Use once"), kbTab);
    m_pinSelectedAssetButton = new QPushButton(QStringLiteral("Pin"), kbTab);
    m_removeSelectedAssetButton = new QPushButton(QStringLiteral("Remove selected"), kbTab);
    m_clearKnowledgeBaseButton = new QPushButton(QStringLiteral("Clear KB"), kbTab);
    kbButtons->addWidget(m_prioritizeSelectedAssetButton);
    kbButtons->addWidget(m_pinSelectedAssetButton);
    kbButtons->addWidget(m_removeSelectedAssetButton);
    kbButtons->addWidget(m_clearKnowledgeBaseButton);
    kbButtons->addStretch(1);
    kbLayout->addWidget(m_sourceInventory);
    kbLayout->addLayout(kbFilterLayout);
    kbLayout->addWidget(m_sourceInventoryList, 1);
    kbLayout->addLayout(kbButtons);
    tabs->addTab(kbTab, QStringLiteral("Knowledge Base"));

    m_outlinePlan = new QPlainTextEdit(tabs);
    m_outlinePlan->setReadOnly(true);
    tabs->addTab(m_outlinePlan, QStringLiteral("Outline Plan"));

    m_localSources = new QPlainTextEdit(tabs);
    m_localSources->setReadOnly(true);
    tabs->addTab(m_localSources, QStringLiteral("Local Sources"));

    m_externalSources = new QPlainTextEdit(tabs);
    m_externalSources->setReadOnly(true);
    tabs->addTab(m_externalSources, QStringLiteral("External Sources"));

    m_memoriesView = new QPlainTextEdit(tabs);
    m_memoriesView->setReadOnly(true);
    tabs->addTab(m_memoriesView, QStringLiteral("Memory"));

    m_sessionSummary = new QPlainTextEdit(tabs);
    m_sessionSummary->setReadOnly(true);
    tabs->addTab(m_sessionSummary, QStringLiteral("Session Summary"));

    m_privacyPreview = new QPlainTextEdit(tabs);
    m_privacyPreview->setReadOnly(true);
    tabs->addTab(m_privacyPreview, QStringLiteral("Privacy"));

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

    m_taskProgressBar = new QProgressBar(this);
    m_taskProgressBar->setMinimumWidth(360);
    m_taskProgressBar->setTextVisible(true);
    m_taskProgressBar->setVisible(false);
    m_taskProgressBar->setRange(0, 100);
    m_taskProgressBar->setValue(0);
    m_taskProgressBar->setFormat(QStringLiteral("%p%"));
    statusBar()->addPermanentWidget(m_taskProgressBar, 1);

    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopRequested);
    connect(m_reindexButton, &QPushButton::clicked, this, &MainWindow::reindexRequested);
    connect(m_refreshModelsButton, &QPushButton::clicked, this, &MainWindow::refreshModelsRequested);
    connect(m_newConversationButton, &QPushButton::clicked, this, &MainWindow::newConversationRequested);
    connect(m_deleteConversationButton, &QPushButton::clicked, this, &MainWindow::onDeleteConversationClicked);
    connect(m_rememberButton, &QPushButton::clicked, this, &MainWindow::onRememberClicked);
    connect(m_importFilesButton, &QPushButton::clicked, this, &MainWindow::onImportFilesClicked);
    connect(m_importFolderButton, &QPushButton::clicked, this, &MainWindow::onImportFolderClicked);
    connect(m_modelCombo, &QComboBox::currentTextChanged, this, &MainWindow::onModelSelectionChanged);
    connect(m_conversationsList, &QListWidget::currentItemChanged, this, &MainWindow::onConversationItemChanged);
    connect(m_busyIndicatorTimer, &QTimer::timeout, this, &MainWindow::updateBusyIndicator);
    connect(m_promptLabGenerateButton, &QPushButton::clicked, this, &MainWindow::onPromptLabGenerateClicked);
    connect(m_promptLabUseButton, &QPushButton::clicked, this, &MainWindow::onPromptLabUseClicked);
    connect(m_promptLabImportButton, &QPushButton::clicked, this, &MainWindow::onPromptLabImportAssetsClicked);
    connect(m_promptLabBrowseFilesButton, &QPushButton::clicked, this, &MainWindow::onPromptLabBrowseFilesClicked);
    connect(m_promptLabBrowseFolderButton, &QPushButton::clicked, this, &MainWindow::onPromptLabBrowseFolderClicked);
    connect(m_promptLabCopyRecipeButton, &QPushButton::clicked, this, &MainWindow::onPromptLabCopyRecipeClicked);
    connect(m_copyLastAnswerButton, &QPushButton::clicked, this, &MainWindow::onCopyLastAnswerClicked);
    connect(m_sourceInventoryFilter, &QLineEdit::textChanged, this, &MainWindow::onKnowledgeBaseFilterTextChanged);
    connect(m_prioritizeSelectedAssetButton, &QPushButton::clicked, this, &MainWindow::onPrioritizeSelectedKnowledgeAssetsClicked);
    connect(m_pinSelectedAssetButton, &QPushButton::clicked, this, &MainWindow::onPinSelectedKnowledgeAssetsClicked);
    connect(m_removeSelectedAssetButton, &QPushButton::clicked, this, &MainWindow::onRemoveSelectedKnowledgeAssetsClicked);
    connect(m_clearKnowledgeBaseButton, &QPushButton::clicked, this, &MainWindow::onClearKnowledgeBaseClicked);
    connect(m_removePrioritizedAssetButton, &QPushButton::clicked, this, &MainWindow::onRemoveSelectedPrioritizedAssetsClicked);
    connect(m_clearPrioritizedAssetsButton, &QPushButton::clicked, this, &MainWindow::onClearPrioritizedAssetsClicked);
    connect(m_reasoningTraceToggleButton, &QPushButton::toggled, this, &MainWindow::onReasoningTraceToggleToggled);

    if (m_promptLabPreview != nullptr) {
        m_promptLabPreview->setPlainText(buildPromptLabRecipe());
    }
    rebuildPrioritizedKnowledgeAssetsUi();
}


void MainWindow::appendTranscriptEntry(const QString &role, const QString &text)
{
    insertTranscriptMessage(role, text);
}

void MainWindow::insertTranscriptMessage(const QString &role, const QString &text)
{
    if (m_transcript == nullptr) {
        return;
    }

    QTextCursor cursor = m_transcript->textCursor();
    cursor.movePosition(QTextCursor::End);
    if (!m_transcript->document()->isEmpty()) {
        cursor.insertHtml(QStringLiteral("<div style=\"height:6px\"></div>"));
    }

    const QString html = messageToRichHtml(role, text, &m_transcriptCodeBlocks);
    cursor.insertHtml(html);
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


void MainWindow::resetTaskProgressBar()
{
    if (m_taskProgressBar == nullptr) {
        return;
    }

    m_taskProgressBar->setVisible(false);
    m_taskProgressBar->setRange(0, 100);
    m_taskProgressBar->setValue(0);
    m_taskProgressBar->setFormat(QStringLiteral("%p%"));
}

void MainWindow::beginResponseProgress(const QString &label)
{
    if (m_taskProgressBar == nullptr || m_indexingActive) {
        return;
    }

    m_responseProgressActive = true;
    m_responseFirstTokenReceived = false;
    m_responseProgressValue = 5;
    m_streamReceivedChars = 0;
    m_streamEstimatedChars = 1400;

    m_taskProgressBar->setVisible(true);
    m_taskProgressBar->setRange(0, 100);
    m_taskProgressBar->setValue(m_responseProgressValue);
    m_taskProgressBar->setFormat(label.isEmpty()
                                 ? QStringLiteral("Preparing prompt... %p%")
                                 : label + QStringLiteral(" %p%"));
}

void MainWindow::setResponseProgressStage(int value, const QString &label)
{
    if (m_taskProgressBar == nullptr || !m_responseProgressActive || m_indexingActive) {
        return;
    }

    const int clampedValue = qBound(0, value, 100);
    m_responseProgressValue = qMax(m_responseProgressValue, clampedValue);

    m_taskProgressBar->setVisible(true);
    m_taskProgressBar->setRange(0, 100);
    m_taskProgressBar->setValue(m_responseProgressValue);
    m_taskProgressBar->setFormat(label.isEmpty()
                                 ? QStringLiteral("%p%")
                                 : label + QStringLiteral(" %p%"));
}

void MainWindow::setResponseProgressBusy(const QString &label)
{
    if (m_taskProgressBar == nullptr || !m_responseProgressActive || m_indexingActive) {
        return;
    }

    m_taskProgressBar->setVisible(true);
    m_taskProgressBar->setRange(0, 0);
    m_taskProgressBar->setFormat(label.isEmpty() ? QStringLiteral("Working...") : label);
}

void MainWindow::updateResponseStreamingProgress(const QString &chunk)
{
    if (m_taskProgressBar == nullptr || !m_responseProgressActive || m_indexingActive || chunk.isEmpty()) {
        return;
    }

    if (!m_responseFirstTokenReceived) {
        m_responseFirstTokenReceived = true;
        m_responseProgressValue = qMax(m_responseProgressValue, 65);
    }

    m_streamReceivedChars += chunk.size();
    if (m_streamReceivedChars >= m_streamEstimatedChars) {
        m_streamEstimatedChars = m_streamReceivedChars + qMax(300, chunk.size() * 4);
    }

    const double ratio = m_streamEstimatedChars > 0
            ? static_cast<double>(m_streamReceivedChars) / static_cast<double>(m_streamEstimatedChars)
            : 0.0;

    int nextValue = 65 + static_cast<int>(ratio * 30.0);
    nextValue = qBound(65, nextValue, 95);
    m_responseProgressValue = qMax(m_responseProgressValue, nextValue);

    m_taskProgressBar->setVisible(true);
    m_taskProgressBar->setRange(0, 100);
    m_taskProgressBar->setValue(m_responseProgressValue);
    m_taskProgressBar->setFormat(QStringLiteral("Generating answer... %p%"));
}

void MainWindow::finishResponseProgress(const QString &label)
{
    if (m_taskProgressBar == nullptr) {
        return;
    }

    m_taskProgressBar->setVisible(true);
    m_taskProgressBar->setRange(0, 100);
    m_taskProgressBar->setValue(100);
    m_taskProgressBar->setFormat(label.isEmpty() ? QStringLiteral("Done") : label);

    m_responseProgressActive = false;
    m_responseFirstTokenReceived = false;
    m_responseProgressValue = 0;
    m_streamReceivedChars = 0;
    m_streamEstimatedChars = 1400;

    QTimer::singleShot(1400, this, [this]() {
        if (!m_responseProgressActive && !m_indexingActive) {
            resetTaskProgressBar();
        }
    });
}

void MainWindow::cancelResponseProgress(const QString &label)
{
    if (m_taskProgressBar == nullptr) {
        return;
    }

    m_taskProgressBar->setVisible(true);
    m_taskProgressBar->setRange(0, 100);
    m_taskProgressBar->setValue(0);
    m_taskProgressBar->setFormat(label.isEmpty() ? QStringLiteral("Canceled") : label);

    m_responseProgressActive = false;
    m_responseFirstTokenReceived = false;
    m_responseProgressValue = 0;
    m_streamReceivedChars = 0;
    m_streamEstimatedChars = 1400;

    QTimer::singleShot(1600, this, [this]() {
        if (!m_responseProgressActive && !m_indexingActive) {
            resetTaskProgressBar();
        }
    });
}

void MainWindow::appendUserMessage(const QString &text)
{
    m_streamingAssistant = false;
    m_streamingAssistantStartPosition = -1;
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
        m_streamingAssistantStartPosition = cursor.position();
        cursor.insertText(transcriptPrefix(QStringLiteral("assistant")), prefixFormat);
        m_streamingAssistant = true;
    }

    cursor.insertText(text, bodyFormat);
    m_transcript->setTextCursor(cursor);
    m_transcript->ensureCursorVisible();
    updateResponseStreamingProgress(text);
}

void MainWindow::finalizeAssistantMessage(const QString &text)
{
    m_lastAssistantMessage = text;

    if (m_streamingAssistant && m_streamingAssistantStartPosition >= 0) {
        QTextCursor cursor(m_transcript->document());
        cursor.setPosition(m_streamingAssistantStartPosition);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        m_transcript->setTextCursor(cursor);
        insertTranscriptMessage(QStringLiteral("assistant"), text);
    }

    m_streamingAssistant = false;
    m_streamingAssistantStartPosition = -1;

    if (m_responseProgressActive) {
        finishResponseProgress(QStringLiteral("Answer complete"));
    }
}

void MainWindow::appendSystemMessage(const QString &text)
{
    m_streamingAssistant = false;
    m_streamingAssistantStartPosition = -1;
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
    m_streamingAssistantStartPosition = -1;
    m_lastAssistantMessage.clear();
    m_transcriptCodeBlocks.clear();

    const QStringList blocks = text.split(QRegularExpression(QStringLiteral("\n\\s*\n")), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        const QString trimmed = block.trimmed();
        if (trimmed.startsWith(QStringLiteral("USER> "))) {
            appendTranscriptEntry(QStringLiteral("user"), trimmed.mid(6));
        } else if (trimmed.startsWith(QStringLiteral("ASSISTANT> "))) {
            const QString message = trimmed.mid(11);
            m_lastAssistantMessage = message;
            appendTranscriptEntry(QStringLiteral("assistant"), message);
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

    if (!m_responseProgressActive || m_indexingActive) {
        return;
    }

    if (text.startsWith(QStringLiteral("Analyzing knowledge base"), Qt::CaseInsensitive)) {
        setResponseProgressStage(15, QStringLiteral("Preparing grounded context..."));
    } else if (text.startsWith(QStringLiteral("Searching external sources"), Qt::CaseInsensitive)) {
        setResponseProgressBusy(QStringLiteral("Searching external sources..."));
    } else if (text.startsWith(QStringLiteral("External search finished"), Qt::CaseInsensitive)) {
        setResponseProgressStage(50, QStringLiteral("External context ready"));
    } else if (text.startsWith(QStringLiteral("Sending request to local model"), Qt::CaseInsensitive)) {
        setResponseProgressStage(60, QStringLiteral("Sending request to local model..."));
    } else if (text.startsWith(QStringLiteral("Awaiting first local tokens"), Qt::CaseInsensitive)) {
        setResponseProgressBusy(QStringLiteral("Waiting for first token..."));
    } else if (text.startsWith(QStringLiteral("Streaming response locally"), Qt::CaseInsensitive)) {
        setResponseProgressStage(65, QStringLiteral("Generating answer..."));
    } else if (text == QStringLiteral("Stopped.")) {
        cancelResponseProgress(QStringLiteral("Generation stopped"));
    } else if (text == QStringLiteral("Error.")) {
        cancelResponseProgress(QStringLiteral("Generation failed"));
    }
}

void MainWindow::setBusy(bool busy)
{
    m_sendButton->setEnabled(!busy && !m_indexingActive);
    m_stopButton->setEnabled(busy);
    m_input->setReadOnly(busy || m_indexingActive);
    m_newConversationButton->setEnabled(!busy);
    if (m_deleteConversationButton != nullptr) m_deleteConversationButton->setEnabled(!busy);
    m_importFilesButton->setEnabled(!busy && !m_indexingActive);
    m_importFolderButton->setEnabled(!busy && !m_indexingActive);
    m_reindexButton->setEnabled(!busy && !m_indexingActive);
    m_promptLabGenerateButton->setEnabled(!busy);
    m_promptLabUseButton->setEnabled(!busy);
    m_promptLabImportButton->setEnabled(!busy && !m_indexingActive);
    if (m_promptLabBrowseFilesButton != nullptr) m_promptLabBrowseFilesButton->setEnabled(!busy && !m_indexingActive);
    if (m_promptLabBrowseFolderButton != nullptr) m_promptLabBrowseFolderButton->setEnabled(!busy && !m_indexingActive);
    if (m_promptLabCopyRecipeButton != nullptr) m_promptLabCopyRecipeButton->setEnabled(!busy);
    if (m_prioritizeSelectedAssetButton != nullptr) m_prioritizeSelectedAssetButton->setEnabled(!busy && !m_indexingActive);
    if (m_pinSelectedAssetButton != nullptr) m_pinSelectedAssetButton->setEnabled(!busy && !m_indexingActive);
    if (m_removeSelectedAssetButton != nullptr) m_removeSelectedAssetButton->setEnabled(!busy && !m_indexingActive);
    if (m_clearKnowledgeBaseButton != nullptr) m_clearKnowledgeBaseButton->setEnabled(!busy && !m_indexingActive);
    if (m_removePrioritizedAssetButton != nullptr) m_removePrioritizedAssetButton->setEnabled(!busy);
    if (m_clearPrioritizedAssetsButton != nullptr) m_clearPrioritizedAssetsButton->setEnabled(!busy);

    if (busy) {
        beginResponseProgress(QStringLiteral("Preparing prompt..."));
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

void MainWindow::setIndexingActive(bool active)
{
    m_indexingActive = active;

    m_sendButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_deleteConversationButton != nullptr) m_deleteConversationButton->setEnabled(!active && !m_stopButton->isEnabled());
    m_input->setReadOnly(active || m_stopButton->isEnabled());
    m_reindexButton->setEnabled(!active && !m_stopButton->isEnabled());
    m_importFilesButton->setEnabled(!active && !m_stopButton->isEnabled());
    m_importFolderButton->setEnabled(!active && !m_stopButton->isEnabled());
    m_promptLabImportButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_promptLabBrowseFilesButton != nullptr) m_promptLabBrowseFilesButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_promptLabBrowseFolderButton != nullptr) m_promptLabBrowseFolderButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_prioritizeSelectedAssetButton != nullptr) m_prioritizeSelectedAssetButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_pinSelectedAssetButton != nullptr) m_pinSelectedAssetButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_removeSelectedAssetButton != nullptr) m_removeSelectedAssetButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_clearKnowledgeBaseButton != nullptr) m_clearKnowledgeBaseButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_removePrioritizedAssetButton != nullptr) m_removePrioritizedAssetButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_clearPrioritizedAssetsButton != nullptr) m_clearPrioritizedAssetsButton->setEnabled(!active && !m_stopButton->isEnabled());

    if (m_taskProgressBar == nullptr) {
        return;
    }

    if (active) {
        m_taskProgressBar->setVisible(true);
        m_taskProgressBar->setRange(0, 0);
        m_taskProgressBar->setFormat(QStringLiteral("Indexing local docs..."));
        m_statusLabel->setText(QStringLiteral("Indexing local docs..."));
    } else {
        resetTaskProgressBar();
        if (!m_stopButton->isEnabled()) {
            m_statusLabel->setText(QStringLiteral("Ready."));
        }
    }
}

void MainWindow::setIndexingProgress(int value, int maximum, const QString &label)
{
    if (m_taskProgressBar == nullptr) {
        return;
    }

    m_taskProgressBar->setVisible(true);

    if (maximum <= 0) {
        m_taskProgressBar->setRange(0, 0);
        m_taskProgressBar->setFormat(label.isEmpty() ? QStringLiteral("Working...") : label);
    } else {
        m_taskProgressBar->setRange(0, maximum);
        m_taskProgressBar->setValue(qBound(0, value, maximum));
        if (label.isEmpty()) {
            m_taskProgressBar->setFormat(QStringLiteral("%v / %m"));
        } else {
            m_taskProgressBar->setFormat(QStringLiteral("%1  (%2/%3)").arg(label).arg(qBound(0, value, maximum)).arg(maximum));
        }
    }

    if (!label.isEmpty()) {
        m_statusLabel->setText(label);
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
    if (m_sourceInventory != nullptr) {
        QStringList lines = text.split(QLatin1Char('\n'));
        QStringList summary;
        while (!lines.isEmpty()) {
            const QString line = lines.takeFirst();
            if (line.trimmed().isEmpty()) {
                break;
            }
            summary << line;
        }
        m_sourceInventory->setPlainText(summary.join(QStringLiteral("\n")));
    }

    if (m_sourceInventoryList == nullptr) {
        return;
    }

    m_sourceInventoryList->clear();
    if (text.trimmed().isEmpty() || text.trimmed() == QStringLiteral("<none>")) {
        updateKnowledgeBaseFilterStatus();
        return;
    }

    const QStringList blocks = text.split(QRegularExpression(QStringLiteral("\n\\s*\n")), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        const QStringList lines = block.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QString filePath;
        QStringList details;
        for (const QString &line : lines) {
            if (line.startsWith(QStringLiteral("File: "))) {
                filePath = line.mid(6).trimmed();
            } else {
                details << line.trimmed();
            }
        }
        if (filePath.isEmpty()) {
            continue;
        }
        auto *item = new QListWidgetItem(filePath, m_sourceInventoryList);
        item->setData(Qt::UserRole, filePath);
        item->setData(Qt::UserRole + 1, QFileInfo(filePath).fileName());
        item->setToolTip(details.join(QStringLiteral("\n")));
    }

    QStringList availablePaths;
    for (int i = 0; i < m_sourceInventoryList->count(); ++i) {
        QListWidgetItem *item = m_sourceInventoryList->item(i);
        if (item != nullptr) {
            const QString path = item->data(Qt::UserRole).toString().trimmed();
            if (!path.isEmpty()) {
                availablePaths << path;
            }
        }
    }
    QStringList filteredOneShot;
    for (const QString &path : m_oneShotPrioritizedAssets) {
        if (availablePaths.contains(path)) {
            filteredOneShot << path;
        }
    }
    m_oneShotPrioritizedAssets = filteredOneShot;

    QStringList filteredPinned;
    for (const QString &path : m_pinnedKnowledgeAssets) {
        if (availablePaths.contains(path)) {
            filteredPinned << path;
        }
    }
    m_pinnedKnowledgeAssets = filteredPinned;
    rebuildPrioritizedKnowledgeAssetsUi();
    emitPrioritizedKnowledgeAssetsState();
    applyKnowledgeBaseFilter();
}

void MainWindow::applyKnowledgeBaseFilter()
{
    if (m_sourceInventoryList == nullptr) {
        updateKnowledgeBaseFilterStatus();
        return;
    }

    const QString needle = m_sourceInventoryFilter != nullptr ? m_sourceInventoryFilter->text().trimmed() : QString();
    for (int i = 0; i < m_sourceInventoryList->count(); ++i) {
        QListWidgetItem *item = m_sourceInventoryList->item(i);
        if (item == nullptr) {
            continue;
        }

        bool matches = true;
        if (!needle.isEmpty()) {
            const QString fullPath = item->data(Qt::UserRole).toString();
            const QString fileName = item->data(Qt::UserRole + 1).toString();
            const QString toolTip = item->toolTip();
            matches = fullPath.contains(needle, Qt::CaseInsensitive)
                   || fileName.contains(needle, Qt::CaseInsensitive)
                   || toolTip.contains(needle, Qt::CaseInsensitive);
        }

        item->setHidden(!matches);
        if (!matches) {
            item->setSelected(false);
        }
    }

    updateKnowledgeBaseFilterStatus();
}

void MainWindow::updateKnowledgeBaseFilterStatus()
{
    if (m_sourceInventoryFilterStatus == nullptr || m_sourceInventoryList == nullptr) {
        return;
    }

    const int total = m_sourceInventoryList->count();
    int visible = 0;
    for (int i = 0; i < total; ++i) {
        QListWidgetItem *item = m_sourceInventoryList->item(i);
        if (item != nullptr && !item->isHidden()) {
            ++visible;
        }
    }

    m_sourceInventoryFilterStatus->setText(QStringLiteral("%1 / %2 shown").arg(visible).arg(total));
}

QStringList MainWindow::selectedKnowledgeAssetPaths() const
{
    QStringList paths;
    if (m_sourceInventoryList == nullptr) {
        return paths;
    }

    const QList<QListWidgetItem *> items = m_sourceInventoryList->selectedItems();
    for (QListWidgetItem *item : items) {
        if (item == nullptr) {
            continue;
        }
        const QString path = item->data(Qt::UserRole).toString().trimmed();
        if (!path.isEmpty() && !paths.contains(path)) {
            paths << path;
        }
    }
    return paths;
}

void MainWindow::rebuildPrioritizedKnowledgeAssetsUi()
{
    if (m_prioritizedAssetsList == nullptr || m_prioritizedAssetsStatus == nullptr) {
        return;
    }

    m_prioritizedAssetsList->clear();

    auto addItem = [this](const QString &path, bool pinned) {
        auto *item = new QListWidgetItem(QStringLiteral("%1 %2")
                                             .arg(pinned ? QStringLiteral("[PIN]") : QStringLiteral("[NEXT]"),
                                                  QFileInfo(path).fileName()),
                                         m_prioritizedAssetsList);
        item->setData(Qt::UserRole, path);
        item->setData(Qt::UserRole + 1, pinned ? QStringLiteral("pin") : QStringLiteral("next"));
        item->setToolTip(path);
        item->setForeground(QColor(pinned ? QStringLiteral("#f59e0b") : QStringLiteral("#38bdf8")));
    };

    for (const QString &path : m_pinnedKnowledgeAssets) {
        addItem(path, true);
    }
    for (const QString &path : m_oneShotPrioritizedAssets) {
        if (!m_pinnedKnowledgeAssets.contains(path)) {
            addItem(path, false);
        }
    }

    const int total = m_prioritizedAssetsList->count();
    if (total == 0) {
        m_prioritizedAssetsStatus->setText(QStringLiteral("No prioritized KB assets"));
    } else {
        m_prioritizedAssetsStatus->setText(QStringLiteral("%1 active | %2 pinned | %3 next")
                                           .arg(total)
                                           .arg(m_pinnedKnowledgeAssets.size())
                                           .arg(m_oneShotPrioritizedAssets.size()));
    }
}

void MainWindow::emitPrioritizedKnowledgeAssetsState()
{
    QStringList combined = m_pinnedKnowledgeAssets;
    for (const QString &path : m_oneShotPrioritizedAssets) {
        if (!combined.contains(path)) {
            combined << path;
        }
    }
    emit prioritizedKnowledgeAssetsChanged(combined);
}

void MainWindow::onKnowledgeBaseFilterTextChanged(const QString &text)
{
    Q_UNUSED(text);
    applyKnowledgeBaseFilter();
}

void MainWindow::onRemoveSelectedKnowledgeAssetsClicked()
{
    if (m_sourceInventoryList == nullptr) {
        return;
    }

    const QStringList paths = selectedKnowledgeAssetPaths();

    if (paths.isEmpty()) {
        QMessageBox::information(this,
                                 QStringLiteral("Knowledge Base"),
                                 QStringLiteral("Select one or more knowledge base assets first."));
        return;
    }

    const auto answer = QMessageBox::question(this,
                                              QStringLiteral("Remove knowledge assets"),
                                              QStringLiteral("Remove %1 selected asset(s) from the knowledge base?").arg(paths.size()));
    if (answer != QMessageBox::Yes) {
        return;
    }

    emit removeKnowledgeAssetsRequested(paths);
}

void MainWindow::onPrioritizeSelectedKnowledgeAssetsClicked()
{
    const QStringList paths = selectedKnowledgeAssetPaths();
    if (paths.isEmpty()) {
        QMessageBox::information(this,
                                 QStringLiteral("Knowledge Base"),
                                 QStringLiteral("Select one or more knowledge base assets first."));
        return;
    }

    int added = 0;
    for (const QString &path : paths) {
        if (m_pinnedKnowledgeAssets.contains(path) || m_oneShotPrioritizedAssets.contains(path)) {
            continue;
        }
        m_oneShotPrioritizedAssets << path;
        ++added;
    }

    rebuildPrioritizedKnowledgeAssetsUi();
    emitPrioritizedKnowledgeAssetsState();
    m_statusLabel->setText(QStringLiteral("Queued %1 KB asset(s) for the next prompt.").arg(added > 0 ? added : paths.size()));
}

void MainWindow::onPinSelectedKnowledgeAssetsClicked()
{
    const QStringList paths = selectedKnowledgeAssetPaths();
    if (paths.isEmpty()) {
        QMessageBox::information(this,
                                 QStringLiteral("Knowledge Base"),
                                 QStringLiteral("Select one or more knowledge base assets first."));
        return;
    }

    int added = 0;
    for (const QString &path : paths) {
        m_oneShotPrioritizedAssets.removeAll(path);
        if (!m_pinnedKnowledgeAssets.contains(path)) {
            m_pinnedKnowledgeAssets << path;
            ++added;
        }
    }

    rebuildPrioritizedKnowledgeAssetsUi();
    emitPrioritizedKnowledgeAssetsState();
    m_statusLabel->setText(QStringLiteral("Pinned %1 KB asset(s) for retrieval priority.").arg(added > 0 ? added : paths.size()));
}

void MainWindow::onRemoveSelectedPrioritizedAssetsClicked()
{
    if (m_prioritizedAssetsList == nullptr) {
        return;
    }

    const QList<QListWidgetItem *> items = m_prioritizedAssetsList->selectedItems();
    if (items.isEmpty()) {
        QMessageBox::information(this,
                                 QStringLiteral("Prioritized KB assets"),
                                 QStringLiteral("Select one or more prioritized assets to remove."));
        return;
    }

    for (QListWidgetItem *item : items) {
        if (item == nullptr) {
            continue;
        }
        const QString path = item->data(Qt::UserRole).toString().trimmed();
        m_oneShotPrioritizedAssets.removeAll(path);
        m_pinnedKnowledgeAssets.removeAll(path);
    }

    rebuildPrioritizedKnowledgeAssetsUi();
    emitPrioritizedKnowledgeAssetsState();
    m_statusLabel->setText(QStringLiteral("Removed selected prioritized KB assets."));
}

void MainWindow::onClearPrioritizedAssetsClicked()
{
    m_oneShotPrioritizedAssets.clear();
    m_pinnedKnowledgeAssets.clear();
    rebuildPrioritizedKnowledgeAssetsUi();
    emitPrioritizedKnowledgeAssetsState();
    m_statusLabel->setText(QStringLiteral("Cleared prioritized KB assets."));
}

void MainWindow::onClearKnowledgeBaseClicked()
{
    const auto answer = QMessageBox::warning(this,
                                             QStringLiteral("Clear knowledge base"),
                                             QStringLiteral("Remove all imported assets from the knowledge base? This cannot be undone."),
                                             QMessageBox::Yes | QMessageBox::No,
                                             QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    emit clearKnowledgeBaseRequested();
}

void MainWindow::onDeleteConversationClicked()
{
    if (m_conversationsList == nullptr) {
        return;
    }

    QListWidgetItem *item = m_conversationsList->currentItem();
    if (item == nullptr) {
        QMessageBox::information(this,
                                 QStringLiteral("Conversations"),
                                 QStringLiteral("Select a conversation to delete first."));
        return;
    }

    const QString conversationId = item->data(Qt::UserRole).toString().trimmed();
    const QString title = item->text().trimmed().isEmpty() ? QStringLiteral("this conversation") : item->text().trimmed();
    const auto answer = QMessageBox::warning(this,
                                             QStringLiteral("Delete conversation"),
                                             QStringLiteral("Delete '%1'? This removes the saved transcript and summary for that conversation.").arg(title),
                                             QMessageBox::Yes | QMessageBox::No,
                                             QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    emit deleteConversationRequested(conversationId);
}

void MainWindow::onReasoningTraceToggleToggled(bool checked)
{
    if (m_reasoningTraceToggleButton != nullptr) {
        m_reasoningTraceToggleButton->setText(checked
                                              ? QStringLiteral("Capture reasoning trace: ON")
                                              : QStringLiteral("Capture reasoning trace: OFF"));
    }

    if (m_reasoningTraceInfoLabel != nullptr) {
        m_reasoningTraceInfoLabel->setText(checked
                                           ? QStringLiteral("Enabled. Amelia will request backend thinking streams when the model supports them and capture explicit reasoning notes here. This is not hidden internal chain-of-thought.")
                                           : QStringLiteral("Off by default. When enabled, Amelia logs backend thinking streams or explicit model-authored work notes here when available."));
    }

    emit reasoningTraceCaptureToggled(checked);
}

void MainWindow::onTranscriptAnchorClicked(const QUrl &url)
{
    if (url.scheme() == QStringLiteral("copycode")) {
        bool ok = false;
        const int index = url.path().isEmpty() ? url.host().toInt(&ok) : url.path().mid(1).toInt(&ok);
        if (!ok || index < 0 || index >= m_transcriptCodeBlocks.size()) {
            return;
        }
        QApplication::clipboard()->setText(m_transcriptCodeBlocks.at(index));
        m_statusLabel->setText(QStringLiteral("Code block copied to clipboard."));
        return;
    }

    if (url.isValid()) {
        QDesktopServices::openUrl(url);
    }
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
    const bool hadOneShotPriorities = !m_oneShotPrioritizedAssets.isEmpty();
    emit promptSubmitted(prompt, m_externalSearchCheck->isChecked());
    if (hadOneShotPriorities) {
        m_oneShotPrioritizedAssets.clear();
        rebuildPrioritizedKnowledgeAssetsUi();
        emitPrioritizedKnowledgeAssetsState();
    }
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
    const QStringList localAssets = m_promptLabAssets != nullptr ? splitAssetPaths(m_promptLabAssets->text()) : QStringList();
    const QStringList kbAssets = m_promptLabKbAssets != nullptr ? splitAssetPaths(m_promptLabKbAssets->text()) : QStringList();

    QStringList directives;
    if (preset == QStringLiteral("Code patch")) {
        directives << QStringLiteral("Study the selected assets before suggesting code changes.")
                   << QStringLiteral("Preserve existing naming, architecture, and coding style.")
                   << QStringLiteral("Output paste-ready functions or files only when asked.");
    } else if (preset == QStringLiteral("Runbook / docs")) {
        directives << QStringLiteral("Extract grounded operational steps from the selected assets.")
                   << QStringLiteral("Prefer prerequisites, procedure, validation, rollback, and caveats.")
                   << QStringLiteral("Do not invent commands, versions, or environment-specific details.");
    } else if (preset == QStringLiteral("Incident investigation")) {
        directives << QStringLiteral("Correlate clues from logs, configs, and notes in the selected assets.")
                   << QStringLiteral("Separate evidence, hypotheses, risks, and next checks.")
                   << QStringLiteral("Prefer the smallest high-signal next diagnostic step.");
    } else if (preset == QStringLiteral("Dataset from assets")) {
        directives << QStringLiteral("Transform the selected assets into reusable supervision examples.")
                   << QStringLiteral("Produce compact input/output pairs grounded in those assets.")
                   << QStringLiteral("Reject unsupported labels or invented metadata.");
    } else if (preset == QStringLiteral("KB-only tutoring")) {
        directives << QStringLiteral("Teach only from the KB material referenced here.")
                   << QStringLiteral("Start with a concise table of contents when the topic is broad.")
                   << QStringLiteral("State when the KB does not support a claim.");
    } else if (preset == QStringLiteral("Summarize new KB assets")) {
        directives << QStringLiteral("Summarize what appears to be new or newly imported KB material.")
                   << QStringLiteral("Highlight themes, domains, and what Amelia can now answer better.")
                   << QStringLiteral("Do not claim anything that is not supported by retrieved KB context.");
    } else if (preset == QStringLiteral("Compare assets / revisions")) {
        directives << QStringLiteral("Compare the selected local assets and KB references carefully.")
                   << QStringLiteral("Call out additions, removals, operational impacts, and unresolved gaps.")
                   << QStringLiteral("Prefer a structured diff-style answer.");
    } else if (preset == QStringLiteral("Flashcards / Q&A")) {
        directives << QStringLiteral("Generate concise grounded questions and answers from the selected assets.")
                   << QStringLiteral("Prefer teachable chunks and avoid invented facts.")
                   << QStringLiteral("Group related cards by topic when possible.");
    } else {
        directives << QStringLiteral("Ground every answer in the selected assets and the user request.")
                   << QStringLiteral("Prefer explicit references to file names, sections, or KB hints when possible.")
                   << QStringLiteral("Say when information is missing instead of guessing.");
    }

    const QString displayGoal = goal.isEmpty() ? QStringLiteral("<describe the target outcome>") : goal;
    const QString displayLocalAssets = localAssets.isEmpty() ? QStringLiteral("<none listed>") : localAssets.join(QStringLiteral("; "));
    const QString displayKbAssets = kbAssets.isEmpty() ? QStringLiteral("<none listed>") : kbAssets.join(QStringLiteral("; "));

    QString escapedGoal = displayGoal;
    QString escapedLocalAssets = displayLocalAssets;
    QString escapedKbAssets = displayKbAssets;
    escapedGoal.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    escapedLocalAssets.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    escapedKbAssets.replace(QStringLiteral("\""), QStringLiteral("\\\""));

    QStringList lines;
    lines << QStringLiteral("Prompt Lab recipe");
    lines << QStringLiteral("=================");
    lines << QStringLiteral("Preset: %1").arg(preset);
    lines << QStringLiteral("Goal: %1").arg(displayGoal);
    lines << QStringLiteral("Local assets: %1").arg(displayLocalAssets);
    lines << QStringLiteral("KB assets: %1").arg(displayKbAssets);
    lines << QString();
    lines << QStringLiteral("Use this with Amelia:");
    lines << QStringLiteral("You will answer only from the selected local assets and/or KB references.");
    for (const QString &directive : directives) {
        lines << QStringLiteral("- %1").arg(directive);
    }
    if (!notes.isEmpty()) {
        lines << QStringLiteral("- Extra constraints: %1").arg(notes);
    }
    lines << QString();
    lines << QStringLiteral("Suggested working prompt:");
    lines << QStringLiteral("Analyze the selected assets and help with this goal: %1").arg(displayGoal);
    lines << QStringLiteral("Use only the imported local assets and the referenced KB materials. Identify which materials were used.");
    lines << QString();
    lines << QStringLiteral("JSONL training sample preview:");
    lines << QStringLiteral("{\"messages\":[{\"role\":\"system\",\"content\":\"You are Amelia. Answer only from the selected local assets and KB references.\"},{\"role\":\"user\",\"content\":\"Goal: %1\nLocal assets: %2\nKB assets: %3\"},{\"role\":\"assistant\",\"content\":\"Grounded response based on the selected assets and KB references.\"}]}")
                .arg(escapedGoal, escapedLocalAssets, escapedKbAssets);

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
    const QStringList localPaths = splitAssetPaths(m_promptLabAssets != nullptr ? m_promptLabAssets->text() : QString());
    const QStringList kbRefs = splitAssetPaths(m_promptLabKbAssets != nullptr ? m_promptLabKbAssets->text() : QString());
    if (localPaths.isEmpty()) {
        if (!kbRefs.isEmpty()) {
            QMessageBox::information(this,
                                     QStringLiteral("Prompt Lab"),
                                     QStringLiteral("KB asset references do not need importing. Generate or use the recipe directly."));
            m_statusLabel->setText(QStringLiteral("Prompt Lab is using KB asset references only."));
            return;
        }
        QMessageBox::information(this,
                                 QStringLiteral("Prompt Lab"),
                                 QStringLiteral("Add one or more local asset paths first, or populate KB assets if they are already indexed."));
        return;
    }

    emit importPathsRequested(localPaths);
    m_statusLabel->setText(QStringLiteral("Prompt Lab requested local asset import for %1 path(s).%2")
                           .arg(localPaths.size())
                           .arg(kbRefs.isEmpty() ? QString() : QStringLiteral(" KB references will be used as-is.")));
}

void MainWindow::onPromptLabBrowseFilesClicked()
{
    const QStringList files = QFileDialog::getOpenFileNames(this,
                                                            QStringLiteral("Select Prompt Lab assets"),
                                                            QString(),
                                                            QStringLiteral("All files (*.*)"));
    appendPathsToLineEdit(m_promptLabAssets, files);
    if (!files.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Prompt Lab added %1 file asset(s).").arg(files.size()));
    }
}

void MainWindow::onPromptLabBrowseFolderClicked()
{
    const QString folder = QFileDialog::getExistingDirectory(this,
                                                             QStringLiteral("Select Prompt Lab asset folder"),
                                                             QString());
    if (!folder.trimmed().isEmpty()) {
        appendPathsToLineEdit(m_promptLabAssets, {folder});
        m_statusLabel->setText(QStringLiteral("Prompt Lab added folder asset: %1").arg(folder));
    }
}

void MainWindow::onPromptLabCopyRecipeClicked()
{
    const QString recipe = buildPromptLabRecipe();
    if (m_promptLabPreview != nullptr) {
        m_promptLabPreview->setPlainText(recipe);
    }
    QApplication::clipboard()->setText(recipe);
    m_statusLabel->setText(QStringLiteral("Prompt Lab recipe copied to clipboard."));
}

void MainWindow::onCopyLastAnswerClicked()
{
    if (m_lastAssistantMessage.trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Copy answer"), QStringLiteral("There is no completed assistant answer to copy yet."));
        return;
    }
    QApplication::clipboard()->setText(m_lastAssistantMessage);
    m_statusLabel->setText(QStringLiteral("Last assistant answer copied to clipboard."));
}

void MainWindow::onCopyCodeBlocksClicked()
{
    if (m_lastAssistantMessage.trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Copy code"), QStringLiteral("There is no completed assistant answer to inspect yet."));
        return;
    }
    const QString codeOnly = extractCodeBlocks(m_lastAssistantMessage);
    if (codeOnly.trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Copy code"), QStringLiteral("The last assistant answer does not contain fenced code blocks."));
        return;
    }
    QApplication::clipboard()->setText(codeOnly);
    m_statusLabel->setText(QStringLiteral("Code blocks from the last assistant answer copied to clipboard."));
}

void MainWindow::showAboutAmelia()
{
    QMessageBox::about(this,
                       QStringLiteral("About Amelia"),
                       QStringLiteral("<b>Amelia Qt6 v%1</b><br><br>A local offline coding tool and assistant built with C++ and Qt6.<br><br>This build includes copy-friendly colored transcript/diagnostic views, fenced-code formatting, last-answer/code copy helpers, local Ollama inference, persistent knowledge, richer Prompt Lab KB-aware asset helpers, prompt budgeting, outline-first document generation, asynchronous PDF indexing, and operational diagnostics.")
                           .arg(QLatin1StringView(AmeliaVersion::kDisplayVersion)));
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
