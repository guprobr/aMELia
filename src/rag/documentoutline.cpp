#include "rag/documentoutline.h"

#include "rag/semanticchunker.h"

#include <QRegularExpression>
#include <QSet>

bool looksLikeContentsEntry(const QString &trimmed)
{
    if (trimmed.isEmpty() || trimmed.size() > 220 || isPageMarkerLine(trimmed)) {
        return false;
    }

    static const QRegularExpression dottedLeaderExpression(
            QStringLiteral(R"(^[^\n]{2,220}?\.{3,}\s*\d+$)"));
    static const QRegularExpression trailingPageNumberExpression(
            QStringLiteral(R"(^(?:\d+(?:\.\d+){0,4}|[ivxlcdmIVXLCDM]+[.)]?)?\s*[A-Za-z©][^\n]{2,200}\s\d+$)"));
    return dottedLeaderExpression.match(trimmed).hasMatch()
            || trailingPageNumberExpression.match(trimmed).hasMatch();
}

QString normalizeOutlineKey(QString text)
{
    text = text.trimmed();
    text.replace(QRegularExpression(QStringLiteral(R"(\.{2,}\s*\d+$)")), QString());
    text = text.simplified().toLower();
    return text;
}

QStringList extractDocumentOutlineLines(const QString &text, int maxLines)
{
    QStringList outline;
    QSet<QString> seen;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);

    auto addLine = [&](const QString &value) {
        const QString trimmed = value.trimmed();
        if (trimmed.isEmpty() || isPageMarkerLine(trimmed)) {
            return;
        }
        const QString key = normalizeOutlineKey(trimmed);
        if (key.isEmpty() || seen.contains(key)) {
            return;
        }
        seen.insert(key);
        outline << trimmed;
    };

    static const QRegularExpression splitNumberOnlyExpression(
            QStringLiteral(R"(^\d+(?:\.\d+){0,4}[.)]?$)"));

    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.isEmpty() || isPageMarkerLine(trimmed)) {
            continue;
        }

        if (looksLikeContentsEntry(trimmed)) {
            addLine(trimmed);
            continue;
        }

        if (splitNumberOnlyExpression.match(trimmed).hasMatch()) {
            for (int j = i + 1; j < lines.size(); ++j) {
                const QString nextTrimmed = lines.at(j).trimmed();
                if (nextTrimmed.isEmpty() || isPageMarkerLine(nextTrimmed)) {
                    continue;
                }
                if (nextTrimmed.size() <= 180
                        && !looksLikeContentsEntry(nextTrimmed)
                        && !splitNumberOnlyExpression.match(nextTrimmed).hasMatch()) {
                    addLine(trimmed + QLatin1Char(' ') + nextTrimmed);
                }
                break;
            }
            continue;
        }

        if (isHeadingLikeLine(trimmed)) {
            addLine(trimmed);
            continue;
        }

        const QString lowered = trimmed.toLower();
        if (lowered == QStringLiteral("contents")
                || lowered == QStringLiteral("table of contents")
                || lowered == QStringLiteral("document version history")
                || lowered == QStringLiteral("copyright notice")
                || lowered == QStringLiteral("corporate headquarters")) {
            addLine(trimmed);
        }
    }

    if (maxLines > 0 && outline.size() > maxLines) {
        return outline.mid(0, maxLines);
    }
    return outline;
}

QString balancedTrimForStudy(QString text, int maxChars)
{
    text = text.trimmed();
    if (maxChars <= 0 || text.size() <= maxChars) {
        return text;
    }

    const QString marker = QStringLiteral("\n[... middle omitted for budget ...]\n");
    const int markerChars = marker.size();
    const int remaining = qMax(0, maxChars - markerChars);
    const int headChars = qMax(0, static_cast<int>(remaining * 0.62));
    const int tailChars = qMax(0, remaining - headChars);
    return text.left(headChars).trimmed() + marker + text.right(tailChars).trimmed();
}

QString stripTrailingOutlinePageNumber(QString text)
{
    text = text.trimmed();
    text.remove(QRegularExpression(QStringLiteral(R"(\.{2,}\s*\d+$)")));
    text.remove(QRegularExpression(QStringLiteral(R"(\s+\d+$)")));
    return text.trimmed();
}

bool isTopLevelHeadingText(const QString &trimmed)
{
    static const QRegularExpression topLevelHeadingExpression(
            QStringLiteral(R"(^\d+[.)]?\s+\S+)"));
    return topLevelHeadingExpression.match(trimmed).hasMatch();
}

bool isTopLevelNumberOnlyLine(const QString &trimmed)
{
    static const QRegularExpression topLevelNumberOnlyExpression(
            QStringLiteral(R"(^\d+[.)]?$)"));
    return topLevelNumberOnlyExpression.match(trimmed).hasMatch();
}

QStringList extractMajorSectionHeadings(const QString &text, int maxSections)
{
    QStringList headings;
    QSet<QString> seen;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);

    auto addHeading = [&](QString value) {
        value = stripTrailingOutlinePageNumber(value);
        const QString normalized = normalizeOutlineKey(value);
        if (normalized.isEmpty() || seen.contains(normalized)) {
            return;
        }
        seen.insert(normalized);
        headings << value.trimmed();
    };

    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.isEmpty() || isPageMarkerLine(trimmed)) {
            continue;
        }

        const QString lowered = trimmed.toLower();
        if (lowered == QStringLiteral("contents")
                || lowered == QStringLiteral("table of contents")
                || lowered == QStringLiteral("document version history")
                || lowered == QStringLiteral("copyright notice")
                || lowered == QStringLiteral("corporate headquarters")) {
            addHeading(trimmed);
            continue;
        }

        if (looksLikeContentsEntry(trimmed)) {
            const QString cleaned = stripTrailingOutlinePageNumber(trimmed);
            if (isTopLevelHeadingText(cleaned)) {
                addHeading(cleaned);
            }
            continue;
        }

        if (isTopLevelNumberOnlyLine(trimmed)) {
            for (int j = i + 1; j < lines.size(); ++j) {
                const QString nextTrimmed = lines.at(j).trimmed();
                if (nextTrimmed.isEmpty() || isPageMarkerLine(nextTrimmed)) {
                    continue;
                }
                if (!looksLikeContentsEntry(nextTrimmed)
                        && !isTopLevelNumberOnlyLine(nextTrimmed)
                        && !nextTrimmed.startsWith(QLatin1String("["))) {
                    addHeading(trimmed + QLatin1Char(' ') + nextTrimmed);
                }
                break;
            }
            continue;
        }

        if (isTopLevelHeadingText(trimmed)) {
            addHeading(trimmed);
        }
    }

    if (maxSections > 0 && headings.size() > maxSections) {
        return headings.mid(0, maxSections);
    }
    return headings;
}
