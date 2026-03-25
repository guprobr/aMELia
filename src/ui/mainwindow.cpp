#include "ui/mainwindow.h"
#include "core/appconfig.h"
#include "core/appversion.h"
#include "core/transcriptformatter.h"

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
#include <QEvent>
#include <QDateTime>
#include <QDragMoveEvent>
#include <QDialogButtonBox>
#include <QDialog>
#include <QDir>
#include <QComboBox>
#include <QFileDialog>
#include <QDropEvent>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHeaderView>
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
#include <QMimeData>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPushButton>
#include <QCloseEvent>
#include <QProgressBar>
#include <QStatusBar>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QSplitter>
#include <QStackedLayout>
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
QColor blendColors(const QColor &base, const QColor &accent, qreal accentRatio)
{
    const qreal ratio = qBound<qreal>(0.0, accentRatio, 1.0);
    const qreal inverse = 1.0 - ratio;
    return QColor::fromRgbF(base.redF() * inverse + accent.redF() * ratio,
                            base.greenF() * inverse + accent.greenF() * ratio,
                            base.blueF() * inverse + accent.blueF() * ratio,
                            base.alphaF() * inverse + accent.alphaF() * ratio);
}

QString cssColor(const QColor &color)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(QString::number(color.alphaF(), 'f', 3));
}

QColor transcriptPrefixColor(const QPalette &palette, const QString &role)
{
    const QString lower = role.toLower();
    const QColor highlight = palette.color(QPalette::Highlight);
    const QColor link = palette.color(QPalette::Link);
    const QColor text = palette.color(QPalette::Text);
    const QColor base = palette.color(QPalette::Base);

    if (lower == QStringLiteral("user")) {
        return link.isValid() ? link : highlight;
    }
    if (lower == QStringLiteral("assistant")) {
        return blendColors(highlight, text, 0.20);
    }
    if (lower == QStringLiteral("system")) {
        return blendColors(text, highlight, 0.42);
    }
    if (lower == QStringLiteral("status")) {
        return blendColors(base, highlight, 0.72);
    }
    return blendColors(text, highlight, 0.18);
}

QColor transcriptBodyColor(const QPalette &palette, const QString &role)
{
    const QString lower = role.toLower();
    const QColor text = palette.color(QPalette::Text);
    if (lower == QStringLiteral("system")) {
        return blendColors(text, palette.color(QPalette::Highlight), 0.18);
    }
    return text;
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

QColor diagnosticCategoryColor(const QPalette &palette, const QString &category)
{
    const QString lower = category.toLower();
    const QColor highlight = palette.color(QPalette::Highlight);
    const QColor link = palette.color(QPalette::Link);
    const QColor text = palette.color(QPalette::Text);

    if (lower == QStringLiteral("backend")) {
        return blendColors(link, highlight, 0.30);
    }
    if (lower == QStringLiteral("search")) {
        return blendColors(highlight, QColor(Qt::green), 0.35);
    }
    if (lower == QStringLiteral("rag")) {
        return blendColors(highlight, QColor(Qt::cyan), 0.28);
    }
    if (lower == QStringLiteral("memory")) {
        return blendColors(highlight, QColor(255, 140, 0), 0.35);
    }
    if (lower == QStringLiteral("planner")) {
        return blendColors(highlight, QColor(148, 0, 211), 0.28);
    }
    if (lower == QStringLiteral("guardrail")) {
        return blendColors(highlight, QColor(Qt::red), 0.42);
    }
    if (lower == QStringLiteral("ingest")) {
        return blendColors(highlight, QColor(Qt::yellow), 0.38);
    }
    if (lower == QStringLiteral("startup")) {
        return blendColors(highlight, QColor(255, 105, 180), 0.28);
    }
    if (lower == QStringLiteral("budget")) {
        return blendColors(link, QColor(Qt::cyan), 0.18);
    }
    if (lower == QStringLiteral("chat")) {
        return blendColors(text, highlight, 0.35);
    }
    if (lower == QStringLiteral("reasoning")) {
        return blendColors(text, QColor(148, 0, 211), 0.30);
    }
    return blendColors(text, highlight, 0.18);
}

constexpr int kKnowledgeNodeTypeRole = Qt::UserRole + 20;
constexpr int kKnowledgePathRole = Qt::UserRole + 21;
constexpr int kKnowledgeCollectionIdRole = Qt::UserRole + 22;
constexpr int kKnowledgeSearchBlobRole = Qt::UserRole + 23;
constexpr int kKnowledgeGroupLabelRole = Qt::UserRole + 24;
constexpr int kKnowledgePropertiesRole = Qt::UserRole + 25;
constexpr char kKnowledgeMoveMimeType[] = "application/x-amelia-kb-paths";

class KnowledgeTreeWidget final : public QTreeWidget {
    Q_OBJECT
public:
    explicit KnowledgeTreeWidget(QWidget *parent = nullptr)
        : QTreeWidget(parent)
    {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::CopyAction);
        setDragDropOverwriteMode(false);
    }

signals:
    void knowledgeAssetsDropped(const QStringList &paths,
                                const QString &targetCollectionId,
                                const QString &targetGroupLabel);

protected:
    QStringList mimeTypes() const override
    {
        return {QString::fromLatin1(kKnowledgeMoveMimeType)};
    }

    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
    {
        QStringList paths;
        QSet<QString> seen;

        std::function<void(const QTreeWidgetItem *)> collectRecursive = [&](const QTreeWidgetItem *item) {
            if (item == nullptr) {
                return;
            }

            const QString directPath = item->data(0, kKnowledgePathRole).toString().trimmed();
            if (!directPath.isEmpty()) {
                if (!seen.contains(directPath)) {
                    seen.insert(directPath);
                    paths << directPath;
                }
                return;
            }

            for (int i = 0; i < item->childCount(); ++i) {
                collectRecursive(item->child(i));
            }
        };

        for (const QTreeWidgetItem *item : items) {
            collectRecursive(item);
        }

        if (paths.isEmpty()) {
            return nullptr;
        }

        auto *mimeData = new QMimeData();
        mimeData->setData(QString::fromLatin1(kKnowledgeMoveMimeType), paths.join(QLatin1Char('\n')).toUtf8());
        return mimeData;
    }

    Qt::DropActions supportedDropActions() const override
    {
        return Qt::CopyAction;
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (resolveDropTarget(event->position().toPoint()).first.trimmed().isEmpty()) {
            event->ignore();
            return;
        }
        if (!event->mimeData()->hasFormat(QString::fromLatin1(kKnowledgeMoveMimeType))) {
            event->ignore();
            return;
        }
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }

    void dropEvent(QDropEvent *event) override
    {
        if (!event->mimeData()->hasFormat(QString::fromLatin1(kKnowledgeMoveMimeType))) {
            event->ignore();
            return;
        }

        const auto target = resolveDropTarget(event->position().toPoint());
        const QString targetCollectionId = target.first.trimmed();
        if (targetCollectionId.isEmpty()) {
            event->ignore();
            return;
        }

        const QString payload = QString::fromUtf8(event->mimeData()->data(QString::fromLatin1(kKnowledgeMoveMimeType)));
        QStringList paths;
        QSet<QString> seen;
        for (const QString &line : payload.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty() && !seen.contains(trimmed)) {
                seen.insert(trimmed);
                paths << trimmed;
            }
        }
        if (paths.isEmpty()) {
            event->ignore();
            return;
        }

        emit knowledgeAssetsDropped(paths, targetCollectionId, target.second);
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }

private:
    QPair<QString, QString> resolveDropTarget(const QPoint &pos) const
    {
        QTreeWidgetItem *targetItem = itemAt(pos);
        if (targetItem == nullptr) {
            return {};
        }

        QString nodeType = targetItem->data(0, kKnowledgeNodeTypeRole).toString().trimmed();
        if (nodeType == QStringLiteral("file")) {
            targetItem = targetItem->parent();
            if (targetItem == nullptr) {
                return {};
            }
            nodeType = targetItem->data(0, kKnowledgeNodeTypeRole).toString().trimmed();
        }

        const QString collectionId = targetItem->data(0, kKnowledgeCollectionIdRole).toString().trimmed();
        if (collectionId.isEmpty()) {
            return {};
        }

        QString targetGroupLabel;
        if (nodeType == QStringLiteral("group")) {
            targetGroupLabel = targetItem->data(0, kKnowledgeGroupLabelRole).toString().trimmed();
        }
        return {collectionId, targetGroupLabel};
    }
};

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

QString decodeMarkdownCellText(QString cell)
{
    cell.replace(QRegularExpression(QStringLiteral(R"((?i)<br\s*/?>)")), QStringLiteral("\n"));
    cell.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    return cell.trimmed();
}

int countMarkdownFenceDelimiters(const QString &text)
{
    int count = 0;
    int offset = 0;
    while ((offset = text.indexOf(QStringLiteral("```"), offset)) >= 0) {
        ++count;
        offset += 3;
    }
    return count;
}

QStringList splitMarkdownTableRow(const QString &line)
{
    QString working = line.trimmed();
    if (working.startsWith(QLatin1Char('|'))) {
        working.remove(0, 1);
    }
    if (working.endsWith(QLatin1Char('|'))) {
        working.chop(1);
    }

    QStringList cells;
    QString current;
    bool escaped = false;
    bool inFence = false;
    for (int i = 0; i < working.size(); ++i) {
        const QChar ch = working.at(i);
        if (escaped) {
            current += ch;
            escaped = false;
            continue;
        }
        if (!inFence && ch == QLatin1Char('\\')) {
            current += ch;
            escaped = true;
            continue;
        }
        if (i + 2 < working.size() && working.mid(i, 3) == QStringLiteral("```")) {
            current += QStringLiteral("```");
            inFence = !inFence;
            i += 2;
            continue;
        }
        if (!inFence && ch == QLatin1Char('|')) {
            cells.push_back(current.trimmed());
            current.clear();
            continue;
        }
        current += ch;
    }
    cells.push_back(current.trimmed());
    return cells;
}

bool isMarkdownTableLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(QLatin1Char('|')) && trimmed.count(QLatin1Char('|')) >= 2;
}

bool isMarkdownTableSeparatorLine(const QString &line)
{
    const QStringList cells = splitMarkdownTableRow(line);
    if (cells.isEmpty()) {
        return false;
    }
    for (const QString &cell : cells) {
        if (cell.isEmpty()) {
            return false;
        }
        for (const QChar ch : cell) {
            if (ch != QLatin1Char('-') && ch != QLatin1Char(':') && !ch.isSpace()) {
                return false;
            }
        }
    }
    return true;
}

QStringList collectLogicalMarkdownTableBlock(const QStringList &lines, int start, int *end)
{
    QStringList rows;
    if (start < 0 || start + 1 >= lines.size()) {
        if (end != nullptr) {
            *end = qMax(0, start);
        }
        return rows;
    }

    rows << lines.at(start);
    rows << lines.at(start + 1);

    int i = start + 2;
    while (i < lines.size()) {
        if (!isMarkdownTableLine(lines.at(i))) {
            break;
        }

        QString row = lines.at(i);
        bool inFence = (countMarkdownFenceDelimiters(row) % 2) != 0;
        while (i + 1 < lines.size()) {
            const QString next = lines.at(i + 1);
            if (!inFence) {
                break;
            }

            row += QLatin1Char('\n');
            row += next;
            ++i;
            if ((countMarkdownFenceDelimiters(next) % 2) != 0) {
                inFence = !inFence;
            }
        }

        rows << row;
        ++i;
    }

    if (end != nullptr) {
        *end = i;
    }
    return rows;
}

bool markdownTableBlockNeedsRewrite(const QStringList &rows)
{
    static const QRegularExpression brPattern(QStringLiteral(R"((?i)<br\s*/?>)"));
    for (const QString &row : rows) {
        if (row.contains(QStringLiteral("```")) || row.contains(brPattern)
                || row.contains(QStringLiteral("<pre"), Qt::CaseInsensitive)
                || row.contains(QStringLiteral("</pre"), Qt::CaseInsensitive)
                || row.contains(QStringLiteral("<code"), Qt::CaseInsensitive)
                || row.contains(QStringLiteral("</code"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QString rewriteUnsafeMarkdownTables(const QString &markdown)
{
    const QStringList lines = markdown.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    QStringList output;
    int i = 0;
    while (i < lines.size()) {
        if (i + 1 >= lines.size() || !isMarkdownTableLine(lines.at(i)) || !isMarkdownTableSeparatorLine(lines.at(i + 1))) {
            output << lines.at(i);
            ++i;
            continue;
        }

        int end = i;
        const QStringList rows = collectLogicalMarkdownTableBlock(lines, i, &end);
        if (rows.size() < 3 || !markdownTableBlockNeedsRewrite(rows)) {
            for (int j = i; j < end; ++j) {
                output << lines.at(j);
            }
            i = end;
            continue;
        }

        const QStringList headers = splitMarkdownTableRow(rows.at(0));
        int rowNumber = 0;
        for (int row = 2; row < rows.size(); ++row) {
            const QStringList cells = splitMarkdownTableRow(rows.at(row));
            if (cells.isEmpty()) {
                continue;
            }

            ++rowNumber;
            QString title;
            const int titleIndex = headers.indexOf(QStringLiteral("Concept"));
            if (titleIndex >= 0 && titleIndex < cells.size()) {
                title = decodeMarkdownCellText(cells.at(titleIndex));
            }
            if (title.isEmpty()) {
                title = QStringLiteral("Item %1").arg(rowNumber);
            }

            output << QStringLiteral("### %1").arg(title);
            output << QString();
            for (int col = 0; col < headers.size() && col < cells.size(); ++col) {
                const QString header = headers.at(col).trimmed();
                if (header.isEmpty()) {
                    continue;
                }
                const QString value = decodeMarkdownCellText(cells.at(col));
                if (value.isEmpty()) {
                    continue;
                }
                if (value.contains(QStringLiteral("```"))) {
                    const int fence = value.indexOf(QStringLiteral("```"));
                    const QString beforeFence = value.left(fence).trimmed();
                    const QString afterFence = value.mid(fence).trimmed();
                    if (!beforeFence.isEmpty()) {
                        output << QStringLiteral("**%1:** %2").arg(header, beforeFence);
                        output << QString();
                    } else {
                        output << QStringLiteral("**%1**").arg(header);
                        output << QString();
                    }
                    output << afterFence;
                    output << QString();
                } else if (value.contains(QLatin1Char('\n'))) {
                    output << QStringLiteral("**%1:**").arg(header);
                    output << value;
                    output << QString();
                } else {
                    output << QStringLiteral("**%1:** %2").arg(header, value);
                    output << QString();
                }
            }
            if (row + 1 < rows.size()) {
                output << QStringLiteral("---");
                output << QString();
            }
        }

        i = end;
    }

    return output.join(QLatin1Char('\n'));
}

QString normalizeRenderableMarkdown(const QString &text)
{
    return TranscriptFormatter::sanitizeRenderableMarkdown(text);
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
            const int fencePos = line.indexOf(QStringLiteral("```"));
            if (fencePos >= 0) {
                const QString beforeFence = line.left(fencePos);
                if (!beforeFence.isEmpty()) {
                    plainBuffer += beforeFence;
                }
                flushPlain();
                currentLanguage = line.mid(fencePos + 3).trimmed();
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
    const QString normalizedText = normalizeRenderableMarkdown(text);
    const QVector<TranscriptSegment> segments = splitTranscriptSegments(normalizedText);
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

QString markdownFragmentToHtml(const QString &markdown, const QPalette &palette)
{
    const QString trimmed = normalizeRenderableMarkdown(markdown).trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    QTextDocument doc;
    doc.setDocumentMargin(0.0);
    doc.setMarkdown(escapeHtmlLikeTags(trimmed));
    QString html = decodeDoubleEscapedHtmlEntities(bodyFragmentFromDocument(doc));

    const QColor base = palette.color(QPalette::Base);
    const QColor textColor = palette.color(QPalette::Text);
    const QColor border = blendColors(palette.color(QPalette::Mid), textColor, 0.10);
    const QColor codeBackground = blendColors(base, palette.color(QPalette::Window), 0.35);
    const QColor inlineCodeBackground = blendColors(base, palette.color(QPalette::Window), 0.20);
    const QColor blockQuoteBackground = blendColors(base, palette.color(QPalette::Highlight), 0.10);
    const QColor tableHeaderBackground = blendColors(palette.color(QPalette::Button), palette.color(QPalette::Highlight), 0.12);
    const QColor linkColor = palette.color(QPalette::Link);

    html.replace(QStringLiteral("<pre"), QStringLiteral("<pre style=\"background:%1;color:%2;padding:12px;border-radius:10px;border:1px solid %3;overflow:auto;\"").arg(cssColor(codeBackground), cssColor(textColor), cssColor(border)));
    html.replace(QStringLiteral("<code"), QStringLiteral("<code style=\"background:%1;color:%2;padding:2px 5px;border-radius:4px;\"").arg(cssColor(inlineCodeBackground), cssColor(textColor)));
    html.replace(QStringLiteral("<blockquote"), QStringLiteral("<blockquote style=\"border-left:4px solid %1;margin:10px 0;padding:6px 12px;color:%2;background:%3;border-radius:6px;\"").arg(cssColor(palette.color(QPalette::Highlight)), cssColor(textColor), cssColor(blockQuoteBackground)));
    html.replace(QStringLiteral("<table"), QStringLiteral("<table style=\"border-collapse:collapse;width:100%;margin:10px 0;\""));
    html.replace(QStringLiteral("<th"), QStringLiteral("<th style=\"border:1px solid %1;padding:6px 8px;background:%2;color:%3;text-align:left;\"").arg(cssColor(border), cssColor(tableHeaderBackground), cssColor(textColor)));
    html.replace(QStringLiteral("<td"), QStringLiteral("<td style=\"border:1px solid %1;padding:6px 8px;color:%2;\"").arg(cssColor(border), cssColor(textColor)));
    html.replace(QStringLiteral("<a href="), QStringLiteral("<a style=\"color:%1;\" href=").arg(cssColor(linkColor)));
    return html;
}

QString messageToRichHtml(const QString &role,
                          const QString &text,
                          QStringList *codeBlocks,
                          int answerIndex,
                          const QPalette &palette)
{
    const QString rolePrefix = transcriptPrefix(role).toHtmlEscaped();
    const QColor accent = transcriptPrefixColor(palette, role);
    const QColor base = palette.color(QPalette::Base);
    const QColor bodyColor = transcriptBodyColor(palette, role);
    const QColor border = blendColors(palette.color(QPalette::Mid), accent, 0.25);
    const QColor cardBackground = blendColors(base, accent, 0.08);
    const QColor codeActionBackground = blendColors(base, palette.color(QPalette::Button), 0.35);
    const QColor codeBackground = blendColors(base, palette.color(QPalette::Window), 0.48);
    const QColor footerBorder = blendColors(border, palette.color(QPalette::WindowText), 0.12);
    const QColor linkColor = palette.color(QPalette::Link);
    QStringList bodyParts;

    const QString normalizedText = normalizeRenderableMarkdown(text);
    const QVector<TranscriptSegment> segments = splitTranscriptSegments(normalizedText);
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
                "<span style=\"font-size:11px;font-weight:700;color:%1;text-transform:uppercase;letter-spacing:0.08em;\">%2</span>"
                "<a href=\"copycode:%3\" style=\"font-size:12px;color:%4;text-decoration:none;background:%5;padding:4px 8px;border-radius:6px;border:1px solid %6;\">Copy code</a>"
                "</div>"
                "<pre style=\"margin:0;background:%7;color:%8;padding:12px;border-radius:10px;border:1px solid %9;overflow:auto;white-space:pre;tab-size:4;\"><code>%10</code></pre>"
                "</div>")
                .arg(cssColor(accent),
                     languageBadge,
                     QString::number(codeIndex),
                     cssColor(linkColor),
                     cssColor(codeActionBackground),
                     cssColor(border),
                     cssColor(codeBackground),
                     cssColor(palette.color(QPalette::Text)),
                     cssColor(border),
                     code.toHtmlEscaped());
        } else {
            const QString html = markdownFragmentToHtml(segment.text, palette);
            if (!html.trimmed().isEmpty()) {
                bodyParts << html;
            }
        }
    }

    if (bodyParts.isEmpty()) {
        bodyParts << QStringLiteral("<p>%1</p>").arg(normalizedText.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
    }

    QString footerHtml;
    if (role.compare(QStringLiteral("assistant"), Qt::CaseInsensitive) == 0 && answerIndex >= 0) {
        footerHtml = QStringLiteral(
            "<div style=\"margin-top:12px;padding-top:8px;border-top:1px solid %1;text-align:right;\">"
            "<a href=\"copyanswer:%2\" style=\"font-size:12px;color:%3;text-decoration:none;\">Copy Answer</a>"
            "</div>")
            .arg(cssColor(footerBorder), QString::number(answerIndex), cssColor(linkColor));
    }

    return QStringLiteral(
        "<div style=\"margin:8px 0 14px 0;padding:10px 12px;border-radius:12px;background:%1;border:1px solid %2;\">"
        "<div style=\"font-weight:700;color:%3;margin:0 0 8px 0;\">%4</div>"
        "<div style=\"color:%5;\">%6</div>"
        "%7"
        "</div>")
        .arg(cssColor(cardBackground), cssColor(border), cssColor(accent), rolePrefix, cssColor(bodyColor), bodyParts.join(QStringLiteral("\n")), footerHtml);
}

QString formatByteCount(qint64 bytes)
{
    static const QStringList units = {QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB"), QStringLiteral("TB")};
    double value = static_cast<double>(qMax<qint64>(0, bytes));
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < units.size() - 1) {
        value /= 1024.0;
        ++unitIndex;
    }

    const int precision = (unitIndex == 0 || value >= 10.0) ? 0 : 1;
    return QStringLiteral("%1 %2").arg(QString::number(value, 'f', precision), units.at(unitIndex));
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

    m_newConversationAction = fileMenu->addAction(QStringLiteral("New conversation"));
    m_newConversationAction->setShortcut(QKeySequence::New);
    connect(m_newConversationAction, &QAction::triggered, this, &MainWindow::newConversationRequested);

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
    QFont sessionTitleFont = sessionTitle->font();
    sessionTitleFont.setBold(true);
    sessionTitle->setFont(sessionTitleFont);
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
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    title->setFont(titleFont);
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
    m_transcript->setStyleSheet(QString());
    connect(transcriptBrowser, &QTextBrowser::anchorClicked, this, &MainWindow::onTranscriptAnchorClicked);
    if (QScrollBar *transcriptScrollBar = transcriptBrowser->verticalScrollBar()) {
        connect(transcriptScrollBar, &QScrollBar::valueChanged, this, [this, transcriptScrollBar](int value) {
            m_transcriptAutoScroll = value >= transcriptScrollBar->maximum() - 4;
        });
        connect(transcriptScrollBar, &QScrollBar::rangeChanged, this, [this, transcriptScrollBar](int, int) {
            if (m_transcriptAutoScroll) {
                transcriptScrollBar->setValue(transcriptScrollBar->maximum());
            }
        });
    }

    m_input = new QPlainTextEdit(chatPane);
    m_input->setPlaceholderText(QStringLiteral("Type your prompt here..."));
    m_input->setMaximumHeight(120);

    auto *priorityPanel = new QWidget(chatPane);
    auto *priorityLayout = new QVBoxLayout(priorityPanel);
    priorityLayout->setContentsMargins(0, 0, 0, 0);
    priorityLayout->setSpacing(4);
    auto *priorityHeader = new QHBoxLayout();
    auto *priorityTitle = new QLabel(QStringLiteral("Prioritized KB assets"), priorityPanel);
    QFont priorityTitleFont = priorityTitle->font();
    priorityTitleFont.setBold(true);
    priorityTitle->setFont(priorityTitleFont);
    m_prioritizedAssetsStatus = new QLabel(QStringLiteral("No prioritized KB assets"), priorityPanel);
    m_prioritizedAssetsStatus->setStyleSheet(QString());
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
    m_modelCombo->addItem(QStringLiteral("gpt-oss:20b"));
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
    m_cancelIndexingButton = new QPushButton(QStringLiteral("Cancel index"), chatPane);
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
    QFont busyLabelFont = m_busyIndicatorLabel->font();
    busyLabelFont.setBold(true);
    m_busyIndicatorLabel->setFont(busyLabelFont);
    m_busyIndicatorLabel->hide();
    m_busyIndicatorTimer = new QTimer(this);
    m_busyIndicatorTimer->setInterval(100);

    m_stopButton->setEnabled(false);
    m_cancelIndexingButton->hide();
    m_cancelIndexingButton->setEnabled(false);

    controlsLayout->addWidget(m_importFilesButton);
    controlsLayout->addWidget(m_importFolderButton);
    controlsLayout->addWidget(m_rememberButton);
    controlsLayout->addWidget(m_copyLastAnswerButton);
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(m_refreshModelsButton);
    controlsLayout->addWidget(m_reindexButton);
    controlsLayout->addWidget(m_cancelIndexingButton);
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
    QFont contextTitleFont = contextTitle->font();
    contextTitleFont.setBold(true);
    contextTitle->setFont(contextTitleFont);

    auto *tabs = new QTabWidget(rightPane);

    auto *diagnosticsTab = new QWidget(tabs);
    auto *diagnosticsLayout = new QVBoxLayout(diagnosticsTab);
    auto *diagnosticsHeader = new QVBoxLayout();
    auto *diagnosticsToggleRow = new QHBoxLayout();
    m_reasoningTraceToggleButton = new QPushButton(QStringLiteral("Capture reasoning trace: OFF"), diagnosticsTab);
    m_reasoningTraceToggleButton->setCheckable(true);
    m_verboseDiagnosticsToggleButton = new QPushButton(QStringLiteral("Verbose diagnostics: OFF"), diagnosticsTab);
    m_verboseDiagnosticsToggleButton->setCheckable(true);
    diagnosticsToggleRow->addWidget(m_reasoningTraceToggleButton, 0);
    diagnosticsToggleRow->addWidget(m_verboseDiagnosticsToggleButton, 0);
    diagnosticsToggleRow->addStretch(1);
    m_reasoningTraceInfoLabel = new QLabel(QStringLiteral("Off by default. When enabled, Amelia requests backend thinking streams when available and logs them here, along with any explicit model-authored reasoning notes."), diagnosticsTab);
    m_reasoningTraceInfoLabel->setWordWrap(true);
    m_reasoningTraceInfoLabel->setStyleSheet(QString());
    m_verboseDiagnosticsInfoLabel = new QLabel(QStringLiteral("Off by default. When enabled, Amelia shows verbose request/response summaries from Ollama. Essential errors still appear even when this stays off."), diagnosticsTab);
    m_verboseDiagnosticsInfoLabel->setWordWrap(true);
    m_verboseDiagnosticsInfoLabel->setStyleSheet(QString());
    diagnosticsHeader->addLayout(diagnosticsToggleRow);
    diagnosticsHeader->addWidget(m_reasoningTraceInfoLabel);
    diagnosticsHeader->addWidget(m_verboseDiagnosticsInfoLabel);
    m_diagnostics = new QTextEdit(diagnosticsTab);
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setStyleSheet(QString());
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
    m_sourceInventory->setMaximumHeight(96);

    m_sourceInventoryStats = new QPlainTextEdit(kbTab);
    m_sourceInventoryStats->setReadOnly(true);
    m_sourceInventoryStats->setMaximumHeight(170);
    m_sourceInventoryStats->setPlaceholderText(QStringLiteral("Knowledge Base footprint and collection statistics appear here."));

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

    m_sourceInventoryTree = new KnowledgeTreeWidget(kbTab);
    m_sourceInventoryTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_sourceInventoryTree->setRootIsDecorated(true);
    m_sourceInventoryTree->setAlternatingRowColors(true);
    m_sourceInventoryTree->setSortingEnabled(true);
    m_sourceInventoryTree->setHeaderLabels({QStringLiteral("Knowledge asset"), QStringLiteral("Kind"), QStringLiteral("Location / notes")});
    m_sourceInventoryTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sourceInventoryTree->viewport()->setAcceptDrops(true);

    auto *kbInventoryOverlay = new QWidget(kbTab);
    auto *kbInventoryOverlayLayout = new QVBoxLayout(kbInventoryOverlay);
    kbInventoryOverlayLayout->setContentsMargins(24, 24, 24, 24);
    kbInventoryOverlayLayout->addStretch(1);
    m_sourceInventoryRefreshLabel = new QLabel(QStringLiteral("Refreshing Knowledge Base..."), kbInventoryOverlay);
    m_sourceInventoryRefreshLabel->setAlignment(Qt::AlignCenter);
    m_sourceInventoryRefreshLabel->setWordWrap(true);
    QFont refreshLabelFont = m_sourceInventoryRefreshLabel->font();
    refreshLabelFont.setBold(true);
    refreshLabelFont.setPointSize(refreshLabelFont.pointSize() + 1);
    m_sourceInventoryRefreshLabel->setFont(refreshLabelFont);
    kbInventoryOverlayLayout->addWidget(m_sourceInventoryRefreshLabel, 0, Qt::AlignCenter);
    kbInventoryOverlayLayout->addStretch(1);

    auto *kbInventoryHost = new QWidget(kbTab);
    m_sourceInventoryStack = new QStackedLayout(kbInventoryHost);
    m_sourceInventoryStack->setContentsMargins(0, 0, 0, 0);
    m_sourceInventoryStack->addWidget(m_sourceInventoryTree);
    m_sourceInventoryStack->addWidget(kbInventoryOverlay);
    m_sourceInventoryStack->setCurrentIndex(0);

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
    kbLayout->addWidget(m_sourceInventoryStats);
    kbLayout->addLayout(kbFilterLayout);
    kbLayout->addWidget(kbInventoryHost, 1);
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

    auto *memoryTab = new QWidget(tabs);
    auto *memoryLayout = new QVBoxLayout(memoryTab);
    m_memoriesView = new QTreeWidget(memoryTab);
    m_memoriesView->setColumnCount(4);
    m_memoriesView->setHeaderLabels({QStringLiteral("Category"),
                                     QStringLiteral("Key"),
                                     QStringLiteral("Value"),
                                     QStringLiteral("Updated")});
    m_memoriesView->setRootIsDecorated(false);
    m_memoriesView->setAlternatingRowColors(true);
    m_memoriesView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_memoriesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_memoriesView->setUniformRowHeights(true);
    m_memoriesView->setWordWrap(false);
    m_memoriesView->setTextElideMode(Qt::ElideMiddle);
    m_memoriesView->header()->setStretchLastSection(false);
    m_memoriesView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_memoriesView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_memoriesView->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_memoriesView->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_memoryDetails = new QPlainTextEdit(memoryTab);
    m_memoryDetails->setReadOnly(true);
    m_memoryDetails->setPlaceholderText(QStringLiteral("Select a memory to inspect its full details and description."));
    m_memoryDetails->setMaximumHeight(180);
    m_deleteMemoryButton = new QPushButton(QStringLiteral("Delete selected"), memoryTab);
    m_deleteMemoryButton->setEnabled(false);

    auto *memoryButtons = new QHBoxLayout();
    memoryButtons->addWidget(m_deleteMemoryButton);
    memoryButtons->addStretch(1);

    memoryLayout->addWidget(m_memoriesView, 1);
    memoryLayout->addWidget(m_memoryDetails);
    memoryLayout->addLayout(memoryButtons);
    tabs->addTab(memoryTab, QStringLiteral("Memory"));

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

    setActionTip(m_newConversationAction, QStringLiteral("Create a fresh conversation and keep the current history saved."));
    setActionTip(m_configurationAction, QStringLiteral("Open Amelia's JSON configuration editor and save, cancel, or revert to factory defaults."));
    setActionTip(m_clearMemoriesAction, QStringLiteral("Remove all persisted memories saved by Amelia."));
    setActionTip(quitAction, QStringLiteral("Close Amelia Qt6."));
    setActionTip(m_aboutAmeliaAction, QStringLiteral("Show version and build details for Amelia Qt6."));
    setActionTip(m_aboutQtAction, QStringLiteral("Show the Qt version and licensing information."));

    setWidgetTip(m_conversationsList, QStringLiteral("Saved conversations. Select one to restore its transcript and summary."));
    setWidgetTip(m_newConversationButton, QStringLiteral("Start a new conversation without deleting older ones."));
    setWidgetTip(m_deleteConversationButton, QStringLiteral("Delete the selected saved conversation from local storage."));
    setWidgetTip(m_transcript, QStringLiteral("Formatted transcript view with clickable copy links for code blocks and full assistant answers."));
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
    setWidgetTip(m_deleteMemoryButton, QStringLiteral("Delete the selected persisted memory entry."));
    setWidgetTip(m_copyLastAnswerButton, QStringLiteral("Copy the most recent full assistant answer to the clipboard."));
    setWidgetTip(m_importFilesButton, QStringLiteral("Import one or more files into the local Knowledge Base."));
    setWidgetTip(m_importFolderButton, QStringLiteral("Import an entire folder into the local Knowledge Base."));
    setWidgetTip(m_statusLabel, QStringLiteral("Current high-level status for prompt preparation, indexing, and generation."));
    setWidgetTip(m_reasoningTraceToggleButton, QStringLiteral("Toggle capture of backend reasoning traces when the model exposes them."));
    setWidgetTip(m_verboseDiagnosticsToggleButton, QStringLiteral("Show or hide verbose Ollama request/response diagnostics. Essential errors remain visible either way."));
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
    setWidgetTip(m_sourceInventoryStats, QStringLiteral("Knowledge Base storage footprint, chunk counts, and per-collection statistics."));
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
    setWidgetTip(m_memoriesView, QStringLiteral("Persisted memories Amelia can reuse across conversations. Select one to inspect its description and metadata."));
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
    connect(m_reindexButton, &QPushButton::clicked, this, &MainWindow::onReindexClicked);
    connect(m_cancelIndexingButton, &QPushButton::clicked, this, &MainWindow::cancelIndexingRequested);
    connect(m_refreshModelsButton, &QPushButton::clicked, this, &MainWindow::refreshModelsRequested);
    connect(m_newConversationButton, &QPushButton::clicked, this, &MainWindow::newConversationRequested);
    connect(m_deleteConversationButton, &QPushButton::clicked, this, &MainWindow::onDeleteConversationClicked);
    connect(m_rememberButton, &QPushButton::clicked, this, &MainWindow::onRememberClicked);
    connect(m_deleteMemoryButton, &QPushButton::clicked, this, &MainWindow::onDeleteSelectedMemoryClicked);
    connect(m_importFilesButton, &QPushButton::clicked, this, &MainWindow::onImportFilesClicked);
    connect(m_importFolderButton, &QPushButton::clicked, this, &MainWindow::onImportFolderClicked);
    connect(m_modelCombo, &QComboBox::currentTextChanged, this, &MainWindow::onModelSelectionChanged);
    connect(m_conversationsList, &QListWidget::currentItemChanged, this, &MainWindow::onConversationItemChanged);
    connect(m_memoriesView, &QTreeWidget::itemSelectionChanged, this, [this]() {
        const bool hasSelection = m_memoriesView != nullptr && !m_memoriesView->selectedItems().isEmpty();
        if (m_deleteMemoryButton != nullptr) {
            m_deleteMemoryButton->setEnabled(hasSelection);
        }
        updateSelectedMemoryDetails();
    });
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
    connect(m_verboseDiagnosticsToggleButton, &QPushButton::toggled, this, &MainWindow::onVerboseDiagnosticsToggleToggled);
    if (auto *knowledgeTree = qobject_cast<KnowledgeTreeWidget *>(m_sourceInventoryTree)) {
        connect(knowledgeTree, &KnowledgeTreeWidget::knowledgeAssetsDropped, this, &MainWindow::onKnowledgeAssetsDropped);
        connect(knowledgeTree, &QTreeWidget::customContextMenuRequested, this, &MainWindow::onKnowledgeTreeContextMenuRequested);
    }

    if (m_promptLabPreview != nullptr) {
        m_promptLabPreview->setPlainText(buildPromptLabRecipe());
    }
    rebuildPrioritizedKnowledgeAssetsUi();
    applyPaletteAwareFormatting();
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_indexingActive) {
        m_closePendingAfterIndexCancel = true;
        emit cancelIndexingRequested();
        if (m_statusLabel != nullptr) {
            m_statusLabel->setText(QStringLiteral("Canceling indexing before exit..."));
        }
        event->ignore();
        return;
    }

    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event == nullptr) {
        return;
    }

    if (event->type() == QEvent::PaletteChange
            || event->type() == QEvent::ApplicationPaletteChange
            || event->type() == QEvent::StyleChange) {
        applyPaletteAwareFormatting();
    }
}

void MainWindow::applyPaletteAwareFormatting()
{
    const QPalette palette = this->palette();
    const QColor subdued = blendColors(palette.color(QPalette::Text), palette.color(QPalette::PlaceholderText), 0.55);
    const QColor highlight = palette.color(QPalette::Highlight);

    auto applySubduedLabel = [&](QLabel *label) {
        if (label == nullptr) {
            return;
        }
        QPalette labelPalette = label->palette();
        labelPalette.setColor(QPalette::WindowText, subdued);
        label->setPalette(labelPalette);
    };

    auto applyHighlightLabel = [&](QLabel *label) {
        if (label == nullptr) {
            return;
        }
        QPalette labelPalette = label->palette();
        labelPalette.setColor(QPalette::WindowText, highlight);
        label->setPalette(labelPalette);
    };

    applySubduedLabel(m_prioritizedAssetsStatus);
    applySubduedLabel(m_reasoningTraceInfoLabel);
    applySubduedLabel(m_verboseDiagnosticsInfoLabel);
    applyHighlightLabel(m_busyIndicatorLabel);

    if (m_transcript != nullptr && !m_transcriptPlainText.isEmpty()) {
        rebuildTranscriptFromPlainText(m_transcriptPlainText);
    }
    if (m_diagnostics != nullptr && !m_diagnosticsPlainText.isEmpty()) {
        rebuildDiagnosticsFromPlainText(m_diagnosticsPlainText);
    }
    rebuildPrioritizedKnowledgeAssetsUi();
}

void MainWindow::appendTranscriptEntry(const QString &role, const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    if (!lines.isEmpty()) {
        if (!m_transcriptPlainText.isEmpty()) {
            m_transcriptPlainText += QLatin1Char('\n');
        }
        m_transcriptPlainText += transcriptPrefix(role) + lines.constFirst();
        for (int i = 1; i < lines.size(); ++i) {
            m_transcriptPlainText += QLatin1Char('\n') + lines.at(i);
        }
    }

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

    const QString renderText = role == QStringLiteral("assistant")
            ? TranscriptFormatter::sanitizeFinalAssistantMarkdown(text)
            : text;
    int answerIndex = -1;
    if (role == QStringLiteral("assistant")) {
        answerIndex = m_transcriptAssistantAnswers.size();
        m_transcriptAssistantAnswers.push_back(renderText);
    }
    const QString html = messageToRichHtml(role, renderText, &m_transcriptCodeBlocks, answerIndex, m_transcript->palette());
    cursor.insertHtml(html);
    cursor.insertBlock();
    m_transcript->setTextCursor(cursor);
    m_transcript->ensureCursorVisible();
}

void MainWindow::appendDiagnosticEntry(const QString &timestamp, const QString &category, const QString &message)
{
    if (!m_diagnosticsPlainText.isEmpty()) {
        m_diagnosticsPlainText += QLatin1Char('\n');
    }
    m_diagnosticsPlainText += QStringLiteral("[%1] [%2] %3").arg(timestamp, category, message);
    insertDiagnosticEntry(timestamp, category, message);
}

void MainWindow::insertDiagnosticEntry(const QString &timestamp, const QString &category, const QString &message)
{
    if (m_diagnostics == nullptr) {
        return;
    }

    QTextCursor cursor = m_diagnostics->textCursor();
    cursor.movePosition(QTextCursor::End);
    if (!m_diagnostics->document()->isEmpty()) {
        cursor.insertBlock();
    }

    const QPalette palette = m_diagnostics->palette();

    QTextCharFormat timeFormat;
    timeFormat.setForeground(blendColors(palette.color(QPalette::Text), palette.color(QPalette::PlaceholderText), 0.55));

    QTextCharFormat categoryFormat;
    categoryFormat.setFontWeight(QFont::Bold);
    categoryFormat.setForeground(diagnosticCategoryColor(palette, category));

    QTextCharFormat messageFormat;
    messageFormat.setForeground(palette.color(QPalette::Text));

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

void MainWindow::updateKnowledgeBaseControlsEnabled()
{
    const bool busy = m_stopButton != nullptr && m_stopButton->isEnabled();
    const bool enableKbControls = !busy && !m_indexingActive && !m_knowledgeInventoryRefreshVisible;

    if (m_sourceInventoryFilter != nullptr) m_sourceInventoryFilter->setEnabled(enableKbControls);
    if (m_sourceInventorySortCombo != nullptr) m_sourceInventorySortCombo->setEnabled(enableKbControls);
    if (m_sourceInventoryTree != nullptr) {
        m_sourceInventoryTree->setEnabled(enableKbControls);
        m_sourceInventoryTree->setDragEnabled(enableKbControls);
        m_sourceInventoryTree->setAcceptDrops(enableKbControls);
        m_sourceInventoryTree->viewport()->setAcceptDrops(enableKbControls);
    }
    if (m_prioritizedAssetsList != nullptr) m_prioritizedAssetsList->setEnabled(enableKbControls);
    if (m_prioritizeSelectedAssetButton != nullptr) m_prioritizeSelectedAssetButton->setEnabled(enableKbControls);
    if (m_pinSelectedAssetButton != nullptr) m_pinSelectedAssetButton->setEnabled(enableKbControls);
    if (m_renameKnowledgeLabelButton != nullptr) m_renameKnowledgeLabelButton->setEnabled(enableKbControls);
    if (m_removeSelectedAssetButton != nullptr) m_removeSelectedAssetButton->setEnabled(enableKbControls);
    if (m_clearKnowledgeBaseButton != nullptr) m_clearKnowledgeBaseButton->setEnabled(enableKbControls);
    if (m_removePrioritizedAssetButton != nullptr) m_removePrioritizedAssetButton->setEnabled(enableKbControls);
    if (m_clearPrioritizedAssetsButton != nullptr) m_clearPrioritizedAssetsButton->setEnabled(enableKbControls);
}

bool MainWindow::confirmKnowledgeBaseReindexAction(const QString &action, const QString &details) const
{
    QString prompt = action.trimmed();
    if (!details.trimmed().isEmpty()) {
        prompt += QStringLiteral("\n\n") + details.trimmed();
    }
    prompt += QStringLiteral("\n\nAmelia will need to refresh and reindex the Knowledge Base after this change.");
    prompt += QStringLiteral("\nThe Knowledge Base tree will be temporarily locked while the refreshed inventory is rebuilt.");
    prompt += QStringLiteral("\n\nContinue?");
    return QMessageBox::question(const_cast<MainWindow *>(this),
                                 QStringLiteral("Knowledge Base reindex required"),
                                 prompt,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
}

void MainWindow::setKnowledgeInventoryRefreshVisible(bool visible, const QString &label)
{
    m_knowledgeInventoryRefreshVisible = visible;
    if (!label.trimmed().isEmpty()) {
        m_knowledgeInventoryRefreshBaseLabel = label.trimmed();
    } else if (m_knowledgeInventoryRefreshBaseLabel.trimmed().isEmpty()) {
        m_knowledgeInventoryRefreshBaseLabel = QStringLiteral("Refreshing Knowledge Base...");
    }

    if (m_sourceInventoryStack != nullptr) {
        m_sourceInventoryStack->setCurrentIndex(visible ? 1 : 0);
    }
    if (m_sourceInventoryRefreshLabel != nullptr) {
        const QString frame = m_busyFrames.isEmpty() ? QStringLiteral("•") : m_busyFrames.at(m_busyFrameIndex % m_busyFrames.size());
        m_sourceInventoryRefreshLabel->setText(visible ? QStringLiteral("%1 %2").arg(frame, m_knowledgeInventoryRefreshBaseLabel) : QString());
    }

    updateKnowledgeBaseControlsEnabled();

    if (visible) {
        if (m_busyIndicatorTimer != nullptr && !m_busyIndicatorTimer->isActive()) {
            m_busyIndicatorTimer->start();
        }
    } else if (m_busyIndicatorTimer != nullptr && (m_stopButton == nullptr || !m_stopButton->isEnabled())) {
        m_busyIndicatorTimer->stop();
    }
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
    QScrollBar *scrollBar = m_transcript != nullptr ? m_transcript->verticalScrollBar() : nullptr;
    const bool preserveScroll = scrollBar != nullptr && !m_transcriptAutoScroll;
    const int previousScrollValue = preserveScroll ? scrollBar->value() : 0;

    QTextCursor cursor = m_transcript->textCursor();
    cursor.movePosition(QTextCursor::End);

    const QPalette palette = m_transcript != nullptr ? m_transcript->palette() : QApplication::palette();

    QTextCharFormat prefixFormat;
    prefixFormat.setFontWeight(QFont::Bold);
    prefixFormat.setForeground(transcriptPrefixColor(palette, QStringLiteral("assistant")));

    QTextCharFormat bodyFormat;
    bodyFormat.setForeground(transcriptBodyColor(palette, QStringLiteral("assistant")));

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
    if (preserveScroll && scrollBar != nullptr) {
        scrollBar->setValue(previousScrollValue);
    } else {
        m_transcript->ensureCursorVisible();
    }
    updateResponseStreamingProgress(text);
}

void MainWindow::finalizeAssistantMessage(const QString &text)
{
    const QString cleaned = TranscriptFormatter::sanitizeFinalAssistantMarkdown(text);
    m_lastAssistantMessage = cleaned;
    QScrollBar *scrollBar = m_transcript != nullptr ? m_transcript->verticalScrollBar() : nullptr;
    const bool preserveScroll = scrollBar != nullptr && !m_transcriptAutoScroll;
    const int previousScrollValue = preserveScroll ? scrollBar->value() : 0;

    if (m_streamingAssistant && m_streamingAssistantStartPosition >= 0) {
        QTextCursor cursor(m_transcript->document());
        cursor.setPosition(m_streamingAssistantStartPosition);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        m_transcript->setTextCursor(cursor);
        insertTranscriptMessage(QStringLiteral("assistant"), cleaned);
        if (preserveScroll && scrollBar != nullptr) {
            scrollBar->setValue(previousScrollValue);
        }
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
    if (m_memoriesView == nullptr) {
        return;
    }

    m_memoriesView->clear();

    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (doc.isArray()) {
        const QJsonArray array = doc.array();
        for (const QJsonValue &value : array) {
            if (!value.isObject()) {
                continue;
            }

            const QJsonObject obj = value.toObject();
            auto *item = new QTreeWidgetItem(m_memoriesView);
            const QString category = obj.value(QStringLiteral("category")).toString();
            const QString key = obj.value(QStringLiteral("key")).toString();
            const QString memoryValue = obj.value(QStringLiteral("value")).toString();
            const QString updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
            const QString createdAt = obj.value(QStringLiteral("createdAt")).toString();
            const double confidence = obj.value(QStringLiteral("confidence")).toDouble(1.0);
            const bool pinned = obj.value(QStringLiteral("pinned")).toBool(false);
            QString description = obj.value(QStringLiteral("description")).toString().trimmed();
            if (description.isEmpty()) {
                description = QStringLiteral("Stored as a %1 memory%2. Amelia can reuse it later when the prompt matches.")
                        .arg(category.isEmpty() ? QStringLiteral("general") : category,
                             key.trimmed().isEmpty() ? QString() : QStringLiteral(" under key '%1'").arg(key));
            }

            item->setText(0, category);
            item->setText(1, key);
            item->setText(2, memoryValue);
            item->setText(3, updatedAt);
            item->setData(0, Qt::UserRole, obj.value(QStringLiteral("id")).toString());
            item->setData(0, Qt::UserRole + 1, createdAt);
            item->setData(0, Qt::UserRole + 2, updatedAt);
            item->setData(0, Qt::UserRole + 3, confidence);
            item->setData(0, Qt::UserRole + 4, pinned);
            item->setData(0, Qt::UserRole + 5, description);
            item->setToolTip(2, memoryValue);
        }

        if (m_memoriesView->topLevelItemCount() == 0) {
            auto *emptyItem = new QTreeWidgetItem(m_memoriesView);
            emptyItem->setText(0, QStringLiteral("<none>"));
            emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
        }
    } else {
        auto *item = new QTreeWidgetItem(m_memoriesView);
        item->setText(0, QStringLiteral("raw"));
        item->setText(2, text.trimmed().isEmpty() ? QStringLiteral("<none>") : text.trimmed());
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    }

    m_memoriesView->resizeColumnToContents(0);
    m_memoriesView->resizeColumnToContents(1);
    m_memoriesView->resizeColumnToContents(3);
    if (m_deleteMemoryButton != nullptr) {
        m_deleteMemoryButton->setEnabled(false);
    }
    updateSelectedMemoryDetails();
}

void MainWindow::updateSelectedMemoryDetails()
{
    if (m_memoryDetails == nullptr) {
        return;
    }

    if (m_memoriesView == nullptr) {
        m_memoryDetails->clear();
        return;
    }

    const QList<QTreeWidgetItem *> selectedItems = m_memoriesView->selectedItems();
    if (selectedItems.isEmpty()) {
        m_memoryDetails->setPlainText(QStringLiteral("Select a memory to inspect its full details and description."));
        return;
    }

    const QTreeWidgetItem *item = selectedItems.constFirst();
    if (item == nullptr) {
        m_memoryDetails->setPlainText(QStringLiteral("Select a memory to inspect its full details and description."));
        return;
    }

    const QString category = item->text(0).trimmed();
    const QString key = item->text(1).trimmed();
    const QString value = item->text(2).trimmed();
    const QString updatedAt = item->data(0, Qt::UserRole + 2).toString().trimmed();
    const QString createdAt = item->data(0, Qt::UserRole + 1).toString().trimmed();
    const double confidence = item->data(0, Qt::UserRole + 3).toDouble();
    const bool pinned = item->data(0, Qt::UserRole + 4).toBool();
    const QString description = item->data(0, Qt::UserRole + 5).toString().trimmed();

    QStringList lines;
    lines << QStringLiteral("Description: %1").arg(description.isEmpty() ? QStringLiteral("<none>") : description);
    lines << QStringLiteral("Category: %1").arg(category.isEmpty() ? QStringLiteral("<none>") : category);
    lines << QStringLiteral("Key: %1").arg(key.isEmpty() ? QStringLiteral("<note>") : key);
    lines << QStringLiteral("Value: %1").arg(value.isEmpty() ? QStringLiteral("<empty>") : value);
    lines << QStringLiteral("Pinned: %1").arg(pinned ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("Confidence: %1").arg(QString::number(confidence, 'f', 2));
    lines << QStringLiteral("Created: %1").arg(createdAt.isEmpty() ? QStringLiteral("<unknown>") : createdAt);
    lines << QStringLiteral("Updated: %1").arg(updatedAt.isEmpty() ? QStringLiteral("<unknown>") : updatedAt);

    m_memoryDetails->setPlainText(lines.join(QStringLiteral("\n")));
}

void MainWindow::setSessionSummary(const QString &text)
{
    m_sessionSummary->setPlainText(text);
}

void MainWindow::rebuildTranscriptFromPlainText(const QString &text)
{
    m_transcriptPlainText = text;
    m_transcript->clear();
    m_transcriptAutoScroll = true;
    m_streamingAssistant = false;
    m_streamingAssistantStartPosition = -1;
    m_lastAssistantMessage.clear();
    m_transcriptCodeBlocks.clear();
    m_transcriptAssistantAnswers.clear();

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
            m_lastAssistantMessage = TranscriptFormatter::sanitizeFinalAssistantMarkdown(message);
        }
        insertTranscriptMessage(currentRole, message);
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
    if (QScrollBar *scrollBar = m_transcript->verticalScrollBar()) {
        scrollBar->setValue(scrollBar->maximum());
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
    const bool allowConversationChanges = !busy && !m_indexingActive;
    const bool allowModelControls = !busy && !m_indexingActive;

    m_sendButton->setEnabled(!busy && !m_indexingActive);
    m_stopButton->setEnabled(busy);
    m_input->setReadOnly(busy || m_indexingActive);
    if (m_newConversationAction != nullptr) m_newConversationAction->setEnabled(allowConversationChanges);
    if (m_conversationsList != nullptr) m_conversationsList->setEnabled(allowConversationChanges);
    m_newConversationButton->setEnabled(allowConversationChanges);
    if (m_deleteConversationButton != nullptr) m_deleteConversationButton->setEnabled(allowConversationChanges);
    if (m_modelCombo != nullptr) m_modelCombo->setEnabled(allowModelControls);
    if (m_refreshModelsButton != nullptr) m_refreshModelsButton->setEnabled(allowModelControls);
    m_importFilesButton->setEnabled(!busy && !m_indexingActive);
    m_importFolderButton->setEnabled(!busy && !m_indexingActive);
    m_reindexButton->setEnabled(!busy && !m_indexingActive);
    m_promptLabGenerateButton->setEnabled(!busy);
    m_promptLabUseButton->setEnabled(!busy);
    m_promptLabImportButton->setEnabled(!busy && !m_indexingActive);
    if (m_promptLabBrowseFilesButton != nullptr) m_promptLabBrowseFilesButton->setEnabled(!busy && !m_indexingActive);
    if (m_promptLabBrowseFolderButton != nullptr) m_promptLabBrowseFolderButton->setEnabled(!busy && !m_indexingActive);
    if (m_promptLabCopyRecipeButton != nullptr) m_promptLabCopyRecipeButton->setEnabled(!busy);
    if (m_cancelIndexingButton != nullptr) m_cancelIndexingButton->setEnabled(m_indexingActive && !busy);
    updateKnowledgeBaseControlsEnabled();

    if (busy) {
        beginResponseProgress(QStringLiteral("Preparing prompt..."));
        m_busyFrameIndex = 0;
        updateBusyIndicator();
        m_busyIndicatorLabel->show();
        m_busyIndicatorTimer->start();
    } else {
        m_busyIndicatorLabel->hide();
        m_busyIndicatorLabel->clear();
        if (!m_knowledgeInventoryRefreshVisible && m_busyIndicatorTimer != nullptr) {
            m_busyIndicatorTimer->stop();
        }
    }
}

void MainWindow::setIndexingActive(bool active)
{
    m_indexingActive = active;

    const bool allowConversationChanges = !active && !m_stopButton->isEnabled();
    const bool allowModelControls = !active && !m_stopButton->isEnabled();

    m_sendButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_newConversationAction != nullptr) m_newConversationAction->setEnabled(allowConversationChanges);
    if (m_conversationsList != nullptr) m_conversationsList->setEnabled(allowConversationChanges);
    m_newConversationButton->setEnabled(allowConversationChanges);
    if (m_deleteConversationButton != nullptr) m_deleteConversationButton->setEnabled(allowConversationChanges);
    if (m_modelCombo != nullptr) m_modelCombo->setEnabled(allowModelControls);
    if (m_refreshModelsButton != nullptr) m_refreshModelsButton->setEnabled(allowModelControls);
    m_input->setReadOnly(active || m_stopButton->isEnabled());
    m_reindexButton->setEnabled(!active && !m_stopButton->isEnabled());
    m_importFilesButton->setEnabled(!active && !m_stopButton->isEnabled());
    m_importFolderButton->setEnabled(!active && !m_stopButton->isEnabled());
    m_promptLabImportButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_promptLabBrowseFilesButton != nullptr) m_promptLabBrowseFilesButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_promptLabBrowseFolderButton != nullptr) m_promptLabBrowseFolderButton->setEnabled(!active && !m_stopButton->isEnabled());
    if (m_cancelIndexingButton != nullptr) {
        m_cancelIndexingButton->setVisible(active);
        m_cancelIndexingButton->setEnabled(active);
    }
    updateKnowledgeBaseControlsEnabled();

    if (m_taskProgressBar == nullptr) {
        return;
    }

    if (active) {
        setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Refreshing Knowledge Base index..."));
        m_taskProgressBar->setVisible(true);
        m_taskProgressBar->setRange(0, 0);
        m_taskProgressBar->setFormat(QStringLiteral("Indexing local docs..."));
        m_statusLabel->setText(QStringLiteral("Indexing local docs..."));
    } else {
        setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Refreshing Knowledge Base view..."));
        if (m_cancelIndexingButton != nullptr) {
            m_cancelIndexingButton->hide();
            m_cancelIndexingButton->setEnabled(false);
        }
        resetTaskProgressBar();
        if (!m_stopButton->isEnabled()) {
            m_statusLabel->setText(QStringLiteral("Ready."));
        }
        if (m_closePendingAfterIndexCancel) {
            m_closePendingAfterIndexCancel = false;
            QTimer::singleShot(0, this, [this]() { close(); });
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
        if (m_knowledgeInventoryRefreshVisible) {
            setKnowledgeInventoryRefreshVisible(true, label);
        }
    }
}

void MainWindow::setBackendSummary(const QString &text)
{
    m_backendSummary->setPlainText(text);
}

void MainWindow::rebuildDiagnosticsFromPlainText(const QString &text)
{
    m_diagnosticsPlainText = text;
    m_diagnostics->clear();

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QRegularExpression regex(QStringLiteral(R"(^\[([^\]]+)\]\s+\[([^\]]+)\]\s+(.*)$)"));

    for (const QString &line : lines) {
        const QRegularExpressionMatch match = regex.match(line);
        if (match.hasMatch()) {
            insertDiagnosticEntry(match.captured(1), match.captured(2), match.captured(3));
        } else {
            insertDiagnosticEntry(QStringLiteral("log"), QStringLiteral("misc"), line);
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
    if (m_sourceInventoryStats != nullptr) {
        m_sourceInventoryStats->clear();
    }

    if (m_sourceInventoryTree == nullptr) {
        return;
    }

    m_sourceInventoryTree->clear();
    if (text.trimmed().isEmpty() || text.trimmed() == QStringLiteral("<none>")) {
        if (m_sourceInventoryStats != nullptr) {
            m_sourceInventoryStats->setPlainText(QStringLiteral("No imported Knowledge Base assets."));
        }
        updateKnowledgeBaseFilterStatus();
        if (!m_indexingActive) {
            setKnowledgeInventoryRefreshVisible(false);
        }
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject()) {
        if (m_sourceInventory != nullptr) {
            m_sourceInventory->setPlainText(text);
        }
        if (m_sourceInventoryStats != nullptr) {
            m_sourceInventoryStats->setPlainText(QStringLiteral("Statistics unavailable for the current inventory payload."));
        }
        updateKnowledgeBaseFilterStatus();
        if (!m_indexingActive) {
            setKnowledgeInventoryRefreshVisible(false);
        }
        return;
    }

    const QJsonObject root = doc.object();
    const qint64 totalBytes = static_cast<qint64>(root.value(QStringLiteral("totalBytes")).toDouble());
    const QJsonArray collections = root.value(QStringLiteral("collections")).toArray();

    QStringList summary;
    summary << QStringLiteral("Knowledge root: %1").arg(root.value(QStringLiteral("knowledgeRoot")).toString(QStringLiteral("<unknown>")));
    summary << QStringLiteral("Collections root: %1").arg(root.value(QStringLiteral("collectionsRoot")).toString(QStringLiteral("<unknown>")));
    summary << QStringLiteral("Workspace jail root: %1").arg(root.value(QStringLiteral("workspaceJailRoot")).toString(QStringLiteral("<unknown>")));
    summary << QStringLiteral("Collections: %1 | Sources: %2 | Chunks: %3 | Stored size: %4")
                  .arg(collections.size())
                  .arg(root.value(QStringLiteral("sources")).toInt())
                  .arg(root.value(QStringLiteral("chunks")).toInt())
                  .arg(formatByteCount(totalBytes));
    summary << QStringLiteral("Semantic retrieval: %1 | Chunking: %2")
                  .arg(root.value(QStringLiteral("semanticEnabled")).toBool() ? QStringLiteral("enabled") : QStringLiteral("disabled"),
                       root.value(QStringLiteral("chunkingStrategy")).toString(QStringLiteral("<unknown>")));
    if (m_sourceInventory != nullptr) {
        m_sourceInventory->setPlainText(summary.join(QStringLiteral("\n")));
    }

    QStringList stats;
    stats << QStringLiteral("Library footprint");
    stats << QStringLiteral("  Collections: %1").arg(collections.size());
    stats << QStringLiteral("  Files: %1").arg(root.value(QStringLiteral("sources")).toInt());
    stats << QStringLiteral("  Chunks: %1").arg(root.value(QStringLiteral("chunks")).toInt());
    stats << QStringLiteral("  Stored size: %1").arg(formatByteCount(totalBytes));
    stats << QStringLiteral("  Embedding backend: %1").arg(root.value(QStringLiteral("embeddingBackend")).toString(QStringLiteral("<unknown>")));
    stats << QString();
    stats << QStringLiteral("Per-collection stats");

    const auto appendStatsLine = [&](const QString &line) {
        stats << line;
        if (m_sourceInventoryStats != nullptr) {
            m_sourceInventoryStats->setPlainText(stats.join(QStringLiteral("\n")));
        }
    };

    if (collections.isEmpty()) {
        appendStatsLine(QStringLiteral("  <none>"));
    }
    QStringList availablePaths;
    for (const QJsonValue &collectionValue : collections) {
        if (!collectionValue.isObject()) {
            continue;
        }

        const QJsonObject collection = collectionValue.toObject();
        const int collectionChunkCount = collection.value(QStringLiteral("chunkCount")).toInt();
        const qint64 collectionBytes = static_cast<qint64>(collection.value(QStringLiteral("totalBytes")).toDouble());
        appendStatsLine(QStringLiteral("  • %1 — %2 files, %3 chunks, %4")
                            .arg(collection.value(QStringLiteral("label")).toString(QStringLiteral("Imported collection")))
                            .arg(collection.value(QStringLiteral("fileCount")).toInt())
                            .arg(collectionChunkCount)
                            .arg(formatByteCount(collectionBytes)));

        auto *collectionItem = new QTreeWidgetItem(m_sourceInventoryTree);
        collectionItem->setText(0, collection.value(QStringLiteral("label")).toString(QStringLiteral("Imported collection")));
        collectionItem->setText(1, QStringLiteral("Collection"));
        collectionItem->setText(2, QStringLiteral("%1 file(s) • %2 chunks • %3")
                                    .arg(collection.value(QStringLiteral("fileCount")).toInt())
                                    .arg(collectionChunkCount)
                                    .arg(formatByteCount(collectionBytes)));
        collectionItem->setData(0, kKnowledgeNodeTypeRole, QStringLiteral("collection"));
        collectionItem->setData(0, kKnowledgeCollectionIdRole, collection.value(QStringLiteral("collectionId")).toString());
        QJsonObject collectionProperties = collection;
        collectionProperties.insert(QStringLiteral("nodeType"), QStringLiteral("collection"));
        collectionItem->setData(0, kKnowledgePropertiesRole, QString::fromUtf8(QJsonDocument(collectionProperties).toJson(QJsonDocument::Compact)));
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
            const int groupChunkCount = group.value(QStringLiteral("chunkCount")).toInt();
            const qint64 groupBytes = static_cast<qint64>(group.value(QStringLiteral("totalBytes")).toDouble());
            auto *groupItem = new QTreeWidgetItem(collectionItem);
            groupItem->setText(0, group.value(QStringLiteral("label")).toString(QStringLiteral("(root)")));
            groupItem->setText(1, QStringLiteral("Folder"));
            groupItem->setText(2, QStringLiteral("%1 file(s) • %2 chunks • %3")
                                   .arg(group.value(QStringLiteral("fileCount")).toInt())
                                   .arg(groupChunkCount)
                                   .arg(formatByteCount(groupBytes)));
            groupItem->setData(0, kKnowledgeNodeTypeRole, QStringLiteral("group"));
            groupItem->setData(0, kKnowledgeCollectionIdRole, collection.value(QStringLiteral("collectionId")).toString());
            groupItem->setData(0, kKnowledgeGroupLabelRole, group.value(QStringLiteral("label")).toString(QStringLiteral("(root)")));
            QJsonObject groupProperties = group;
            groupProperties.insert(QStringLiteral("nodeType"), QStringLiteral("group"));
            groupItem->setData(0, kKnowledgePropertiesRole, QString::fromUtf8(QJsonDocument(groupProperties).toJson(QJsonDocument::Compact)));
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
                const QString zeroChunkReason = file.value(QStringLiteral("zeroChunkReason")).toString().trimmed();
                const bool hasZeroChunkWarning = chunkCount <= 0 && !zeroChunkReason.isEmpty();
                const QString displayPath = relativePath.isEmpty() ? fileName : relativePath;

                auto *fileItem = new QTreeWidgetItem(groupItem);
                fileItem->setText(0, fileName.isEmpty() ? relativePath : fileName);
                fileItem->setText(1, extension.isEmpty() ? sourceType : extension);
                fileItem->setText(2, hasZeroChunkWarning
                                      ? QStringLiteral("%1 — ZERO CHUNKS: %2").arg(displayPath, zeroChunkReason)
                                      : displayPath);
                fileItem->setData(0, kKnowledgeNodeTypeRole, QStringLiteral("file"));
                fileItem->setData(0, kKnowledgePathRole, filePath);
                fileItem->setData(0, kKnowledgeCollectionIdRole, collection.value(QStringLiteral("collectionId")).toString());
                fileItem->setData(0, kKnowledgeGroupLabelRole, group.value(QStringLiteral("label")).toString(QStringLiteral("(root)")));
                QJsonObject fileProperties = file;
                fileProperties.insert(QStringLiteral("nodeType"), QStringLiteral("file"));
                fileItem->setData(0, kKnowledgePropertiesRole, QString::fromUtf8(QJsonDocument(fileProperties).toJson(QJsonDocument::Compact)));
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
                                      + sourceType
                                      + QLatin1Char('\n')
                                      + zeroChunkReason);

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
                if (hasZeroChunkWarning) {
                    details << QStringLiteral("Zero-chunk reason: %1").arg(zeroChunkReason);
                }
                details << QStringLiteral("Stored size: %1").arg(formatByteCount(static_cast<qint64>(file.value(QStringLiteral("fileSizeBytes")).toDouble())));
                const QString detailsText = details.join(QStringLiteral("\n"));
                fileItem->setToolTip(0, detailsText);
                fileItem->setToolTip(1, detailsText);
                fileItem->setToolTip(2, detailsText);

                if (hasZeroChunkWarning) {
                    const QColor warningColor = blendColors(m_sourceInventoryTree->palette().color(QPalette::Highlight), QColor(Qt::red), 0.40);
                    for (int column = 0; column < 3; ++column) {
                        fileItem->setForeground(column, warningColor);
                    }
                }

                if (!filePath.isEmpty()) {
                    availablePaths << filePath;
                    const QString kbDisplayPath = QStringLiteral("%1 / %2")
                                                      .arg(collection.value(QStringLiteral("label")).toString(),
                                                           relativePath.isEmpty() ? fileName : relativePath);
                    m_knowledgeDisplayNames.insert(filePath, kbDisplayPath);
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
    if (!m_indexingActive) {
        setKnowledgeInventoryRefreshVisible(false);
    }
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

QTreeWidgetItem *MainWindow::knowledgeContextItem(const QPoint *treePos) const
{
    if (m_sourceInventoryTree == nullptr) {
        return nullptr;
    }

    if (treePos != nullptr) {
        if (QTreeWidgetItem *item = m_sourceInventoryTree->itemAt(*treePos)) {
            return item;
        }
    }

    if (QTreeWidgetItem *current = m_sourceInventoryTree->currentItem()) {
        return current;
    }

    const QList<QTreeWidgetItem *> items = m_sourceInventoryTree->selectedItems();
    return items.isEmpty() ? nullptr : items.constFirst();
}

void MainWindow::showKnowledgePropertiesDialog(const QString &title,
                                               const QJsonObject &properties,
                                               const QString &nodeType)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(860, 620);

    auto *layout = new QVBoxLayout(&dialog);
    auto *summaryLabel = new QLabel(QStringLiteral("<b>%1</b>").arg(title), &dialog);
    summaryLabel->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel);

    QStringList lines;
    if (nodeType == QStringLiteral("collection")) {
        lines << QStringLiteral("Collection id: %1").arg(properties.value(QStringLiteral("collectionId")).toString(QStringLiteral("<unknown>")));
        lines << QStringLiteral("Created at: %1").arg(properties.value(QStringLiteral("createdAt")).toString(QStringLiteral("<unknown>")));
        lines << QStringLiteral("Collection root: %1").arg(properties.value(QStringLiteral("collectionRoot")).toString(QStringLiteral("<unknown>")));
        lines << QStringLiteral("Files: %1").arg(properties.value(QStringLiteral("fileCount")).toInt());
        lines << QStringLiteral("Chunks: %1").arg(properties.value(QStringLiteral("chunkCount")).toInt());
        lines << QStringLiteral("Stored size: %1").arg(formatByteCount(static_cast<qint64>(properties.value(QStringLiteral("totalBytes")).toDouble())));
    } else if (nodeType == QStringLiteral("group")) {
        lines << QStringLiteral("Collection: %1").arg(properties.value(QStringLiteral("collectionLabel")).toString(QStringLiteral("<unknown>")));
        lines << QStringLiteral("Folder label: %1").arg(properties.value(QStringLiteral("label")).toString(QStringLiteral("(root)")));
        lines << QStringLiteral("Folder path: %1").arg(properties.value(QStringLiteral("folderPath")).toString(QStringLiteral("<unknown>")));
        lines << QStringLiteral("Files: %1").arg(properties.value(QStringLiteral("fileCount")).toInt());
        lines << QStringLiteral("Chunks: %1").arg(properties.value(QStringLiteral("chunkCount")).toInt());
        lines << QStringLiteral("Stored size: %1").arg(formatByteCount(static_cast<qint64>(properties.value(QStringLiteral("totalBytes")).toDouble())));
    } else {
        const qint64 modifiedMs = static_cast<qint64>(properties.value(QStringLiteral("fileModifiedMs")).toDouble());
        lines << QStringLiteral("Collection: %1").arg(properties.value(QStringLiteral("collectionLabel")).toString(QStringLiteral("<unknown>")));
        lines << QStringLiteral("Folder: %1").arg(properties.value(QStringLiteral("groupLabel")).toString(QStringLiteral("(root)")));
        lines << QStringLiteral("Relative path: %1").arg(properties.value(QStringLiteral("relativePath")).toString(QStringLiteral("<unknown>")));
        lines << QStringLiteral("Internal path: %1").arg(properties.value(QStringLiteral("filePath")).toString(QStringLiteral("<unknown>")));
        lines << QStringLiteral("Original path: %1").arg(properties.value(QStringLiteral("originalPath")).toString(QStringLiteral("<unknown>")));
        lines << QStringLiteral("Type / role: %1 / %2")
                     .arg(properties.value(QStringLiteral("sourceType")).toString(QStringLiteral("<unknown>")),
                          properties.value(QStringLiteral("sourceRole")).toString(QStringLiteral("<unknown>")));
        lines << QStringLiteral("Extractor: %1").arg(properties.value(QStringLiteral("extractor")).toString(QStringLiteral("<unknown>")));
        lines << QStringLiteral("Modified: %1").arg(modifiedMs > 0
                                                       ? QDateTime::fromMSecsSinceEpoch(modifiedMs).toString(Qt::ISODate)
                                                       : QStringLiteral("<unknown>"));
        lines << QStringLiteral("Stored size: %1").arg(formatByteCount(static_cast<qint64>(properties.value(QStringLiteral("fileSizeBytes")).toDouble())));
        lines << QStringLiteral("Text chars: %1").arg(properties.value(QStringLiteral("textCharCount")).toInt());
        lines << QStringLiteral("Lines: %1").arg(properties.value(QStringLiteral("lineCount")).toInt());
        lines << QStringLiteral("Words: %1").arg(properties.value(QStringLiteral("wordCount")).toInt());
        lines << QStringLiteral("Chunks: %1").arg(properties.value(QStringLiteral("chunkCount")).toInt());
        lines << QStringLiteral("Chunking profile: %1").arg(properties.value(QStringLiteral("chunkingProfile")).toString(QStringLiteral("<unknown>")));
        const QString zeroChunkReason = properties.value(QStringLiteral("zeroChunkReason")).toString().trimmed();
        if (properties.value(QStringLiteral("chunkCount")).toInt() <= 0 && !zeroChunkReason.isEmpty()) {
            lines << QStringLiteral("Zero-chunk reason: %1").arg(zeroChunkReason);
        }
    }

    auto *summaryEdit = new QPlainTextEdit(&dialog);
    summaryEdit->setReadOnly(true);
    summaryEdit->setPlainText(lines.join(QStringLiteral("\n")));
    summaryEdit->setMinimumHeight(nodeType == QStringLiteral("file") ? 210 : 170);
    layout->addWidget(summaryEdit);

    if (nodeType == QStringLiteral("file")) {
        const QJsonArray previews = properties.value(QStringLiteral("chunksPreview")).toArray();
        auto *chunkLabel = new QLabel(QStringLiteral("Chunk dump preview"), &dialog);
        layout->addWidget(chunkLabel);

        QStringList chunkLines;
        if (previews.isEmpty()) {
            const QString zeroChunkReason = properties.value(QStringLiteral("zeroChunkReason")).toString().trimmed();
            if (properties.value(QStringLiteral("chunkCount")).toInt() <= 0 && !zeroChunkReason.isEmpty()) {
                chunkLines << QStringLiteral("This asset produced zero chunks.");
                chunkLines << QStringLiteral("Reason: %1").arg(zeroChunkReason);
            } else {
                chunkLines << QStringLiteral("No chunk preview is available for this asset.");
            }
        } else {
            for (const QJsonValue &value : previews) {
                if (!value.isObject()) {
                    continue;
                }
                const QJsonObject preview = value.toObject();
                chunkLines << QStringLiteral("--- Chunk %1 | %2 chars | %3 words ---")
                                  .arg(preview.value(QStringLiteral("chunkIndex")).toInt())
                                  .arg(preview.value(QStringLiteral("charCount")).toInt())
                                  .arg(preview.value(QStringLiteral("wordCount")).toInt());
                chunkLines << preview.value(QStringLiteral("text")).toString();
                chunkLines << QString();
            }
            const int omitted = properties.value(QStringLiteral("omittedChunkCount")).toInt();
            if (omitted > 0) {
                chunkLines << QStringLiteral("(%1 additional chunk(s) omitted from preview)").arg(omitted);
            }
        }

        auto *chunkDump = new QPlainTextEdit(&dialog);
        chunkDump->setReadOnly(true);
        chunkDump->setPlainText(chunkLines.join(QStringLiteral("\n")));
        layout->addWidget(chunkDump, 1);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
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
        const QPalette palette = m_prioritizedAssetsList->palette();
        const QColor baseColor = pinned
                ? blendColors(palette.color(QPalette::Highlight), QColor(Qt::yellow), 0.35)
                : (palette.color(QPalette::Link).isValid() ? palette.color(QPalette::Link) : palette.color(QPalette::Highlight));
        item->setForeground(baseColor);
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
    if (!confirmKnowledgeBaseReindexAction(QStringLiteral("Remove %1 selected Knowledge Base asset(s)?").arg(paths.size()))) {
        return;
    }

    setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Refreshing Knowledge Base after asset removal..."));
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

    setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Refreshing Knowledge Base after collection rename..."));
    emit renameKnowledgeCollectionRequested(collectionId, newLabel);
}

void MainWindow::onKnowledgeAssetsDropped(const QStringList &paths,
                                         const QString &targetCollectionId,
                                         const QString &targetGroupLabel)
{
    if (paths.isEmpty()) {
        return;
    }
    if (m_indexingActive || (m_stopButton != nullptr && m_stopButton->isEnabled())) {
        m_statusLabel->setText(QStringLiteral("Finish the current task before moving Knowledge Base assets."));
        return;
    }

    QStringList uniquePaths;
    for (const QString &path : paths) {
        const QString cleaned = QDir::cleanPath(path.trimmed());
        if (!cleaned.isEmpty() && !uniquePaths.contains(cleaned)) {
            uniquePaths << cleaned;
        }
    }
    if (uniquePaths.isEmpty() || targetCollectionId.trimmed().isEmpty()) {
        return;
    }
    if (!confirmKnowledgeBaseReindexAction(QStringLiteral("Move %1 Knowledge Base asset(s) to the selected collection?").arg(uniquePaths.size()))) {
        return;
    }

    setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Refreshing Knowledge Base after asset move..."));
    emit moveKnowledgeAssetsRequested(uniquePaths, targetCollectionId.trimmed(), targetGroupLabel.trimmed());
    m_statusLabel->setText(QStringLiteral("Moving %1 Knowledge Base asset(s)...").arg(uniquePaths.size()));
}

void MainWindow::onKnowledgeTreeContextMenuRequested(const QPoint &pos)
{
    if (m_sourceInventoryTree == nullptr) {
        return;
    }

    QTreeWidgetItem *item = knowledgeContextItem(&pos);
    if (item != nullptr && !item->isSelected()) {
        m_sourceInventoryTree->clearSelection();
        item->setSelected(true);
        m_sourceInventoryTree->setCurrentItem(item);
    }

    const QString nodeType = item != nullptr ? item->data(0, kKnowledgeNodeTypeRole).toString().trimmed() : QString();
    QMenu menu(this);
    QAction *createCollectionAction = menu.addAction(QStringLiteral("Create collection..."));
    QAction *addFileToCollectionAction = nullptr;
    QAction *addFolderToCollectionAction = nullptr;
    QAction *renameCollectionAction = nullptr;
    QAction *deleteCollectionAction = nullptr;
    QAction *renameAssetAction = nullptr;
    QAction *deleteAssetAction = nullptr;
    QAction *propertiesAction = nullptr;

    if (nodeType == QStringLiteral("collection")) {
        menu.addSeparator();
        addFileToCollectionAction = menu.addAction(QStringLiteral("Add file to collection..."));
        addFolderToCollectionAction = menu.addAction(QStringLiteral("Add folder to collection..."));
        menu.addSeparator();
        renameCollectionAction = menu.addAction(QStringLiteral("Rename collection..."));
        deleteCollectionAction = menu.addAction(QStringLiteral("Delete collection"));
        propertiesAction = menu.addAction(QStringLiteral("Properties..."));
    } else if (nodeType == QStringLiteral("group")) {
        menu.addSeparator();
        propertiesAction = menu.addAction(QStringLiteral("Properties..."));
    } else if (nodeType == QStringLiteral("file")) {
        menu.addSeparator();
        renameAssetAction = menu.addAction(QStringLiteral("Rename asset..."));
        deleteAssetAction = menu.addAction(QStringLiteral("Delete asset"));
        propertiesAction = menu.addAction(QStringLiteral("Properties..."));
    }

    QAction *selected = menu.exec(m_sourceInventoryTree->viewport()->mapToGlobal(pos));
    if (selected == nullptr) {
        return;
    }

    if (selected == createCollectionAction) {
        const QString label = promptForKnowledgeLabel(QStringLiteral("Create Knowledge Base collection"), QStringLiteral("Imported collection"));
        if (!label.isEmpty()) {
            setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Refreshing Knowledge Base after collection create..."));
            emit createKnowledgeCollectionRequested(label);
        }
        return;
    }

    if (item == nullptr) {
        return;
    }

    if (selected == addFileToCollectionAction || selected == addFolderToCollectionAction) {
        const QString collectionId = item->data(0, kKnowledgeCollectionIdRole).toString().trimmed();
        if (collectionId.isEmpty()) {
            return;
        }

        QStringList importPaths;
        if (selected == addFileToCollectionAction) {
            importPaths = QFileDialog::getOpenFileNames(this,
                                                        QStringLiteral("Add files to Knowledge Base collection"),
                                                        QString(),
                                                        QStringLiteral("All Files (*.*)"));
        } else {
            const QString folder = QFileDialog::getExistingDirectory(this,
                                                                     QStringLiteral("Add folder to Knowledge Base collection"));
            if (!folder.trimmed().isEmpty()) {
                importPaths << folder.trimmed();
            }
        }
        if (importPaths.isEmpty()) {
            return;
        }
        if (!confirmKnowledgeBaseReindexAction(QStringLiteral("Add %1 path(s) to collection '%2'?").arg(importPaths.size()).arg(item->text(0).trimmed()))) {
            return;
        }

        setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Adding assets to Knowledge Base collection..."));
        emit addPathsToKnowledgeCollectionRequested(importPaths, collectionId);
        return;
    }

    if (selected == renameCollectionAction) {
        onRenameSelectedKnowledgeGroupClicked();
        return;
    }

    if (selected == deleteCollectionAction) {
        const QString collectionId = item->data(0, kKnowledgeCollectionIdRole).toString().trimmed();
        const QJsonDocument propertiesDoc = QJsonDocument::fromJson(item->data(0, kKnowledgePropertiesRole).toString().toUtf8());
        const QJsonObject properties = propertiesDoc.isObject() ? propertiesDoc.object() : QJsonObject();
        const int fileCount = properties.value(QStringLiteral("fileCount")).toInt();
        if (!collectionId.isEmpty() && confirmKnowledgeBaseReindexAction(QStringLiteral("Delete collection '%1'?").arg(item->text(0)),
                                                                         QStringLiteral("This will remove %1 contained asset(s) and rebuild the Knowledge Base inventory.").arg(fileCount))) {
            setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Refreshing Knowledge Base after collection delete..."));
            emit deleteKnowledgeCollectionRequested(collectionId);
        }
        return;
    }

    if (selected == renameAssetAction) {
        const QString path = item->data(0, kKnowledgePathRole).toString().trimmed();
        if (path.isEmpty()) {
            return;
        }
        bool ok = false;
        const QString newName = QInputDialog::getText(this,
                                                      QStringLiteral("Rename Knowledge Base asset"),
                                                      QStringLiteral("Asset file name:"),
                                                      QLineEdit::Normal,
                                                      item->text(0).trimmed(),
                                                      &ok).trimmed();
        if (ok && !newName.isEmpty() && newName != item->text(0).trimmed()) {
            if (!confirmKnowledgeBaseReindexAction(QStringLiteral("Rename asset '%1' to '%2'?").arg(item->text(0).trimmed(), newName))) {
                return;
            }
            setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Refreshing Knowledge Base after asset rename..."));
            emit renameKnowledgeAssetRequested(path, newName);
        }
        return;
    }

    if (selected == deleteAssetAction) {
        const QStringList paths = selectedKnowledgeAssetPaths();
        if (paths.isEmpty()) {
            return;
        }
        if (confirmKnowledgeBaseReindexAction(QStringLiteral("Remove %1 selected Knowledge Base asset(s)?").arg(paths.size()))) {
            setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Refreshing Knowledge Base after asset removal..."));
            emit removeKnowledgeAssetsRequested(paths);
        }
        return;
    }

    if (selected == propertiesAction) {
        const QJsonDocument propertiesDoc = QJsonDocument::fromJson(item->data(0, kKnowledgePropertiesRole).toString().toUtf8());
        if (!propertiesDoc.isObject()) {
            QMessageBox::information(this,
                                     QStringLiteral("Knowledge Base properties"),
                                     QStringLiteral("Properties are not available for this item yet."));
            return;
        }
        showKnowledgePropertiesDialog(QStringLiteral("%1 properties").arg(item->text(0).trimmed()), propertiesDoc.object(), nodeType);
    }
}

void MainWindow::onClearKnowledgeBaseClicked()
{
    if (!confirmKnowledgeBaseReindexAction(QStringLiteral("Clear the entire Knowledge Base?"),
                                           QStringLiteral("This removes all imported assets from Amelia's local Knowledge Base and cannot be undone."))) {
        return;
    }

    setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Clearing Knowledge Base..."));
    emit clearKnowledgeBaseRequested();
}

void MainWindow::onDeleteConversationClicked()
{
    if (m_indexingActive || (m_stopButton != nullptr && m_stopButton->isEnabled())) {
        m_statusLabel->setText(QStringLiteral("Finish the current task before deleting a conversation."));
        return;
    }

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

void MainWindow::onVerboseDiagnosticsToggleToggled(bool checked)
{
    if (m_verboseDiagnosticsToggleButton != nullptr) {
        m_verboseDiagnosticsToggleButton->setText(checked
                                                  ? QStringLiteral("Verbose diagnostics: ON")
                                                  : QStringLiteral("Verbose diagnostics: OFF"));
    }

    if (m_verboseDiagnosticsInfoLabel != nullptr) {
        m_verboseDiagnosticsInfoLabel->setText(checked
                                               ? QStringLiteral("Enabled. Amelia will show verbose Ollama request/response summaries in the Diagnostics panel and console.")
                                               : QStringLiteral("Off by default. Amelia hides verbose request/response summaries, while still keeping essential diagnostics visible."));
    }

    emit verboseDiagnosticsToggled(checked);
}

void MainWindow::onTranscriptAnchorClicked(const QUrl &url)
{
    const auto extractIndex = [&url]() -> int {
        QString rawIndex = url.host();
        if (rawIndex.isEmpty()) {
            rawIndex = url.path();
        }
        if (rawIndex.startsWith(QLatin1Char('/'))) {
            rawIndex.remove(0, 1);
        }

        bool ok = false;
        const int index = rawIndex.toInt(&ok);
        return ok ? index : -1;
    };

    if (url.scheme() == QStringLiteral("copycode")) {
        const int index = extractIndex();
        if (index < 0 || index >= m_transcriptCodeBlocks.size()) {
            return;
        }
        QApplication::clipboard()->setText(m_transcriptCodeBlocks.at(index));
        m_statusLabel->setText(QStringLiteral("Code block copied to clipboard."));
        return;
    }

    if (url.scheme() == QStringLiteral("copyanswer")) {
        const int index = extractIndex();
        if (index < 0 || index >= m_transcriptAssistantAnswers.size()) {
            return;
        }
        QApplication::clipboard()->setText(m_transcriptAssistantAnswers.at(index));
        m_statusLabel->setText(QStringLiteral("Answer copied to clipboard."));
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
        uniqueModels << (currentModel.trimmed().isEmpty() ? QStringLiteral("gpt-oss:20b") : currentModel.trimmed());
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
    if (m_updatingConversationList || m_indexingActive || current == nullptr || (m_stopButton != nullptr && m_stopButton->isEnabled())) {
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

void MainWindow::onDeleteSelectedMemoryClicked()
{
    if (m_memoriesView == nullptr) {
        return;
    }

    const QList<QTreeWidgetItem *> selectedItems = m_memoriesView->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    QTreeWidgetItem *item = selectedItems.constFirst();
    if (item == nullptr) {
        return;
    }

    const QString memoryId = item->data(0, Qt::UserRole).toString().trimmed();
    if (memoryId.isEmpty()) {
        return;
    }

    const QString preview = item->text(2).trimmed();
    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Delete memory"),
        QStringLiteral("Delete this persisted memory?\n\n%1").arg(preview.isEmpty() ? QStringLiteral("<empty>") : preview),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    emit deleteMemoryRequested(memoryId);
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
    if (!confirmKnowledgeBaseReindexAction(QStringLiteral("Import %1 file(s) into Knowledge Base label '%2'?").arg(paths.size()).arg(label))) {
        return;
    }

    setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Importing Knowledge Base files..."));
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
    if (!confirmKnowledgeBaseReindexAction(QStringLiteral("Import folder '%1' into Knowledge Base label '%2'?").arg(QFileInfo(folder).fileName(), label))) {
        return;
    }

    setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Importing Knowledge Base folder..."));
    emit importPathsRequested(QStringList() << folder, label);
}

void MainWindow::onReindexClicked()
{
    if (!confirmKnowledgeBaseReindexAction(QStringLiteral("Start a full Knowledge Base reindex now?"),
                                           QStringLiteral("Use this when you want Amelia to rebuild chunk coverage, embeddings, and inventory metadata for the current Knowledge Base."))) {
        return;
    }

    setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Reindex requested..."));
    emit reindexRequested();
}

void MainWindow::onModelSelectionChanged(const QString &model)
{
    if (m_updatingModelList || m_indexingActive || (m_stopButton != nullptr && m_stopButton->isEnabled()) || model.trimmed().isEmpty()) {
        return;
    }
    emit backendModelSelected(model.trimmed());
}

void MainWindow::updateBusyIndicator()
{
    const QString frame = m_busyFrames.isEmpty()
            ? QStringLiteral("•")
            : m_busyFrames.at(m_busyFrameIndex % m_busyFrames.size());

    if (m_busyIndicatorLabel != nullptr && m_busyIndicatorLabel->isVisible()) {
        m_busyIndicatorLabel->setText(QStringLiteral("%1 Thinking / budgeting / retrieving / generating").arg(frame));
    }
    if (m_knowledgeInventoryRefreshVisible && m_sourceInventoryRefreshLabel != nullptr) {
        const QString base = m_knowledgeInventoryRefreshBaseLabel.trimmed().isEmpty()
                ? QStringLiteral("Refreshing Knowledge Base...")
                : m_knowledgeInventoryRefreshBaseLabel.trimmed();
        m_sourceInventoryRefreshLabel->setText(QStringLiteral("%1 %2").arg(frame, base));
    }
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
    if (!confirmKnowledgeBaseReindexAction(QStringLiteral("Import %1 Prompt Lab path(s) into Knowledge Base label '%2'?").arg(localPaths.size()).arg(label))) {
        return;
    }

    setKnowledgeInventoryRefreshVisible(true, QStringLiteral("Importing Prompt Lab assets..."));
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
#include "mainwindow.moc"
