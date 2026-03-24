#pragma once

#include <QString>
#include <atomic>
#include <QStringList>
#include <functional>
#include <QFileInfo>
#include <QVector>

#include "backend/outlineplanner.h"
#include "rag/embeddingclient.h"

struct RagHit {
    QString filePath;
    QString fileName;
    QString sourceType;
    QString sourceRole;
    QString excerpt;
    QString chunkText;
    QString matchReason;
    int score = 0;
    int chunkIndex = 0;
    double lexicalScore = 0.0;
    double semanticScore = 0.0;
    double rerankScore = 0.0;
};

struct DocumentSelectionStats {
    int fileCount = 0;
    int totalChars = 0;
    int totalChunks = 0;
    int maxCharsInFile = 0;
    int maxChunksInFile = 0;
};

class RagIndexer {
public:
    void setDocsRoot(const QString &rootPath);
    void setCachePath(const QString &cachePath);
    void setSemanticEnabled(bool enabled);
    void configureEmbeddingBackend(const QString &baseUrl, const QString &model, int timeoutMs, int batchSize);
    void setDiagnosticCallback(const std::function<void(const QString &, const QString &)> &callback);
    void requestCancel();

    int reindex(const std::function<void(int, int, const QString &)> &progressCallback = {});
    bool lastReindexCanceled() const;
    bool loadCache();
    bool saveCache() const;
    bool cacheNeedsRefresh() const;

    QVector<RagHit> searchHits(const QString &query,
                               int limit = 4,
                               RetrievalIntent intent = RetrievalIntent::General,
                               const QStringList &preferredRoles = {}) const;
    QVector<RagHit> searchHitsInFiles(const QString &query,
                                      const QStringList &preferredPaths,
                                      int limit = 4,
                                      RetrievalIntent intent = RetrievalIntent::General,
                                      const QStringList &preferredRoles = {}) const;
    QVector<RagHit> representativeHitsInFiles(const QStringList &preferredPaths,
                                              int perFileLimit = 8,
                                              bool preferStructure = true) const;
    DocumentSelectionStats estimateDocumentSelectionStats(const QStringList &preferredPaths,
                                                          int maxFiles = -1) const;
    QString formatDocumentStudyPrompt(const QStringList &preferredPaths,
                                      int maxFiles = 1,
                                      int outlineLineLimit = 180,
                                      int maxCharsPerFile = 70000,
                                      int hardPacketBudgetChars = 0) const;
    QString formatExactExtractionPrompt(const QStringList &preferredPaths,
                                        const QString &query,
                                        int maxFiles = 1,
                                        int maxCharsPerFile = 70000,
                                        int hardPacketBudgetChars = 0) const;
    QString formatHitsForPrompt(const QVector<RagHit> &hits) const;
    QString formatHitsForUi(const QVector<RagHit> &hits) const;
    QString formatInventoryForUi() const;
    QString embeddingBackendName() const;
    int chunkCount() const;
    int sourceCount() const;

    QStringList supportedExtensions() const;
    int importPaths(const QStringList &paths, const QString &destinationRoot, const QString &label, QString *message = nullptr) const;
    int addPathsToCollection(const QStringList &paths,
                             const QString &destinationRoot,
                             const QString &collectionId,
                             QString *message = nullptr) const;
    int removeKnowledgePaths(const QStringList &paths, const QString &destinationRoot, QString *message = nullptr) const;
    int moveKnowledgePaths(const QStringList &paths,
                           const QString &destinationRoot,
                           const QString &targetCollectionId,
                           const QString &targetGroupLabel = QString(),
                           QString *message = nullptr) const;
    bool createCollection(const QString &destinationRoot, const QString &label, QString *message = nullptr) const;
    bool deleteCollection(const QString &destinationRoot, const QString &collectionId, QString *message = nullptr) const;
    bool renameKnowledgePath(const QString &path, const QString &destinationRoot, const QString &newFileName, QString *message = nullptr) const;
    bool clearKnowledgeLibrary(const QString &destinationRoot, QString *message = nullptr) const;
    bool renameCollectionLabel(const QString &destinationRoot, const QString &collectionId, const QString &newLabel, QString *message = nullptr);

private:
    struct Chunk {
        QString filePath;
        QString fileName;
        QString sourceType;
        QString sourceRole;
        QString text;
        QVector<float> embedding;
        int chunkIndex = 0;
        QString textFingerprint;
        qint64 fileModifiedMs = 0;
    };

    struct SourceInfo {
        QString filePath;
        QString fileName;
        QString sourceType;
        QString sourceRole;
        QString extractor;
        QString collectionId;
        QString collectionLabel;
        QString groupId;
        QString groupLabel;
        QString relativePath;
        QString originalPath;
        qint64 fileModifiedMs = 0;
        qint64 fileSizeBytes = 0;
        int chunkCount = 0;
        QString fileContentHash;
        int lineCount = 0;
        int wordCount = 0;
        int textCharCount = 0;
        QString chunkingProfile;
        QString zeroChunkReason;
    };

    void rebuildEmbeddings();
    void ensureEmbeddingsForChunks(QVector<Chunk> &chunks) const;
    static bool sourceMatchesFile(const SourceInfo &source, const QFileInfo &info);
    static QString chunkingStrategyName();

    QString m_docsRoot;
    QString m_cachePath;
    QVector<Chunk> m_chunks;
    QVector<SourceInfo> m_sources;
    EmbeddingClient m_embeddingClient;
    bool m_semanticEnabled = true;
    std::atomic_bool m_cancelRequested{false};
    bool m_lastReindexCanceled = false;
    std::function<void(const QString &, const QString &)> m_diagnosticCallback;
};
