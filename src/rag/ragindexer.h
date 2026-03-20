#pragma once

#include <QString>
#include <atomic>
#include <QStringList>
#include <functional>
#include <QFileInfo>
#include <QVector>

#include "backend/outlineplanner.h"

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

class RagIndexer {
public:
    void setDocsRoot(const QString &rootPath);
    void setCachePath(const QString &cachePath);
    void setSemanticEnabled(bool enabled);
    void requestCancel();

    int reindex(const std::function<void(int, int, const QString &)> &progressCallback = {});
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
    QString formatHitsForPrompt(const QVector<RagHit> &hits) const;
    QString formatHitsForUi(const QVector<RagHit> &hits) const;
    QString formatInventoryForUi() const;
    int chunkCount() const;
    int sourceCount() const;

    QStringList supportedExtensions() const;
    int importPaths(const QStringList &paths, const QString &destinationRoot, const QString &label, QString *message = nullptr) const;
    int removeKnowledgePaths(const QStringList &paths, const QString &destinationRoot, QString *message = nullptr) const;
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
    };

    void rebuildEmbeddings();
    void ensureEmbeddingsForChunks(QVector<Chunk> &chunks) const;
    static bool sourceMatchesFile(const SourceInfo &source, const QFileInfo &info);

    QString m_docsRoot;
    QString m_cachePath;
    QVector<Chunk> m_chunks;
    QVector<SourceInfo> m_sources;
    bool m_semanticEnabled = true;
    std::atomic_bool m_cancelRequested{false};
};
