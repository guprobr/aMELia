#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QFutureWatcher>
#include <QHash>

#include "core/appconfig.h"
#include "backend/llmclient.h"
#include "backend/outlineplanner.h"

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
    void cancelReindex();
    void probeBackend();
    void refreshBackendModels();
    void newConversation();
    void loadConversationById(const QString &conversationId);
    void rememberNote(const QString &text);
    void deleteMemoryById(const QString &memoryId);
    void clearMemories();
    void setBackendModel(const QString &model);
    void importKnowledgePaths(const QStringList &paths, const QString &label);
    void addKnowledgePathsToCollection(const QStringList &paths, const QString &collectionId);
    void createKnowledgeCollection(const QString &label);
    void deleteKnowledgeCollection(const QString &collectionId);
    void removeKnowledgeAssets(const QStringList &paths);
    void moveKnowledgeAssets(const QStringList &paths, const QString &targetCollectionId, const QString &targetGroupLabel);
    void renameKnowledgeAsset(const QString &path, const QString &newFileName);
    void renameKnowledgeCollection(const QString &collectionId, const QString &newLabel);
    void clearKnowledgeBase();
    void prepareForShutdown();
    void startBootstrap();
    void deleteConversationById(const QString &conversationId);
    void setReasoningTraceEnabled(bool enabled);
    void setVerboseDiagnosticsEnabled(bool enabled);
    void setPrioritizedKnowledgeAssets(const QStringList &paths);

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
    void desktopNotificationRequested(const QString &title, const QString &message, int severity);
    void startupFinished();

private slots:
    void onSearchStarted(const QString &query, const QString &requestUrl);
    void onSearchFinished(const QString &query,
                          const QString &formattedContext,
                          const QString &formattedSources);
    void onSearchError(const QString &query, const QString &message);
    void onModelStarted();
    void onModelDelta(const QString &text);
    void onModelReasoningTrace(const QString &text);
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
    struct DiagnosticEntry {
        QString category;
        QString message;
        QString line;
        bool verbose = false;
    };

    void addDiagnostic(const QString &category, const QString &message);
    void emitDiagnostics();
    bool isVerboseDiagnostic(const QString &category, const QString &message) const;
    void seedInitialKnowledge();

    void notifyTaskStarted(const QString &title, const QString &message);
    void notifyTaskSucceeded(const QString &title, const QString &message);
    void notifyTaskFailed(const QString &title, const QString &message);
    void resetReasoningLoopGuard();
    void maybeRecoverFromReasoningOnlyLoop(const QString &text);
    void restartActiveGenerationWithoutReasoning();
    QString normalizeReasoningTraceForLoopDetection(const QString &text) const;
    QString buildReasoningLoopEvidence() const;
    QString sanitizePromptSection(const QString &text) const;
    QString deduplicatePromptSection(const QString &text, int maxRepeatedParagraphs = 1) const;
    bool hasSubstantialPromptOverlap(const QString &a, const QString &b) const;
    bool shouldSkipHistoryMessageForPrompt(const Message &message, const QString &userPrompt) const;

    struct PromptPreparationResult {
        quint64 serial = 0;
        QString prompt;
        QString sanitizedPreview;
        QString memoryContext;
        OutlinePlan outlinePlan;
        bool outlineOnlyFirstPass = false;
        QString localContext;
        QString localUi;
        QStringList prioritizedAssetsRequested;
        QStringList prioritizedAssetsUsed;
        int prioritizedHits = 0;
        int retrievedHits = 0;
        double bestHitScore = 0.0;
        bool shouldSearch = false;
        QString sanitizedSearchQuery;
    };

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
    QStringList m_prioritizedKnowledgeAssets;
    QStringList m_currentRequestPrioritizedKnowledgeAssets;
    QVector<DiagnosticEntry> m_diagnosticEntries;
    int m_startupChunkCount = 0;
    int m_streamChunkCount = 0;
    qint64 m_requestStartedMs = 0;

    // Best rerank score from the most recent RAG search — used to decide
    // whether to inject a CONTEXT_QUALITY_WARNING into the prompt.
    double m_lastBestHitScore = 0.0;

    struct StartupLoadResult {
        bool cacheLoaded = false;
        bool cacheStale = false;
        int chunkCount = 0;
        int sourceCount = 0;
    };

    QFutureWatcher<StartupLoadResult> *m_startupLoadWatcher = nullptr;
    QFutureWatcher<int> *m_reindexWatcher = nullptr;
    QFutureWatcher<PromptPreparationResult> *m_promptPreparationWatcher = nullptr;
    quint64 m_promptPreparationSerial = 0;
    bool m_busy = false;
    bool m_indexing = false;
    bool m_reasoningTraceEnabled = false;
    bool m_verboseDiagnosticsEnabled = false;
    int m_reasoningTraceNoteCount = 0;
    bool m_outlineOnlyFirstPass = false;
    bool m_shuttingDown = false;
    bool m_bootstrapStarted = false;
    QString m_activePrompt;
    QString m_activeLocalContext;
    QString m_activeExternalContext;
    QString m_activeMemoryContext;
    QString m_lastReasoningTraceNormalized;
    QStringList m_recentReasoningTraceNormalized;
    QHash<QString, int> m_reasoningTraceFrequency;
    qint64 m_firstReasoningTraceMs = 0;
    int m_reasoningCharsBeforeAnswer = 0;
    int m_reasoningRepeatStreak = 0;
    bool m_forceDisableReasoningForActiveRequest = false;
    bool m_reasoningFallbackRetryAttempted = false;
};
