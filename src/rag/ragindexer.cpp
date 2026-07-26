#include "rag/ragindexer.h"

#include "rag/cancellation.h"
#include "rag/documentextractor.h"
#include "rag/documentoutline.h"
#include "rag/documentstudybudget.h"
#include "rag/embeddingclient.h"
#include "rag/jsonutil.h"
#include "rag/kbfileops.h"
#include "rag/kbmanifest.h"
#include "rag/lexicalscoring.h"
#include "rag/semanticchunker.h"
#include "rag/sourceclassifier.h"
#include "rag/textcleanup.h"
#include "rag/textmetrics.h"

#include <algorithm>
#include <cmath>

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
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <utility>


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

void RagIndexer::configureEmbeddingBackend(const QString &baseUrl, const QString &model, int timeoutMs, int batchSize, bool forceCpu)
{
    m_embeddingClient.configureOllama(baseUrl, model, timeoutMs, batchSize, forceCpu);
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
    return QStringLiteral("semantic-embedding-chunks-v6");
}

int RagIndexer::reindex(const std::function<void(int, int, const QString &)> &progressCallback)
{
    m_cancelRequested.store(false, std::memory_order_relaxed);
    m_lastReindexCanceled = false;

    auto countChunksForIndexes = [](const QVector<int> &indexes) {
        return indexes.size();
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
            m_sources.clear();
            m_chunks.clear();
            saveCache();
            m_lastReindexCanceled = true;
            if (progressCallback) {
                progressCallback(0, 1, QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
            }
            return 0;
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

    QHash<QString, QVector<int>> existingChunkIndexesByPath;
    existingChunkIndexesByPath.reserve(m_sources.size());
    QHash<QString, QVector<float>> cachedEmbeddingsByFingerprint;
    cachedEmbeddingsByFingerprint.reserve(m_chunks.size());
    for (int chunkIndex = 0; chunkIndex < m_chunks.size(); ++chunkIndex) {
        const Chunk &chunk = m_chunks.at(chunkIndex);
        existingChunkIndexesByPath[chunk.filePath].push_back(chunkIndex);
        if (m_semanticEnabled && !chunk.embedding.isEmpty()) {
            const QString fingerprint = chunk.textFingerprint.trimmed().isEmpty()
                    ? stableHashHex(chunk.text.simplified())
                    : chunk.textFingerprint;
            if (!fingerprint.isEmpty() && !cachedEmbeddingsByFingerprint.contains(fingerprint)) {
                cachedEmbeddingsByFingerprint.insert(fingerprint, chunk.embedding);
            }
        }
    }

    const auto materializeExistingChunks = [this, &existingChunkIndexesByPath](const QString &path) {
        QVector<Chunk> chunksForPath;
        const auto indexesIt = existingChunkIndexesByPath.constFind(path);
        if (indexesIt == existingChunkIndexesByPath.cend()) {
            return chunksForPath;
        }
        const QVector<int> &indexes = indexesIt.value();
        chunksForPath.reserve(indexes.size());
        for (int index : indexes) {
            if (index >= 0 && index < m_chunks.size()) {
                chunksForPath.push_back(m_chunks.at(index));
            }
        }
        return chunksForPath;
    };

    auto countWorkingChunks = [&existingChunkIndexesByPath, &countChunksForIndexes](const QHash<QString, SourceInfo> &workingSourcesByPath,
                                                             const QHash<QString, QVector<Chunk>> &workingChunksByPath) {
        int count = 0;
        for (auto it = workingSourcesByPath.cbegin(); it != workingSourcesByPath.cend(); ++it) {
            const auto updatedIt = workingChunksByPath.constFind(it.key());
            if (updatedIt != workingChunksByPath.cend()) {
                count += updatedIt.value().size();
                continue;
            }
            const auto existingIt = existingChunkIndexesByPath.constFind(it.key());
            if (existingIt != existingChunkIndexesByPath.cend()) {
                count += countChunksForIndexes(existingIt.value());
            }
        }
        return count;
    };

    auto finalizeWorkingState = [this, &progressCallback, &existingChunkIndexesByPath](const QHash<QString, SourceInfo> &workingSourcesByPath,
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

        int totalChunkCount = 0;
        for (const SourceInfo &source : finalizedSources) {
            const auto updatedIt = workingChunksByPath.constFind(source.filePath);
            if (updatedIt != workingChunksByPath.cend()) {
                totalChunkCount += updatedIt.value().size();
                continue;
            }
            const auto existingIt = existingChunkIndexesByPath.constFind(source.filePath);
            if (existingIt != existingChunkIndexesByPath.cend()) {
                totalChunkCount += existingIt.value().size();
            }
        }

        QVector<Chunk> finalizedChunks;
        finalizedChunks.reserve(totalChunkCount);
        for (const SourceInfo &source : finalizedSources) {
            const auto updatedIt = workingChunksByPath.constFind(source.filePath);
            if (updatedIt != workingChunksByPath.cend()) {
                const QVector<Chunk> &chunksForPath = updatedIt.value();
                for (const Chunk &chunk : chunksForPath) {
                    finalizedChunks.push_back(chunk);
                }
                continue;
            }

            const auto existingIt = existingChunkIndexesByPath.constFind(source.filePath);
            if (existingIt == existingChunkIndexesByPath.cend()) {
                continue;
            }
            for (int index : existingIt.value()) {
                if (index >= 0 && index < m_chunks.size()) {
                    finalizedChunks.push_back(m_chunks.at(index));
                }
            }
        }

        m_sources = std::move(finalizedSources);
        m_chunks = std::move(finalizedChunks);
        saveCache();
        m_lastReindexCanceled = canceled;

        if (progressCallback) {
            progressCallback(progressValue, progressMaximum, label);
        }
        return m_chunks.size();
    };

    QHash<QString, SourceInfo> workingSourcesByPath = existingSourcesByPath;
    QHash<QString, QVector<Chunk>> workingChunksByPath;
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
        const auto chunkIndexesIt = existingChunkIndexesByPath.constFind(path);
        const bool hasExistingCacheEntry = sourceIt != existingSourcesByPath.cend() && chunkIndexesIt != existingChunkIndexesByPath.cend();
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
            registerChunkEmbeddings(reusedChunks);
            workingChunksByPath.insert(path, std::move(reusedChunks));
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
            QVector<Chunk> reusedChunks = materializeExistingChunks(path);
            const int reusedChunkCount = reusedChunks.size();
            commitReusedSource(sourceIt.value(),
                               std::move(reusedChunks),
                               QStringLiteral("Using cached index %1 / %2: %3 (%4 chunks)").arg(i + 1).arg(totalFiles).arg(info.fileName()).arg(reusedChunkCount),
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
            QVector<Chunk> reusedChunks = materializeExistingChunks(path);
            for (Chunk &chunk : reusedChunks) {
                chunk.fileModifiedMs = reusedSource.fileModifiedMs;
            }
            const int reusedChunkCount = reusedChunks.size();
            commitReusedSource(reusedSource,
                               std::move(reusedChunks),
                               QStringLiteral("Using content-hash cache %1 / %2: %3 (%4 chunks)").arg(i + 1).arg(totalFiles).arg(info.fileName()).arg(reusedChunkCount),
                               true);
            continue;
        }

        if (progressCallback) {
            QString phaseLabel;
            if (info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
                phaseLabel = QStringLiteral("Extracting PDF %1 / %2: %3").arg(i + 1).arg(totalFiles).arg(info.fileName());
            } else if (info.suffix().compare(QStringLiteral("docx"), Qt::CaseInsensitive) == 0) {
                phaseLabel = QStringLiteral("Extracting DOCX %1 / %2: %3").arg(i + 1).arg(totalFiles).arg(info.fileName());
            } else {
                phaseLabel = QStringLiteral("Indexing file %1 / %2: %3").arg(i + 1).arg(totalFiles).arg(info.fileName());
            }
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

            QVector<QVector<float>> blockEmbeddings;
            if (m_semanticEnabled && !blocks.isEmpty()) {
                QStringList blockTexts;
                blockTexts.reserve(blocks.size());
                for (const QString &block : blocks) {
                    blockTexts.push_back(block);
                }
                if (progressCallback) {
                    progressCallback(0, blockTexts.size(),
                                     QStringLiteral("Mapping structure %1 / %2: %3 — 0/%4 blocks")
                                         .arg(i + 1)
                                         .arg(totalFiles)
                                         .arg(info.fileName())
                                         .arg(blockTexts.size()));
                }
                blockEmbeddings = m_embeddingClient.embedTexts(blockTexts,
                                                               [progressCallback, i, totalFiles, info](int completed, int total) {
                                                                   if (progressCallback) {
                                                                       progressCallback(completed, total,
                                                                                        QStringLiteral("Mapping structure %1 / %2: %3 — %4/%5 blocks")
                                                                                            .arg(i + 1)
                                                                                            .arg(totalFiles)
                                                                                            .arg(info.fileName())
                                                                                            .arg(completed)
                                                                                            .arg(total));
                                                                   }
                                                               },
                                                               &m_cancelRequested);
                if (m_cancelRequested.load(std::memory_order_relaxed)) {
                    return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
                }
            }

            const QVector<SemanticChunk> semanticChunks = buildSemanticChunksFromBlocks(blocks, blockEmbeddings, profile, &m_cancelRequested);
            candidateChunkCount = semanticChunks.size();
            if (m_cancelRequested.load(std::memory_order_relaxed)) {
                return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
            }

            struct PendingChunk {
                QString text;
                QString fingerprint;
                QVector<float> embedding;
            };

            QVector<PendingChunk> pendingChunks;
            pendingChunks.reserve(semanticChunks.size());
            QStringList embeddingsInput;
            QVector<int> embeddingIndexes;
            QSet<QString> seenChunkFingerprints;
            embeddingsInput.reserve(semanticChunks.size());
            embeddingIndexes.reserve(semanticChunks.size());

            for (const SemanticChunk &semanticChunk : semanticChunks) {
                if (m_cancelRequested.load(std::memory_order_relaxed)) {
                    return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
                }
                const QString normalizedChunk = semanticChunk.text.trimmed();
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
                    } else if (!semanticChunk.embedding.isEmpty()) {
                        pendingChunk.embedding = semanticChunk.embedding;
                    }
                }

                pendingChunks.push_back(std::move(pendingChunk));
                if (m_semanticEnabled && pendingChunks.constLast().embedding.isEmpty()) {
                    embeddingIndexes.push_back(pendingChunks.size() - 1);
                    embeddingsInput.push_back(normalizedChunk);
                }
            }

            if (m_semanticEnabled && !embeddingsInput.isEmpty()) {
                if (progressCallback) {
                    progressCallback(0, embeddingsInput.size(),
                                     QStringLiteral("Embedding %1 / %2: %3 — 0/%4 chunks")
                                         .arg(i + 1)
                                         .arg(totalFiles)
                                         .arg(info.fileName())
                                         .arg(embeddingsInput.size()));
                }
                QVector<QVector<float>> embeddings = m_embeddingClient.embedTexts(embeddingsInput,
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
                if (m_cancelRequested.load(std::memory_order_relaxed)) {
                    return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
                }

                const int assignCount = qMin(embeddingIndexes.size(), embeddings.size());
                for (int assignIndex = 0; assignIndex < assignCount; ++assignIndex) {
                    pendingChunks[embeddingIndexes.at(assignIndex)].embedding = std::move(embeddings[assignIndex]);
                }
            }
            if (m_cancelRequested.load(std::memory_order_relaxed)) {
                return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
            }

            rebuiltChunksForPath.reserve(pendingChunks.size());
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
                rebuiltChunksForPath.push_back(std::move(chunk));
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

        const int committedChunkCount = rebuiltChunksForPath.size();
        workingSourcesByPath.insert(path, source);
        registerChunkEmbeddings(rebuiltChunksForPath);
        workingChunksByPath.insert(path, std::move(rebuiltChunksForPath));
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
                                 .arg(committedChunkCount)
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
                                    .arg(countWorkingChunks(workingSourcesByPath, workingChunksByPath)),
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

    QSaveFile file(m_cachePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    auto writeBytes = [&file](const QByteArray &bytes) {
        return file.write(bytes) == bytes.size();
    };
    auto writeLiteral = [&writeBytes](const char *literal) {
        return writeBytes(QByteArray(literal));
    };
    auto writeJsonString = [&writeBytes](const QString &value) {
        return writeBytes(jsonQuotedUtf8(value));
    };
    auto writeJsonIntegerField = [&writeJsonString, &writeLiteral, &writeBytes](const QString &key, qint64 value, bool trailingComma) {
        return writeJsonString(key)
                && writeLiteral(":")
                && writeBytes(QByteArray::number(value))
                && (!trailingComma || writeLiteral(","));
    };
    auto writeJsonBoolField = [&writeJsonString, &writeLiteral](const QString &key, bool value, bool trailingComma) {
        return writeJsonString(key)
                && writeLiteral(":")
                && writeLiteral(value ? "true" : "false")
                && (!trailingComma || writeLiteral(","));
    };
    auto writeJsonStringField = [&writeJsonString, &writeLiteral](const QString &key, const QString &value, bool trailingComma) {
        return writeJsonString(key)
                && writeLiteral(":")
                && writeJsonString(value)
                && (!trailingComma || writeLiteral(","));
    };

    bool ok = true;
    ok = ok && writeLiteral("{");
    ok = ok && writeJsonStringField(QStringLiteral("format"), QStringLiteral("amelia-rag-cache-v3"), true);
    ok = ok && writeJsonStringField(QStringLiteral("docsRoot"), m_docsRoot, true);
    ok = ok && writeJsonBoolField(QStringLiteral("semanticEnabled"), m_semanticEnabled, true);
    ok = ok && writeJsonStringField(QStringLiteral("chunkingStrategy"), chunkingStrategyName(), true);
    ok = ok && writeJsonStringField(QStringLiteral("embeddingBackend"), m_embeddingClient.backendName(), true);
    ok = ok && writeJsonStringField(QStringLiteral("embeddingCacheKey"), m_embeddingClient.cacheKey(), true);
    ok = ok && writeJsonString(QStringLiteral("chunks"));
    ok = ok && writeLiteral(":[");
    for (int chunkIndex = 0; ok && chunkIndex < m_chunks.size(); ++chunkIndex) {
        if (chunkIndex > 0) {
            ok = ok && writeLiteral(",");
        }
        const Chunk &chunk = m_chunks.at(chunkIndex);
        ok = ok && writeLiteral("{");
        ok = ok && writeJsonStringField(QStringLiteral("filePath"), chunk.filePath, true);
        ok = ok && writeJsonStringField(QStringLiteral("fileName"), chunk.fileName, true);
        ok = ok && writeJsonStringField(QStringLiteral("sourceType"), chunk.sourceType, true);
        ok = ok && writeJsonStringField(QStringLiteral("sourceRole"), chunk.sourceRole, true);
        ok = ok && writeJsonStringField(QStringLiteral("text"), chunk.text, true);
        ok = ok && writeJsonIntegerField(QStringLiteral("chunkIndex"), chunk.chunkIndex, true);
        ok = ok && writeJsonStringField(QStringLiteral("textFingerprint"), chunk.textFingerprint, true);
        ok = ok && writeJsonIntegerField(QStringLiteral("fileModifiedMs"), chunk.fileModifiedMs, !chunk.embedding.isEmpty());
        if (ok && !chunk.embedding.isEmpty()) {
            ok = ok && writeJsonString(QStringLiteral("embedding"));
            ok = ok && writeLiteral(":[");
            for (int embeddingIndex = 0; ok && embeddingIndex < chunk.embedding.size(); ++embeddingIndex) {
                if (embeddingIndex > 0) {
                    ok = ok && writeLiteral(",");
                }
                ok = ok && writeBytes(QByteArray::number(static_cast<double>(chunk.embedding.at(embeddingIndex)), 'g', 9));
            }
            ok = ok && writeLiteral("]");
        }
        ok = ok && writeLiteral("}");
    }
    ok = ok && writeLiteral("],");

    ok = ok && writeJsonString(QStringLiteral("sources"));
    ok = ok && writeLiteral(":[");
    for (int sourceIndex = 0; ok && sourceIndex < m_sources.size(); ++sourceIndex) {
        if (sourceIndex > 0) {
            ok = ok && writeLiteral(",");
        }
        const SourceInfo &source = m_sources.at(sourceIndex);
        ok = ok && writeLiteral("{");
        ok = ok && writeJsonStringField(QStringLiteral("filePath"), source.filePath, true);
        ok = ok && writeJsonStringField(QStringLiteral("fileName"), source.fileName, true);
        ok = ok && writeJsonStringField(QStringLiteral("sourceType"), source.sourceType, true);
        ok = ok && writeJsonStringField(QStringLiteral("sourceRole"), source.sourceRole, true);
        ok = ok && writeJsonStringField(QStringLiteral("extractor"), source.extractor, true);
        ok = ok && writeJsonStringField(QStringLiteral("collectionId"), source.collectionId, true);
        ok = ok && writeJsonStringField(QStringLiteral("collectionLabel"), source.collectionLabel, true);
        ok = ok && writeJsonStringField(QStringLiteral("groupId"), source.groupId, true);
        ok = ok && writeJsonStringField(QStringLiteral("groupLabel"), source.groupLabel, true);
        ok = ok && writeJsonStringField(QStringLiteral("relativePath"), source.relativePath, true);
        ok = ok && writeJsonStringField(QStringLiteral("originalPath"), source.originalPath, true);
        ok = ok && writeJsonIntegerField(QStringLiteral("fileModifiedMs"), source.fileModifiedMs, true);
        ok = ok && writeJsonIntegerField(QStringLiteral("fileSizeBytes"), source.fileSizeBytes, true);
        ok = ok && writeJsonIntegerField(QStringLiteral("chunkCount"), source.chunkCount, true);
        ok = ok && writeJsonStringField(QStringLiteral("fileContentHash"), source.fileContentHash, true);
        ok = ok && writeJsonIntegerField(QStringLiteral("lineCount"), source.lineCount, true);
        ok = ok && writeJsonIntegerField(QStringLiteral("wordCount"), source.wordCount, true);
        ok = ok && writeJsonIntegerField(QStringLiteral("textCharCount"), source.textCharCount, true);
        ok = ok && writeJsonStringField(QStringLiteral("chunkingProfile"), source.chunkingProfile, true);
        ok = ok && writeJsonStringField(QStringLiteral("zeroChunkReason"), source.zeroChunkReason, false);
        ok = ok && writeLiteral("}");
    }
    ok = ok && writeLiteral("]}");

    if (!ok) {
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
    int maxPerFile = (intent == RetrievalIntent::DocumentGeneration || intent == RetrievalIntent::Architecture) ? 10 : 6;
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

DocumentSelectionStats RagIndexer::estimateDocumentSelectionStats(const QStringList &preferredPaths,
                                                                 int maxFiles) const
{
    DocumentSelectionStats stats;
    if (preferredPaths.isEmpty()) {
        return stats;
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
        if (maxFiles > 0 && orderedPaths.size() >= maxFiles) {
            break;
        }
    }

    for (const QString &path : std::as_const(orderedPaths)) {
        int textChars = 0;
        int chunkCount = 0;
        bool matchedSource = false;
        for (const SourceInfo &source : m_sources) {
            if (QDir::cleanPath(source.filePath) != path) {
                continue;
            }
            textChars = qMax(0, source.textCharCount);
            chunkCount = qMax(0, source.chunkCount);
            matchedSource = true;
            break;
        }

        if (!matchedSource) {
            for (const Chunk &chunk : m_chunks) {
                if (QDir::cleanPath(chunk.filePath) != path) {
                    continue;
                }
                textChars += chunk.text.size();
                ++chunkCount;
            }
        }

        ++stats.fileCount;
        stats.totalChars += textChars;
        stats.totalChunks += chunkCount;
        stats.maxCharsInFile = qMax(stats.maxCharsInFile, textChars);
        stats.maxChunksInFile = qMax(stats.maxChunksInFile, chunkCount);
    }

    return stats;
}

QString RagIndexer::formatDocumentStudyPrompt(const QStringList &preferredPaths,
                                                   int maxFiles,
                                                   int outlineLineLimit,
                                                   int maxCharsPerFile,
                                                   int hardPacketBudgetChars) const
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

        const DocumentStudyPacketProfile packetProfile = buildDocumentStudyPacketProfile(text.size(),
                                                                                        fileChunks.size(),
                                                                                        outlineLineLimit,
                                                                                        maxCharsPerFile);
        const QStringList outlineLines = extractDocumentOutlineLines(text, packetProfile.effectiveOutlineLineLimit);
        const QString outlineText = outlineLines.isEmpty()
                ? QStringLiteral("<no explicit outline lines extracted>")
                : outlineLines.join(QStringLiteral("\n"));

        QStringList majorHeadings = extractMajorSectionHeadings(text, 64);
        if (majorHeadings.isEmpty()) {
            majorHeadings = extractMajorSectionHeadings(outlineText, 64);
        }

        QStringList coverageHeadingCandidates = majorHeadings;
        QSet<QString> seenCoverageHeadingKeys;
        for (const QString &heading : std::as_const(coverageHeadingCandidates)) {
            seenCoverageHeadingKeys.insert(normalizeOutlineKey(stripTrailingOutlinePageNumber(heading)));
        }
        for (const QString &outlineLine : std::as_const(outlineLines)) {
            const QString cleaned = stripTrailingOutlinePageNumber(outlineLine).trimmed();
            const QString normalized = normalizeOutlineKey(cleaned);
            if (cleaned.isEmpty() || normalized.isEmpty() || seenCoverageHeadingKeys.contains(normalized)) {
                continue;
            }
            if (cleaned.size() > 140) {
                continue;
            }
            seenCoverageHeadingKeys.insert(normalized);
            coverageHeadingCandidates << cleaned;
        }

        struct SectionAnchor {
            QString heading;
            int chunkPos = -1;
        };

        auto headingBodyKey = [](QString heading) {
            heading = stripTrailingOutlinePageNumber(heading).trimmed();
            heading.remove(QRegularExpression(QStringLiteral(R"(^\d+(?:\.\d+){0,4}[.)]?\s*)")));
            heading.remove(QRegularExpression(QStringLiteral(R"(^[A-Z][.)]\s*)")));
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
            int nextPos = startPos + 1;
            while (nextPos < endPosExclusive && nextPos <= startPos + 3) {
                const bool shouldMerge = headText.size() < 1200
                        || (blockEndsWithProceduralLead(headText) && !blockContainsStructuredContent(headText));
                if (!shouldMerge) {
                    break;
                }
                headText = normalizeBlockText(headText + QStringLiteral("\n\n") + orderedFileChunks.at(nextPos)->text.trimmed());
                ++nextPos;
            }
            headText = balancedTrimForStudy(headText, qMin(maxChars, qMax(700, static_cast<int>(maxChars * 0.75))));
            if (endPosExclusive - startPos <= 1 || headText.size() >= maxChars - 160) {
                return headText;
            }

            const int tailBudget = qMax(220, maxChars - headText.size() - marker.size());
            if (tailBudget < 220) {
                return headText;
            }

            int tailPos = endPosExclusive - 1;
            QString tailText = orderedFileChunks.at(tailPos)->text.trimmed();
            while (tailPos > startPos && (tailText.trimmed().isEmpty() || tailText == headText)) {
                --tailPos;
                tailText = orderedFileChunks.at(tailPos)->text.trimmed();
            }
            if (tailPos <= startPos || tailText == headText) {
                return headText;
            }
            tailText = balancedTrimForStudy(tailText, tailBudget);
            if (tailText.trimmed().isEmpty()) {
                return headText;
            }
            return headText + marker + tailText;
        };

        auto chunkHeadingFallback = [&](int chunkPos, int spanOrdinal) {
            if (chunkPos < 0 || chunkPos >= fileChunks.size()) {
                return QStringLiteral("DOCUMENT_SPAN_%1").arg(spanOrdinal);
            }

            const QString chunkText = fileChunks.at(chunkPos)->text;
            const QStringList chunkOutlineLines = extractDocumentOutlineLines(chunkText, 4);
            for (const QString &candidate : std::as_const(chunkOutlineLines)) {
                const QString cleaned = stripTrailingOutlinePageNumber(candidate).trimmed();
                if (!cleaned.isEmpty() && cleaned.size() <= 120 && !looksLikeContentsEntry(cleaned)) {
                    return cleaned;
                }
            }

            const QStringList chunkHeadings = extractMajorSectionHeadings(chunkText, 2);
            for (const QString &candidate : std::as_const(chunkHeadings)) {
                const QString cleaned = stripTrailingOutlinePageNumber(candidate).trimmed();
                if (!cleaned.isEmpty() && cleaned.size() <= 120) {
                    return cleaned;
                }
            }

            return QStringLiteral("DOCUMENT_SPAN_%1").arg(spanOrdinal);
        };

        auto normalizeAnchors = [](QVector<SectionAnchor> inputAnchors) {
            std::sort(inputAnchors.begin(), inputAnchors.end(), [](const SectionAnchor &a, const SectionAnchor &b) {
                if (a.chunkPos != b.chunkPos) {
                    return a.chunkPos < b.chunkPos;
                }
                return a.heading < b.heading;
            });

            QVector<SectionAnchor> normalizedAnchors;
            QSet<QString> seenAnchorKeys;
            QSet<int> seenChunkPositions;
            for (const SectionAnchor &anchor : std::as_const(inputAnchors)) {
                const QString anchorKey = normalizeOutlineKey(anchor.heading);
                if (anchor.chunkPos < 0 || anchorKey.isEmpty() || seenAnchorKeys.contains(anchorKey)) {
                    continue;
                }
                if (seenChunkPositions.contains(anchor.chunkPos) && !anchor.heading.startsWith(QStringLiteral("DOCUMENT_SPAN_"))) {
                    continue;
                }
                seenAnchorKeys.insert(anchorKey);
                seenChunkPositions.insert(anchor.chunkPos);
                normalizedAnchors.push_back(anchor);
            }
            return normalizedAnchors;
        };

        QVector<SectionAnchor> anchors;
        for (const QString &heading : std::as_const(coverageHeadingCandidates)) {
            const QString cleanedHeading = stripTrailingOutlinePageNumber(heading).trimmed();
            const QString normalizedHeading = normalizeOutlineKey(cleanedHeading);
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

                const QStringList chunkOutlineLines = extractDocumentOutlineLines(chunkText, 4);
                for (const QString &chunkLine : std::as_const(chunkOutlineLines)) {
                    const QString normalizedChunkLine = normalizeOutlineKey(stripTrailingOutlinePageNumber(chunkLine));
                    if (normalizedChunkLine == normalizedHeading) {
                        score += 8;
                        break;
                    }
                    if (!bodyKey.isEmpty() && normalizedChunkLine.contains(bodyKey)) {
                        score += 3;
                    }
                }

                const QStringList chunkHeadings = extractMajorSectionHeadings(chunkText, 4);
                for (const QString &chunkHeading : std::as_const(chunkHeadings)) {
                    const QString normalizedChunkHeading = normalizeOutlineKey(chunkHeading);
                    if (normalizedChunkHeading == normalizedHeading) {
                        score += 8;
                        break;
                    }
                    if (!bodyKey.isEmpty() && normalizedChunkHeading.contains(bodyKey)) {
                        score += 3;
                    }
                }

                if (score >= bestScore && score > 0) {
                    bestScore = score;
                    bestChunkPos = i;
                }
            }

            if (bestChunkPos >= 0) {
                anchors.push_back({cleanedHeading, bestChunkPos});
            }
        }

        anchors = normalizeAnchors(anchors);

        const int desiredCoverageAnchors = qBound(6,
                                                  qMax(anchors.size(), qMin(fileChunks.size(), qMax(6, packetProfile.coverageBudget / qMax(420, packetProfile.minSectionChars)))),
                                                  packetProfile.anchorCap);
        auto hasNearbyAnchor = [&](int chunkPos, int distance) {
            for (const SectionAnchor &anchor : std::as_const(anchors)) {
                if (std::abs(anchor.chunkPos - chunkPos) <= distance) {
                    return true;
                }
            }
            return false;
        };

        if (!fileChunks.isEmpty() && anchors.size() < desiredCoverageAnchors) {
            const int lastChunkPos = fileChunks.size() - 1;
            for (int sampleIndex = 0; sampleIndex < desiredCoverageAnchors; ++sampleIndex) {
                const int denominator = qMax(1, desiredCoverageAnchors - 1);
                const int chunkPos = qRound(lastChunkPos * (sampleIndex / static_cast<double>(denominator)));
                if (chunkPos < 0 || chunkPos > lastChunkPos || hasNearbyAnchor(chunkPos, 2)) {
                    continue;
                }
                anchors.push_back({chunkHeadingFallback(chunkPos, sampleIndex + 1), chunkPos});
            }
            if (!hasNearbyAnchor(lastChunkPos, 1)) {
                anchors.push_back({chunkHeadingFallback(lastChunkPos, anchors.size() + 1), lastChunkPos});
            }
            anchors = normalizeAnchors(anchors);
        }

        if (anchors.size() > packetProfile.anchorCap) {
            QVector<SectionAnchor> sampledAnchors;
            sampledAnchors.reserve(packetProfile.anchorCap);
            for (int sampleIndex = 0; sampleIndex < packetProfile.anchorCap; ++sampleIndex) {
                const int denominator = qMax(1, packetProfile.anchorCap - 1);
                const int pos = qRound((anchors.size() - 1) * (sampleIndex / static_cast<double>(denominator)));
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
        const int effectiveMaxCharsPerFile = packetProfile.effectiveMaxCharsPerFile;
        const int previewBudget = packetProfile.previewBudget;
        const int coverageBudget = packetProfile.coverageBudget;
        const int minSectionChars = packetProfile.minSectionChars;
        const int maxSectionCharsCap = packetProfile.maxSectionCharsCap;
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

        const int fullDocumentInlineThreshold = packetProfile.fullDocumentInlineThreshold;
        const bool includeFullDocumentPreview = packetProfile.includeFullDocumentPreview
                && text.size() <= fullDocumentInlineThreshold;
        // use the full file budget for small files, fall back to previewBudget when needed
        const int fullPreviewBudget = (text.size() <= fullDocumentInlineThreshold)
                ? qMin(text.size(), packetProfile.effectiveMaxCharsPerFile)
                : previewBudget;
        const QString fullDocumentPreview = includeFullDocumentPreview
                ? balancedTrimForStudy(text, fullPreviewBudget)
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
        const int effectivePacketBudget = hardPacketBudgetChars > 0
                ? qMax(12000, qMin(hardPacketBudgetChars, effectiveMaxCharsPerFile))
                : effectiveMaxCharsPerFile;
        packet = balancedTrimForStudy(packet, effectivePacketBudget);
        packets << packet;
    }

    return packets.join(QStringLiteral("\n\n"));
}

QString RagIndexer::formatExactExtractionPrompt(const QStringList &preferredPaths,
                                               const QString &query,
                                               int maxFiles,
                                               int maxCharsPerFile,
                                               int hardPacketBudgetChars) const
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

    QStringList packets;
    const QString actionableQuery = query.trimmed().isEmpty()
            ? QStringLiteral("commands snippets yaml config example procedures warnings placeholders appendix")
            : query + QStringLiteral(" commands snippets yaml config example procedures warnings placeholders appendix");

    for (const QString &path : std::as_const(orderedPaths)) {
        QVector<const Chunk *> fileChunks = chunksByPath.value(path);
        if (fileChunks.isEmpty()) {
            continue;
        }
        std::sort(fileChunks.begin(), fileChunks.end(), [](const Chunk *a, const Chunk *b) {
            return a->chunkIndex < b->chunkIndex;
        });

        const Chunk *firstChunk = fileChunks.constFirst();
        const QString fileName = firstChunk != nullptr ? firstChunk->fileName : QFileInfo(path).fileName();
        const QString sourceRole = firstChunk != nullptr ? firstChunk->sourceRole : QStringLiteral("reference");
        const QString sourceType = firstChunk != nullptr ? firstChunk->sourceType : QStringLiteral("doc");

        const int effectiveBudget = hardPacketBudgetChars > 0
                ? qMax(16000, qMin(hardPacketBudgetChars, qMax(16000, maxCharsPerFile)))
                : qMax(16000, maxCharsPerFile);
        int remainingBudget = effectiveBudget;

        QStringList allChunks;
        allChunks.reserve(fileChunks.size());
        int totalChars = 0;
        for (const Chunk *chunk : std::as_const(fileChunks)) {
            const QString chunkText = normalizeBlockText(chunk->text);
            allChunks << chunkText;
            totalChars += chunkText.size();
        }

        QString packet = QStringLiteral("=== EXACT_EXTRACTION_PACKET: %1 | role=%2 | type=%3 ===\n")
                .arg(fileName, sourceRole, sourceType);
        packet += QStringLiteral("SOURCE_PATH: %1\n").arg(path);
        packet += QStringLiteral("EXTRACTION_MODE: ordered raw chunk coverage\n");
        packet += QStringLiteral("NOTE: Prefer exact snippets, commands, YAML, config fragments, placeholders, warnings, and procedures from the raw chunk windows below.\n\n");
        remainingBudget -= packet.size();
        if (remainingBudget <= 1200) {
            packets << packet.trimmed();
            continue;
        }

        // Near-fit path: when the file is reasonably close to the packet budget, emit the whole document trimmed once.
        if (totalChars <= remainingBudget * 1.75) {
            const QString joined = allChunks.join(QStringLiteral("\n\n"));
            const QString trimmed = balancedTrimForStudy(joined, remainingBudget - 120);
            packet += QStringLiteral("--- FULL_FILE (budget-trimmed) ---\n") + trimmed + QStringLiteral("\n\n");
            packets << packet.trimmed();
            continue;
        }
        QHash<int, int> chunkIndexToPos;
        for (int i = 0; i < fileChunks.size(); ++i) {
            chunkIndexToPos.insert(fileChunks.at(i)->chunkIndex, i);
        }

        QVector<RagHit> focusHits = searchHitsInFiles(actionableQuery,
                                                      QStringList{path},
                                                      24,
                                                      RetrievalIntent::DocumentGeneration);
        QVector<int> focusPositions;
        for (const RagHit &hit : std::as_const(focusHits)) {
            const int pos = chunkIndexToPos.value(hit.chunkIndex, -1);
            if (pos >= 0) {
                focusPositions.push_back(pos);
            }
        }

        QVector<QPair<int, int>> actionableCandidates;
        actionableCandidates.reserve(fileChunks.size());
        for (int i = 0; i < fileChunks.size(); ++i) {
            const int actionability = chunkActionabilityScore(allChunks.at(i));
            if (actionability > 0) {
                actionableCandidates.push_back(qMakePair(actionability, i));
            }
        }
        std::sort(actionableCandidates.begin(), actionableCandidates.end(), [](const QPair<int, int> &a, const QPair<int, int> &b) {
            if (a.first != b.first) {
                return a.first > b.first;
            }
            return a.second < b.second;
        });
        const int actionableWindowLimit = qBound(4, qMax(6, qMin(14, effectiveBudget / 4200)), 14);
        for (int i = 0; i < actionableCandidates.size() && i < actionableWindowLimit; ++i) {
            focusPositions.push_back(actionableCandidates.at(i).second);
        }

        const int sampleWindowCount = qBound(6,
                                             qMax(6, qMin(16, effectiveBudget / 2200)),
                                             16);
        if (!fileChunks.isEmpty()) {
            const int lastPos = fileChunks.size() - 1;
            for (int sampleIndex = 0; sampleIndex < sampleWindowCount; ++sampleIndex) {
                const int denominator = qMax(1, sampleWindowCount - 1);
                const int samplePos = qRound(lastPos * (sampleIndex / static_cast<double>(denominator)));
                focusPositions.push_back(samplePos);
            }
        }

        std::sort(focusPositions.begin(), focusPositions.end());
        focusPositions.erase(std::unique(focusPositions.begin(), focusPositions.end()), focusPositions.end());

        struct ChunkRange {
            int start = -1;
            int end = -1;
        };
        QVector<ChunkRange> ranges;
        const int focusCount = focusPositions.isEmpty() ? sampleWindowCount : focusPositions.size();
        const int targetWindowChars = qBound(1600,
                                             effectiveBudget / qMax(5, qMin(14, focusCount)),
                                             4200);
        for (int focusPos : std::as_const(focusPositions)) {
            if (focusPos < 0 || focusPos >= fileChunks.size()) {
                continue;
            }
            int start = focusPos;
            int end = focusPos;
            int windowChars = allChunks.at(focusPos).size();
            int left = focusPos - 1;
            int right = focusPos + 1;
            while ((left >= 0 || right < fileChunks.size()) && windowChars < targetWindowChars) {
                const int leftChars = left >= 0 ? allChunks.at(left).size() : -1;
                const int rightChars = right < fileChunks.size() ? allChunks.at(right).size() : -1;
                const bool takeRight = rightChars >= 0 && (leftChars < 0 || rightChars <= leftChars || end <= focusPos);
                if (takeRight) {
                    windowChars += rightChars + 2;
                    end = right;
                    ++right;
                } else if (leftChars >= 0) {
                    windowChars += leftChars + 2;
                    start = left;
                    --left;
                } else {
                    break;
                }
            }
            ranges.push_back({start, end});
        }

        std::sort(ranges.begin(), ranges.end(), [](const ChunkRange &a, const ChunkRange &b) {
            if (a.start != b.start) {
                return a.start < b.start;
            }
            return a.end < b.end;
        });

        QVector<ChunkRange> mergedRanges;
        for (const ChunkRange &range : std::as_const(ranges)) {
            if (mergedRanges.isEmpty() || range.start > mergedRanges.constLast().end + 1) {
                mergedRanges.push_back(range);
            } else {
                mergedRanges.last().end = qMax(mergedRanges.constLast().end, range.end);
            }
        }

        for (const ChunkRange &range : std::as_const(mergedRanges)) {
            if (range.start < 0 || range.end < range.start) {
                continue;
            }
            QString rangeText = QStringLiteral("--- RAW_CHUNK_RANGE %1-%2 ---\n")
                    .arg(fileChunks.at(range.start)->chunkIndex)
                    .arg(fileChunks.at(range.end)->chunkIndex);
            for (int pos = range.start; pos <= range.end; ++pos) {
                rangeText += QStringLiteral("[chunk %1]\n%2\n\n")
                        .arg(fileChunks.at(pos)->chunkIndex)
                        .arg(allChunks.at(pos));
            }
            if (rangeText.size() > remainingBudget) {
                if (remainingBudget < 1200) {
                    break;
                }
                rangeText = balancedTrimForStudy(rangeText, remainingBudget);
            }
            if (rangeText.trimmed().isEmpty() || rangeText.size() > remainingBudget) {
                break;
            }
            packet += rangeText;
            remainingBudget -= rangeText.size();
            if (remainingBudget < 1200) {
                break;
            }
        }

        if (remainingBudget >= 280) {
            packet += QStringLiteral("\n[Coverage note] Raw chunk windows were emitted in source order and biased toward search hits, intrinsically actionable chunks (commands/YAML/config/warnings/procedures), and evenly spaced spans across the file. If the document is larger than the packet budget, continue in another batch for full coverage.\n");
        }
        packets << packet.trimmed();
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

