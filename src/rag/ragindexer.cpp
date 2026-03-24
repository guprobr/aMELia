#include "rag/ragindexer.h"

#include "rag/embeddingclient.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <utility>

namespace {
bool isCancelRequested(const std::atomic_bool *cancelRequested);

QString detectSourceType(const QFileInfo &info)
{
    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("log")) {
        return QStringLiteral("log");
    }
    if (suffix == QStringLiteral("yaml") || suffix == QStringLiteral("yml") || suffix == QStringLiteral("json")
        || suffix == QStringLiteral("xml") || suffix == QStringLiteral("ini") || suffix == QStringLiteral("cfg")
        || suffix == QStringLiteral("conf") || suffix == QStringLiteral("service")) {
        return QStringLiteral("config");
    }
    if (suffix == QStringLiteral("md") || suffix == QStringLiteral("txt") || suffix == QStringLiteral("pdf") || suffix == QStringLiteral("csv")) {
        return QStringLiteral("doc");
    }
    if (suffix == QStringLiteral("cpp") || suffix == QStringLiteral("cxx") || suffix == QStringLiteral("cc")
        || suffix == QStringLiteral("c") || suffix == QStringLiteral("h") || suffix == QStringLiteral("hpp")
        || suffix == QStringLiteral("py") || suffix == QStringLiteral("sh") || suffix == QStringLiteral("cmake")) {
        return QStringLiteral("code");
    }
    return QStringLiteral("misc");
}

QString detectSourceRole(const QFileInfo &info, const QString &sourceType, const QString &text)
{
    const QString fingerprint = (info.fileName() + QLatin1Char(' ') + info.filePath() + QLatin1Char(' ') + text.left(4000)).toLower();

    if (sourceType == QStringLiteral("log") || fingerprint.contains(QStringLiteral("alarm")) || fingerprint.contains(QStringLiteral("traceback"))) {
        return QStringLiteral("log");
    }
    if (sourceType == QStringLiteral("config")) {
        return QStringLiteral("config");
    }
    if (sourceType == QStringLiteral("code")) {
        return QStringLiteral("code");
    }

    if (fingerprint.contains(QStringLiteral("hld")) || fingerprint.contains(QStringLiteral("lld"))
            || fingerprint.contains(QStringLiteral("topology")) || fingerprint.contains(QStringLiteral("architecture"))
            || fingerprint.contains(QStringLiteral("design")) || fingerprint.contains(QStringLiteral("scenario"))
            || fingerprint.contains(QStringLiteral("ciq"))) {
        return QStringLiteral("scenario");
    }

    if (fingerprint.contains(QStringLiteral("runbook")) || fingerprint.contains(QStringLiteral("mop"))
            || fingerprint.contains(QStringLiteral("operator training")) || fingerprint.contains(QStringLiteral("training manual"))
            || fingerprint.contains(QStringLiteral("procedure")) || fingerprint.contains(QStringLiteral("deployment"))
            || fingerprint.contains(QStringLiteral("install")) || fingerprint.contains(QStringLiteral("bootstrap"))
            || fingerprint.contains(QStringLiteral("workflow"))) {
        return QStringLiteral("procedure");
    }

    if (fingerprint.contains(QStringLiteral("guide")) || fingerprint.contains(QStringLiteral("manual"))
            || fingerprint.contains(QStringLiteral("reference")) || fingerprint.contains(QStringLiteral("concatenated"))
            || fingerprint.contains(QStringLiteral("release notes")) || fingerprint.contains(QStringLiteral("overview"))) {
        return QStringLiteral("reference");
    }

    return sourceType == QStringLiteral("doc") ? QStringLiteral("reference") : sourceType;
}

QStringList extensions()
{
    return {
        QStringLiteral("*.txt"), QStringLiteral("*.md"), QStringLiteral("*.log"), QStringLiteral("*.yaml"),
        QStringLiteral("*.yml"), QStringLiteral("*.json"), QStringLiteral("*.ini"), QStringLiteral("*.cfg"),
        QStringLiteral("*.conf"), QStringLiteral("*.xml"), QStringLiteral("*.csv"), QStringLiteral("*.service"),
        QStringLiteral("*.sh"), QStringLiteral("*.py"), QStringLiteral("*.c"), QStringLiteral("*.cc"),
        QStringLiteral("*.cpp"), QStringLiteral("*.cxx"), QStringLiteral("*.h"), QStringLiteral("*.hpp"),
        QStringLiteral("*.cmake"), QStringLiteral("*.pdf")
    };
}

QString trimTrailingWhitespacePerLine(const QString &text)
{
    QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (QString &line : lines) {
        while (!line.isEmpty() && (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t')))) {
            line.chop(1);
        }
    }
    return lines.join(QStringLiteral("\n"));
}

QString collapseExcessBlankLines(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("\\n{4,}")), QStringLiteral("\n\n\n"));
    return text;
}

QString cleanedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QChar(0x00A0), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QStringLiteral("    "));
    text.replace(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]")), QStringLiteral(" "));
    text = trimTrailingWhitespacePerLine(text);
    text = collapseExcessBlankLines(text);
    return text.trimmed();
}

QString stripRepeatedPdfBoilerplate(const QString &text, const std::atomic_bool *cancelRequested = nullptr)
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

QString cleanedPdfText(QString text, const std::atomic_bool *cancelRequested = nullptr)
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

bool isPageMarkerLine(const QString &trimmed)
{
    return trimmed.startsWith(QStringLiteral("[[PAGE ")) && trimmed.endsWith(QStringLiteral("]]"));
}

bool isFenceDelimiter(const QString &trimmed)
{
    return trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~"));
}

bool isHeadingLikeLine(const QString &trimmed)
{
    if (trimmed.startsWith(QLatin1Char('#'))) {
        return true;
    }
    static const QRegularExpression numberedHeading(QStringLiteral(R"(^\d+(?:\.\d+){0,4}[.)]?\s+\S+)"));
    static const QRegularExpression titledHeading(QStringLiteral(R"(^[A-Z][A-Za-z0-9 _/().:+-]{2,80}:$)"));
    return numberedHeading.match(trimmed).hasMatch() || titledHeading.match(trimmed).hasMatch();
}

bool isBulletLine(const QString &trimmed)
{
    static const QRegularExpression bulletExpression(QStringLiteral(R"(^(?:[-*+]\s+|\d+[.)]\s+|[a-zA-Z][.)]\s+))"));
    return bulletExpression.match(trimmed).hasMatch();
}

bool isStructuredCodeLikeLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    if (line.startsWith(QStringLiteral("    ")) || line.startsWith(QLatin1Char('\t'))) {
        return true;
    }
    return trimmed.startsWith(QLatin1Char('$'))
            || trimmed.startsWith(QLatin1Char('#'))
            || trimmed.contains(QStringLiteral("::"))
            || trimmed.contains(QStringLiteral("=>"))
            || trimmed.contains(QLatin1Char('{'))
            || trimmed.contains(QLatin1Char('}'))
            || trimmed.contains(QLatin1Char('='));
}

QString normalizeBlockText(const QString &text)
{
    return collapseExcessBlankLines(trimTrailingWhitespacePerLine(text)).trimmed();
}

int countWordsInText(const QString &text)
{
    int words = 0;
    bool inWord = false;
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber()) {
            if (!inWord) {
                ++words;
                inWord = true;
            }
        } else {
            inWord = false;
        }
    }
    return words;
}

int countLinesInText(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return 0;
    }
    return text.count(QLatin1Char('\n')) + 1;
}

QString compactPreviewText(QString text, int maxChars)
{
    text = normalizeBlockText(text);
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(qMax(80, maxChars)).trimmed() + QStringLiteral(" …");
}

struct ChunkingProfile {
    int targetChunkChars = 1400;
    int minimumChunkChars = 650;
    int hardChunkChars = 1800;
    int overlapChars = 140;
    QString label;
};

ChunkingProfile chooseChunkingProfile(const QString &sourceType,
                                     qint64 fileSizeBytes,
                                     bool semanticReady,
                                     int blockCount)
{
    ChunkingProfile profile;
    if (sourceType == QStringLiteral("code") || sourceType == QStringLiteral("config")) {
        profile.targetChunkChars = semanticReady ? 1000 : 2100;
        profile.minimumChunkChars = semanticReady ? 380 : 780;
        profile.hardChunkChars = semanticReady ? 1300 : 2850;
        profile.overlapChars = semanticReady ? 90 : 220;
        profile.label = semanticReady ? QStringLiteral("code-compact-semantic") : QStringLiteral("code-wide-lexical");
    } else if (sourceType == QStringLiteral("log")) {
        profile.targetChunkChars = semanticReady ? 1050 : 2200;
        profile.minimumChunkChars = semanticReady ? 420 : 900;
        profile.hardChunkChars = semanticReady ? 1350 : 3000;
        profile.overlapChars = semanticReady ? 90 : 220;
        profile.label = semanticReady ? QStringLiteral("log-compact-semantic") : QStringLiteral("log-wide-lexical");
    } else {
        profile.targetChunkChars = semanticReady ? 1200 : 2400;
        profile.minimumChunkChars = semanticReady ? 520 : 1100;
        profile.hardChunkChars = semanticReady ? 1500 : 3200;
        profile.overlapChars = semanticReady ? 110 : 360;
        profile.label = semanticReady ? QStringLiteral("doc-balanced-semantic") : QStringLiteral("doc-wide-lexical");
    }

    const bool largeFile = fileSizeBytes >= 1024 * 1024;
    const bool veryLargeFile = fileSizeBytes >= 4 * 1024 * 1024 || blockCount >= 450;
    if (largeFile) {
        profile.targetChunkChars += semanticReady ? 120 : 300;
        profile.minimumChunkChars += semanticReady ? 80 : 140;
        profile.hardChunkChars += semanticReady ? 180 : 500;
        profile.overlapChars += semanticReady ? 20 : 60;
        profile.label += QStringLiteral("-large");
    }
    if (veryLargeFile) {
        profile.targetChunkChars += semanticReady ? 80 : 300;
        profile.minimumChunkChars += semanticReady ? 60 : 140;
        profile.hardChunkChars += semanticReady ? 120 : 450;
        profile.overlapChars += semanticReady ? 15 : 50;
        profile.label += QStringLiteral("-xlarge");
    }
    return profile;
}

QStringList splitOversizedBlock(const QString &block, int maxBlockChars, const std::atomic_bool *cancelRequested = nullptr)
{
    const QString normalized = normalizeBlockText(block);
    if (normalized.isEmpty()) {
        return {};
    }
    if (normalized.size() <= maxBlockChars) {
        return {normalized};
    }

    QStringList parts;
    int offset = 0;
    while (offset < normalized.size()) {
        if (isCancelRequested(cancelRequested)) {
            return {};
        }
        const int remaining = normalized.size() - offset;
        if (remaining <= maxBlockChars) {
            parts << normalized.mid(offset).trimmed();
            break;
        }

        const int hardEnd = qMin(normalized.size(), offset + maxBlockChars);
        const int minimumSplit = offset + qMax(600, maxBlockChars / 2);
        int split = normalized.lastIndexOf(QStringLiteral("\n\n"), hardEnd);
        if (split < minimumSplit) {
            split = normalized.lastIndexOf(QLatin1Char('\n'), hardEnd);
        }
        if (split < minimumSplit) {
            split = normalized.lastIndexOf(QLatin1Char(' '), hardEnd);
        }
        if (split < minimumSplit) {
            split = hardEnd;
        }

        const QString piece = normalized.mid(offset, split - offset).trimmed();
        if (!piece.isEmpty()) {
            parts << piece;
        }
        offset = split;
        while (offset < normalized.size() && normalized.at(offset).isSpace()) {
            ++offset;
        }
    }

    return parts;
}

QVector<QString> buildSemanticBlocks(const QString &text, const std::atomic_bool *cancelRequested = nullptr)
{
    QVector<QString> blocks;
    if (text.trimmed().isEmpty()) {
        return blocks;
    }

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    QStringList currentLines;
    QString pendingPrefix;
    bool inFence = false;

    auto flushCurrent = [&]() {
        if (currentLines.isEmpty()) {
            return;
        }
        const QString block = normalizeBlockText(currentLines.join(QStringLiteral("\n")));
        if (!block.isEmpty()) {
            const QStringList pieces = splitOversizedBlock(block, 1800, cancelRequested);
            for (const QString &piece : pieces) {
                if (!piece.trimmed().isEmpty()) {
                    blocks.push_back(piece.trimmed());
                }
            }
        }
        currentLines.clear();
    };

    for (const QString &line : lines) {
        if (isCancelRequested(cancelRequested)) {
            return {};
        }
        const QString trimmed = line.trimmed();

        if (isPageMarkerLine(trimmed)) {
            flushCurrent();
            pendingPrefix = trimmed;
            continue;
        }

        if (isFenceDelimiter(trimmed)) {
            if (!inFence) {
                flushCurrent();
                if (!pendingPrefix.isEmpty()) {
                    currentLines << pendingPrefix;
                    pendingPrefix.clear();
                }
            }
            currentLines << line;
            inFence = !inFence;
            if (!inFence) {
                flushCurrent();
            }
            continue;
        }

        if (inFence) {
            currentLines << line;
            continue;
        }

        if (trimmed.isEmpty()) {
            flushCurrent();
            continue;
        }

        if (isHeadingLikeLine(trimmed)) {
            flushCurrent();
            QString headingBlock = trimmed;
            if (!pendingPrefix.isEmpty()) {
                headingBlock.prepend(pendingPrefix + QStringLiteral("\n"));
                pendingPrefix.clear();
            }
            blocks.push_back(headingBlock.trimmed());
            continue;
        }

        const bool startsStructuredBlock = isBulletLine(trimmed) || isStructuredCodeLikeLine(line);
        if (startsStructuredBlock && !currentLines.isEmpty()) {
            const QString previousTrimmed = currentLines.constLast().trimmed();
            const bool previousStructured = isBulletLine(previousTrimmed) || isStructuredCodeLikeLine(currentLines.constLast());
            if (!previousStructured) {
                flushCurrent();
            }
        }

        if (!pendingPrefix.isEmpty() && currentLines.isEmpty()) {
            currentLines << pendingPrefix;
            pendingPrefix.clear();
        }
        currentLines << line;
    }

    flushCurrent();
    if (!pendingPrefix.isEmpty()) {
        blocks.push_back(pendingPrefix);
    }
    return blocks;
}

int totalBlockChars(const QStringList &blocks)
{
    int total = 0;
    for (const QString &block : blocks) {
        total += block.size();
    }
    if (blocks.size() > 1) {
        total += (blocks.size() - 1) * 2;
    }
    return total;
}

QStringList overlapTailBlocks(const QStringList &blocks, int overlapChars, const std::atomic_bool *cancelRequested = nullptr)
{
    QStringList carry;
    int carryChars = 0;
    for (int i = blocks.size() - 1; i >= 0; --i) {
        if (isCancelRequested(cancelRequested)) {
            return {};
        }
        const QString &block = blocks.at(i);
        const int blockChars = block.size() + (carry.isEmpty() ? 0 : 2);
        if (!carry.isEmpty() && carryChars + blockChars > overlapChars * 2) {
            break;
        }
        carry.prepend(block);
        carryChars += blockChars;
        if (carryChars >= overlapChars) {
            break;
        }
    }
    return carry;
}

QVector<QString> buildChunksFromBlocks(const QVector<QString> &blocks,
                                      const ChunkingProfile &profile,
                                      const std::atomic_bool *cancelRequested = nullptr)
{
    QVector<QString> chunks;
    if (blocks.isEmpty()) {
        return chunks;
    }

    QStringList currentBlocks;
    int currentChars = 0;

    auto flushChunk = [&]() {
        if (currentBlocks.isEmpty()) {
            return;
        }
        const QString chunk = normalizeBlockText(currentBlocks.join(QStringLiteral("\n\n")));
        if (!chunk.isEmpty()) {
            chunks.push_back(chunk);
        }
    };

    for (const QString &block : blocks) {
        if (isCancelRequested(cancelRequested)) {
            return {};
        }
        if (block.trimmed().isEmpty()) {
            continue;
        }

        const int separatorChars = currentBlocks.isEmpty() ? 0 : 2;
        const int projected = currentChars + separatorChars + block.size();
        const bool shouldSplit = !currentBlocks.isEmpty()
                && projected > profile.targetChunkChars
                && currentChars >= profile.minimumChunkChars;
        const bool mustSplit = !currentBlocks.isEmpty() && projected > profile.hardChunkChars;
        if (shouldSplit || mustSplit) {
            const QStringList carry = overlapTailBlocks(currentBlocks, profile.overlapChars, cancelRequested);
            flushChunk();
            currentBlocks = carry;
            currentChars = totalBlockChars(currentBlocks);
        }

        if (block.size() > profile.hardChunkChars && currentBlocks.isEmpty()) {
            const QStringList slices = splitOversizedBlock(block, profile.targetChunkChars, cancelRequested);
            for (int sliceIndex = 0; sliceIndex < slices.size(); ++sliceIndex) {
                const QString &slice = slices.at(sliceIndex);
                if (slice.trimmed().isEmpty()) {
                    continue;
                }
                currentBlocks << slice;
                currentChars = totalBlockChars(currentBlocks);
                const bool lastSlice = sliceIndex == slices.size() - 1;
                if (!lastSlice) {
                    flushChunk();
                    currentBlocks = overlapTailBlocks(currentBlocks, profile.overlapChars, cancelRequested);
                    currentChars = totalBlockChars(currentBlocks);
                }
            }
            continue;
        }

        currentBlocks << block;
        currentChars = totalBlockChars(currentBlocks);
    }

    flushChunk();

    if (chunks.size() >= 2 && chunks.constLast().size() < profile.minimumChunkChars / 2) {
        const QString merged = normalizeBlockText(chunks.at(chunks.size() - 2) + QStringLiteral("\n\n") + chunks.constLast());
        chunks[chunks.size() - 2] = merged;
        chunks.removeLast();
    }

    return chunks;
}

bool shouldKeepChunkText(const QString &text)
{
    const QString simplified = text.simplified();
    if (simplified.isEmpty()) {
        return false;
    }

    if (simplified.size() >= 80) {
        return true;
    }

    int alnumCount = 0;
    int wordCount = 0;
    bool inWord = false;
    for (const QChar ch : simplified) {
        if (ch.isLetterOrNumber()) {
            ++alnumCount;
            if (!inWord) {
                ++wordCount;
                inWord = true;
            }
        } else {
            inWord = false;
        }
    }

    const bool structuredShortChunk = text.contains(QLatin1Char('\n'))
            && (text.contains(QLatin1Char('{'))
                || text.contains(QLatin1Char('='))
                || text.contains(QStringLiteral(":"))
                || text.contains(QStringLiteral("->")));
    if (structuredShortChunk) {
        return alnumCount >= 18 && wordCount >= 3;
    }

    return alnumCount >= 32 && wordCount >= 6;
}

bool isCancelRequested(const std::atomic_bool *cancelRequested)
{
    return cancelRequested != nullptr && cancelRequested->load(std::memory_order_relaxed);
}

bool readTextFile(const QString &path, QString *text, QString *extractor, std::atomic_bool *cancelRequested)
{
    QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("pdf")) {
        auto runPdfToText = [&](const QStringList &arguments, QString *output) -> bool {
            QProcess process;
            process.start(QStringLiteral("pdftotext"), arguments);
            if (!process.waitForStarted(1500)) {
                return false;
            }
            while (!process.waitForFinished(150)) {
                if (isCancelRequested(cancelRequested)) {
                    process.kill();
                    process.waitForFinished(1000);
                    return false;
                }
            }
            if (isCancelRequested(cancelRequested)) {
                process.kill();
                process.waitForFinished(1000);
                return false;
            }
            if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
                return false;
            }
            if (output != nullptr) {
                *output = QString::fromUtf8(process.readAllStandardOutput());
            }
            return true;
        };

        QString extractedText;
        bool extracted = runPdfToText({QStringLiteral("-enc"), QStringLiteral("UTF-8"), QStringLiteral("-layout"), path, QStringLiteral("-")},
                                      &extractedText);
        QString extractorName = QStringLiteral("pdf:pdftotext-layout-paged");
        if (extracted) {
            const QString normalized = cleanedPdfText(extractedText, cancelRequested);
            const int wordCount = countWordsInText(normalized);
            if (wordCount < 80) {
                QString fallbackText;
                if (runPdfToText({QStringLiteral("-enc"), QStringLiteral("UTF-8"), QStringLiteral("-raw"), path, QStringLiteral("-")},
                                 &fallbackText)) {
                    const QString fallbackNormalized = cleanedPdfText(fallbackText, cancelRequested);
                    if (countWordsInText(fallbackNormalized) > wordCount) {
                        extractedText = fallbackText;
                        extractorName = QStringLiteral("pdf:pdftotext-raw-paged");
                    }
                }
            }
            if (text != nullptr) {
                *text = cleanedPdfText(extractedText, cancelRequested);
            }
            if (extractor != nullptr) {
                *extractor = extractorName;
            }
            return true;
        }

        if (extractor != nullptr) {
            *extractor = isCancelRequested(cancelRequested)
                    ? QStringLiteral("canceled")
                    : QStringLiteral("pdf:pdftotext-failed");
        }
        return false;
    }

    if (isCancelRequested(cancelRequested)) {
        if (extractor != nullptr) {
            *extractor = QStringLiteral("canceled");
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (extractor != nullptr) {
            *extractor = QStringLiteral("read-failed");
        }
        return false;
    }

    QByteArray bytes;
    bytes.reserve(static_cast<int>(qMin<qint64>(info.size(), 4 * 1024 * 1024)));
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(256 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            file.close();
            if (extractor != nullptr) {
                *extractor = QStringLiteral("read-failed");
            }
            return false;
        }
        bytes.append(chunk);
        if (isCancelRequested(cancelRequested)) {
            file.close();
            if (extractor != nullptr) {
                *extractor = QStringLiteral("canceled");
            }
            return false;
        }
    }
    file.close();

    if (bytes.contains('\0')) {
        if (extractor != nullptr) {
            *extractor = QStringLiteral("binary-skipped");
        }
        return false;
    }

    if (text != nullptr) {
        *text = cleanedText(QString::fromUtf8(bytes));
    }
    if (extractor != nullptr) {
        *extractor = QStringLiteral("direct");
    }
    return true;
}

QString fallbackZeroChunkReason(const QString &extractor, int textCharCount, int wordCount)
{
    const QString normalizedExtractor = extractor.trimmed().toLower();
    if (normalizedExtractor == QStringLiteral("canceled")) {
        return QStringLiteral("Indexing was canceled before this asset finished chunk generation.");
    }
    if (normalizedExtractor == QStringLiteral("binary-skipped")) {
        return QStringLiteral("The file appears to be binary or otherwise unsupported for text chunking.");
    }
    if (normalizedExtractor == QStringLiteral("read-failed")) {
        return QStringLiteral("Amelia could not read the file contents from disk.");
    }
    if (normalizedExtractor == QStringLiteral("pdf:pdftotext-failed")) {
        return QStringLiteral("PDF text extraction failed. The document may be scanned, image-only, encrypted, or unsupported by pdftotext.");
    }
    if (normalizedExtractor.startsWith(QStringLiteral("pdf:")) && textCharCount <= 0) {
        return QStringLiteral("PDF extraction produced no usable text after cleanup. The document may be scanned or image-only.");
    }
    if (textCharCount <= 0 || wordCount <= 0) {
        return QStringLiteral("Text extraction finished, but no usable text remained after cleanup.");
    }
    return QStringLiteral("The asset was ingested, but no chunk survived filtering or deduplication.");
}

QString canonicalPathFor(const QString &path)
{
    const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    if (cleaned.isEmpty()) {
        return QString();
    }

    QFileInfo info(cleaned);
    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
        canonical = info.absoluteFilePath();
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(canonical));
}

QString manifestPathForRoot(const QString &destinationRoot)
{
    return QDir(destinationRoot).filePath(QStringLiteral(".amelia_kb_manifest.json"));
}

QString collectionsRootFor(const QString &destinationRoot)
{
    return QDir(destinationRoot).filePath(QStringLiteral("collections"));
}

QString stableHashHex(const QString &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha1).toHex());
}

bool computeFileContentHash(const QString &path,
                            QString *hashHex,
                            std::atomic_bool *cancelRequested = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(256 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            file.close();
            return false;
        }
        if (!chunk.isEmpty()) {
            hash.addData(chunk);
        }
        if (isCancelRequested(cancelRequested)) {
            file.close();
            return false;
        }
    }
    file.close();

    if (hashHex != nullptr) {
        *hashHex = QString::fromLatin1(hash.result().toHex());
    }
    return true;
}

QString sanitizeLabelComponent(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9._-]+)")), QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral(R"(_{2,})")), QStringLiteral("_"));
    value.remove(QRegularExpression(QStringLiteral(R"(^[_\.]+|[_\.]+$)")));
    if (value.isEmpty()) {
        return QStringLiteral("item");
    }
    return value;
}

QString groupLabelFromRelativePath(const QString &relativePath)
{
    const QString relativeDir = QFileInfo(relativePath).path();
    if (relativeDir == QStringLiteral(".") || relativeDir.trimmed().isEmpty()) {
        return QStringLiteral("(root)");
    }
    return QDir::fromNativeSeparators(relativeDir);
}

QString ensureUniqueRelativePath(QString relativePath, QSet<QString> *usedRelativePaths)
{
    if (usedRelativePaths == nullptr) {
        return QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
    }

    relativePath = QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
    if (!usedRelativePaths->contains(relativePath)) {
        usedRelativePaths->insert(relativePath);
        return relativePath;
    }

    QFileInfo info(relativePath);
    const QString dirPath = info.path() == QStringLiteral(".") ? QString() : info.path();
    const QString baseName = info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
    const QString suffix = info.suffix();

    int attempt = 1;
    while (true) {
        const QString candidateName = suffix.isEmpty()
                ? QStringLiteral("%1_%2").arg(baseName).arg(attempt)
                : QStringLiteral("%1_%2.%3").arg(baseName).arg(attempt).arg(suffix);
        const QString candidate = dirPath.isEmpty()
                ? candidateName
                : QDir::cleanPath(dirPath + QLatin1Char('/') + candidateName);
        if (!usedRelativePaths->contains(candidate)) {
            usedRelativePaths->insert(candidate);
            return candidate;
        }
        ++attempt;
    }
}

QString standaloneRelativePathFor(const QFileInfo &info)
{
    const QString parentPath = canonicalPathFor(info.absolutePath());
    const QString parentName = sanitizeLabelComponent(QFileInfo(parentPath).fileName());
    const QString parentHash = stableHashHex(parentPath).left(10);
    return QDir::cleanPath(QStringLiteral("%1_%2/%3").arg(parentName, parentHash, info.fileName()));
}

struct ManifestEntry {
    QString internalPath;
    QString relativePath;
    QString originalPath;
    QString groupId;
    QString groupLabel;
};

struct ManifestCollection {
    QString collectionId;
    QString label;
    QString createdAt;
    QVector<ManifestEntry> entries;
};

QJsonObject manifestEntryToJson(const ManifestEntry &entry)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("internalPath"), entry.internalPath);
    obj.insert(QStringLiteral("relativePath"), entry.relativePath);
    obj.insert(QStringLiteral("originalPath"), entry.originalPath);
    obj.insert(QStringLiteral("groupId"), entry.groupId);
    obj.insert(QStringLiteral("groupLabel"), entry.groupLabel);
    return obj;
}

ManifestEntry manifestEntryFromJson(const QJsonObject &obj)
{
    ManifestEntry entry;
    entry.internalPath = obj.value(QStringLiteral("internalPath")).toString();
    entry.relativePath = obj.value(QStringLiteral("relativePath")).toString();
    entry.originalPath = obj.value(QStringLiteral("originalPath")).toString();
    entry.groupId = obj.value(QStringLiteral("groupId")).toString();
    entry.groupLabel = obj.value(QStringLiteral("groupLabel")).toString();
    return entry;
}

QJsonObject manifestCollectionToJson(const ManifestCollection &collection)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("collectionId"), collection.collectionId);
    obj.insert(QStringLiteral("label"), collection.label);
    obj.insert(QStringLiteral("createdAt"), collection.createdAt);

    QJsonArray entries;
    for (const ManifestEntry &entry : collection.entries) {
        entries.push_back(manifestEntryToJson(entry));
    }
    obj.insert(QStringLiteral("entries"), entries);
    return obj;
}

ManifestCollection manifestCollectionFromJson(const QJsonObject &obj)
{
    ManifestCollection collection;
    collection.collectionId = obj.value(QStringLiteral("collectionId")).toString();
    collection.label = obj.value(QStringLiteral("label")).toString();
    collection.createdAt = obj.value(QStringLiteral("createdAt")).toString();

    const QJsonArray entries = obj.value(QStringLiteral("entries")).toArray();
    collection.entries.reserve(entries.size());
    for (const QJsonValue &value : entries) {
        if (!value.isObject()) {
            continue;
        }
        const ManifestEntry entry = manifestEntryFromJson(value.toObject());
        if (!entry.internalPath.isEmpty()) {
            collection.entries.push_back(entry);
        }
    }
    return collection;
}

QVector<ManifestCollection> loadManifestCollections(const QString &destinationRoot)
{
    QVector<ManifestCollection> collections;
    QFile file(manifestPathForRoot(destinationRoot));
    if (!file.exists()) {
        return collections;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return collections;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return collections;
    }

    const QJsonArray array = doc.object().value(QStringLiteral("collections")).toArray();
    collections.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const ManifestCollection collection = manifestCollectionFromJson(value.toObject());
        if (!collection.collectionId.isEmpty()) {
            collections.push_back(collection);
        }
    }
    return collections;
}

bool saveManifestCollections(const QString &destinationRoot, const QVector<ManifestCollection> &collections)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("amelia-kb-manifest-v1"));
    root.insert(QStringLiteral("knowledgeRoot"), QDir::cleanPath(QDir::fromNativeSeparators(destinationRoot)));

    QJsonArray array;
    for (const ManifestCollection &collection : collections) {
        array.push_back(manifestCollectionToJson(collection));
    }
    root.insert(QStringLiteral("collections"), array);

    QSaveFile file(manifestPathForRoot(destinationRoot));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    return file.commit();
}

bool labelExistsInManifest(const QVector<ManifestCollection> &collections,
                           const QString &label,
                           const QString &excludedCollectionId = QString())
{
    const QString needle = label.trimmed();
    for (const ManifestCollection &collection : collections) {
        if (!excludedCollectionId.isEmpty() && collection.collectionId == excludedCollectionId) {
            continue;
        }
        if (collection.label.compare(needle, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

struct SourceMetadata {
    QString collectionId;
    QString collectionLabel;
    QString groupId;
    QString groupLabel;
    QString relativePath;
    QString originalPath;
};

QHash<QString, SourceMetadata> buildMetadataByInternalPath(const QVector<ManifestCollection> &collections)
{
    QHash<QString, SourceMetadata> map;
    for (const ManifestCollection &collection : collections) {
        for (const ManifestEntry &entry : collection.entries) {
            SourceMetadata metadata;
            metadata.collectionId = collection.collectionId;
            metadata.collectionLabel = collection.label;
            metadata.groupId = entry.groupId;
            metadata.groupLabel = entry.groupLabel;
            metadata.relativePath = entry.relativePath;
            metadata.originalPath = entry.originalPath;
            map.insert(canonicalPathFor(entry.internalPath), metadata);
        }
    }
    return map;
}

bool copyFileIntoCollection(const QString &sourceFile,
                            const QString &collectionRoot,
                            const QString &relativePath,
                            const QString &collectionId,
                            QVector<ManifestEntry> *entries,
                            QSet<QString> *usedRelativePaths)
{
    if (entries == nullptr) {
        return false;
    }

    QFileInfo info(sourceFile);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

    const QString uniqueRelativePath = ensureUniqueRelativePath(relativePath, usedRelativePaths);
    const QString destinationPath = QDir(collectionRoot).filePath(uniqueRelativePath);
    QDir().mkpath(QFileInfo(destinationPath).dir().absolutePath());
    QFile::remove(destinationPath);
    if (!QFile::copy(info.absoluteFilePath(), destinationPath)) {
        return false;
    }

    ManifestEntry entry;
    entry.internalPath = canonicalPathFor(destinationPath);
    entry.relativePath = uniqueRelativePath;
    entry.originalPath = canonicalPathFor(info.absoluteFilePath());
    entry.groupLabel = groupLabelFromRelativePath(uniqueRelativePath);
    entry.groupId = stableHashHex(collectionId + QStringLiteral("|") + entry.groupLabel);
    entries->push_back(entry);
    return true;
}

bool moveStoredKnowledgeFile(const QString &sourcePath, const QString &destinationPath)
{
    const QString normalizedSource = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(sourcePath).absoluteFilePath()));
    const QString normalizedDestination = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(destinationPath).absoluteFilePath()));
    if (normalizedSource == normalizedDestination) {
        return true;
    }

    QDir().mkpath(QFileInfo(destinationPath).dir().absolutePath());
    QFile::remove(destinationPath);
    if (QFile::rename(sourcePath, destinationPath)) {
        return true;
    }
    if (!QFile::copy(sourcePath, destinationPath)) {
        return false;
    }
    return QFile::remove(sourcePath);
}

int importPathIntoCollection(const QString &path,
                             const QString &collectionId,
                             const QString &collectionRoot,
                             QVector<ManifestEntry> *entries,
                             QSet<QString> *usedRelativePaths)
{
    QFileInfo info(path);
    if (!info.exists()) {
        return 0;
    }

    int copied = 0;
    if (info.isFile()) {
        if (copyFileIntoCollection(info.absoluteFilePath(),
                                   collectionRoot,
                                   standaloneRelativePathFor(info),
                                   collectionId,
                                   entries,
                                   usedRelativePaths)) {
            ++copied;
        }
        return copied;
    }

    const QString rootPath = canonicalPathFor(info.absoluteFilePath());
    QDirIterator it(info.absoluteFilePath(), extensions(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourceFile = it.next();
        const QFileInfo sourceInfo(sourceFile);
        QString relative = QDir(rootPath).relativeFilePath(sourceFile);
        if (relative.startsWith(QStringLiteral("../")) || relative.trimmed().isEmpty()) {
            relative = standaloneRelativePathFor(sourceInfo);
        }
        if (copyFileIntoCollection(sourceFile,
                                   collectionRoot,
                                   relative,
                                   collectionId,
                                   entries,
                                   usedRelativePaths)) {
            ++copied;
        }
    }

    return copied;
}

void pruneEmptyKnowledgeDirectories(const QString &path, const QString &stopRoot)
{
    QString currentPath = QFileInfo(path).dir().absolutePath();
    const QString canonicalStopRoot = canonicalPathFor(stopRoot);
    while (!currentPath.isEmpty()) {
        const QString canonicalCurrent = canonicalPathFor(currentPath);
        if (canonicalCurrent.isEmpty() || canonicalCurrent == canonicalStopRoot) {
            break;
        }

        QDir dir(currentPath);
        const QStringList entries = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
        if (!entries.isEmpty()) {
            break;
        }
        dir.rmdir(currentPath);
        currentPath = QFileInfo(currentPath).dir().absolutePath();
    }
}

bool isReservedKnowledgeMetadataFile(const QFileInfo &info)
{
    return info.fileName().startsWith(QStringLiteral(".amelia_"));
}

QStringList queryTerms(const QString &query)
{
    QString normalized = query.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9._+:-]+")), QStringLiteral(" "));
    const QStringList parts = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    static const QSet<QString> stopWords = {
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"), QStringLiteral("with"),
        QStringLiteral("from"), QStringLiteral("into"), QStringLiteral("that"), QStringLiteral("this"),
        QStringLiteral("what"), QStringLiteral("when"), QStringLiteral("where"), QStringLiteral("which"),
        QStringLiteral("your"), QStringLiteral("through"), QStringLiteral("create"), QStringLiteral("write"),
        QStringLiteral("format"), QStringLiteral("whole"), QStringLiteral("please"), QStringLiteral("show"),
        QStringLiteral("explain"), QStringLiteral("about")
    };

    static const QHash<QString, QStringList> expansions = {
        {QStringLiteral("deploy"), {QStringLiteral("deployment"), QStringLiteral("install"), QStringLiteral("bootstrap")}},
        {QStringLiteral("deployment"), {QStringLiteral("deploy"), QStringLiteral("install")}},
        {QStringLiteral("runbook"), {QStringLiteral("mop"), QStringLiteral("playbook"), QStringLiteral("procedure")}},
        {QStringLiteral("mop"), {QStringLiteral("runbook"), QStringLiteral("procedure")}},
        {QStringLiteral("hld"), {QStringLiteral("architecture"), QStringLiteral("topology")}},
        {QStringLiteral("lld"), {QStringLiteral("design"), QStringLiteral("implementation")}},
        {QStringLiteral("k8s"), {QStringLiteral("kubernetes")}},
        {QStringLiteral("harbor"), {QStringLiteral("registry")}}
    };

    QStringList terms;
    QSet<QString> seen;
    for (const QString &part : parts) {
        if (part.size() < 2 || stopWords.contains(part) || seen.contains(part)) {
            continue;
        }
        seen.insert(part);
        terms.push_back(part);
        const auto expansionIt = expansions.constFind(part);
        if (expansionIt != expansions.cend()) {
            for (const QString &expanded : expansionIt.value()) {
                if (!seen.contains(expanded)) {
                    seen.insert(expanded);
                    terms.push_back(expanded);
                }
            }
        }
    }
    return terms;
}

double lexicalScoreChunk(const QString &text, const QString &fileName, const QStringList &terms, const QString &query)
{
    if (terms.isEmpty()) {
        return 0.0;
    }

    const QString lower = text.toLower();
    const QString fileLower = fileName.toLower();
    int matched = 0;
    double score = 0.0;
    for (const QString &term : terms) {
        const int bodyCount = lower.count(term);
        const int fileCount = fileLower.count(term);
        if (bodyCount > 0 || fileCount > 0) {
            ++matched;
        }
        if (bodyCount > 0) {
            score += 0.95 + qMin(1.25, 0.16 * static_cast<double>(bodyCount - 1));
        }
        if (fileCount > 0) {
            score += 0.55 + qMin(0.75, 0.18 * static_cast<double>(fileCount - 1));
        }
    }

    const double coverage = static_cast<double>(matched) / static_cast<double>(terms.size());
    score += coverage * 1.8;

    const QString queryLower = query.toLower().trimmed();
    if (!queryLower.isEmpty() && lower.contains(queryLower)) {
        score += 1.2;
    }
    if (!queryLower.isEmpty() && fileLower.contains(queryLower)) {
        score += 0.75;
    }

    return score;
}

double roleBias(RetrievalIntent intent, const QString &role, const QStringList &preferredRoles)
{
    double bias = 0.0;
    if (intent == RetrievalIntent::DocumentGeneration) {
        if (role == QStringLiteral("scenario")) bias += 0.55;
        else if (role == QStringLiteral("procedure")) bias += 0.48;
        else if (role == QStringLiteral("reference")) bias += 0.24;
        else if (role == QStringLiteral("config")) bias += 0.08;
    } else if (intent == RetrievalIntent::Troubleshooting) {
        if (role == QStringLiteral("log")) bias += 0.55;
        else if (role == QStringLiteral("config")) bias += 0.46;
        else if (role == QStringLiteral("procedure")) bias += 0.22;
        else if (role == QStringLiteral("reference")) bias += 0.12;
    } else if (intent == RetrievalIntent::Architecture) {
        if (role == QStringLiteral("scenario")) bias += 0.58;
        else if (role == QStringLiteral("reference")) bias += 0.30;
        else if (role == QStringLiteral("procedure")) bias += 0.15;
    } else if (intent == RetrievalIntent::Implementation) {
        if (role == QStringLiteral("procedure")) bias += 0.55;
        else if (role == QStringLiteral("config")) bias += 0.32;
        else if (role == QStringLiteral("reference")) bias += 0.24;
        else if (role == QStringLiteral("scenario")) bias += 0.18;
    } else {
        if (role == QStringLiteral("procedure") || role == QStringLiteral("reference")) {
            bias += 0.10;
        }
    }

    for (const QString &preferred : preferredRoles) {
        if (role == preferred.trimmed().toLower()) {
            bias += 0.28;
            break;
        }
    }

    return bias;
}

QString makeExcerpt(const QString &text, const QStringList &terms)
{
    const QString lower = text.toLower();
    int firstIndex = -1;
    for (const QString &term : terms) {
        const int idx = lower.indexOf(term);
        if (idx >= 0 && (firstIndex < 0 || idx < firstIndex)) {
            firstIndex = idx;
        }
    }

    if (firstIndex < 0) {
        return text.left(300).trimmed();
    }

    const int start = qMax(0, firstIndex - 90);
    return text.mid(start, 360).trimmed();
}

int headingLikeLineCount(const QString &text)
{
    int count = 0;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (isHeadingLikeLine(line.trimmed())) {
            ++count;
        }
    }
    return count;
}

int splitHeadingPairCount(const QString &text)
{
    static const QRegularExpression splitHeadingExpression(
            QStringLiteral(R"((?:^|\n)\s*\d+(?:\.\d+){0,3}[.)]?\s*\n\s*[A-Z][^\n]{3,140})"),
            QRegularExpression::MultilineOption);

    int count = 0;
    QRegularExpressionMatchIterator iterator = splitHeadingExpression.globalMatch(text);
    while (iterator.hasNext()) {
        iterator.next();
        ++count;
    }
    return count;
}

bool chunkLooksLikeStructure(const QString &text)
{
    const QString lower = text.toLower();
    if (lower.contains(QStringLiteral("table of contents"))
            || lower.contains(QStringLiteral("contents"))
            || lower.contains(QStringLiteral("document version history"))
            || lower.contains(QStringLiteral("overview"))) {
        return true;
    }

    return headingLikeLineCount(text) >= 3 || splitHeadingPairCount(text) >= 2;
}

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

QString matchReason(double lexical, double semantic, const QString &role, bool neuralSemantic)
{
    QStringList reasons;
    if (lexical >= 1.8) {
        reasons << QStringLiteral("strong lexical overlap");
    } else if (lexical > 0.0) {
        reasons << QStringLiteral("keyword overlap");
    }
    if (semantic >= 0.65) {
        reasons << (neuralSemantic
                    ? QStringLiteral("strong embedding similarity")
                    : QStringLiteral("strong surface-form vector similarity"));
    } else if (semantic >= 0.38) {
        reasons << (neuralSemantic
                    ? QStringLiteral("embedding similarity")
                    : QStringLiteral("surface-form vector similarity"));
    }
    if (!role.trimmed().isEmpty()) {
        reasons << QStringLiteral("role=%1").arg(role);
    }
    return reasons.join(QStringLiteral(", "));
}
}

void RagIndexer::setDocsRoot(const QString &rootPath)
{
    m_docsRoot = QDir::cleanPath(QDir::fromNativeSeparators(rootPath.trimmed()));
}

void RagIndexer::setCachePath(const QString &cachePath)
{
    m_cachePath = QDir::cleanPath(QDir::fromNativeSeparators(cachePath.trimmed()));
}

void RagIndexer::setSemanticEnabled(bool enabled)
{
    m_semanticEnabled = enabled;
}

void RagIndexer::configureEmbeddingBackend(const QString &baseUrl, const QString &model, int timeoutMs, int batchSize)
{
    m_embeddingClient.configureOllama(baseUrl, model, timeoutMs, batchSize);
}

void RagIndexer::setDiagnosticCallback(const std::function<void(const QString &, const QString &)> &callback)
{
    m_diagnosticCallback = callback;
    m_embeddingClient.setDiagnosticCallback(callback);
}

void RagIndexer::requestCancel()
{
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool RagIndexer::lastReindexCanceled() const
{
    return m_lastReindexCanceled;
}

void RagIndexer::rebuildEmbeddings()
{
    ensureEmbeddingsForChunks(m_chunks);
}

void RagIndexer::ensureEmbeddingsForChunks(QVector<Chunk> &chunks) const
{
    if (!m_semanticEnabled) {
        for (Chunk &chunk : chunks) {
            chunk.embedding.clear();
        }
        return;
    }

    QStringList missingTexts;
    QVector<int> missingIndexes;
    missingTexts.reserve(chunks.size());
    missingIndexes.reserve(chunks.size());
    for (int i = 0; i < chunks.size(); ++i) {
        if (chunks[i].embedding.isEmpty()) {
            missingIndexes.push_back(i);
            missingTexts.push_back(chunks[i].text);
        }
    }

    if (missingTexts.isEmpty()) {
        return;
    }

    const QVector<QVector<float>> embeddings = m_embeddingClient.embedTexts(missingTexts, {}, &m_cancelRequested);
    if (m_cancelRequested.load(std::memory_order_relaxed)) {
        return;
    }
    const int assignCount = qMin(missingIndexes.size(), embeddings.size());
    for (int i = 0; i < assignCount; ++i) {
        chunks[missingIndexes.at(i)].embedding = embeddings.at(i);
    }
}

bool RagIndexer::sourceMatchesFile(const SourceInfo &source, const QFileInfo &info)
{
    return source.fileModifiedMs == info.lastModified().toMSecsSinceEpoch()
            && source.fileSizeBytes == info.size();
}

QString RagIndexer::chunkingStrategyName()
{
    return QStringLiteral("semantic-blocks-v5-speed-cache");
}

int RagIndexer::reindex(const std::function<void(int, int, const QString &)> &progressCallback)
{
    m_cancelRequested.store(false, std::memory_order_relaxed);
    m_lastReindexCanceled = false;

    auto finalizeWorkingState = [this, &progressCallback](const QHash<QString, SourceInfo> &workingSourcesByPath,
                                                          const QHash<QString, QVector<Chunk>> &workingChunksByPath,
                                                          const QString &label,
                                                          bool canceled,
                                                          int progressValue,
                                                          int progressMaximum) -> int {
        QVector<SourceInfo> finalizedSources;
        finalizedSources.reserve(workingSourcesByPath.size());
        for (auto it = workingSourcesByPath.cbegin(); it != workingSourcesByPath.cend(); ++it) {
            finalizedSources.push_back(it.value());
        }
        std::sort(finalizedSources.begin(), finalizedSources.end(), [](const SourceInfo &a, const SourceInfo &b) {
            if (a.collectionLabel != b.collectionLabel) {
                return a.collectionLabel.toLower() < b.collectionLabel.toLower();
            }
            if (a.groupLabel != b.groupLabel) {
                return a.groupLabel.toLower() < b.groupLabel.toLower();
            }
            if (a.relativePath != b.relativePath) {
                return a.relativePath.toLower() < b.relativePath.toLower();
            }
            return a.fileName.toLower() < b.fileName.toLower();
        });

        QVector<Chunk> finalizedChunks;
        for (const SourceInfo &source : finalizedSources) {
            const QVector<Chunk> chunksForPath = workingChunksByPath.value(source.filePath);
            for (const Chunk &chunk : chunksForPath) {
                finalizedChunks.push_back(chunk);
            }
        }

        m_sources = finalizedSources;
        m_chunks = finalizedChunks;
        saveCache();
        m_lastReindexCanceled = canceled;

        if (progressCallback) {
            progressCallback(progressValue, progressMaximum, label);
        }
        return m_chunks.size();
    };

    if (m_docsRoot.trimmed().isEmpty()) {
        m_chunks.clear();
        m_sources.clear();
        saveCache();
        return 0;
    }

    const QVector<ManifestCollection> manifestCollections = loadManifestCollections(m_docsRoot);
    const QHash<QString, SourceMetadata> metadataByPath = buildMetadataByInternalPath(manifestCollections);

    QStringList paths;
    QDirIterator gatherIt(m_docsRoot, extensions(), QDir::Files, QDirIterator::Subdirectories);
    while (gatherIt.hasNext()) {
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            return finalizeWorkingState({}, {},
                                        QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."),
                                        true,
                                        0,
                                        1);
        }

        const QString path = gatherIt.next();
        const QFileInfo info(path);
        if (isReservedKnowledgeMetadataFile(info)) {
            continue;
        }
        paths.push_back(path);
    }
    std::sort(paths.begin(), paths.end());

    const int totalFiles = paths.size();
    if (progressCallback) {
        progressCallback(0, totalFiles > 0 ? totalFiles : 1, QStringLiteral("Scanning local documents..."));
    }

    QHash<QString, SourceInfo> existingSourcesByPath;
    existingSourcesByPath.reserve(m_sources.size());
    for (const SourceInfo &source : m_sources) {
        existingSourcesByPath.insert(source.filePath, source);
    }

    QHash<QString, QVector<Chunk>> existingChunksByPath;
    existingChunksByPath.reserve(m_sources.size());
    QHash<QString, QVector<float>> cachedEmbeddingsByFingerprint;
    cachedEmbeddingsByFingerprint.reserve(m_chunks.size());
    for (const Chunk &chunk : m_chunks) {
        existingChunksByPath[chunk.filePath].push_back(chunk);
        if (m_semanticEnabled && !chunk.embedding.isEmpty()) {
            const QString fingerprint = chunk.textFingerprint.trimmed().isEmpty()
                    ? stableHashHex(chunk.text.simplified())
                    : chunk.textFingerprint;
            if (!fingerprint.isEmpty() && !cachedEmbeddingsByFingerprint.contains(fingerprint)) {
                cachedEmbeddingsByFingerprint.insert(fingerprint, chunk.embedding);
            }
        }
    }

    QHash<QString, SourceInfo> workingSourcesByPath = existingSourcesByPath;
    QHash<QString, QVector<Chunk>> workingChunksByPath = existingChunksByPath;
    QHash<QString, QVector<float>> workingEmbeddingsByFingerprint = cachedEmbeddingsByFingerprint;

    QSet<QString> currentPathSet;
    for (const QString &path : paths) {
        currentPathSet.insert(path);
    }
    QSet<QString> pendingPaths = currentPathSet;
    for (auto it = workingSourcesByPath.begin(); it != workingSourcesByPath.end();) {
        if (!currentPathSet.contains(it.key())) {
            workingChunksByPath.remove(it.key());
            it = workingSourcesByPath.erase(it);
            continue;
        }
        ++it;
    }

    int reusedFiles = 0;
    int rebuiltFiles = 0;
    int committedFiles = 0;
    int reusedByHashFiles = 0;
    int reusedChunkEmbeddings = 0;

    const auto applyMetadata = [this, &metadataByPath](SourceInfo &source) {
        const QString canonicalPath = canonicalPathFor(source.filePath);
        const auto metadataIt = metadataByPath.constFind(canonicalPath);
        if (metadataIt != metadataByPath.cend()) {
            const SourceMetadata &metadata = metadataIt.value();
            source.collectionId = metadata.collectionId;
            source.collectionLabel = metadata.collectionLabel;
            source.groupId = metadata.groupId;
            source.groupLabel = metadata.groupLabel;
            source.relativePath = metadata.relativePath;
            source.originalPath = metadata.originalPath;
            return;
        }

        source.collectionId = QStringLiteral("legacy");
        source.collectionLabel = QStringLiteral("Legacy imports");
        source.relativePath = QDir(m_docsRoot).relativeFilePath(source.filePath);
        source.groupLabel = groupLabelFromRelativePath(source.relativePath);
        source.groupId = stableHashHex(source.collectionId + QStringLiteral("|") + source.groupLabel);
        source.originalPath.clear();
    };

    const auto registerChunkEmbeddings = [this, &workingEmbeddingsByFingerprint](const QVector<Chunk> &chunks) {
        if (!m_semanticEnabled) {
            return;
        }
        for (const Chunk &chunk : chunks) {
            if (chunk.embedding.isEmpty()) {
                continue;
            }
            const QString fingerprint = chunk.textFingerprint.trimmed().isEmpty()
                    ? stableHashHex(chunk.text.simplified())
                    : chunk.textFingerprint;
            if (!fingerprint.isEmpty() && !workingEmbeddingsByFingerprint.contains(fingerprint)) {
                workingEmbeddingsByFingerprint.insert(fingerprint, chunk.embedding);
            }
        }
    };

    const auto cancelWithPartialCommit = [&](const QString &label) -> int {
        for (const QString &pendingPath : std::as_const(pendingPaths)) {
            workingSourcesByPath.remove(pendingPath);
            workingChunksByPath.remove(pendingPath);
        }
        return finalizeWorkingState(workingSourcesByPath,
                                    workingChunksByPath,
                                    label,
                                    true,
                                    qBound(0, committedFiles, totalFiles > 0 ? totalFiles : 1),
                                    totalFiles > 0 ? totalFiles : 1);
    };

    for (int i = 0; i < totalFiles; ++i) {
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
        }

        const QString path = paths.at(i);
        const QFileInfo info(path);
        const QString sourceType = detectSourceType(info);

        const auto sourceIt = existingSourcesByPath.constFind(path);
        const auto chunksIt = existingChunksByPath.constFind(path);
        const bool hasExistingCacheEntry = sourceIt != existingSourcesByPath.cend() && chunksIt != existingChunksByPath.cend();
        const bool canReuseFast = hasExistingCacheEntry && sourceMatchesFile(sourceIt.value(), info);

        auto commitReusedSource = [&](SourceInfo source,
                                      QVector<Chunk> reusedChunks,
                                      const QString &label,
                                      bool countedAsHashReuse) {
            applyMetadata(source);
            ensureEmbeddingsForChunks(reusedChunks);
            if (!m_semanticEnabled) {
                for (Chunk &chunk : reusedChunks) {
                    chunk.embedding.clear();
                }
            }
            source.chunkCount = reusedChunks.size();
            workingSourcesByPath.insert(path, source);
            workingChunksByPath.insert(path, reusedChunks);
            registerChunkEmbeddings(reusedChunks);
            ++reusedFiles;
            if (countedAsHashReuse) {
                ++reusedByHashFiles;
            }
            ++committedFiles;
            pendingPaths.remove(path);

            if (progressCallback) {
                progressCallback(i + 1,
                                 totalFiles > 0 ? totalFiles : 1,
                                 label);
            }
        };

        if (canReuseFast) {
            commitReusedSource(sourceIt.value(),
                               chunksIt.value(),
                               QStringLiteral("Using cached index %1 / %2: %3 (%4 chunks)").arg(i + 1).arg(totalFiles).arg(info.fileName()).arg(chunksIt.value().size()),
                               false);
            continue;
        }

        QString fileContentHash;
        bool matchedByContentHash = false;
        if (hasExistingCacheEntry) {
            if (progressCallback) {
                progressCallback(i,
                                 totalFiles > 0 ? totalFiles : 1,
                                 QStringLiteral("Hashing %1 / %2: %3").arg(i + 1).arg(totalFiles).arg(info.fileName()));
            }
            if (computeFileContentHash(path, &fileContentHash, &m_cancelRequested)) {
                const QString cachedHash = sourceIt.value().fileContentHash.trimmed();
                matchedByContentHash = !cachedHash.isEmpty() && cachedHash == fileContentHash;
            }
            if (m_cancelRequested.load(std::memory_order_relaxed)) {
                return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
            }
        }

        if (matchedByContentHash) {
            SourceInfo reusedSource = sourceIt.value();
            reusedSource.fileModifiedMs = info.lastModified().toMSecsSinceEpoch();
            reusedSource.fileSizeBytes = info.size();
            reusedSource.fileContentHash = fileContentHash;
            QVector<Chunk> reusedChunks = chunksIt.value();
            for (Chunk &chunk : reusedChunks) {
                chunk.fileModifiedMs = reusedSource.fileModifiedMs;
            }
            commitReusedSource(reusedSource,
                               reusedChunks,
                               QStringLiteral("Using content-hash cache %1 / %2: %3 (%4 chunks)").arg(i + 1).arg(totalFiles).arg(info.fileName()).arg(reusedChunks.size()),
                               true);
            continue;
        }

        if (progressCallback) {
            const QString phaseLabel = (info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0)
                    ? QStringLiteral("Extracting PDF %1 / %2: %3").arg(i + 1).arg(totalFiles).arg(info.fileName())
                    : QStringLiteral("Indexing file %1 / %2: %3").arg(i + 1).arg(totalFiles).arg(info.fileName());
            progressCallback(i, totalFiles > 0 ? totalFiles : 1, phaseLabel);
        }

        QString textValue;
        QString extractor;
        const bool ok = readTextFile(path, &textValue, &extractor, &m_cancelRequested);
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
        }
        if (fileContentHash.isEmpty()) {
            computeFileContentHash(path, &fileContentHash, &m_cancelRequested);
            if (m_cancelRequested.load(std::memory_order_relaxed)) {
                return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
            }
        }
        const QString sourceRole = detectSourceRole(info, sourceType, textValue);

        SourceInfo source;
        source.filePath = path;
        source.fileName = info.fileName();
        source.sourceType = sourceType;
        source.sourceRole = sourceRole;
        source.extractor = extractor.isEmpty() ? QStringLiteral("unknown") : extractor;
        source.fileModifiedMs = info.lastModified().toMSecsSinceEpoch();
        source.fileSizeBytes = info.size();
        source.fileContentHash = fileContentHash;
        source.chunkingProfile = QStringLiteral("unavailable");
        applyMetadata(source);

        source.textCharCount = textValue.size();
        source.lineCount = countLinesInText(textValue);
        source.wordCount = countWordsInText(textValue);

        QVector<Chunk> rebuiltChunksForPath;
        const QString trimmedTextValue = textValue.trimmed();
        int blockCount = 0;
        int candidateChunkCount = 0;
        int filteredChunkCount = 0;
        int duplicateChunkCount = 0;
        int reusedChunkEmbeddingsForFile = 0;
        if (ok && !trimmedTextValue.isEmpty()) {
            const bool semanticReady = m_semanticEnabled && m_embeddingClient.isConfigured();
            const QVector<QString> blocks = buildSemanticBlocks(textValue, &m_cancelRequested);
            blockCount = blocks.size();
            const ChunkingProfile profile = chooseChunkingProfile(sourceType,
                                                                 source.fileSizeBytes,
                                                                 semanticReady,
                                                                 blocks.size());
            source.chunkingProfile = profile.label;

            const QVector<QString> chunkTexts = buildChunksFromBlocks(blocks, profile, &m_cancelRequested);
            candidateChunkCount = chunkTexts.size();
            if (m_cancelRequested.load(std::memory_order_relaxed)) {
                return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
            }

            struct PendingChunk {
                QString text;
                QString fingerprint;
                QVector<float> embedding;
            };

            QVector<PendingChunk> pendingChunks;
            pendingChunks.reserve(chunkTexts.size());
            QStringList embeddingsInput;
            QVector<int> embeddingIndexes;
            QSet<QString> seenChunkFingerprints;
            embeddingsInput.reserve(chunkTexts.size());
            embeddingIndexes.reserve(chunkTexts.size());

            for (const QString &chunkText : chunkTexts) {
                if (m_cancelRequested.load(std::memory_order_relaxed)) {
                    return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
                }
                const QString normalizedChunk = chunkText.trimmed();
                if (!shouldKeepChunkText(normalizedChunk)) {
                    ++filteredChunkCount;
                    continue;
                }
                const QString fingerprint = stableHashHex(normalizedChunk.simplified());
                if (seenChunkFingerprints.contains(fingerprint)) {
                    ++duplicateChunkCount;
                    continue;
                }
                seenChunkFingerprints.insert(fingerprint);

                PendingChunk pendingChunk;
                pendingChunk.text = normalizedChunk;
                pendingChunk.fingerprint = fingerprint;
                if (m_semanticEnabled) {
                    const auto cachedEmbeddingIt = workingEmbeddingsByFingerprint.constFind(fingerprint);
                    if (cachedEmbeddingIt != workingEmbeddingsByFingerprint.cend()) {
                        pendingChunk.embedding = cachedEmbeddingIt.value();
                        ++reusedChunkEmbeddingsForFile;
                    }
                }

                pendingChunks.push_back(pendingChunk);
                if (m_semanticEnabled && pendingChunk.embedding.isEmpty()) {
                    embeddingIndexes.push_back(pendingChunks.size() - 1);
                    embeddingsInput.push_back(normalizedChunk);
                }
            }

            QVector<QVector<float>> embeddings;
            if (m_semanticEnabled && !embeddingsInput.isEmpty()) {
                if (progressCallback) {
                    progressCallback(0, embeddingsInput.size(),
                                     QStringLiteral("Embedding %1 / %2: %3 — 0/%4 chunks")
                                         .arg(i + 1)
                                         .arg(totalFiles)
                                         .arg(info.fileName())
                                         .arg(embeddingsInput.size()));
                }
                embeddings = m_embeddingClient.embedTexts(embeddingsInput,
                                                          [progressCallback, i, totalFiles, info](int completed, int total) {
                                                              if (progressCallback) {
                                                                  progressCallback(completed, total,
                                                                                   QStringLiteral("Embedding %1 / %2: %3 — %4/%5 chunks")
                                                                                       .arg(i + 1)
                                                                                       .arg(totalFiles)
                                                                                       .arg(info.fileName())
                                                                                       .arg(completed)
                                                                                       .arg(total));
                                                              }
                                                          },
                                                          &m_cancelRequested);
            }
            if (m_cancelRequested.load(std::memory_order_relaxed)) {
                return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
            }

            const int assignCount = qMin(embeddingIndexes.size(), embeddings.size());
            for (int assignIndex = 0; assignIndex < assignCount; ++assignIndex) {
                pendingChunks[embeddingIndexes.at(assignIndex)].embedding = embeddings.at(assignIndex);
            }

            int chunkIndex = 0;
            for (const PendingChunk &pendingChunk : std::as_const(pendingChunks)) {
                if (m_cancelRequested.load(std::memory_order_relaxed)) {
                    return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
                }

                Chunk chunk;
                chunk.filePath = path;
                chunk.fileName = info.fileName();
                chunk.sourceType = sourceType;
                chunk.sourceRole = sourceRole;
                chunk.text = pendingChunk.text;
                chunk.textFingerprint = pendingChunk.fingerprint;
                chunk.chunkIndex = chunkIndex++;
                chunk.fileModifiedMs = source.fileModifiedMs;
                if (m_semanticEnabled && !pendingChunk.embedding.isEmpty()) {
                    chunk.embedding = pendingChunk.embedding;
                }
                rebuiltChunksForPath.push_back(chunk);
                ++source.chunkCount;
            }
        }

        if (source.chunkCount <= 0) {
            if (!ok) {
                source.zeroChunkReason = fallbackZeroChunkReason(source.extractor, source.textCharCount, source.wordCount);
            } else if (trimmedTextValue.isEmpty()) {
                source.zeroChunkReason = fallbackZeroChunkReason(source.extractor, source.textCharCount, source.wordCount);
            } else if (blockCount <= 0) {
                source.zeroChunkReason = QStringLiteral("Text was extracted, but Amelia could not derive any semantic blocks from it.");
            } else if (candidateChunkCount <= 0) {
                source.zeroChunkReason = QStringLiteral("Text was parsed into blocks, but chunk generation produced no chunk candidates.");
            } else if ((filteredChunkCount + duplicateChunkCount) >= candidateChunkCount) {
                if (filteredChunkCount > 0 && duplicateChunkCount > 0) {
                    source.zeroChunkReason = QStringLiteral("All %1 chunk candidate(s) were removed during filtering (%2 weak/noisy, %3 duplicate).").arg(candidateChunkCount).arg(filteredChunkCount).arg(duplicateChunkCount);
                } else if (filteredChunkCount > 0) {
                    source.zeroChunkReason = QStringLiteral("All %1 chunk candidate(s) were filtered out as too weak or noisy.").arg(candidateChunkCount);
                } else if (duplicateChunkCount > 0) {
                    source.zeroChunkReason = QStringLiteral("All %1 chunk candidate(s) were duplicates of other extracted content.").arg(candidateChunkCount);
                }
            }

            if (source.zeroChunkReason.trimmed().isEmpty()) {
                source.zeroChunkReason = fallbackZeroChunkReason(source.extractor, source.textCharCount, source.wordCount);
            }
        } else {
            source.zeroChunkReason.clear();
        }

        workingSourcesByPath.insert(path, source);
        workingChunksByPath.insert(path, rebuiltChunksForPath);
        registerChunkEmbeddings(rebuiltChunksForPath);
        reusedChunkEmbeddings += reusedChunkEmbeddingsForFile;
        ++rebuiltFiles;
        ++committedFiles;
        pendingPaths.remove(path);

        if (progressCallback) {
            const QString speedSuffix = reusedChunkEmbeddingsForFile > 0
                    ? QStringLiteral(" | shared embedding cache hits=%1").arg(reusedChunkEmbeddingsForFile)
                    : QString();
            progressCallback(i + 1,
                             totalFiles > 0 ? totalFiles : 1,
                             QStringLiteral("Indexed %1 / %2: %3 (%4 chunks%5)")
                                 .arg(i + 1)
                                 .arg(totalFiles)
                                 .arg(info.fileName())
                                 .arg(rebuiltChunksForPath.size())
                                 .arg(speedSuffix));
        }
    }

    return finalizeWorkingState(workingSourcesByPath,
                                workingChunksByPath,
                                QStringLiteral("Index ready: %1 files (%2 reused, %3 rebuilt, %4 hash-reused, %5 shared-embedding hits, %6 chunks)")
                                    .arg(totalFiles)
                                    .arg(reusedFiles)
                                    .arg(rebuiltFiles)
                                    .arg(reusedByHashFiles)
                                    .arg(reusedChunkEmbeddings)
                                    .arg([&workingChunksByPath]() {
                                        int count = 0;
                                        for (auto it = workingChunksByPath.cbegin(); it != workingChunksByPath.cend(); ++it) {
                                            count += it.value().size();
                                        }
                                        return count;
                                    }()),
                                false,
                                totalFiles > 0 ? totalFiles : 1,
                                totalFiles > 0 ? totalFiles : 1);
}


bool RagIndexer::loadCache()
{
    if (m_cachePath.trimmed().isEmpty()) {
        return false;
    }

    QFile file(m_cachePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return false;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("amelia-rag-cache-v3")) {
        return false;
    }
    if (root.value(QStringLiteral("docsRoot")).toString() != m_docsRoot) {
        return false;
    }
    if (root.value(QStringLiteral("chunkingStrategy")).toString() != chunkingStrategyName()) {
        return false;
    }
    if (m_semanticEnabled
        && root.value(QStringLiteral("embeddingCacheKey")).toString() != m_embeddingClient.cacheKey()) {
        return false;
    }

    QVector<Chunk> cachedChunks;
    const QJsonArray chunkArray = root.value(QStringLiteral("chunks")).toArray();
    cachedChunks.reserve(chunkArray.size());
    for (const QJsonValue &value : chunkArray) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        Chunk chunk;
        chunk.filePath = obj.value(QStringLiteral("filePath")).toString();
        chunk.fileName = obj.value(QStringLiteral("fileName")).toString();
        chunk.sourceType = obj.value(QStringLiteral("sourceType")).toString();
        chunk.sourceRole = obj.value(QStringLiteral("sourceRole")).toString();
        chunk.text = obj.value(QStringLiteral("text")).toString();
        chunk.chunkIndex = obj.value(QStringLiteral("chunkIndex")).toInt();
        chunk.textFingerprint = obj.value(QStringLiteral("textFingerprint")).toString();
        chunk.fileModifiedMs = static_cast<qint64>(obj.value(QStringLiteral("fileModifiedMs")).toDouble());
        const QJsonArray embeddingArray = obj.value(QStringLiteral("embedding")).toArray();
        chunk.embedding.reserve(embeddingArray.size());
        for (const QJsonValue &embeddingValue : embeddingArray) {
            chunk.embedding.push_back(static_cast<float>(embeddingValue.toDouble()));
        }
        if (chunk.filePath.isEmpty() || chunk.text.isEmpty()) {
            continue;
        }
        cachedChunks.push_back(chunk);
    }

    QVector<SourceInfo> cachedSources;
    const QJsonArray sourceArray = root.value(QStringLiteral("sources")).toArray();
    cachedSources.reserve(sourceArray.size());
    for (const QJsonValue &value : sourceArray) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        SourceInfo source;
        source.filePath = obj.value(QStringLiteral("filePath")).toString();
        source.fileName = obj.value(QStringLiteral("fileName")).toString();
        source.sourceType = obj.value(QStringLiteral("sourceType")).toString();
        source.sourceRole = obj.value(QStringLiteral("sourceRole")).toString();
        source.extractor = obj.value(QStringLiteral("extractor")).toString();
        source.collectionId = obj.value(QStringLiteral("collectionId")).toString();
        source.collectionLabel = obj.value(QStringLiteral("collectionLabel")).toString();
        source.groupId = obj.value(QStringLiteral("groupId")).toString();
        source.groupLabel = obj.value(QStringLiteral("groupLabel")).toString();
        source.relativePath = obj.value(QStringLiteral("relativePath")).toString();
        source.originalPath = obj.value(QStringLiteral("originalPath")).toString();
        source.fileModifiedMs = static_cast<qint64>(obj.value(QStringLiteral("fileModifiedMs")).toDouble());
        source.fileSizeBytes = static_cast<qint64>(obj.value(QStringLiteral("fileSizeBytes")).toDouble());
        source.chunkCount = obj.value(QStringLiteral("chunkCount")).toInt();
        source.fileContentHash = obj.value(QStringLiteral("fileContentHash")).toString();
        source.lineCount = obj.value(QStringLiteral("lineCount")).toInt();
        source.wordCount = obj.value(QStringLiteral("wordCount")).toInt();
        source.textCharCount = obj.value(QStringLiteral("textCharCount")).toInt();
        source.chunkingProfile = obj.value(QStringLiteral("chunkingProfile")).toString();
        source.zeroChunkReason = obj.value(QStringLiteral("zeroChunkReason")).toString();
        if (source.chunkCount <= 0 && source.zeroChunkReason.trimmed().isEmpty()) {
            source.zeroChunkReason = fallbackZeroChunkReason(source.extractor, source.textCharCount, source.wordCount);
        }
        if (source.filePath.isEmpty()) {
            continue;
        }
        cachedSources.push_back(source);
    }

    m_chunks = std::move(cachedChunks);
    m_sources = std::move(cachedSources);
    if (!m_semanticEnabled) {
        for (Chunk &chunk : m_chunks) {
            chunk.embedding.clear();
        }
    }
    return !m_sources.isEmpty() || !m_chunks.isEmpty();
}

bool RagIndexer::saveCache() const
{
    if (m_cachePath.trimmed().isEmpty()) {
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("amelia-rag-cache-v3"));
    root.insert(QStringLiteral("docsRoot"), m_docsRoot);
    root.insert(QStringLiteral("semanticEnabled"), m_semanticEnabled);
    root.insert(QStringLiteral("chunkingStrategy"), chunkingStrategyName());
    root.insert(QStringLiteral("embeddingBackend"), m_embeddingClient.backendName());
    root.insert(QStringLiteral("embeddingCacheKey"), m_embeddingClient.cacheKey());

    QJsonArray chunkArray;
    for (const Chunk &chunk : m_chunks) {
        QJsonObject obj;
        obj.insert(QStringLiteral("filePath"), chunk.filePath);
        obj.insert(QStringLiteral("fileName"), chunk.fileName);
        obj.insert(QStringLiteral("sourceType"), chunk.sourceType);
        obj.insert(QStringLiteral("sourceRole"), chunk.sourceRole);
        obj.insert(QStringLiteral("text"), chunk.text);
        obj.insert(QStringLiteral("chunkIndex"), chunk.chunkIndex);
        obj.insert(QStringLiteral("textFingerprint"), chunk.textFingerprint);
        obj.insert(QStringLiteral("fileModifiedMs"), static_cast<double>(chunk.fileModifiedMs));
        if (!chunk.embedding.isEmpty()) {
            QJsonArray embeddingArray;
            for (float value : chunk.embedding) {
                embeddingArray.push_back(static_cast<double>(value));
            }
            obj.insert(QStringLiteral("embedding"), embeddingArray);
        }
        chunkArray.push_back(obj);
    }
    root.insert(QStringLiteral("chunks"), chunkArray);

    QJsonArray sourceArray;
    for (const SourceInfo &source : m_sources) {
        QJsonObject obj;
        obj.insert(QStringLiteral("filePath"), source.filePath);
        obj.insert(QStringLiteral("fileName"), source.fileName);
        obj.insert(QStringLiteral("sourceType"), source.sourceType);
        obj.insert(QStringLiteral("sourceRole"), source.sourceRole);
        obj.insert(QStringLiteral("extractor"), source.extractor);
        obj.insert(QStringLiteral("collectionId"), source.collectionId);
        obj.insert(QStringLiteral("collectionLabel"), source.collectionLabel);
        obj.insert(QStringLiteral("groupId"), source.groupId);
        obj.insert(QStringLiteral("groupLabel"), source.groupLabel);
        obj.insert(QStringLiteral("relativePath"), source.relativePath);
        obj.insert(QStringLiteral("originalPath"), source.originalPath);
        obj.insert(QStringLiteral("fileModifiedMs"), static_cast<double>(source.fileModifiedMs));
        obj.insert(QStringLiteral("fileSizeBytes"), static_cast<double>(source.fileSizeBytes));
        obj.insert(QStringLiteral("chunkCount"), source.chunkCount);
        obj.insert(QStringLiteral("fileContentHash"), source.fileContentHash);
        obj.insert(QStringLiteral("lineCount"), source.lineCount);
        obj.insert(QStringLiteral("wordCount"), source.wordCount);
        obj.insert(QStringLiteral("textCharCount"), source.textCharCount);
        obj.insert(QStringLiteral("chunkingProfile"), source.chunkingProfile);
        obj.insert(QStringLiteral("zeroChunkReason"), source.zeroChunkReason);
        sourceArray.push_back(obj);
    }
    root.insert(QStringLiteral("sources"), sourceArray);

    QSaveFile file(m_cachePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool RagIndexer::cacheNeedsRefresh() const
{
    if (m_docsRoot.trimmed().isEmpty()) {
        return false;
    }

    QHash<QString, SourceInfo> cachedSourcesByPath;
    cachedSourcesByPath.reserve(m_sources.size());
    for (const SourceInfo &source : m_sources) {
        cachedSourcesByPath.insert(source.filePath, source);
    }

    QSet<QString> currentPaths;
    QDirIterator it(m_docsRoot, extensions(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (isReservedKnowledgeMetadataFile(info)) {
            continue;
        }
        currentPaths.insert(path);
        const auto sourceIt = cachedSourcesByPath.constFind(path);
        if (sourceIt == cachedSourcesByPath.cend()) {
            return true;
        }
        if (!sourceMatchesFile(sourceIt.value(), info)) {
            return true;
        }
    }

    if (currentPaths.size() != m_sources.size()) {
        return true;
    }

    for (const SourceInfo &source : m_sources) {
        if (!currentPaths.contains(source.filePath)) {
            return true;
        }
    }

    if (m_semanticEnabled) {
        for (const Chunk &chunk : m_chunks) {
            if (chunk.embedding.isEmpty()) {
                return true;
            }
        }
    }

    return false;
}

QVector<RagHit> RagIndexer::searchHits(const QString &query,
                                       int limit,
                                       RetrievalIntent intent,
                                       const QStringList &preferredRoles) const
{
    return searchHitsInFiles(query, QStringList(), limit, intent, preferredRoles);
}

QVector<RagHit> RagIndexer::searchHitsInFiles(const QString &query,
                                              const QStringList &preferredPaths,
                                              int limit,
                                              RetrievalIntent intent,
                                              const QStringList &preferredRoles) const
{
    const QStringList terms = queryTerms(query);

    int expectedEmbeddingDims = 0;
    for (const Chunk &chunk : m_chunks) {
        if (!chunk.embedding.isEmpty()) {
            expectedEmbeddingDims = chunk.embedding.size();
            break;
        }
    }

    QVector<float> queryEmbedding;
    bool neuralSemantic = false;
    bool semanticActive = false;
    if (m_semanticEnabled && expectedEmbeddingDims > 0) {
        queryEmbedding = m_embeddingClient.embedText(query);
        neuralSemantic = m_embeddingClient.lastRequestUsedNeural();
        if (queryEmbedding.size() != expectedEmbeddingDims && expectedEmbeddingDims == m_embeddingClient.localFallbackDimensions()) {
            queryEmbedding = m_embeddingClient.embedTextLocalFallback(query);
            neuralSemantic = false;
        }
        semanticActive = !queryEmbedding.isEmpty() && queryEmbedding.size() == expectedEmbeddingDims;
    }

    const double lexicalWeight = neuralSemantic ? 0.85 : 0.98;
    const double semanticWeight = neuralSemantic ? 1.55 : 0.28;
    const double threshold = neuralSemantic ? 0.92 : 1.00;

    QSet<QString> pathFilter;
    for (const QString &path : preferredPaths) {
        const QString trimmed = path.trimmed();
        if (!trimmed.isEmpty()) {
            pathFilter.insert(QDir::cleanPath(trimmed));
        }
    }

    QVector<RagHit> ranked;
    ranked.reserve(m_chunks.size());

    for (const Chunk &chunk : m_chunks) {
        if (!pathFilter.isEmpty() && !pathFilter.contains(QDir::cleanPath(chunk.filePath))) {
            continue;
        }

        const double lexical = lexicalScoreChunk(chunk.text, chunk.fileName, terms, query);
        const double semantic = semanticActive
                ? qMax(0.0f, EmbeddingClient::cosineSimilarity(queryEmbedding, chunk.embedding))
                : 0.0;
        const double role = roleBias(intent, chunk.sourceRole, preferredRoles);
        const bool strongPureSemanticHit = neuralSemantic && lexical <= 0.0 && semantic >= 0.72;
        if (!strongPureSemanticHit && lexical <= 0.0 && (!semanticActive || semantic < 0.38)) {
            continue;
        }

        const double finalScore = lexical * lexicalWeight + semantic * semanticWeight + role;
        if (finalScore <= threshold) {
            continue;
        }

        RagHit hit;
        hit.filePath = chunk.filePath;
        hit.fileName = chunk.fileName;
        hit.sourceType = chunk.sourceType;
        hit.sourceRole = chunk.sourceRole;
        hit.excerpt = makeExcerpt(chunk.text, terms);
        hit.chunkText = chunk.text;
        hit.matchReason = matchReason(lexical, semantic, chunk.sourceRole, neuralSemantic);
        hit.lexicalScore = lexical;
        hit.semanticScore = semantic;
        hit.rerankScore = finalScore;
        hit.score = qRound(finalScore * 100.0);
        hit.chunkIndex = chunk.chunkIndex;
        ranked.push_back(hit);
    }

    std::sort(ranked.begin(), ranked.end(), [](const RagHit &a, const RagHit &b) {
        if (!qFuzzyCompare(a.rerankScore + 1.0, b.rerankScore + 1.0)) {
            return a.rerankScore > b.rerankScore;
        }
        if (!qFuzzyCompare(a.lexicalScore + 1.0, b.lexicalScore + 1.0)) {
            return a.lexicalScore > b.lexicalScore;
        }
        if (!qFuzzyCompare(a.semanticScore + 1.0, b.semanticScore + 1.0)) {
            return a.semanticScore > b.semanticScore;
        }
        if (a.fileName != b.fileName) {
            return a.fileName < b.fileName;
        }
        return a.chunkIndex < b.chunkIndex;
    });

    QVector<RagHit> hits;
    QHash<QString, int> fileCounts;
    int maxPerFile = (intent == RetrievalIntent::DocumentGeneration || intent == RetrievalIntent::Architecture) ? 6 : 3;
    if (!pathFilter.isEmpty()) {
        maxPerFile = qMax(maxPerFile, limit);
    }
    for (const RagHit &candidate : ranked) {
        const QString fileKey = candidate.filePath;
        if (fileCounts.value(fileKey) >= maxPerFile) {
            continue;
        }
        hits.push_back(candidate);
        fileCounts[fileKey] += 1;
        if (hits.size() >= limit) {
            break;
        }
    }

    return hits;
}

QVector<RagHit> RagIndexer::representativeHitsInFiles(const QStringList &preferredPaths,
                                                      int perFileLimit,
                                                      bool preferStructure) const
{
    QVector<RagHit> hits;
    if (preferredPaths.isEmpty() || perFileLimit <= 0) {
        return hits;
    }

    QSet<QString> pathFilter;
    QStringList orderedPaths;
    for (const QString &path : preferredPaths) {
        const QString cleaned = QDir::cleanPath(path.trimmed());
        if (cleaned.isEmpty() || pathFilter.contains(cleaned)) {
            continue;
        }
        pathFilter.insert(cleaned);
        orderedPaths.push_back(cleaned);
    }
    if (orderedPaths.isEmpty()) {
        return hits;
    }

    QHash<QString, QVector<const Chunk *>> chunksByPath;
    chunksByPath.reserve(orderedPaths.size());
    for (const Chunk &chunk : m_chunks) {
        const QString cleanedPath = QDir::cleanPath(chunk.filePath);
        if (!pathFilter.contains(cleanedPath)) {
            continue;
        }
        chunksByPath[cleanedPath].push_back(&chunk);
    }

    for (const QString &path : std::as_const(orderedPaths)) {
        QVector<const Chunk *> fileChunks = chunksByPath.value(path);
        if (fileChunks.isEmpty()) {
            continue;
        }

        std::sort(fileChunks.begin(), fileChunks.end(), [](const Chunk *a, const Chunk *b) {
            return a->chunkIndex < b->chunkIndex;
        });

        const int desired = qMax(4, perFileLimit);
        QSet<int> selectedIndexes;

        auto addChunkIndex = [&](int index) {
            if (index >= 0 && index < fileChunks.size()) {
                selectedIndexes.insert(index);
            }
        };

        addChunkIndex(0);
        addChunkIndex(1);
        addChunkIndex(fileChunks.size() - 2);
        addChunkIndex(fileChunks.size() - 1);

        QVector<int> structureIndexes;
        structureIndexes.reserve(fileChunks.size());
        if (preferStructure) {
            for (int i = 0; i < fileChunks.size(); ++i) {
                if (chunkLooksLikeStructure(fileChunks.at(i)->text)) {
                    structureIndexes.push_back(i);
                }
            }

            const int targetStructureCount = qMin(desired, qMax(4, desired - 2));
            const int earlyWindow = qMin(fileChunks.size(), qMax(10, desired * 3));
            for (const int index : std::as_const(structureIndexes)) {
                if (index >= earlyWindow) {
                    break;
                }
                if (selectedIndexes.size() >= targetStructureCount) {
                    break;
                }
                selectedIndexes.insert(index);
            }

            const int remainingStructureSlots = qMax(0, targetStructureCount - static_cast<int>(selectedIndexes.size()));
            if (remainingStructureSlots > 0 && !structureIndexes.isEmpty()) {
                if (structureIndexes.size() <= remainingStructureSlots) {
                    for (const int index : std::as_const(structureIndexes)) {
                        selectedIndexes.insert(index);
                    }
                } else {
                    for (int slot = 0; slot < remainingStructureSlots; ++slot) {
                        const double ratio = static_cast<double>(slot + 1) / static_cast<double>(remainingStructureSlots + 1);
                        const int candidatePos = qBound(0,
                                                        qRound(ratio * static_cast<double>(structureIndexes.size() - 1)),
                                                        structureIndexes.size() - 1);
                        selectedIndexes.insert(structureIndexes.at(candidatePos));
                    }
                }
            }
        }

        const int spacingSlots = qMax(0, desired - static_cast<int>(selectedIndexes.size()));
        if (spacingSlots > 0) {
            if (fileChunks.size() <= spacingSlots) {
                for (int i = 0; i < fileChunks.size(); ++i) {
                    selectedIndexes.insert(i);
                }
            } else {
                for (int slot = 0; slot < spacingSlots; ++slot) {
                    const double ratio = static_cast<double>(slot + 1) / static_cast<double>(spacingSlots + 1);
                    const int candidate = qBound(0,
                                                 qRound(ratio * static_cast<double>(fileChunks.size() - 1)),
                                                 fileChunks.size() - 1);
                    selectedIndexes.insert(candidate);
                }
            }
        }

        QList<int> orderedChunkIndexes = selectedIndexes.values();
        std::sort(orderedChunkIndexes.begin(), orderedChunkIndexes.end());

        int emittedForFile = 0;
        for (const int selectedIndex : orderedChunkIndexes) {
            if (emittedForFile >= desired) {
                break;
            }
            const Chunk *chunk = fileChunks.at(selectedIndex);

            RagHit hit;
            hit.filePath = chunk->filePath;
            hit.fileName = chunk->fileName;
            hit.sourceType = chunk->sourceType;
            hit.sourceRole = chunk->sourceRole;
            hit.excerpt = makeExcerpt(chunk->text, QStringList());
            hit.chunkText = chunk->text;
            hit.matchReason = chunkLooksLikeStructure(chunk->text)
                    ? QStringLiteral("document structure / headings coverage")
                    : QStringLiteral("representative document coverage");
            hit.lexicalScore = 0.0;
            hit.semanticScore = 0.0;
            hit.rerankScore = 2.0 + (chunkLooksLikeStructure(chunk->text) ? 0.5 : 0.0);
            hit.score = qRound(hit.rerankScore * 100.0);
            hit.chunkIndex = chunk->chunkIndex;
            hits.push_back(hit);
            ++emittedForFile;
        }
    }

    return hits;
}

QString RagIndexer::formatDocumentStudyPrompt(const QStringList &preferredPaths,
                                                   int maxFiles,
                                                   int outlineLineLimit,
                                                   int maxCharsPerFile) const
{
    if (preferredPaths.isEmpty() || maxFiles <= 0) {
        return QString();
    }

    QSet<QString> selectedPaths;
    QStringList orderedPaths;
    for (const QString &path : preferredPaths) {
        const QString cleaned = QDir::cleanPath(path.trimmed());
        if (cleaned.isEmpty() || selectedPaths.contains(cleaned)) {
            continue;
        }
        selectedPaths.insert(cleaned);
        orderedPaths << cleaned;
        if (orderedPaths.size() >= maxFiles) {
            break;
        }
    }
    if (orderedPaths.isEmpty()) {
        return QString();
    }

    QHash<QString, QVector<const Chunk *>> chunksByPath;
    for (const Chunk &chunk : m_chunks) {
        const QString cleanedPath = QDir::cleanPath(chunk.filePath);
        if (selectedPaths.contains(cleanedPath)) {
            chunksByPath[cleanedPath].push_back(&chunk);
        }
    }

    auto rebuildFromChunks = [](QVector<const Chunk *> fileChunks) {
        if (fileChunks.isEmpty()) {
            return QString();
        }
        std::sort(fileChunks.begin(), fileChunks.end(), [](const Chunk *a, const Chunk *b) {
            return a->chunkIndex < b->chunkIndex;
        });

        QStringList parts;
        parts.reserve(fileChunks.size());
        for (const Chunk *chunk : std::as_const(fileChunks)) {
            const QString chunkText = chunk->text.trimmed();
            if (!chunkText.isEmpty()) {
                parts << chunkText;
            }
        }
        return normalizeBlockText(parts.join(QStringLiteral("\n\n")));
    };

    QStringList packets;
    packets.reserve(orderedPaths.size());
    for (const QString &path : std::as_const(orderedPaths)) {
        QFileInfo info(path);
        QString sourceType = detectSourceType(info);
        QString sourceRole = sourceType == QStringLiteral("doc") ? QStringLiteral("reference") : sourceType;
        for (const SourceInfo &source : m_sources) {
            if (QDir::cleanPath(source.filePath) == path) {
                sourceType = source.sourceType;
                sourceRole = source.sourceRole;
                break;
            }
        }

        QString text;
        QString extractor;
        readTextFile(path, &text, &extractor, nullptr);
        if (text.trimmed().isEmpty()) {
            text = rebuildFromChunks(chunksByPath.value(path));
            if (extractor.trimmed().isEmpty()) {
                extractor = QStringLiteral("chunk-rebuild");
            }
        }

        text = normalizeBlockText(text);
        if (text.isEmpty()) {
            continue;
        }

        QVector<const Chunk *> fileChunks = chunksByPath.value(path);
        std::sort(fileChunks.begin(), fileChunks.end(), [](const Chunk *a, const Chunk *b) {
            return a->chunkIndex < b->chunkIndex;
        });

        const QStringList outlineLines = extractDocumentOutlineLines(text, outlineLineLimit);
        const QString outlineText = outlineLines.isEmpty()
                ? QStringLiteral("<no explicit outline lines extracted>")
                : outlineLines.join(QStringLiteral("\n"));

        QStringList majorHeadings = extractMajorSectionHeadings(text, 64);
        if (majorHeadings.isEmpty()) {
            majorHeadings = extractMajorSectionHeadings(outlineText, 64);
        }

        struct SectionAnchor {
            QString heading;
            int chunkPos = -1;
        };

        auto headingBodyKey = [](QString heading) {
            heading = stripTrailingOutlinePageNumber(heading).trimmed();
            heading.remove(QRegularExpression(QStringLiteral(R"(^\d+[.)]?\s*)")));
            return heading.simplified().toLower();
        };

        auto combinedSectionPreview = [](const QVector<const Chunk *> &orderedFileChunks,
                                         int startPos,
                                         int endPosExclusive,
                                         int maxChars) {
            if (orderedFileChunks.isEmpty() || startPos < 0 || startPos >= orderedFileChunks.size() || maxChars <= 0) {
                return QString();
            }

            endPosExclusive = qBound(startPos + 1, endPosExclusive, orderedFileChunks.size());
            const QString marker = QStringLiteral("\n[... later in this section ...]\n");
            QString headText = orderedFileChunks.at(startPos)->text.trimmed();
            if (headText.size() < 700 && startPos + 1 < endPosExclusive) {
                headText = normalizeBlockText(headText + QStringLiteral("\n\n") + orderedFileChunks.at(startPos + 1)->text.trimmed());
            }
            headText = balancedTrimForStudy(headText, qMin(maxChars, qMax(500, static_cast<int>(maxChars * 0.68))));
            if (endPosExclusive - startPos <= 1 || headText.size() >= maxChars - 160) {
                return headText;
            }

            const int tailBudget = qMax(220, maxChars - headText.size() - marker.size());
            if (tailBudget < 220) {
                return headText;
            }

            QString tailText = orderedFileChunks.at(endPosExclusive - 1)->text.trimmed();
            if (tailText == headText) {
                return headText;
            }
            tailText = balancedTrimForStudy(tailText, tailBudget);
            if (tailText.trimmed().isEmpty()) {
                return headText;
            }
            return headText + marker + tailText;
        };

        QVector<SectionAnchor> anchors;
        for (const QString &heading : std::as_const(majorHeadings)) {
            const QString normalizedHeading = normalizeOutlineKey(stripTrailingOutlinePageNumber(heading));
            const QString bodyKey = headingBodyKey(heading);
            if (normalizedHeading.isEmpty()) {
                continue;
            }

            int bestChunkPos = -1;
            int bestScore = 0;
            for (int i = 0; i < fileChunks.size(); ++i) {
                const QString chunkText = fileChunks.at(i)->text;
                const QString loweredChunk = chunkText.toLower();
                int score = 0;
                if (!bodyKey.isEmpty() && loweredChunk.contains(bodyKey)) {
                    score += 4;
                }
                if (loweredChunk.contains(normalizedHeading)) {
                    score += 5;
                }
                const QStringList chunkHeadings = extractMajorSectionHeadings(chunkText, 4);
                for (const QString &chunkHeading : chunkHeadings) {
                    if (normalizeOutlineKey(chunkHeading) == normalizedHeading) {
                        score += 8;
                        break;
                    }
                }
                if (score >= bestScore && score > 0) {
                    bestScore = score;
                    bestChunkPos = i;
                }
            }

            if (bestChunkPos >= 0) {
                anchors.push_back({heading, bestChunkPos});
            }
        }

        std::sort(anchors.begin(), anchors.end(), [](const SectionAnchor &a, const SectionAnchor &b) {
            if (a.chunkPos != b.chunkPos) {
                return a.chunkPos < b.chunkPos;
            }
            return a.heading < b.heading;
        });

        QVector<SectionAnchor> uniqueAnchors;
        QSet<QString> seenAnchorKeys;
        for (const SectionAnchor &anchor : std::as_const(anchors)) {
            const QString anchorKey = normalizeOutlineKey(anchor.heading);
            if (anchor.chunkPos < 0 || seenAnchorKeys.contains(anchorKey)) {
                continue;
            }
            seenAnchorKeys.insert(anchorKey);
            uniqueAnchors.push_back(anchor);
        }
        anchors = uniqueAnchors;

        const bool hugeDocument = fileChunks.size() >= 2000 || text.size() >= 500000;
        if (hugeDocument && anchors.size() > 64) {
            QVector<SectionAnchor> sampledAnchors;
            sampledAnchors.reserve(64);
            for (int sampleIndex = 0; sampleIndex < 64; ++sampleIndex) {
                const int pos = qRound((anchors.size() - 1) * (sampleIndex / 63.0));
                if (pos < 0 || pos >= anchors.size()) {
                    continue;
                }
                if (!sampledAnchors.isEmpty() && sampledAnchors.constLast().chunkPos == anchors.at(pos).chunkPos) {
                    continue;
                }
                sampledAnchors.push_back(anchors.at(pos));
            }
            if (!sampledAnchors.isEmpty()) {
                anchors = sampledAnchors;
            }
        }

        const int effectiveMaxCharsPerFile = hugeDocument ? qMin(maxCharsPerFile, 18000) : maxCharsPerFile;
        const int previewBudget = hugeDocument ? 0 : qMin(8000, qMax(2400, effectiveMaxCharsPerFile / 8));
        const int coverageBudget = qMax(2400, effectiveMaxCharsPerFile - previewBudget - 800);
        const int minSectionChars = hugeDocument ? 400 : 800;
        const int maxSectionCharsCap = hugeDocument ? 1600 : 4000;
        QStringList coverageSections;
        int remainingBudget = coverageBudget;
        int remainingSections = anchors.size();
        for (int i = 0; i < anchors.size(); ++i) {
            const int startPos = anchors.at(i).chunkPos;
            const int endPosExclusive = (i + 1 < anchors.size()) ? anchors.at(i + 1).chunkPos : fileChunks.size();
            const int maxSectionChars = qBound(minSectionChars,
                                               remainingSections > 0 ? remainingBudget / remainingSections : remainingBudget,
                                               maxSectionCharsCap);
            const QString excerpt = combinedSectionPreview(fileChunks,
                                                          startPos,
                                                          endPosExclusive,
                                                          maxSectionChars);
            if (!excerpt.trimmed().isEmpty()) {
                const QString sectionBlock = QStringLiteral("[%1]\n%2")
                        .arg(stripTrailingOutlinePageNumber(anchors.at(i).heading), excerpt);
                coverageSections << sectionBlock;
                remainingBudget = qMax(0, remainingBudget - sectionBlock.size() - 2);
            }
            --remainingSections;
            if (remainingBudget <= 320) {
                break;
            }
        }

        QString sectionCoverageText = coverageSections.join(QStringLiteral("\n\n"));
        if (sectionCoverageText.trimmed().isEmpty()) {
            sectionCoverageText = QStringLiteral("<section sweep unavailable; falling back to balanced document preview>");
        }

        const int fullDocumentInlineThreshold = qMin(effectiveMaxCharsPerFile, 32000);
        const bool includeFullDocumentPreview = !hugeDocument && text.size() <= fullDocumentInlineThreshold;
        const QString fullDocumentPreview = includeFullDocumentPreview
                ? balancedTrimForStudy(text, previewBudget)
                : QString();

        QString packet = QStringLiteral(
                "=== DOCUMENT_STUDY_PACKET: %1 | role=%2 | type=%3 | extractor=%4 ===\n"
                "DOCUMENT_OUTLINE_MAP:\n%5\n\n"
                "SECTION_COVERAGE_PACKET:\n%6")
                .arg(info.fileName(),
                     sourceRole,
                     sourceType,
                     extractor.isEmpty() ? QStringLiteral("cache") : extractor,
                     outlineText,
                     sectionCoverageText);
        if (!fullDocumentPreview.trimmed().isEmpty()) {
            packet += QStringLiteral("\n\nFULL_DOCUMENT_TEXT:\n%1").arg(fullDocumentPreview);
        }
        packets << packet;
    }

    return packets.join(QStringLiteral("\n\n"));
}

QString RagIndexer::formatHitsForPrompt(const QVector<RagHit> &hits) const
{
    QStringList lines;
    for (const RagHit &hit : hits) {
        // Explicit source attribution on its own line so the model can cite
        // the file name in its answer rather than guessing.
        QString promptText = hit.chunkText.trimmed().isEmpty() ? hit.excerpt : hit.chunkText;
        if (hit.matchReason.contains(QStringLiteral("coverage"), Qt::CaseInsensitive)) {
            promptText = compactPreviewText(promptText, 900);
        }
        lines << QStringLiteral("--- Source: %1 | role=%2 | type=%3 | chunk=%4 | rerank=%5 ---\n%6")
                     .arg(hit.fileName,
                          hit.sourceRole,
                          hit.sourceType,
                          QString::number(hit.chunkIndex),
                          QString::number(hit.rerankScore, 'f', 2),
                          promptText);
    }
    return lines.join(QStringLiteral("\n\n"));
}

QString RagIndexer::formatHitsForUi(const QVector<RagHit> &hits) const
{
    if (hits.isEmpty()) {
        return QStringLiteral("<none>");
    }

    QStringList lines;
    for (const RagHit &hit : hits) {
        lines << QStringLiteral("File: %1").arg(hit.filePath);
        lines << QStringLiteral("Role: %1 | Type: %2").arg(hit.sourceRole, hit.sourceType);
        lines << QStringLiteral("Chunk: %1 | Rerank: %2 | Lexical: %3 | Embedding: %4")
                     .arg(hit.chunkIndex)
                     .arg(QString::number(hit.rerankScore, 'f', 2))
                     .arg(QString::number(hit.lexicalScore, 'f', 2))
                     .arg(QString::number(hit.semanticScore, 'f', 2));
        lines << QStringLiteral("Reason: %1").arg(hit.matchReason);
        lines << QStringLiteral("Excerpt: %1").arg(hit.excerpt);
        lines << QString();
    }
    return lines.join(QStringLiteral("\n"));
}

QString RagIndexer::embeddingBackendName() const
{
    return m_embeddingClient.backendName();
}

QString RagIndexer::formatInventoryForUi() const
{
    const QVector<ManifestCollection> manifestCollections = loadManifestCollections(m_docsRoot);
    if (m_sources.isEmpty() && manifestCollections.isEmpty()) {
        return QStringLiteral("<none>");
    }

    QHash<QString, QVector<const Chunk *>> chunksByPath;
    chunksByPath.reserve(m_sources.size());
    for (const Chunk &chunk : m_chunks) {
        chunksByPath[chunk.filePath].push_back(&chunk);
    }

    auto buildChunkPreviewArray = [&](const QString &filePath) {
        QJsonArray preview;
        const QVector<const Chunk *> chunkPointers = chunksByPath.value(filePath);
        if (chunkPointers.isEmpty()) {
            return preview;
        }

        QVector<qsizetype> selectedIndexes;
        if (chunkPointers.size() <= 8) {
            selectedIndexes.reserve(chunkPointers.size());
            for (int i = 0; i < chunkPointers.size(); ++i) {
                selectedIndexes.push_back(i);
            }
        } else {
            selectedIndexes = {0, 1, 2, 3,
                               chunkPointers.size() - 2,
                               chunkPointers.size() - 1};
        }

        int lastIndex = -1;
        for (const int idx : std::as_const(selectedIndexes)) {
            if (idx < 0 || idx >= chunkPointers.size() || idx == lastIndex) {
                continue;
            }
            lastIndex = idx;
            const Chunk *chunk = chunkPointers.at(idx);
            if (chunk == nullptr) {
                continue;
            }
            QJsonObject obj;
            obj.insert(QStringLiteral("chunkIndex"), chunk->chunkIndex);
            obj.insert(QStringLiteral("charCount"), chunk->text.size());
            obj.insert(QStringLiteral("wordCount"), countWordsInText(chunk->text));
            obj.insert(QStringLiteral("text"), compactPreviewText(chunk->text, 900));
            preview.push_back(obj);
        }
        return preview;
    };

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("amelia-kb-inventory-v6"));
    root.insert(QStringLiteral("knowledgeRoot"), m_docsRoot);
    root.insert(QStringLiteral("collectionsRoot"), collectionsRootFor(m_docsRoot));
    root.insert(QStringLiteral("workspaceJailRoot"), QFileInfo(m_docsRoot).dir().absolutePath());
    root.insert(QStringLiteral("sources"), m_sources.size());
    root.insert(QStringLiteral("chunks"), m_chunks.size());
    qint64 totalBytes = 0;
    for (const SourceInfo &source : m_sources) {
        totalBytes += source.fileSizeBytes;
    }
    root.insert(QStringLiteral("totalBytes"), static_cast<double>(totalBytes));
    root.insert(QStringLiteral("semanticEnabled"), m_semanticEnabled);
    root.insert(QStringLiteral("embeddingBackend"), m_embeddingClient.backendName());
    root.insert(QStringLiteral("chunkingStrategy"), chunkingStrategyName());

    QJsonArray collections;
    QHash<QString, int> collectionIndexes;
    QHash<QString, QHash<QString, int>> groupIndexesByCollection;

    auto ensureCollection = [&](const QString &collectionId,
                                const QString &collectionLabel,
                                const QString &createdAt) {
        int collectionIndex = collectionIndexes.value(collectionId, -1);
        if (collectionIndex >= 0) {
            return collectionIndex;
        }

        QJsonObject collection;
        collection.insert(QStringLiteral("collectionId"), collectionId);
        collection.insert(QStringLiteral("label"), collectionLabel);
        collection.insert(QStringLiteral("createdAt"), createdAt);
        collection.insert(QStringLiteral("collectionRoot"), QDir(collectionsRootFor(m_docsRoot)).filePath(collectionId));
        collection.insert(QStringLiteral("fileCount"), 0);
        collection.insert(QStringLiteral("chunkCount"), 0);
        collection.insert(QStringLiteral("totalBytes"), 0.0);
        collection.insert(QStringLiteral("groups"), QJsonArray());
        collections.push_back(collection);
        collectionIndex = collections.size() - 1;
        collectionIndexes.insert(collectionId, collectionIndex);
        return collectionIndex;
    };

    for (const ManifestCollection &collection : manifestCollections) {
        ensureCollection(collection.collectionId,
                         collection.label.trimmed().isEmpty() ? QStringLiteral("Imported collection") : collection.label,
                         collection.createdAt);
    }

    for (const SourceInfo &source : m_sources) {
        const QString collectionId = source.collectionId.trimmed().isEmpty() ? QStringLiteral("legacy") : source.collectionId;
        const QString collectionLabel = source.collectionLabel.trimmed().isEmpty() ? QStringLiteral("Legacy imports") : source.collectionLabel;
        const int collectionIndex = ensureCollection(collectionId, collectionLabel, QString());

        QJsonObject collection = collections.at(collectionIndex).toObject();
        QJsonArray groups = collection.value(QStringLiteral("groups")).toArray();

        const QString groupId = source.groupId.trimmed().isEmpty()
                ? stableHashHex(collectionId + QStringLiteral("|") + source.groupLabel)
                : source.groupId;
        const QString groupLabel = source.groupLabel.trimmed().isEmpty()
                ? QStringLiteral("(root)")
                : source.groupLabel;

        int groupIndex = groupIndexesByCollection[collectionId].value(groupId, -1);
        if (groupIndex < 0) {
            QJsonObject group;
            group.insert(QStringLiteral("groupId"), groupId);
            group.insert(QStringLiteral("label"), groupLabel);
            group.insert(QStringLiteral("collectionId"), collectionId);
            group.insert(QStringLiteral("collectionLabel"), collectionLabel);
            group.insert(QStringLiteral("folderPath"), QDir(QDir(collectionsRootFor(m_docsRoot)).filePath(collectionId)).filePath(groupLabel == QStringLiteral("(root)") ? QString() : groupLabel));
            group.insert(QStringLiteral("fileCount"), 0);
            group.insert(QStringLiteral("chunkCount"), 0);
            group.insert(QStringLiteral("totalBytes"), 0.0);
            group.insert(QStringLiteral("files"), QJsonArray());
            groups.push_back(group);
            groupIndex = groups.size() - 1;
            groupIndexesByCollection[collectionId].insert(groupId, groupIndex);
        }

        QJsonObject group = groups.at(groupIndex).toObject();
        QJsonArray files = group.value(QStringLiteral("files")).toArray();

        QJsonObject file;
        file.insert(QStringLiteral("filePath"), source.filePath);
        file.insert(QStringLiteral("fileName"), source.fileName);
        file.insert(QStringLiteral("relativePath"), source.relativePath);
        file.insert(QStringLiteral("originalPath"), source.originalPath);
        file.insert(QStringLiteral("sourceRole"), source.sourceRole);
        file.insert(QStringLiteral("sourceType"), source.sourceType);
        file.insert(QStringLiteral("extractor"), source.extractor);
        file.insert(QStringLiteral("chunkCount"), source.chunkCount);
        file.insert(QStringLiteral("fileSizeBytes"), static_cast<double>(source.fileSizeBytes));
        file.insert(QStringLiteral("fileModifiedMs"), static_cast<double>(source.fileModifiedMs));
        file.insert(QStringLiteral("extension"), QFileInfo(source.fileName).suffix().toLower());
        file.insert(QStringLiteral("lineCount"), source.lineCount);
        file.insert(QStringLiteral("wordCount"), source.wordCount);
        file.insert(QStringLiteral("textCharCount"), source.textCharCount);
        file.insert(QStringLiteral("chunkingProfile"), source.chunkingProfile);
        file.insert(QStringLiteral("zeroChunkReason"), source.zeroChunkReason);
        file.insert(QStringLiteral("collectionId"), collectionId);
        file.insert(QStringLiteral("collectionLabel"), collectionLabel);
        file.insert(QStringLiteral("groupId"), groupId);
        file.insert(QStringLiteral("groupLabel"), groupLabel);
        const QJsonArray chunkPreview = buildChunkPreviewArray(source.filePath);
        file.insert(QStringLiteral("chunksPreview"), chunkPreview);
        file.insert(QStringLiteral("previewChunkCount"), chunkPreview.size());
        file.insert(QStringLiteral("omittedChunkCount"), qMax(0, source.chunkCount - chunkPreview.size()));
        files.push_back(file);

        group.insert(QStringLiteral("files"), files);
        group.insert(QStringLiteral("fileCount"), files.size());
        group.insert(QStringLiteral("chunkCount"), group.value(QStringLiteral("chunkCount")).toInt() + source.chunkCount);
        group.insert(QStringLiteral("totalBytes"), group.value(QStringLiteral("totalBytes")).toDouble() + static_cast<double>(source.fileSizeBytes));
        groups.replace(groupIndex, group);

        collection.insert(QStringLiteral("groups"), groups);
        collection.insert(QStringLiteral("fileCount"), collection.value(QStringLiteral("fileCount")).toInt() + 1);
        collection.insert(QStringLiteral("chunkCount"), collection.value(QStringLiteral("chunkCount")).toInt() + source.chunkCount);
        collection.insert(QStringLiteral("totalBytes"), collection.value(QStringLiteral("totalBytes")).toDouble() + static_cast<double>(source.fileSizeBytes));
        collections.replace(collectionIndex, collection);
    }

    root.insert(QStringLiteral("collections"), collections);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

int RagIndexer::chunkCount() const
{
    return m_chunks.size();
}

int RagIndexer::sourceCount() const
{
    return m_sources.size();
}

QStringList RagIndexer::supportedExtensions() const
{
    return extensions();
}

int RagIndexer::importPaths(const QStringList &paths, const QString &destinationRoot, const QString &label, QString *message) const
{
    if (paths.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("No knowledge assets were selected for import.");
        }
        return 0;
    }

    QString normalizedLabel = label.trimmed();
    if (normalizedLabel.isEmpty()) {
        normalizedLabel = QFileInfo(paths.constFirst()).completeBaseName().trimmed();
        if (normalizedLabel.isEmpty()) {
            normalizedLabel = QFileInfo(paths.constFirst()).fileName().trimmed();
        }
        if (normalizedLabel.isEmpty()) {
            normalizedLabel = QStringLiteral("Imported collection");
        }
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    if (labelExistsInManifest(collections, normalizedLabel)) {
        if (message != nullptr) {
            *message = QStringLiteral("Knowledge Base label '%1' already exists. Choose a unique label.").arg(normalizedLabel);
        }
        return 0;
    }

    QStringList canonicalInputs;
    for (const QString &path : paths) {
        const QString canonicalPath = canonicalPathFor(path);
        if (!canonicalPath.isEmpty() && !canonicalInputs.contains(canonicalPath)) {
            canonicalInputs.push_back(canonicalPath);
        }
    }
    canonicalInputs.sort();

    const QString createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString collectionId = stableHashHex(normalizedLabel + QStringLiteral("|") + createdAt + QStringLiteral("|") + canonicalInputs.join(QStringLiteral("|")));
    const QString collectionRoot = QDir(collectionsRootFor(destinationRoot)).filePath(collectionId);

    QDir().mkpath(collectionRoot);

    QVector<ManifestEntry> entries;
    QSet<QString> usedRelativePaths;
    int copied = 0;
    for (const QString &path : paths) {
        copied += importPathIntoCollection(path, collectionId, collectionRoot, &entries, &usedRelativePaths);
    }

    if (copied <= 0 || entries.isEmpty()) {
        QDir(collectionRoot).removeRecursively();
        if (message != nullptr) {
            *message = QStringLiteral("No supported files were imported into the Knowledge Base.");
        }
        return 0;
    }

    ManifestCollection collection;
    collection.collectionId = collectionId;
    collection.label = normalizedLabel;
    collection.createdAt = createdAt;
    collection.entries = entries;
    collections.push_back(collection);

    if (!saveManifestCollections(destinationRoot, collections)) {
        QDir(collectionRoot).removeRecursively();
        if (message != nullptr) {
            *message = QStringLiteral("Failed to update the Knowledge Base manifest for label '%1'.").arg(normalizedLabel);
        }
        return 0;
    }

    if (message != nullptr) {
        *message = QStringLiteral("Imported %1 file(s) into Knowledge Base label '%2'.").arg(copied).arg(normalizedLabel);
    }
    return copied;
}

int RagIndexer::addPathsToCollection(const QStringList &paths,
                                   const QString &destinationRoot,
                                   const QString &collectionId,
                                   QString *message) const
{
    if (paths.isEmpty() || collectionId.trimmed().isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("Collection import request is incomplete.");
        }
        return 0;
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    int collectionIndex = -1;
    for (int i = 0; i < collections.size(); ++i) {
        if (collections.at(i).collectionId == collectionId.trimmed()) {
            collectionIndex = i;
            break;
        }
    }
    if (collectionIndex < 0) {
        if (message != nullptr) {
            *message = QStringLiteral("Destination Knowledge Base collection was not found.");
        }
        return 0;
    }

    ManifestCollection updatedCollection = collections.at(collectionIndex);
    QSet<QString> usedRelativePaths;
    for (const ManifestEntry &entry : std::as_const(updatedCollection.entries)) {
        usedRelativePaths.insert(entry.relativePath);
    }

    const QString collectionRoot = QDir(collectionsRootFor(destinationRoot)).filePath(updatedCollection.collectionId);
    QDir().mkpath(collectionRoot);

    QVector<ManifestEntry> newEntries;
    int copied = 0;
    for (const QString &path : paths) {
        copied += importPathIntoCollection(path,
                                           updatedCollection.collectionId,
                                           collectionRoot,
                                           &newEntries,
                                           &usedRelativePaths);
    }

    if (copied <= 0 || newEntries.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("No supported files were added to Knowledge Base collection '%1'.").arg(updatedCollection.label);
        }
        return 0;
    }

    updatedCollection.entries += newEntries;
    collections[collectionIndex] = updatedCollection;
    if (!saveManifestCollections(destinationRoot, collections)) {
        for (const ManifestEntry &entry : std::as_const(newEntries)) {
            QFile::remove(entry.internalPath);
        }
        if (message != nullptr) {
            *message = QStringLiteral("Failed to update the Knowledge Base manifest for collection '%1'.").arg(updatedCollection.label);
        }
        return 0;
    }

    if (message != nullptr) {
        *message = QStringLiteral("Added %1 file(s) to Knowledge Base collection '%2'.").arg(copied).arg(updatedCollection.label);
    }
    return copied;
}

int RagIndexer::removeKnowledgePaths(const QStringList &paths, const QString &destinationRoot, QString *message) const
{
    if (paths.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("No Knowledge Base assets were selected.");
        }
        return 0;
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    const QString canonicalRoot = canonicalPathFor(destinationRoot);
    const QString canonicalCollectionsRoot = canonicalPathFor(collectionsRootFor(destinationRoot));

    QSet<QString> removedCanonicalPaths;
    int removed = 0;
    for (const QString &path : paths) {
        const QString canonicalPath = canonicalPathFor(path);
        if (canonicalPath.isEmpty() || canonicalRoot.isEmpty()) {
            continue;
        }
        if (!(canonicalPath == canonicalRoot || canonicalPath.startsWith(canonicalRoot + QDir::separator()))) {
            continue;
        }
        if (QFile::remove(canonicalPath)) {
            ++removed;
            removedCanonicalPaths.insert(canonicalPath);
            if (!canonicalCollectionsRoot.isEmpty()) {
                pruneEmptyKnowledgeDirectories(canonicalPath, canonicalCollectionsRoot);
            }
        }
    }

    if (!removedCanonicalPaths.isEmpty()) {
        QVector<ManifestCollection> filteredCollections;
        filteredCollections.reserve(collections.size());
        for (const ManifestCollection &collection : collections) {
            ManifestCollection filtered = collection;
            filtered.entries.clear();
            for (const ManifestEntry &entry : collection.entries) {
                if (!removedCanonicalPaths.contains(canonicalPathFor(entry.internalPath))) {
                    filtered.entries.push_back(entry);
                }
            }
            if (!filtered.entries.isEmpty()) {
                filteredCollections.push_back(filtered);
            }
        }
        saveManifestCollections(destinationRoot, filteredCollections);
    }

    if (message != nullptr) {
        *message = removed > 0
                ? QStringLiteral("Removed %1 Knowledge Base asset(s).").arg(removed)
                : QStringLiteral("No Knowledge Base assets were removed.");
    }
    return removed;
}


int RagIndexer::moveKnowledgePaths(const QStringList &paths,
                                   const QString &destinationRoot,
                                   const QString &targetCollectionId,
                                   const QString &targetGroupLabel,
                                   QString *message) const
{
    if (paths.isEmpty() || targetCollectionId.trimmed().isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("Move request is incomplete.");
        }
        return 0;
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    int targetCollectionIndex = -1;
    for (int i = 0; i < collections.size(); ++i) {
        if (collections.at(i).collectionId == targetCollectionId) {
            targetCollectionIndex = i;
            break;
        }
    }
    if (targetCollectionIndex < 0) {
        if (message != nullptr) {
            *message = QStringLiteral("Destination Knowledge Base collection was not found.");
        }
        return 0;
    }

    QSet<QString> selectedPaths;
    for (const QString &path : paths) {
        const QString canonical = canonicalPathFor(path);
        if (!canonical.isEmpty()) {
            selectedPaths.insert(canonical);
        }
    }
    if (selectedPaths.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("No valid Knowledge Base assets were selected for moving.");
        }
        return 0;
    }

    QString normalizedTargetGroupLabel = targetGroupLabel.trimmed();
    if (normalizedTargetGroupLabel == QStringLiteral("(root)")) {
        normalizedTargetGroupLabel.clear();
    }
    if (!normalizedTargetGroupLabel.isEmpty()) {
        normalizedTargetGroupLabel = QDir::cleanPath(QDir::fromNativeSeparators(normalizedTargetGroupLabel));
    }

    QSet<QString> targetUsedRelativePaths;
    for (const ManifestEntry &entry : collections.at(targetCollectionIndex).entries) {
        targetUsedRelativePaths.insert(entry.relativePath);
    }
    for (const ManifestEntry &entry : collections.at(targetCollectionIndex).entries) {
        if (selectedPaths.contains(canonicalPathFor(entry.internalPath))) {
            targetUsedRelativePaths.remove(entry.relativePath);
        }
    }

    const QString targetCollectionRoot = QDir(collectionsRootFor(destinationRoot)).filePath(targetCollectionId);
    QDir().mkpath(targetCollectionRoot);

    struct FileMoveRecord {
        QString sourcePath;
        QString destinationPath;
    };

    QVector<ManifestEntry> movedEntries;
    QVector<FileMoveRecord> successfulMoves;
    QStringList movedSourcePaths;
    int moved = 0;
    int failed = 0;

    for (ManifestCollection &collection : collections) {
        QVector<ManifestEntry> remainingEntries;
        remainingEntries.reserve(collection.entries.size());

        for (const ManifestEntry &entry : std::as_const(collection.entries)) {
            const QString canonicalInternalPath = canonicalPathFor(entry.internalPath);
            if (!selectedPaths.contains(canonicalInternalPath)) {
                remainingEntries.push_back(entry);
                continue;
            }

            const QFileInfo entryInfo(entry.internalPath);
            QString desiredRelativePath = normalizedTargetGroupLabel.isEmpty()
                    ? entry.relativePath
                    : QDir(normalizedTargetGroupLabel).filePath(entryInfo.fileName());
            if (desiredRelativePath.trimmed().isEmpty()) {
                desiredRelativePath = entryInfo.fileName();
            }
            desiredRelativePath = QDir::cleanPath(QDir::fromNativeSeparators(desiredRelativePath));

            if (collection.collectionId == targetCollectionId && entry.relativePath == desiredRelativePath) {
                remainingEntries.push_back(entry);
                continue;
            }

            const QString uniqueRelativePath = ensureUniqueRelativePath(desiredRelativePath, &targetUsedRelativePaths);
            const QString destinationPath = QDir(targetCollectionRoot).filePath(uniqueRelativePath);
            if (!moveStoredKnowledgeFile(entry.internalPath, destinationPath)) {
                remainingEntries.push_back(entry);
                ++failed;
                continue;
            }

            ManifestEntry movedEntry = entry;
            movedEntry.internalPath = canonicalPathFor(destinationPath);
            movedEntry.relativePath = uniqueRelativePath;
            movedEntry.groupLabel = groupLabelFromRelativePath(uniqueRelativePath);
            movedEntry.groupId = stableHashHex(targetCollectionId + QStringLiteral("|") + movedEntry.groupLabel);
            movedEntries.push_back(movedEntry);
            successfulMoves.push_back({canonicalInternalPath, canonicalPathFor(destinationPath)});
            movedSourcePaths.push_back(canonicalInternalPath);
            ++moved;
        }

        collection.entries = remainingEntries;
    }

    if (movedEntries.isEmpty()) {
        if (message != nullptr) {
            *message = failed > 0
                    ? QStringLiteral("Failed to move the selected Knowledge Base asset(s).")
                    : QStringLiteral("No Knowledge Base assets were moved.");
        }
        return 0;
    }

    collections[targetCollectionIndex].entries += movedEntries;

    QVector<ManifestCollection> filteredCollections;
    filteredCollections.reserve(collections.size());
    for (const ManifestCollection &collection : std::as_const(collections)) {
        filteredCollections.push_back(collection);
    }

    if (!saveManifestCollections(destinationRoot, filteredCollections)) {
        for (auto it = successfulMoves.crbegin(); it != successfulMoves.crend(); ++it) {
            moveStoredKnowledgeFile(it->destinationPath, it->sourcePath);
        }
        if (message != nullptr) {
            *message = QStringLiteral("Failed to persist the Knowledge Base move operation.");
        }
        return 0;
    }

    const QString canonicalCollectionsRoot = canonicalPathFor(collectionsRootFor(destinationRoot));
    for (const QString &path : std::as_const(movedSourcePaths)) {
        if (!canonicalCollectionsRoot.isEmpty()) {
            pruneEmptyKnowledgeDirectories(path, canonicalCollectionsRoot);
        }
    }

    QString destinationLabel = targetCollectionId;
    for (const ManifestCollection &collection : std::as_const(filteredCollections)) {
        if (collection.collectionId == targetCollectionId) {
            destinationLabel = collection.label;
            break;
        }
    }

    if (message != nullptr) {
        *message = failed > 0
                ? QStringLiteral("Moved %1 Knowledge Base asset(s) to '%2' (%3 failed).")
                      .arg(moved)
                      .arg(destinationLabel)
                      .arg(failed)
                : QStringLiteral("Moved %1 Knowledge Base asset(s) to '%2'.")
                      .arg(moved)
                      .arg(destinationLabel);
    }

    return moved;
}

bool RagIndexer::createCollection(const QString &destinationRoot, const QString &label, QString *message) const
{
    const QString normalizedLabel = label.trimmed();
    if (normalizedLabel.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("Collection label is empty.");
        }
        return false;
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    if (labelExistsInManifest(collections, normalizedLabel)) {
        if (message != nullptr) {
            *message = QStringLiteral("Knowledge Base label '%1' already exists. Choose a unique label.").arg(normalizedLabel);
        }
        return false;
    }

    const QString createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString collectionId = stableHashHex(normalizedLabel + QStringLiteral("|") + createdAt + QStringLiteral("|empty"));
    ManifestCollection collection;
    collection.collectionId = collectionId;
    collection.label = normalizedLabel;
    collection.createdAt = createdAt;
    collections.push_back(collection);

    const QString collectionRoot = QDir(collectionsRootFor(destinationRoot)).filePath(collectionId);
    QDir().mkpath(collectionRoot);
    if (!saveManifestCollections(destinationRoot, collections)) {
        QDir(collectionRoot).removeRecursively();
        if (message != nullptr) {
            *message = QStringLiteral("Failed to create Knowledge Base collection '%1'.").arg(normalizedLabel);
        }
        return false;
    }

    if (message != nullptr) {
        *message = QStringLiteral("Created Knowledge Base collection '%1'.").arg(normalizedLabel);
    }
    return true;
}

bool RagIndexer::deleteCollection(const QString &destinationRoot, const QString &collectionId, QString *message) const
{
    if (collectionId.trimmed().isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("Collection id is empty.");
        }
        return false;
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    int removedEntries = 0;
    QString removedLabel;
    bool found = false;

    QVector<ManifestCollection> filteredCollections;
    filteredCollections.reserve(collections.size());
    for (const ManifestCollection &collection : std::as_const(collections)) {
        if (collection.collectionId == collectionId) {
            found = true;
            removedEntries = collection.entries.size();
            removedLabel = collection.label;
            continue;
        }
        filteredCollections.push_back(collection);
    }

    if (!found) {
        if (message != nullptr) {
            *message = QStringLiteral("Knowledge Base collection was not found.");
        }
        return false;
    }

    const QString collectionRoot = QDir(collectionsRootFor(destinationRoot)).filePath(collectionId);
    const bool removedFiles = !QFileInfo::exists(collectionRoot) || QDir(collectionRoot).removeRecursively();
    if (!saveManifestCollections(destinationRoot, filteredCollections)) {
        if (message != nullptr) {
            *message = QStringLiteral("Failed to update the Knowledge Base manifest after deleting '%1'.").arg(removedLabel);
        }
        return false;
    }

    if (message != nullptr) {
        *message = removedFiles
                ? QStringLiteral("Deleted Knowledge Base collection '%1' (%2 asset(s)).").arg(removedLabel).arg(removedEntries)
                : QStringLiteral("Removed collection '%1' from the manifest, but some stored files could not be deleted.").arg(removedLabel);
    }
    return true;
}

bool RagIndexer::renameKnowledgePath(const QString &path, const QString &destinationRoot, const QString &newFileName, QString *message) const
{
    const QString canonicalPath = canonicalPathFor(path);
    const QString normalizedFileName = QFileInfo(newFileName.trimmed()).fileName().trimmed();
    if (canonicalPath.isEmpty() || normalizedFileName.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("Asset path or new file name is empty.");
        }
        return false;
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    for (ManifestCollection &collection : collections) {
        const QString collectionRoot = QDir(collectionsRootFor(destinationRoot)).filePath(collection.collectionId);
        QSet<QString> usedRelativePaths;
        for (const ManifestEntry &entry : collection.entries) {
            if (canonicalPathFor(entry.internalPath) != canonicalPath) {
                usedRelativePaths.insert(entry.relativePath);
            }
        }

        for (ManifestEntry &entry : collection.entries) {
            if (canonicalPathFor(entry.internalPath) != canonicalPath) {
                continue;
            }

            const QString relativeDir = QFileInfo(entry.relativePath).path() == QStringLiteral(".")
                    ? QString()
                    : QFileInfo(entry.relativePath).path();
            const QString desiredRelativePath = relativeDir.isEmpty()
                    ? normalizedFileName
                    : QDir(relativeDir).filePath(normalizedFileName);
            const QString uniqueRelativePath = ensureUniqueRelativePath(desiredRelativePath, &usedRelativePaths);
            const QString destinationPath = QDir(collectionRoot).filePath(uniqueRelativePath);
            if (canonicalPathFor(destinationPath) == canonicalPath) {
                if (message != nullptr) {
                    *message = QStringLiteral("Asset already uses that name.");
                }
                return false;
            }
            if (!moveStoredKnowledgeFile(entry.internalPath, destinationPath)) {
                if (message != nullptr) {
                    *message = QStringLiteral("Failed to rename the selected Knowledge Base asset.");
                }
                return false;
            }

            const QString previousPath = entry.internalPath;
            const QString previousRelativePath = entry.relativePath;
            const QString previousGroupLabel = entry.groupLabel;
            const QString previousGroupId = entry.groupId;
            entry.internalPath = canonicalPathFor(destinationPath);
            entry.relativePath = uniqueRelativePath;
            entry.groupLabel = groupLabelFromRelativePath(uniqueRelativePath);
            entry.groupId = stableHashHex(collection.collectionId + QStringLiteral("|") + entry.groupLabel);

            if (!saveManifestCollections(destinationRoot, collections)) {
                moveStoredKnowledgeFile(entry.internalPath, previousPath);
                entry.internalPath = previousPath;
                entry.relativePath = previousRelativePath;
                entry.groupLabel = previousGroupLabel;
                entry.groupId = previousGroupId;
                if (message != nullptr) {
                    *message = QStringLiteral("Failed to persist the Knowledge Base asset rename.");
                }
                return false;
            }

            if (message != nullptr) {
                *message = QStringLiteral("Renamed Knowledge Base asset to '%1'.").arg(normalizedFileName);
            }
            return true;
        }
    }

    if (message != nullptr) {
        *message = QStringLiteral("Knowledge Base asset was not found.");
    }
    return false;
}

bool RagIndexer::clearKnowledgeLibrary(const QString &destinationRoot, QString *message) const
{
    QDir rootDir(destinationRoot);
    const bool existed = rootDir.exists();
    const bool removed = !existed || rootDir.removeRecursively();
    QDir().mkpath(destinationRoot);
    QDir().mkpath(collectionsRootFor(destinationRoot));

    if (message != nullptr) {
        *message = removed
                ? QStringLiteral("Knowledge Base cleared.")
                : QStringLiteral("Failed to clear the Knowledge Base.");
    }
    return removed;
}


bool RagIndexer::renameCollectionLabel(const QString &destinationRoot, const QString &collectionId, const QString &newLabel, QString *message)
{
    const QString normalizedLabel = newLabel.trimmed();
    if (collectionId.trimmed().isEmpty() || normalizedLabel.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("Collection id or label is empty.");
        }
        return false;
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    if (labelExistsInManifest(collections, normalizedLabel, collectionId)) {
        if (message != nullptr) {
            *message = QStringLiteral("Knowledge Base label '%1' already exists. Choose a unique label.").arg(normalizedLabel);
        }
        return false;
    }

    bool updated = false;
    for (ManifestCollection &collection : collections) {
        if (collection.collectionId == collectionId) {
            collection.label = normalizedLabel;
            updated = true;
            break;
        }
    }

    if (!updated) {
        if (message != nullptr) {
            *message = QStringLiteral("Knowledge Base collection was not found.");
        }
        return false;
    }

    if (!saveManifestCollections(destinationRoot, collections)) {
        if (message != nullptr) {
            *message = QStringLiteral("Failed to persist the updated Knowledge Base label.");
        }
        return false;
    }

    for (SourceInfo &source : m_sources) {
        if (source.collectionId == collectionId) {
            source.collectionLabel = normalizedLabel;
        }
    }

    if (message != nullptr) {
        *message = QStringLiteral("Renamed Knowledge Base label to '%1'.").arg(normalizedLabel);
    }
    return true;
}

