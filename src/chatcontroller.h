#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QFutureWatcher>

#include "appconfig.h"
#include "llmclient.h"
#include "outlineplanner.h"

class EmbeddingClient;
class MemoryManager;
class OllamaClient;
class OutlinePlanner;
class PolicyEngine;
class QdrantClient;
class RagIndexer;
class SearchBroker;
class SessionSummarizer;
class StorageManager;
class ToolExecutor;

class ChatController : public QObject {
    Q_OBJECT
public:
    explicit ChatController(const AppConfig &config, QObject *parent = nullptr);
    ~ChatController() override;

    void sendUserPrompt(const QString &prompt, bool allowExternalSearch);
    void stopGeneration();
    void reindexDocs();
    void probeBackend();
    void refreshBackendModels();
    void newConversation();
    void loadConversationById(const QString &conversationId);
    void rememberNote(const QString &text);
    void clearMemories();
    void setBackendModel(const QString &model);
    void importKnowledgePaths(const QStringList &paths);

signals:
    void assistantStreamChunk(const QString &chunk);
    void assistantCompleted(const QString &text);
    void systemNotice(const QString &text);
    void privacyPreviewReady(const QString &text);
    void localSourcesReady(const QString &text);
    void externalSourcesReady(const QString &text);
    void outlinePlanReady(const QString &text);
    void memoriesViewReady(const QString &text);
    void sessionSummaryReady(const QString &text);
    void transcriptRestored(const QString &text);
    void conversationListReady(const QStringList &ids,
                               const QStringList &titles,
                               const QString &currentId);
    void busyChanged(bool busy);
    void indexingStateChanged(bool active);
    void indexingProgressChanged(int value, int maximum, const QString &label);
    void statusChanged(const QString &text);
    void backendSummaryReady(const QString &text);
    void diagnosticsReady(const QString &text);
    void sourceInventoryReady(const QString &text);
    void backendModelsReady(const QStringList &models, const QString &currentModel);

private slots:
    void onSearchStarted(const QString &query, const QString &requestUrl);
    void onSearchFinished(const QString &query,
                          const QString &formattedContext,
                          const QString &formattedSources);
    void onSearchError(const QString &query, const QString &message);
    void onModelStarted();
    void onModelDelta(const QString &text);
    void onModelFinished(const QString &fullText);
    void onModelError(const QString &message);
    void onBackendProbeFinished(bool ok, const QString &message);
    void onModelsListed(const QStringList &models, const QString &message);
    void emitStartupNotices();
    void restoreStartupState();

private:
    struct Message {
        QString role;
        QString content;
    };

    void startGeneration(const QString &prompt,
                         const QString &localContext,
                         const QString &externalContext,
                         const QString &memoryContext);

    // contextIsWeak: true when best RAG rerank score < ragConfidenceThreshold.
    // When true, a CONTEXT_QUALITY_WARNING is injected into the developer block.
    QVector<LlmChatMessage> buildPromptMessages(const QString &userPrompt,
                                                const QString &localContext,
                                                const QString &externalContext,
                                                const QString &memoryContext,
                                                const QString &sessionSummary,
                                                bool contextIsWeak = false) const;
    QString buildGroundingRefusal(const QString &prompt) const;
    bool promptRequiresGrounding(const QString &prompt) const;
    bool promptLooksCasual(const QString &prompt) const;
    QVector<Message> trimmedHistory() const;
    QString buildBackendSummary() const;
    QString buildTranscriptText() const;
    QString ensureConversationForPrompt(const QString &prompt);
    void refreshConversationList();
    void refreshMemoryPanel();
    void refreshSummaryPanel();
    void refreshSourceInventory();
    void persistMessage(const QString &role, const QString &content);
    void updateCurrentSummary();
    QString titleFromPrompt(const QString &prompt) const;
    void addDiagnostic(const QString &category, const QString &message);
    void emitDiagnostics();
    void seedInitialKnowledge();

    AppConfig m_config;
    OllamaClient *m_llmClient = nullptr;
    PolicyEngine *m_policy = nullptr;
    RagIndexer *m_rag = nullptr;
    SearchBroker *m_searchBroker = nullptr;
    ToolExecutor *m_toolExecutor = nullptr;
    EmbeddingClient *m_embeddingClient = nullptr;
    QdrantClient *m_qdrantClient = nullptr;
    StorageManager *m_storage = nullptr;
    MemoryManager *m_memoryManager = nullptr;
    SessionSummarizer *m_sessionSummarizer = nullptr;
    OutlinePlanner *m_outlinePlanner = nullptr;

    QVector<Message> m_history;
    QString m_currentConversationId;
    QString m_currentSummary;
    QString m_currentOutlinePlanPrompt;
    QString m_pendingPrompt;
    QString m_pendingLocalContext;
    QString m_pendingMemoryContext;
    QStringList m_availableModels;
    QStringList m_diagnostics;
    int m_startupChunkCount = 0;
    int m_streamChunkCount = 0;
    qint64 m_requestStartedMs = 0;

    // Best rerank score from the most recent RAG search — used to decide
    // whether to inject a CONTEXT_QUALITY_WARNING into the prompt.
    double m_lastBestHitScore = 0.0;

    QFutureWatcher<int> *m_reindexWatcher = nullptr;
    bool m_busy = false;
    bool m_indexing = false;
    bool m_outlineOnlyFirstPass = false;
};
