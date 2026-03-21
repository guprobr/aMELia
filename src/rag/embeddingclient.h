#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <functional>
#include <atomic>

class EmbeddingClient {
public:
    struct NeuralAttemptResult {
        QVector<QVector<float>> embeddings;
        bool usedNeuralBackend = false;
        bool shouldRetryLegacyEndpoint = false;
        QString endpointName;
        QString errorMessage;
    };

    EmbeddingClient();

    void configureOllama(const QString &baseUrl,
                         const QString &model,
                         int timeoutMs = 120000,
                         int batchSize = 12);

    QString backendName() const;
    QString cacheKey() const;
    bool isConfigured() const;
    bool lastRequestUsedNeural() const;
    int localFallbackDimensions() const;

    QVector<float> embedText(const QString &text) const;
    QVector<float> embedTextLocalFallback(const QString &text) const;
    QVector<QVector<float>> embedTexts(const QStringList &texts) const;
    QVector<QVector<float>> embedTexts(const QStringList &texts,
                                       const std::function<void(int, int)> &progressCallback) const;
    QVector<QVector<float>> embedTexts(const QStringList &texts,
                                       const std::function<void(int, int)> &progressCallback,
                                       const std::atomic_bool *cancelRequested) const;
    static float cosineSimilarity(const QVector<float> &a, const QVector<float> &b);

private:
    NeuralAttemptResult tryOllamaEmbeddings(const QStringList &texts, const std::atomic_bool *cancelRequested = nullptr) const;
    NeuralAttemptResult tryOllamaEmbeddingsViaEndpoint(const QStringList &texts, const QString &endpointPath, const std::atomic_bool *cancelRequested = nullptr) const;

    QString m_ollamaBaseUrl;
    QString m_embeddingModel;
    int m_timeoutMs = 120000;
    int m_batchSize = 12;
    mutable bool m_lastRequestUsedNeural = false;
    mutable int m_consecutiveNeuralFailures = 0;
    mutable qint64 m_disableNeuralUntilMs = 0;
    mutable QString m_lastSuccessfulEndpoint = QStringLiteral("/api/embed");
    mutable QString m_lastErrorSummary;
};
