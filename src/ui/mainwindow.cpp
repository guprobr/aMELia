#include "ui/mainwindow.h"
#include "core/appconfig.h"
#include "core/appversion.h"

#include <algorithm>
#include <functional>
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
#include <QDialogButtonBox>
#include <QDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QKeySequence>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <QTabBar>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
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
        return QColor(QStringLiteral("#a855f7"));
    }
    return QColor(QStringLiteral("#d1d5db"));
}

constexpr int kKnowledgeNodeTypeRole = Qt::UserRole + 20;
constexpr int kKnowledgePathRole = Qt::UserRole + 21;
constexpr int kKnowledgeCollectionIdRole = Qt::UserRole + 22;
constexpr int kKnowledgeSearchBlobRole = Qt::UserRole + 23;

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

QString trimCodeBlockFencePadding(QString code)
{
    while (code.startsWith(QLatin1Char('\n'))) {
        code.remove(0, 1);
    }
    while (code.endsWith(QLatin1Char('\n'))) {
        code.chop(1);
    }
    return code;
}

QVector<TranscriptSegment> splitTranscriptSegments(const QString &text)
{
    QVector<TranscriptSegment> segments;
    QString plainBuffer;
    QString codeBuffer;
    QString currentLanguage;
    bool inCode = false;

    const auto flushPlain = [&segments, &plainBuffer]() {
        if (plainBuffer.isEmpty()) {
            return;
        }
        TranscriptSegment seg;
        seg.isCode = false;
        seg.text = plainBuffer;
        segments.push_back(seg);
        plainBuffer.clear();
    };

    const auto flushCode = [&segments, &codeBuffer, &currentLanguage]() {
        if (codeBuffer.isEmpty()) {
            currentLanguage.clear();
            return;
        }
        TranscriptSegment seg;
        seg.isCode = true;
        seg.language = currentLanguage;
        seg.text = codeBuffer;
        segments.push_back(seg);
        codeBuffer.clear();
        currentLanguage.clear();
    };

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (!inCode) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("```"))) {
                flushPlain();
                currentLanguage = trimmed.mid(3).trimmed();
                inCode = true;
            } else {
                plainBuffer += line;
                if (i + 1 < lines.size()) {
                    plainBuffer += QLatin1Char('\n');
                }
            }
            continue;
        }

        const int fencePos = line.indexOf(QStringLiteral("```"));
        if (fencePos >= 0) {
            const QString beforeFence = line.left(fencePos);
            if (!beforeFence.isEmpty()) {
                codeBuffer += beforeFence;
            }
            flushCode();
            inCode = false;

            const QString trailing = line.mid(fencePos + 3);
            if (!trailing.isEmpty()) {
                plainBuffer += trailing;
                if (i + 1 < lines.size()) {
                    plainBuffer += QLatin1Char('\n');
                }
            }
            continue;
        }

        codeBuffer += line;
        if (i + 1 < lines.size()) {
            codeBuffer += QLatin1Char('\n');
        }
    }

    if (inCode) {
        flushCode();
    } else {
        flushPlain();
    }
    return segments;
}


QString extractCodeBlocks(const QString &text)
{
    QStringList blocks;
    const QVector<TranscriptSegment> segments = splitTranscriptSegments(text);
    for (const TranscriptSegment &segment : segments) {
        if (segment.isCode && !segment.text.trimmed().isEmpty()) {
            const QString code = trimCodeBlockFencePadding(segment.text);
            if (!code.isEmpty()) {
                blocks << code;
            }
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

QString escapeHtmlLikeTags(const QString &text)
{
    QString sanitized = text;
    QRegularExpression tagPattern(QStringLiteral(R"(<(/?[A-Za-z!][^>\n]{0,200})>)"));
    QRegularExpressionMatchIterator it = tagPattern.globalMatch(sanitized);

    struct Replacement {
        int start = 0;
        int length = 0;
        QString value;
    };
    QVector<Replacement> replacements;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (!match.hasMatch()) {
            continue;
        }
        Replacement repl;
        repl.start = match.capturedStart(0);
        repl.length = match.capturedLength(0);
        repl.value = match.captured(0).toHtmlEscaped();
        replacements.push_back(repl);
    }

    for (int i = replacements.size() - 1; i >= 0; --i) {
        const Replacement &repl = replacements.at(i);
        sanitized.replace(repl.start, repl.length, repl.value);
    }
    return sanitized;
}

QString decodeDoubleEscapedHtmlEntities(QString html)
{
    html.replace(QStringLiteral("&amp;lt;"), QStringLiteral("&lt;"));
    html.replace(QStringLiteral("&amp;gt;"), QStringLiteral("&gt;"));
    html.replace(QStringLiteral("&amp;quot;"), QStringLiteral("&quot;"));
    html.replace(QStringLiteral("&amp;apos;"), QStringLiteral("&apos;"));
    html.replace(QStringLiteral("&amp;#39;"), QStringLiteral("&#39;"));
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
    doc.setMarkdown(escapeHtmlLikeTags(trimmed));
    QString html = decodeDoubleEscapedHtmlEntities(bodyFragmentFromDocument(doc));

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
            const QString code = trimCodeBlockFencePadding(segment.text);
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
                "<pre style=\"margin:0;background:#020617;color:#e2e8f0;padding:12px;border-radius:10px;border:1px solid #334155;overflow:auto;white-space:pre;tab-size:4;\"><code>%3</code></pre>"
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

MainWindow::MainWindow(const QString &configPath,
                       const QString &defaultConfigJson,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_configPath(configPath.trimmed().isEmpty() ? ameliaPreferredUserConfigPath() : configPath)
    , m_defaultConfigJson(defaultConfigJson.trimmed().isEmpty() ? ameliaBuiltInDefaultConfigJson() : defaultConfigJson)
    , m_busyFrames({QStringLiteral("◐"), QStringLiteral("◓"), QStringLiteral("◑"), QStringLiteral("◒")})
{
    setWindowTitle(QStringLiteral("Amelia Qt6 v%1").arg(QLatin1StringView(AmeliaVersion::kDisplayVersion)));
    setMinimumSize(1280, 820);

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    auto *settingsMenu = menuBar()->addMenu(QStringLiteral("&Settings"));
    auto *memoryMenu = menuBar()->addMenu(QStringLiteral("&Memory"));
    auto *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));

    auto *newConversationAction = fileMenu->addAction(QStringLiteral("New conversation"));
    newConversationAction->setShortcut(QKeySequence::New);
    connect(newConversationAction, &QAction::triggered, this, &MainWindow::newConversationRequested);

    m_configurationAction = settingsMenu->addAction(QStringLiteral("Configuration..."));
    connect(m_configurationAction, &QAction::triggered, this, &MainWindow::onOpenConfigurationDialog);

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
    m_modelCombo->addItem(QStringLiteral("qwen2.5:7b"));
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
    m_rememberButton = new QPushButton(QStringLiteral("Manual Memory"), chatPane);
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

    m_backendSummary = new QPlainTextEdit(tabs);
    m_backendSummary->setReadOnly(true);

    auto *kbTab = new QWidget(tabs);
    auto *kbLayout = new QVBoxLayout(kbTab);
    m_sourceInventory = new QPlainTextEdit(kbTab);
    m_sourceInventory->setReadOnly(true);
    m_sourceInventory->setMaximumHeight(120);

    auto *kbFilterLayout = new QHBoxLayout();
    m_sourceInventoryFilter = new QLineEdit(kbTab);
    m_sourceInventoryFilter->setClearButtonEnabled(true);
    m_sourceInventoryFilter->setPlaceholderText(QStringLiteral("Search collection labels, folders, file names, or paths..."));
    m_sourceInventorySortCombo = new QComboBox(kbTab);
    m_sourceInventorySortCombo->addItems({QStringLiteral("Sort: Name"), QStringLiteral("Sort: File type")});
    m_sourceInventoryFilterStatus = new QLabel(QStringLiteral("0 / 0 shown"), kbTab);
    kbFilterLayout->addWidget(new QLabel(QStringLiteral("Search:"), kbTab));
    kbFilterLayout->addWidget(m_sourceInventoryFilter, 1);
    kbFilterLayout->addWidget(m_sourceInventorySortCombo, 0);
    kbFilterLayout->addWidget(m_sourceInventoryFilterStatus);

    m_sourceInventoryTree = new QTreeWidget(kbTab);
    m_sourceInventoryTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_sourceInventoryTree->setRootIsDecorated(true);
    m_sourceInventoryTree->setAlternatingRowColors(true);
    m_sourceInventoryTree->setSortingEnabled(true);
    m_sourceInventoryTree->setHeaderLabels({QStringLiteral("Knowledge asset"), QStringLiteral("Kind"), QStringLiteral("Location / notes")});

    auto *kbButtons = new QHBoxLayout();
    m_prioritizeSelectedAssetButton = new QPushButton(QStringLiteral("Use once"), kbTab);
    m_pinSelectedAssetButton = new QPushButton(QStringLiteral("Pin"), kbTab);
    m_renameKnowledgeLabelButton = new QPushButton(QStringLiteral("Rename label"), kbTab);
    m_removeSelectedAssetButton = new QPushButton(QStringLiteral("Remove selected"), kbTab);
    m_clearKnowledgeBaseButton = new QPushButton(QStringLiteral("Clear KB"), kbTab);
    kbButtons->addWidget(m_prioritizeSelectedAssetButton);
    kbButtons->addWidget(m_pinSelectedAssetButton);
    kbButtons->addWidget(m_renameKnowledgeLabelButton);
    kbButtons->addWidget(m_removeSelectedAssetButton);
    kbButtons->addWidget(m_clearKnowledgeBaseButton);
    kbButtons->addStretch(1);
    kbLayout->addWidget(m_sourceInventory);
    kbLayout->addLayout(kbFilterLayout);
    kbLayout->addWidget(m_sourceInventoryTree, 1);
    kbLayout->addLayout(kbButtons);

    tabs->addTab(diagnosticsTab, QStringLiteral("Diagnostics"));
    tabs->addTab(kbTab, QStringLiteral("Knowledge Base"));
    tabs->addTab(promptLabTab, QStringLiteral("Prompt Lab"));
    tabs->addTab(m_backendSummary, QStringLiteral("Backend"));

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

    const auto setActionTip = [](QAction *action, const QString &tip) {
        if (action != nullptr) {
            action->setToolTip(tip);
            action->setStatusTip(tip);
        }
    };
    const auto setWidgetTip = [](QWidget *widget, const QString &tip) {
        if (widget != nullptr) {
            widget->setToolTip(tip);
            widget->setStatusTip(tip);
        }
    };

    setActionTip(newConversationAction, QStringLiteral("Create a fresh conversation and keep the current history saved."));
    setActionTip(m_configurationAction, QStringLiteral("Open Amelia's JSON configuration editor and save, cancel, or revert to factory defaults."));
    setActionTip(m_clearMemoriesAction, QStringLiteral("Remove all persisted memories saved by Amelia."));
    setActionTip(quitAction, QStringLiteral("Close Amelia Qt6."));
    setActionTip(m_aboutAmeliaAction, QStringLiteral("Show version and build details for Amelia Qt6."));
    setActionTip(m_aboutQtAction, QStringLiteral("Show the Qt version and licensing information."));

    setWidgetTip(m_conversationsList, QStringLiteral("Saved conversations. Select one to restore its transcript and summary."));
    setWidgetTip(m_newConversationButton, QStringLiteral("Start a new conversation without deleting older ones."));
    setWidgetTip(m_deleteConversationButton, QStringLiteral("Delete the selected saved conversation from local storage."));
    setWidgetTip(m_transcript, QStringLiteral("Formatted transcript view with clickable copy links for code blocks."));
    setWidgetTip(m_input, QStringLiteral("Write your prompt here. Shift+Enter adds a new line; send when ready."));
    setWidgetTip(m_prioritizedAssetsList, QStringLiteral("Knowledge Base assets currently prioritized for the next prompt or pinned across prompts."));
    setWidgetTip(m_removePrioritizedAssetButton, QStringLiteral("Remove the selected prioritized assets from the active retrieval list."));
    setWidgetTip(m_clearPrioritizedAssetsButton, QStringLiteral("Clear all one-shot and pinned Knowledge Base priorities."));
    setWidgetTip(m_externalSearchCheck, QStringLiteral("Allow Amelia to use sanitized external web search for the current prompt."));
    setWidgetTip(m_modelCombo, QStringLiteral("Choose which Ollama model Amelia should use for generation."));
    setWidgetTip(m_sendButton, QStringLiteral("Submit the current prompt to Amelia."));
    setWidgetTip(m_stopButton, QStringLiteral("Stop the current generation, indexing, or long-running task."));
    setWidgetTip(m_reindexButton, QStringLiteral("Refresh the indexed Knowledge Base and rebuild changed assets."));
    setWidgetTip(m_refreshModelsButton, QStringLiteral("Query Ollama and refresh the available local model list."));
    setWidgetTip(m_rememberButton, QStringLiteral("Save the current input as a manual memory for future grounded prompts."));
    setWidgetTip(m_copyLastAnswerButton, QStringLiteral("Copy the most recent full assistant answer to the clipboard."));
    setWidgetTip(m_importFilesButton, QStringLiteral("Import one or more files into the local Knowledge Base."));
    setWidgetTip(m_importFolderButton, QStringLiteral("Import an entire folder into the local Knowledge Base."));
    setWidgetTip(m_statusLabel, QStringLiteral("Current high-level status for prompt preparation, indexing, and generation."));
    setWidgetTip(m_reasoningTraceToggleButton, QStringLiteral("Toggle capture of backend reasoning traces when the model exposes them."));
    setWidgetTip(m_diagnostics, QStringLiteral("Operational diagnostics, backend events, and optional reasoning trace output."));
    setWidgetTip(m_promptLabPresetCombo, QStringLiteral("Choose a Prompt Lab recipe preset as a starting point."));
    setWidgetTip(m_promptLabGoal, QStringLiteral("Describe the task the recipe should optimize for."));
    setWidgetTip(m_promptLabAssets, QStringLiteral("Local files or folders to include or import for the task."));
    setWidgetTip(m_promptLabBrowseFilesButton, QStringLiteral("Pick one or more local files for the Prompt Lab recipe."));
    setWidgetTip(m_promptLabBrowseFolderButton, QStringLiteral("Pick a local folder for the Prompt Lab recipe."));
    setWidgetTip(m_promptLabKbAssets, QStringLiteral("Reference existing Knowledge Base assets by name, path, tag, or topic."));
    setWidgetTip(m_promptLabNotes, QStringLiteral("Add optional constraints, audience details, schemas, or output formatting notes."));
    setWidgetTip(m_promptLabGenerateButton, QStringLiteral("Generate a Prompt Lab recipe preview from the current fields."));
    setWidgetTip(m_promptLabUseButton, QStringLiteral("Copy the composed Prompt Lab recipe into the main input box."));
    setWidgetTip(m_promptLabCopyRecipeButton, QStringLiteral("Copy the composed Prompt Lab recipe to the clipboard."));
    setWidgetTip(m_promptLabImportButton, QStringLiteral("Import the Prompt Lab local assets directly into the Knowledge Base."));
    setWidgetTip(m_promptLabPreview, QStringLiteral("Preview of the generated Prompt Lab recipe and JSONL helper output."));
    setWidgetTip(m_backendSummary, QStringLiteral("Snapshot of active backend configuration, models, and runtime settings."));
    setWidgetTip(m_sourceInventory, QStringLiteral("Summary of indexed Knowledge Base sources and cache state."));
    setWidgetTip(m_sourceInventoryFilter, QStringLiteral("Filter Knowledge Base collections, folders, file names, original paths, or extractors."));
    setWidgetTip(m_sourceInventorySortCombo, QStringLiteral("Sort the Knowledge Base tree by file name or file type."));
    setWidgetTip(m_sourceInventoryTree, QStringLiteral("Knowledge Base tree grouped by collection label and preserved folder structure."));
    setWidgetTip(m_prioritizeSelectedAssetButton, QStringLiteral("Use the selected Knowledge Base assets for the next prompt only."));
    setWidgetTip(m_pinSelectedAssetButton, QStringLiteral("Keep the selected Knowledge Base assets pinned across prompts until cleared."));
    setWidgetTip(m_renameKnowledgeLabelButton, QStringLiteral("Rename the selected top-level Knowledge Base collection label."));
    setWidgetTip(m_removeSelectedAssetButton, QStringLiteral("Remove the selected assets from the Knowledge Base index and storage."));
    setWidgetTip(m_clearKnowledgeBaseButton, QStringLiteral("Remove all imported assets from the Knowledge Base."));
    setWidgetTip(m_outlinePlan, QStringLiteral("Outline-first planning and structured execution drafts."));
    setWidgetTip(m_localSources, QStringLiteral("Grounding excerpts and citations retrieved from local Knowledge Base assets."));
    setWidgetTip(m_externalSources, QStringLiteral("Sanitized snippets collected from external web search results."));
    setWidgetTip(m_memoriesView, QStringLiteral("Persisted memories Amelia can reuse across conversations."));
    setWidgetTip(m_sessionSummary, QStringLiteral("Rolling summary for the current conversation."));
    setWidgetTip(m_privacyPreview, QStringLiteral("Preview of what local and external context will be shared with the model."));
    setWidgetTip(m_taskProgressBar, QStringLiteral("Progress for prompt preparation, external search, indexing, and answer generation."));

    if (tabs->tabBar() != nullptr) {
        tabs->tabBar()->setTabToolTip(0, QStringLiteral("Diagnostics, backend events, and optional reasoning traces."));
        tabs->tabBar()->setTabToolTip(1, QStringLiteral("Indexed assets, filtering, prioritization, and Knowledge Base maintenance."));
        tabs->tabBar()->setTabToolTip(2, QStringLiteral("Recipe builder for structured prompts and asset-aware workflows."));
        tabs->tabBar()->setTabToolTip(3, QStringLiteral("Backend summary including models and runtime configuration."));
        tabs->tabBar()->setTabToolTip(4, QStringLiteral("Outline planning and structured execution drafts."));
        tabs->tabBar()->setTabToolTip(5, QStringLiteral("Local retrieval evidence used to ground answers."));
        tabs->tabBar()->setTabToolTip(6, QStringLiteral("External sanitized search evidence used to enrich answers."));
        tabs->tabBar()->setTabToolTip(7, QStringLiteral("Long-term memory entries saved by Amelia."));
        tabs->tabBar()->setTabToolTip(8, QStringLiteral("Conversation summary for the active session."));
        tabs->tabBar()->setTabToolTip(9, QStringLiteral("Privacy and data-sharing preview for prompt submission."));
    }

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
    connect(m_sourceInventorySortCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onKnowledgeBaseSortModeChanged);
    connect(m_prioritizeSelectedAssetButton, &QPushButton::clicked, this, &MainWindow::onPrioritizeSelectedKnowledgeAssetsClicked);
    connect(m_pinSelectedAssetButton, &QPushButton::clicked, this, &MainWindow::onPinSelectedKnowledgeAssetsClicked);
    connect(m_renameKnowledgeLabelButton, &QPushButton::clicked, this, &MainWindow::onRenameSelectedKnowledgeGroupClicked);
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

    QString currentRole;
    QStringList currentLines;

    const auto flushMessage = [this, &currentRole, &currentLines]() {
        if (currentRole.isEmpty()) {
            currentLines.clear();
            return;
        }

        QString message = currentLines.join(QStringLiteral("\n"));
        while (message.startsWith(QLatin1Char('\n'))) {
            message.remove(0, 1);
        }
        while (message.endsWith(QLatin1Char('\n'))) {
            message.chop(1);
        }
        if (message.trimmed().isEmpty()) {
            currentRole.clear();
            currentLines.clear();
            return;
        }

        if (currentRole == QStringLiteral("assistant")) {
            m_lastAssistantMessage = message;
        }
        appendTranscriptEntry(currentRole, message);
        currentRole.clear();
        currentLines.clear();
    };

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("USER> "))) {
            flushMessage();
            currentRole = QStringLiteral("user");
            currentLines << line.mid(6);
        } else if (line.startsWith(QStringLiteral("ASSISTANT> "))) {
            flushMessage();
            currentRole = QStringLiteral("assistant");
            currentLines << line.mid(11);
        } else if (line.startsWith(QStringLiteral("[system] "))) {
            flushMessage();
            currentRole = QStringLiteral("system");
            currentLines << line.mid(9);
        } else if (line.startsWith(QStringLiteral("[status] "))) {
            flushMessage();
            currentRole = QStringLiteral("status");
            currentLines << line.mid(9);
        } else if (!currentRole.isEmpty()) {
            currentLines << line;
        } else if (!line.trimmed().isEmpty()) {
            currentRole = QStringLiteral("system");
            currentLines << line;
        }
    }
    flushMessage();
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
    m_knowledgeDisplayNames.clear();

    if (m_sourceInventory != nullptr) {
        m_sourceInventory->clear();
    }

    if (m_sourceInventoryTree == nullptr) {
        return;
    }

    m_sourceInventoryTree->clear();
    if (text.trimmed().isEmpty() || text.trimmed() == QStringLiteral("<none>")) {
        updateKnowledgeBaseFilterStatus();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject()) {
        if (m_sourceInventory != nullptr) {
            m_sourceInventory->setPlainText(text);
        }
        updateKnowledgeBaseFilterStatus();
        return;
    }

    const QJsonObject root = doc.object();
    QStringList summary;
    summary << QStringLiteral("Knowledge root: %1").arg(root.value(QStringLiteral("knowledgeRoot")).toString(QStringLiteral("<unknown>")));
    summary << QStringLiteral("Collections root: %1").arg(root.value(QStringLiteral("collectionsRoot")).toString(QStringLiteral("<unknown>")));
    summary << QStringLiteral("Workspace jail root: %1").arg(root.value(QStringLiteral("workspaceJailRoot")).toString(QStringLiteral("<unknown>")));
    summary << QStringLiteral("Sources: %1 | Chunks: %2 | Semantic retrieval: %3")
                  .arg(root.value(QStringLiteral("sources")).toInt())
                  .arg(root.value(QStringLiteral("chunks")).toInt())
                  .arg(root.value(QStringLiteral("semanticEnabled")).toBool() ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    if (m_sourceInventory != nullptr) {
        m_sourceInventory->setPlainText(summary.join(QStringLiteral("\n")));
    }

    const QJsonArray collections = root.value(QStringLiteral("collections")).toArray();
    QStringList availablePaths;
    for (const QJsonValue &collectionValue : collections) {
        if (!collectionValue.isObject()) {
            continue;
        }

        const QJsonObject collection = collectionValue.toObject();
        auto *collectionItem = new QTreeWidgetItem(m_sourceInventoryTree);
        collectionItem->setText(0, collection.value(QStringLiteral("label")).toString(QStringLiteral("Imported collection")));
        collectionItem->setText(1, QStringLiteral("Collection"));
        collectionItem->setText(2, QStringLiteral("%1 file(s)").arg(collection.value(QStringLiteral("fileCount")).toInt()));
        collectionItem->setData(0, kKnowledgeNodeTypeRole, QStringLiteral("collection"));
        collectionItem->setData(0, kKnowledgeCollectionIdRole, collection.value(QStringLiteral("collectionId")).toString());
        collectionItem->setData(0, kKnowledgeSearchBlobRole,
                                collection.value(QStringLiteral("label")).toString()
                                    + QLatin1Char('\n')
                                    + collection.value(QStringLiteral("collectionId")).toString());
        collectionItem->setExpanded(true);

        const QJsonArray groups = collection.value(QStringLiteral("groups")).toArray();
        for (const QJsonValue &groupValue : groups) {
            if (!groupValue.isObject()) {
                continue;
            }

            const QJsonObject group = groupValue.toObject();
            auto *groupItem = new QTreeWidgetItem(collectionItem);
            groupItem->setText(0, group.value(QStringLiteral("label")).toString(QStringLiteral("(root)")));
            groupItem->setText(1, QStringLiteral("Folder"));
            groupItem->setText(2, QStringLiteral("%1 file(s)").arg(group.value(QStringLiteral("fileCount")).toInt()));
            groupItem->setData(0, kKnowledgeNodeTypeRole, QStringLiteral("group"));
            groupItem->setData(0, kKnowledgeCollectionIdRole, collection.value(QStringLiteral("collectionId")).toString());
            groupItem->setData(0, kKnowledgeSearchBlobRole,
                               collection.value(QStringLiteral("label")).toString()
                                   + QLatin1Char('\n')
                                   + group.value(QStringLiteral("label")).toString());
            groupItem->setExpanded(true);

            const QJsonArray files = group.value(QStringLiteral("files")).toArray();
            for (const QJsonValue &fileValue : files) {
                if (!fileValue.isObject()) {
                    continue;
                }

                const QJsonObject file = fileValue.toObject();
                const QString filePath = file.value(QStringLiteral("filePath")).toString().trimmed();
                const QString fileName = file.value(QStringLiteral("fileName")).toString().trimmed();
                const QString relativePath = file.value(QStringLiteral("relativePath")).toString().trimmed();
                const QString originalPath = file.value(QStringLiteral("originalPath")).toString().trimmed();
                const QString sourceType = file.value(QStringLiteral("sourceType")).toString().trimmed();
                const QString sourceRole = file.value(QStringLiteral("sourceRole")).toString().trimmed();
                const QString extractor = file.value(QStringLiteral("extractor")).toString().trimmed();
                const QString extension = file.value(QStringLiteral("extension")).toString().trimmed();
                const int chunkCount = file.value(QStringLiteral("chunkCount")).toInt();

                auto *fileItem = new QTreeWidgetItem(groupItem);
                fileItem->setText(0, fileName.isEmpty() ? relativePath : fileName);
                fileItem->setText(1, extension.isEmpty() ? sourceType : extension);
                fileItem->setText(2, relativePath);
                fileItem->setData(0, kKnowledgeNodeTypeRole, QStringLiteral("file"));
                fileItem->setData(0, kKnowledgePathRole, filePath);
                fileItem->setData(0, kKnowledgeCollectionIdRole, collection.value(QStringLiteral("collectionId")).toString());
                fileItem->setData(0, kKnowledgeSearchBlobRole,
                                  collection.value(QStringLiteral("label")).toString()
                                      + QLatin1Char('\n')
                                      + group.value(QStringLiteral("label")).toString()
                                      + QLatin1Char('\n')
                                      + fileName
                                      + QLatin1Char('\n')
                                      + relativePath
                                      + QLatin1Char('\n')
                                      + originalPath
                                      + QLatin1Char('\n')
                                      + extractor
                                      + QLatin1Char('\n')
                                      + sourceRole
                                      + QLatin1Char('\n')
                                      + sourceType);

                QStringList details;
                details << QStringLiteral("Collection: %1").arg(collection.value(QStringLiteral("label")).toString());
                details << QStringLiteral("Folder: %1").arg(group.value(QStringLiteral("label")).toString());
                details << QStringLiteral("Relative path: %1").arg(relativePath.isEmpty() ? QStringLiteral("<none>") : relativePath);
                details << QStringLiteral("Internal path: %1").arg(filePath.isEmpty() ? QStringLiteral("<none>") : filePath);
                details << QStringLiteral("Original path: %1").arg(originalPath.isEmpty() ? QStringLiteral("<unknown>") : originalPath);
                details << QStringLiteral("Type / role: %1 / %2").arg(sourceType.isEmpty() ? QStringLiteral("<unknown>") : sourceType,
                                                                      sourceRole.isEmpty() ? QStringLiteral("<unknown>") : sourceRole);
                details << QStringLiteral("Extractor: %1").arg(extractor.isEmpty() ? QStringLiteral("<unknown>") : extractor);
                details << QStringLiteral("Chunks: %1").arg(chunkCount);
                fileItem->setToolTip(0, details.join(QStringLiteral("\n")));
                fileItem->setToolTip(1, details.join(QStringLiteral("\n")));
                fileItem->setToolTip(2, details.join(QStringLiteral("\n")));

                if (!filePath.isEmpty()) {
                    availablePaths << filePath;
                    const QString displayPath = QStringLiteral("%1 / %2")
                                                    .arg(collection.value(QStringLiteral("label")).toString(),
                                                         relativePath.isEmpty() ? fileName : relativePath);
                    m_knowledgeDisplayNames.insert(filePath, displayPath);
                }
            }
        }
    }

    if (m_sourceInventoryTree->columnCount() >= 3) {
        m_sourceInventoryTree->resizeColumnToContents(0);
        m_sourceInventoryTree->resizeColumnToContents(1);
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
    onKnowledgeBaseSortModeChanged(m_sourceInventorySortCombo != nullptr ? m_sourceInventorySortCombo->currentIndex() : 0);
    applyKnowledgeBaseFilter();
}

void MainWindow::applyKnowledgeBaseFilter()
{
    if (m_sourceInventoryTree == nullptr) {
        updateKnowledgeBaseFilterStatus();
        return;
    }

    const QString needle = m_sourceInventoryFilter != nullptr ? m_sourceInventoryFilter->text().trimmed() : QString();

    std::function<bool(QTreeWidgetItem *)> applyRecursive = [&](QTreeWidgetItem *item) -> bool {
        if (item == nullptr) {
            return false;
        }

        bool selfMatches = needle.isEmpty();
        if (!needle.isEmpty()) {
            const QStringList candidates = {
                item->text(0),
                item->text(1),
                item->text(2),
                item->data(0, kKnowledgePathRole).toString(),
                item->data(0, kKnowledgeSearchBlobRole).toString(),
                item->toolTip(0)
            };
            for (const QString &candidate : candidates) {
                if (candidate.contains(needle, Qt::CaseInsensitive)) {
                    selfMatches = true;
                    break;
                }
            }
        }

        bool childMatches = false;
        for (int i = 0; i < item->childCount(); ++i) {
            childMatches = applyRecursive(item->child(i)) || childMatches;
        }

        const bool visible = selfMatches || childMatches;
        item->setHidden(!visible);
        if (!visible) {
            item->setSelected(false);
        }
        return visible;
    };

    for (int i = 0; i < m_sourceInventoryTree->topLevelItemCount(); ++i) {
        applyRecursive(m_sourceInventoryTree->topLevelItem(i));
    }

    updateKnowledgeBaseFilterStatus();
}

void MainWindow::updateKnowledgeBaseFilterStatus()
{
    if (m_sourceInventoryFilterStatus == nullptr || m_sourceInventoryTree == nullptr) {
        return;
    }

    int total = 0;
    int visible = 0;

    std::function<void(QTreeWidgetItem *)> countRecursive = [&](QTreeWidgetItem *item) {
        if (item == nullptr) {
            return;
        }

        if (item->data(0, kKnowledgeNodeTypeRole).toString() == QStringLiteral("file")) {
            ++total;
            if (!item->isHidden()) {
                ++visible;
            }
        }

        for (int i = 0; i < item->childCount(); ++i) {
            countRecursive(item->child(i));
        }
    };

    for (int i = 0; i < m_sourceInventoryTree->topLevelItemCount(); ++i) {
        countRecursive(m_sourceInventoryTree->topLevelItem(i));
    }

    m_sourceInventoryFilterStatus->setText(QStringLiteral("%1 / %2 shown").arg(visible).arg(total));
}

QStringList MainWindow::collectKnowledgePathsFromItem(const QTreeWidgetItem *item) const
{
    QStringList paths;
    if (item == nullptr) {
        return paths;
    }

    const QString nodeType = item->data(0, kKnowledgeNodeTypeRole).toString();
    if (nodeType == QStringLiteral("file")) {
        const QString path = item->data(0, kKnowledgePathRole).toString().trimmed();
        if (!path.isEmpty()) {
            paths << path;
        }
        return paths;
    }

    for (int i = 0; i < item->childCount(); ++i) {
        const QStringList childPaths = collectKnowledgePathsFromItem(item->child(i));
        for (const QString &path : childPaths) {
            if (!paths.contains(path)) {
                paths << path;
            }
        }
    }
    return paths;
}

QTreeWidgetItem *MainWindow::collectionItemForKnowledgeItem(QTreeWidgetItem *item) const
{
    if (item == nullptr) {
        return nullptr;
    }

    QTreeWidgetItem *current = item;
    while (current->parent() != nullptr) {
        current = current->parent();
    }
    return current;
}

QString MainWindow::displayLabelForKnowledgePath(const QString &path) const
{
    const QString display = m_knowledgeDisplayNames.value(path);
    if (!display.trimmed().isEmpty()) {
        return display;
    }
    return QFileInfo(path).fileName();
}

QString MainWindow::promptForKnowledgeLabel(const QString &title, const QString &suggestedLabel) const
{
    bool ok = false;
    const QString label = QInputDialog::getText(const_cast<MainWindow *>(this),
                                                title,
                                                QStringLiteral("Knowledge Base label:"),
                                                QLineEdit::Normal,
                                                suggestedLabel,
                                                &ok).trimmed();
    if (!ok) {
        return QString();
    }
    return label;
}

QStringList MainWindow::selectedKnowledgeAssetPaths() const
{
    QStringList paths;
    if (m_sourceInventoryTree == nullptr) {
        return paths;
    }

    const QList<QTreeWidgetItem *> items = m_sourceInventoryTree->selectedItems();
    for (QTreeWidgetItem *item : items) {
        const QStringList itemPaths = collectKnowledgePathsFromItem(item);
        for (const QString &path : itemPaths) {
            if (!paths.contains(path)) {
                paths << path;
            }
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
                                                  displayLabelForKnowledgePath(path)),
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

void MainWindow::onKnowledgeBaseSortModeChanged(int index)
{
    if (m_sourceInventoryTree == nullptr) {
        return;
    }

    const int column = index == 1 ? 1 : 0;
    m_sourceInventoryTree->sortItems(column, Qt::AscendingOrder);
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
    if (m_sourceInventoryTree == nullptr) {
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

void MainWindow::onRenameSelectedKnowledgeGroupClicked()
{
    if (m_sourceInventoryTree == nullptr) {
        return;
    }

    QTreeWidgetItem *selectedItem = m_sourceInventoryTree->currentItem();
    if (selectedItem == nullptr) {
        const QList<QTreeWidgetItem *> items = m_sourceInventoryTree->selectedItems();
        if (!items.isEmpty()) {
            selectedItem = items.constFirst();
        }
    }

    if (selectedItem == nullptr) {
        QMessageBox::information(this,
                                 QStringLiteral("Knowledge Base"),
                                 QStringLiteral("Select a Knowledge Base collection first."));
        return;
    }

    QTreeWidgetItem *collectionItem = collectionItemForKnowledgeItem(selectedItem);
    if (collectionItem == nullptr) {
        return;
    }

    const QString collectionId = collectionItem->data(0, kKnowledgeCollectionIdRole).toString().trimmed();
    const QString currentLabel = collectionItem->text(0).trimmed();
    if (collectionId.isEmpty()) {
        QMessageBox::information(this,
                                 QStringLiteral("Knowledge Base"),
                                 QStringLiteral("This collection cannot be renamed."));
        return;
    }

    const QString newLabel = promptForKnowledgeLabel(QStringLiteral("Rename Knowledge Base label"), currentLabel);
    if (newLabel.isEmpty() || newLabel == currentLabel) {
        return;
    }

    emit renameKnowledgeCollectionRequested(collectionId, newLabel);
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
        QString rawIndex = url.host();
        if (rawIndex.isEmpty()) {
            rawIndex = url.path();
        }
        if (rawIndex.startsWith(QLatin1Char('/'))) {
            rawIndex.remove(0, 1);
        }

        bool ok = false;
        const int index = rawIndex.toInt(&ok);
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
        uniqueModels << (currentModel.trimmed().isEmpty() ? QStringLiteral("qwen2.5:7b") : currentModel.trimmed());
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
    if (paths.isEmpty()) {
        return;
    }

    QString suggestedLabel = QFileInfo(paths.constFirst()).completeBaseName().trimmed();
    if (suggestedLabel.isEmpty()) {
        suggestedLabel = QFileInfo(paths.constFirst()).fileName().trimmed();
    }
    if (paths.size() > 1) {
        suggestedLabel = QStringLiteral("Imported files");
    }

    const QString label = promptForKnowledgeLabel(QStringLiteral("Import files into Amelia knowledge base"), suggestedLabel);
    if (label.isEmpty()) {
        return;
    }

    emit importPathsRequested(paths, label);
}

void MainWindow::onImportFolderClicked()
{
    const QString folder = QFileDialog::getExistingDirectory(this,
                                                             QStringLiteral("Import folder into Amelia knowledge base"));
    if (folder.isEmpty()) {
        return;
    }

    QString suggestedLabel = QFileInfo(folder).fileName().trimmed();
    if (suggestedLabel.isEmpty()) {
        suggestedLabel = QStringLiteral("Imported folder");
    }

    const QString label = promptForKnowledgeLabel(QStringLiteral("Import folder into Amelia knowledge base"), suggestedLabel);
    if (label.isEmpty()) {
        return;
    }

    emit importPathsRequested(QStringList() << folder, label);
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

    QString suggestedLabel = m_promptLabGoal != nullptr ? m_promptLabGoal->text().trimmed() : QString();
    if (suggestedLabel.isEmpty()) {
        suggestedLabel = QFileInfo(localPaths.constFirst()).completeBaseName().trimmed();
    }
    if (suggestedLabel.isEmpty()) {
        suggestedLabel = QStringLiteral("Prompt Lab assets");
    }

    const QString label = promptForKnowledgeLabel(QStringLiteral("Import Prompt Lab assets into Amelia knowledge base"), suggestedLabel);
    if (label.isEmpty()) {
        return;
    }

    emit importPathsRequested(localPaths, label);
    m_statusLabel->setText(QStringLiteral("Prompt Lab requested local asset import for %1 path(s) into '%2'.%3")
                           .arg(localPaths.size())
                           .arg(label)
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
                       QStringLiteral("<b>aMELia Qt6 v%1</b><br><br>A local offline coding tool and assistant built with C++ and Qt6, allegorically considered a <b>MEL</b>: <b>Model Enhancement Lab</b>.<br><br>This build includes copy-friendly colored transcript/diagnostic views, fenced-code formatting, last-answer/code copy helpers, local Ollama inference, persistent knowledge, a first-run connectivity setup, a full JSON configuration editor, richer Prompt Lab KB-aware asset helpers, prompt budgeting, outline-first document generation, asynchronous PDF indexing, and operational diagnostics.")
                           .arg(QLatin1StringView(AmeliaVersion::kDisplayVersion)));
}

bool MainWindow::saveConfigurationJson(const QString &text, QString *errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Configuration JSON is invalid: %1").arg(parseError.errorString());
        }
        return false;
    }

    QDir dir;
    const QFileInfo info(m_configPath);
    if (!dir.exists(info.path()) && !dir.mkpath(info.path())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to create config directory: %1").arg(QDir::toNativeSeparators(info.path()));
        }
        return false;
    }

    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to open config file for writing: %1").arg(QDir::toNativeSeparators(m_configPath));
        }
        return false;
    }

    if (file.write(doc.toJson(QJsonDocument::Indented)) < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to write config file: %1").arg(QDir::toNativeSeparators(m_configPath));
        }
        return false;
    }

    file.close();
    return true;
}

void MainWindow::onOpenConfigurationDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Amelia configuration"));
    dialog.resize(820, 640);

    auto *layout = new QVBoxLayout(&dialog);
    auto *infoLabel = new QLabel(
        QStringLiteral("Edit Amelia's full JSON configuration below. Save writes the file immediately; changes take effect on the next application start.<br><br><b>Config path:</b> %1")
            .arg(QDir::toNativeSeparators(m_configPath)),
        &dialog);
    infoLabel->setWordWrap(true);

    auto *editor = new QPlainTextEdit(&dialog);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setPlaceholderText(QStringLiteral("Amelia configuration JSON..."));
    QFont editorFont(QStringLiteral("Monospace"));
    editorFont.setStyleHint(QFont::TypeWriter);
    editor->setFont(editorFont);

    QFile file(m_configPath);
    QString initialText = m_defaultConfigJson;
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        initialText = QString::fromUtf8(file.readAll());
        file.close();
    }
    editor->setPlainText(initialText.trimmed().isEmpty() ? m_defaultConfigJson : initialText);

    auto *buttons = new QHBoxLayout();
    auto *revertButton = new QPushButton(QStringLiteral("Revert to Defaults"), &dialog);
    auto *cancelButton = new QPushButton(QStringLiteral("Cancel"), &dialog);
    auto *saveButton = new QPushButton(QStringLiteral("Save and Close"), &dialog);
    buttons->addWidget(revertButton);
    buttons->addStretch(1);
    buttons->addWidget(cancelButton);
    buttons->addWidget(saveButton);

    layout->addWidget(infoLabel);
    layout->addWidget(editor, 1);
    layout->addLayout(buttons);

    connect(revertButton, &QPushButton::clicked, &dialog, [this, editor]() {
        editor->setPlainText(m_defaultConfigJson);
    });
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, &dialog, [this, &dialog, editor]() {
        QString errorMessage;
        if (!saveConfigurationJson(editor->toPlainText(), &errorMessage)) {
            QMessageBox::warning(&dialog, QStringLiteral("Configuration"), errorMessage);
            return;
        }
        QMessageBox::information(&dialog,
                                 QStringLiteral("Configuration"),
                                 QStringLiteral("Configuration saved to %1. Restart Amelia to apply the updated settings.")
                                     .arg(QDir::toNativeSeparators(m_configPath)));
        dialog.accept();
    });

    dialog.exec();
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
