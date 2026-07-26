#include "rag/semanticchunker.h"

#include "rag/cancellation.h"
#include "rag/embeddingclient.h"
#include "rag/textcleanup.h"

#include <QRegularExpression>

bool isPageMarkerLine(const QString &trimmed)
{
    return trimmed.startsWith(QStringLiteral("[[PAGE ")) && trimmed.endsWith(QStringLiteral("]]"));
}

namespace {
bool isFenceDelimiter(const QString &trimmed)
{
    return trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~"));
}
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

namespace {
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

bool isProceduralLeadLine(const QString &trimmed)
{
    if (trimmed.isEmpty() || isPageMarkerLine(trimmed)) {
        return false;
    }

    static const QRegularExpression numberedProcedure(
            QString::fromLatin1(R"(^(?:step\s+\d+[:.)]?|\d+(?:\.\d+){0,4}[.)]?|[a-zA-Z][.)])\s+(?:run|execute|apply|install|configure|create|set|verify|check|edit|copy|add|remove|delete|update|enable|disable|start|stop|restart|import|export|move|rename|assign|pull|push|boot|deploy|bootstrap|connect|mount|unmount|launch|open|select|choose|enter|type|use)\b[^\n]{0,220}:?$)"),
            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression imperativeLead(
            QString::fromLatin1(R"(^(?:run|execute|apply|install|configure|create|set|verify|check|edit|copy|add|remove|delete|update|enable|disable|start|stop|restart|import|export|move|rename|assign|pull|push|boot|deploy|bootstrap|connect|mount|unmount|launch|open|select|choose|enter|type|use)\b[^\n]{0,220}:$)"),
            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression genericStep(
            QString::fromLatin1(R"(^(?:step\s+\d+[:.)]?|\d+(?:\.\d+){0,4}[.)]?)\s+[^\n]{2,220}:$)"),
            QRegularExpression::CaseInsensitiveOption);

    return numberedProcedure.match(trimmed).hasMatch()
            || imperativeLead.match(trimmed).hasMatch()
            || genericStep.match(trimmed).hasMatch();
}

bool currentBlockLooksProcedural(const QStringList &currentLines)
{
    for (int i = currentLines.size() - 1, checked = 0; i >= 0 && checked < 3; --i) {
        const QString trimmed = currentLines.at(i).trimmed();
        if (trimmed.isEmpty() || isPageMarkerLine(trimmed)) {
            continue;
        }
        ++checked;
        if (isProceduralLeadLine(trimmed) || isStructuredCodeLikeLine(currentLines.at(i))) {
            return true;
        }
    }
    return false;
}
}

bool blockEndsWithProceduralLead(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.isEmpty() || isPageMarkerLine(trimmed)) {
            continue;
        }
        return isProceduralLeadLine(trimmed);
    }
    return false;
}

bool blockContainsStructuredContent(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || isPageMarkerLine(trimmed)) {
            continue;
        }
        if (isBulletLine(trimmed) || isStructuredCodeLikeLine(line)) {
            return true;
        }
    }
    return false;
}

QString normalizeBlockText(const QString &text)
{
    return collapseExcessBlankLines(trimTrailingWhitespacePerLine(text)).trimmed();
}

QString compactPreviewText(QString text, int maxChars)
{
    text = normalizeBlockText(text);
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(qMax(80, maxChars)).trimmed() + QStringLiteral(" …");
}

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

namespace {
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
}

QVector<QString> buildSemanticBlocks(const QString &text, const std::atomic_bool *cancelRequested)
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
            if (currentLines.isEmpty()) {
                flushCurrent();
                pendingPrefix = trimmed;
            } else {
                pendingPrefix = trimmed;
                if (!currentBlockLooksProcedural(currentLines)) {
                    flushCurrent();
                }
            }
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
            if (!pendingPrefix.isEmpty() && !currentLines.isEmpty()) {
                continue;
            }
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
            const bool previousProceduralLead = isProceduralLeadLine(previousTrimmed);
            if (!previousStructured && !previousProceduralLead) {
                flushCurrent();
            }
        }

        if (!pendingPrefix.isEmpty()) {
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

namespace {
QVector<float> centroidEmbeddingForIndexes(const QVector<int> &indexes, const QVector<QVector<float>> &blockEmbeddings)
{
    QVector<float> centroid;
    for (int index : indexes) {
        if (index < 0 || index >= blockEmbeddings.size()) {
            continue;
        }
        const QVector<float> &embedding = blockEmbeddings.at(index);
        if (embedding.isEmpty()) {
            continue;
        }
        if (centroid.isEmpty()) {
            centroid = QVector<float>(embedding.size(), 0.0f);
        }
        for (int i = 0; i < centroid.size() && i < embedding.size(); ++i) {
            centroid[i] += embedding.at(i);
        }
    }
    return centroid;
}

int charsForIndexes(const QVector<QString> &blocks, const QVector<int> &indexes)
{
    int total = 0;
    for (int index : indexes) {
        total += blocks.at(index).size();
    }
    if (indexes.size() > 1) {
        total += (indexes.size() - 1) * 2;
    }
    return total;
}

QVector<int> overlapTailIndexes(const QVector<QString> &blocks,
                                const QVector<int> &indexes,
                                int overlapChars,
                                const std::atomic_bool *cancelRequested = nullptr)
{
    QVector<int> carry;
    int carryChars = 0;
    for (int i = indexes.size() - 1; i >= 0; --i) {
        if (isCancelRequested(cancelRequested)) {
            return {};
        }
        const int index = indexes.at(i);
        const int blockChars = blocks.at(index).size() + (carry.isEmpty() ? 0 : 2);
        if (!carry.isEmpty() && carryChars + blockChars > overlapChars * 2) {
            break;
        }
        carry.prepend(index);
        carryChars += blockChars;
        if (carryChars >= overlapChars) {
            break;
        }
    }
    return carry;
}
}

QVector<SemanticChunk> buildSemanticChunksFromBlocks(const QVector<QString> &blocks,
                                                     const QVector<QVector<float>> &blockEmbeddings,
                                                     const ChunkingProfile &profile,
                                                     const std::atomic_bool *cancelRequested)
{
    QVector<SemanticChunk> chunks;
    if (blocks.isEmpty()) {
        return chunks;
    }

    constexpr float kBreakpointSimilarity = 0.28f;
    const bool haveEmbeddings = blockEmbeddings.size() == blocks.size();

    QVector<int> currentIndexes;
    int currentChars = 0;

    auto flushChunk = [&]() {
        if (currentIndexes.isEmpty()) {
            return;
        }
        QStringList parts;
        parts.reserve(currentIndexes.size());
        for (int index : currentIndexes) {
            parts << blocks.at(index);
        }
        const QString text = normalizeBlockText(parts.join(QStringLiteral("\n\n")));
        if (!text.isEmpty()) {
            SemanticChunk chunk;
            chunk.text = text;
            if (haveEmbeddings) {
                chunk.embedding = centroidEmbeddingForIndexes(currentIndexes, blockEmbeddings);
            }
            chunks.push_back(std::move(chunk));
        }
    };

    for (int index = 0; index < blocks.size(); ++index) {
        if (isCancelRequested(cancelRequested)) {
            return {};
        }
        const QString &block = blocks.at(index);
        if (block.trimmed().isEmpty()) {
            continue;
        }

        if (!currentIndexes.isEmpty()) {
            const int projected = currentChars + 2 + block.size();
            const bool overHard = projected > profile.hardChunkChars;

            bool shouldSplit = false;
            if (!overHard && currentChars >= profile.minimumChunkChars) {
                if (haveEmbeddings) {
                    // Only consider a similarity-driven cut once the chunk has
                    // already reached a reasonable size; below that, always merge.
                    if (currentChars >= profile.targetChunkChars) {
                        const QVector<float> &nextEmbedding = blockEmbeddings.at(index);
                        if (!nextEmbedding.isEmpty()) {
                            const QVector<float> centroid = centroidEmbeddingForIndexes(currentIndexes, blockEmbeddings);
                            if (!centroid.isEmpty()) {
                                const float similarity = EmbeddingClient::cosineSimilarity(centroid, nextEmbedding);
                                shouldSplit = similarity < kBreakpointSimilarity;
                            }
                        }
                    }
                } else {
                    // No embedding signal at all (semantic disabled): fall back to
                    // the previous purely char-budget-driven boundary.
                    shouldSplit = projected > profile.targetChunkChars;
                }
            }

            const bool mustSplit = overHard || shouldSplit;
            if (mustSplit) {
                const QVector<int> carry = overlapTailIndexes(blocks, currentIndexes, profile.overlapChars, cancelRequested);
                flushChunk();
                currentIndexes = carry;
                currentChars = charsForIndexes(blocks, currentIndexes);
            }
        }

        if (block.size() > profile.hardChunkChars && currentIndexes.isEmpty()) {
            const QStringList slices = splitOversizedBlock(block, profile.targetChunkChars, cancelRequested);
            for (const QString &slice : slices) {
                if (slice.trimmed().isEmpty()) {
                    continue;
                }
                SemanticChunk chunk;
                chunk.text = slice;
                if (haveEmbeddings && !blockEmbeddings.at(index).isEmpty()) {
                    chunk.embedding = blockEmbeddings.at(index);
                }
                chunks.push_back(std::move(chunk));
            }
            continue;
        }

        currentIndexes << index;
        currentChars = charsForIndexes(blocks, currentIndexes);
    }

    flushChunk();

    if (chunks.size() >= 2 && chunks.constLast().text.size() < profile.minimumChunkChars / 2) {
        SemanticChunk &previous = chunks[chunks.size() - 2];
        const SemanticChunk last = chunks.constLast();
        previous.text = normalizeBlockText(previous.text + QStringLiteral("\n\n") + last.text);
        if (!last.embedding.isEmpty()) {
            if (previous.embedding.isEmpty()) {
                previous.embedding = last.embedding;
            } else {
                for (int i = 0; i < previous.embedding.size() && i < last.embedding.size(); ++i) {
                    previous.embedding[i] += last.embedding.at(i);
                }
            }
        }
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
