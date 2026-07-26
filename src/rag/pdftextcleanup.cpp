#include "rag/pdftextcleanup.h"

#include "rag/cancellation.h"
#include "rag/textcleanup.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>

QString stripRepeatedPdfBoilerplate(const QString &text, const std::atomic_bool *cancelRequested)
{
    const QStringList rawPages = text.split(QChar('\f'), Qt::KeepEmptyParts);
    if (rawPages.size() < 3) {
        return text;
    }

    QHash<QString, int> headerCounts;
    QHash<QString, int> footerCounts;
    QVector<QStringList> pageLines;
    pageLines.reserve(rawPages.size());

    for (const QString &rawPage : rawPages) {
        if (isCancelRequested(cancelRequested)) {
            return QString();
        }
        const QString normalizedPage = collapseExcessBlankLines(trimTrailingWhitespacePerLine(rawPage)).trimmed();
        if (normalizedPage.isEmpty()) {
            pageLines.push_back({});
            continue;
        }

        QStringList lines = normalizedPage.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        for (QString &line : lines) {
            line = line.trimmed();
        }
        while (!lines.isEmpty() && lines.constFirst().isEmpty()) {
            lines.removeFirst();
        }
        while (!lines.isEmpty() && lines.constLast().isEmpty()) {
            lines.removeLast();
        }

        pageLines.push_back(lines);
        const int probeCount = qMin(3, lines.size());
        for (int i = 0; i < probeCount; ++i) {
            const QString line = lines.at(i).trimmed();
            if (!line.isEmpty() && line.size() <= 180) {
                ++headerCounts[line];
            }
        }
        for (int i = 0; i < probeCount; ++i) {
            const QString line = lines.at(lines.size() - 1 - i).trimmed();
            if (!line.isEmpty() && line.size() <= 180) {
                ++footerCounts[line];
            }
        }
    }

    const int repetitionThreshold = qMax(2, rawPages.size() / 3);
    QSet<QString> repeatedHeaders;
    QSet<QString> repeatedFooters;
    for (auto it = headerCounts.cbegin(); it != headerCounts.cend(); ++it) {
        if (it.value() >= repetitionThreshold) {
            repeatedHeaders.insert(it.key());
        }
    }
    for (auto it = footerCounts.cbegin(); it != footerCounts.cend(); ++it) {
        if (it.value() >= repetitionThreshold) {
            repeatedFooters.insert(it.key());
        }
    }

    QStringList cleanedPages;
    cleanedPages.reserve(pageLines.size());
    for (const QStringList &page : std::as_const(pageLines)) {
        if (isCancelRequested(cancelRequested)) {
            return QString();
        }
        QStringList cleanedLines = page;
        while (!cleanedLines.isEmpty() && repeatedHeaders.contains(cleanedLines.constFirst().trimmed())) {
            cleanedLines.removeFirst();
        }
        while (!cleanedLines.isEmpty() && repeatedFooters.contains(cleanedLines.constLast().trimmed())) {
            cleanedLines.removeLast();
        }
        cleanedPages << cleanedLines.join(QStringLiteral("\n")).trimmed();
    }

    return cleanedPages.join(QStringLiteral("\f"));
}

QString cleanedPdfText(QString text, const std::atomic_bool *cancelRequested)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QChar(0x00A0), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QStringLiteral("    "));
    text.replace(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]")), QStringLiteral(" "));
    if (isCancelRequested(cancelRequested)) {
        return QString();
    }
    text = stripRepeatedPdfBoilerplate(text, cancelRequested);
    if (isCancelRequested(cancelRequested)) {
        return QString();
    }

    const QStringList rawPages = text.split(QChar('\f'), Qt::KeepEmptyParts);
    QStringList pages;
    pages.reserve(rawPages.size());
    int pageNumber = 0;
    for (const QString &rawPage : rawPages) {
        if (isCancelRequested(cancelRequested)) {
            return QString();
        }
        const QString normalizedPage = collapseExcessBlankLines(trimTrailingWhitespacePerLine(rawPage)).trimmed();
        if (normalizedPage.isEmpty()) {
            continue;
        }
        ++pageNumber;
        pages << QStringLiteral("[[PAGE %1]]\n%2").arg(pageNumber).arg(normalizedPage);
    }

    if (pages.isEmpty()) {
        return cleanedText(text);
    }

    return pages.join(QStringLiteral("\n\n"));
}
