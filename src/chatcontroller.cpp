#include "chatcontroller.h"

#include "embeddingclient.h"
#include "memorymanager.h"
#include "ollamaclient.h"
#include "policyengine.h"
#include "qdrantclient.h"
#include "ragindexer.h"
#include "searchbroker.h"
#include "sessionsummary.h"
#include "storagemanager.h"
#include "toolexecutor.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QTimer>
#include <QMetaObject>
#include <QtConcurrent/QtConcurrentRun>

#include <cstdio>

namespace {
QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString trimForBudget(const QString &text, int maxChars)
{
    if (maxChars <= 0 || text.size() <= maxChars) {
        return text.trimmed();
    }

    const QString trimmed = text.left(maxChars).trimmed();
    return trimmed + QStringLiteral("\n[... budget-trimmed ...]");
}

bool isStructuredDocumentRequest(const QString &prompt)
{
    const QString lower = prompt.toLower();
    return lower.contains(QStringLiteral("mop"))
            || lower.contains(QStringLiteral("runbook"))
            || lower.contains(QStringLiteral("playbook"))
            || lower.contains(QStringLiteral("guide"))
            || lower.contains(QStringLiteral("markdown"))
            || lower.contains(QStringLiteral(".md"));
}

bool containsAny(const QString &text, const QStringList &needles)
{
    for (const QString &needle : needles) {
        if (text.contains(needle)) {
            return true;
        }
    }
    return false;
}

QString ansiColorForCategory(const QString &category)
{
    const QString lower = category.toLower();
    if (lower == QStringLiteral("backend"))   return QStringLiteral("\x1b[38;5;39m");
    if (lower == QStringLiteral("search"))    return QStringLiteral("\x1b[38;5;46m");
    if (lower == QStringLiteral("rag"))       return QStringLiteral("\x1b[38;5;44m");
    if (lower == QStringLiteral("memory"))    return QStringLiteral("\x1b[38;5;208m");
    if (lower == QStringLiteral("planner"))   return QStringLiteral("\x1b[38;5;141m");
    if (lower == QStringLiteral("guardrail")) return QStringLiteral("\x1b[38;5;196m");
    if (lower == QStringLiteral("ingest"))    return QStringLiteral("\x1b[38;5;220m");
    if (lower == QStringLiteral("startup"))   return QStringLiteral("\x1b[38;5;213m");
    if (lower == QStringLiteral("budget"))    return QStringLiteral("\x1b[38;5;51m");
    if (lower == QStringLiteral("chat"))      return QStringLiteral("\x1b[38;5;177m");
    return QStringLiteral("\x1b[0m");
}

void printDiagnosticToConsole(const QString &category, const QString &line)
{
    const QByteArray payload = line.toUtf8();
    if (qEnvironmentVariableIsSet("NO_COLOR")) {
        std::fprintf(stderr, "%s\n", payload.constData());
    } else {
        const QByteArray color = ansiColorForCategory(category).toUtf8();
        std::fprintf(stderr, "%s%s\x1b[0m\n", color.constData(), payload.constData());
    }
    std::fflush(stderr);
}
}

ChatController::ChatController(const AppConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_llmClient(new OllamaClient(this))
    , m_policy(new PolicyEngine())
    , m_rag(new RagIndexer())
    , m_searchBroker(new SearchBroker(this))
    , m_toolExecutor(new ToolExecutor())
    , m_embeddingClient(new EmbeddingClient())
    , m_qdrantClient(new QdrantClient())
    , m_storage(new StorageManager())
    , m_memoryManager(new MemoryManager(m_storage))
    , m_sessionSummarizer(new SessionSummarizer())
    , m_outlinePlanner(new OutlinePlanner())
{
    QString storageError;
    m_storage->initialize(m_config.dataRoot, m_config.knowledgeRoot, &storageError);

    seedInitialKnowledge();

    m_rag->setDocsRoot(m_storage->knowledgeRoot());
    m_rag->setCachePath(m_storage->ragCachePath());
    m_rag->setSemanticEnabled(m_config.enableSemanticRetrieval);

    m_reindexWatcher = new QFutureWatcher<int>(this);
    connect(m_reindexWatcher, &QFutureWatcher<int>::finished, this, [this]() {
        const int chunks = m_reindexWatcher->result();
        m_startupChunkCount = chunks;
        m_indexing = false;

        emit indexingProgressChanged(qMax(1, chunks), qMax(1, chunks), QStringLiteral("Index complete."));
        emit indexingStateChanged(false);
        emit systemNotice(QStringLiteral("Local docs indexed: %1 chunks across %2 sources.").arg(chunks).arg(m_rag->sourceCount()));
        addDiagnostic(QStringLiteral("rag"),
                      QStringLiteral("Reindex finished: %1 chunks across %2 source(s)")
                          .arg(chunks)
                          .arg(m_rag->sourceCount()));
        refreshSourceInventory();
        emit statusChanged(QStringLiteral("Ready."));
        emit backendSummaryReady(buildBackendSummary());
    });

    m_promptPreparationWatcher = new QFutureWatcher<PromptPreparationResult>(this);
    connect(m_promptPreparationWatcher, &QFutureWatcher<PromptPreparationResult>::finished, this, [this]() {
        const PromptPreparationResult result = m_promptPreparationWatcher->result();
        if (result.serial != m_promptPreparationSerial || !m_busy) {
            return;
        }

        emit privacyPreviewReady(result.sanitizedPreview);
        refreshMemoryPanel();

        m_currentOutlinePlanPrompt = result.outlinePlan.formatForPrompt();
        emit outlinePlanReady(result.outlinePlan.formatForUi());
        m_outlineOnlyFirstPass = result.outlineOnlyFirstPass;
        if (m_outlineOnlyFirstPass) {
            emit systemNotice(QStringLiteral("Structured document request detected. Amelia will use an outline-only first pass to keep the local prompt grounded and compact."));
            addDiagnostic(QStringLiteral("planner"), QStringLiteral("Outline-only first pass enabled for this request"));
        }
        if (result.outlinePlan.enabled) {
            addDiagnostic(QStringLiteral("planner"), QStringLiteral("Outline planner activated for %1 section(s)").arg(result.outlinePlan.sections.size()));
        }

        emit localSourcesReady(result.localUi.isEmpty() ? QStringLiteral("<none>") : result.localUi);
        addDiagnostic(QStringLiteral("rag"),
                      QStringLiteral("Retrieved %1 local hit(s) from %2 source(s); best rerank score=%3")
                          .arg(result.retrievedHits)
                          .arg(m_rag->sourceCount())
                          .arg(QString::number(result.bestHitScore, 'f', 2)));
        m_lastBestHitScore = result.bestHitScore;

        if (result.shouldSearch) {
            m_pendingPrompt = result.prompt;
            m_pendingLocalContext = result.localContext;
            m_pendingMemoryContext = result.memoryContext;
            emit statusChanged(QStringLiteral("Searching external sources with sanitized query..."));
            addDiagnostic(QStringLiteral("search"), QStringLiteral("External search approved with sanitized query: %1").arg(result.sanitizedSearchQuery));
            m_searchBroker->search(result.sanitizedSearchQuery);
            return;
        }

        emit externalSourcesReady(QStringLiteral("<none>"));
        startGeneration(result.prompt, result.localContext, QString(), result.memoryContext);
    });

    const bool cacheLoaded = m_rag->loadCache();
    if (cacheLoaded) {
        m_startupChunkCount = m_rag->chunkCount();
        if (m_rag->cacheNeedsRefresh()) {
            QTimer::singleShot(0, this, [this]() {
                emit systemNotice(QStringLiteral("Knowledge cache is stale. Scheduling an incremental refresh in the background..."));
                reindexDocs();
            });
        }
    } else {
        QTimer::singleShot(0, this, &ChatController::reindexDocs);
    }

    m_searchBroker->setEnabled(m_config.enableExternalSearch);
    m_searchBroker->setEndpoint(m_config.searxngUrl);
    m_searchBroker->setAllowedDomains(m_config.externalSearchDomainAllowlist);
    m_searchBroker->setMaxResults(m_config.maxExternalHits);
    m_searchBroker->setRequestTimeoutMs(m_config.externalSearchTimeoutMs);

    m_llmClient->setProbeTimeoutMs(m_config.ollamaProbeTimeoutMs);
    m_llmClient->setResponseHeadersTimeoutMs(m_config.ollamaResponseHeadersTimeoutMs);
    m_llmClient->setFirstTokenTimeoutMs(m_config.ollamaFirstTokenTimeoutMs);
    m_llmClient->setInactivityTimeoutMs(m_config.ollamaInactivityTimeoutMs);
    m_llmClient->setTotalTimeoutMs(m_config.ollamaTotalTimeoutMs);
    m_llmClient->setGenerationConfig(m_config);

    connect(m_searchBroker, &SearchBroker::searchStarted, this, &ChatController::onSearchStarted);
    connect(m_searchBroker, &SearchBroker::searchFinished, this, &ChatController::onSearchFinished);
    connect(m_searchBroker, &SearchBroker::searchError, this, &ChatController::onSearchError);

    connect(m_llmClient, &OllamaClient::responseStarted, this, &ChatController::onModelStarted);
    connect(m_llmClient, &OllamaClient::responseDelta, this, &ChatController::onModelDelta);
    connect(m_llmClient, &OllamaClient::responseFinished, this, &ChatController::onModelFinished);
    connect(m_llmClient, &OllamaClient::responseError, this, &ChatController::onModelError);
    connect(m_llmClient, &OllamaClient::backendProbeFinished, this, &ChatController::onBackendProbeFinished);
    connect(m_llmClient, &OllamaClient::modelsListed, this, &ChatController::onModelsListed);

    QTimer::singleShot(0, this, &ChatController::emitStartupNotices);
    QTimer::singleShot(0, this, &ChatController::restoreStartupState);
    if (m_config.probeOllamaOnStartup) {
        QTimer::singleShot(0, this, &ChatController::probeBackend);
    }
    Q_UNUSED(storageError)
}

ChatController::~ChatController()
{
    if (m_reindexWatcher != nullptr && m_reindexWatcher->isRunning()) {
        m_reindexWatcher->waitForFinished();
    }
    if (m_promptPreparationWatcher != nullptr && m_promptPreparationWatcher->isRunning()) {
        m_promptPreparationWatcher->waitForFinished();
    }

    delete m_outlinePlanner;
    delete m_policy;
    delete m_rag;
    delete m_toolExecutor;
    delete m_embeddingClient;
    delete m_qdrantClient;
    delete m_memoryManager;
    delete m_sessionSummarizer;
    delete m_storage;
}

void ChatController::sendUserPrompt(const QString &prompt, bool allowExternalSearch)
{
    if (m_indexing) {
        emit systemNotice(QStringLiteral("Local docs are still indexing. Wait for reindex to finish before sending a prompt."));
        return;
    }

    if (m_busy) {
        emit systemNotice(QStringLiteral("A request is already running. Stop it before sending a new one."));
        return;
    }

    if (m_promptPreparationWatcher != nullptr && m_promptPreparationWatcher->isRunning()) {
        emit systemNotice(QStringLiteral("Amelia is still finishing the previous context preparation. Please try again in a moment."));
        return;
    }

    const QString trimmed = prompt.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    ensureConversationForPrompt(trimmed);
    addDiagnostic(QStringLiteral("chat"), QStringLiteral("User prompt received (%1 chars)").arg(trimmed.size()));

    m_busy = true;
    m_streamChunkCount = 0;
    m_requestStartedMs = nowMs();
    emit busyChanged(true);
    emit statusChanged(QStringLiteral("Analyzing knowledge base and preparing grounded context..."));

    m_history.push_back({QStringLiteral("user"), trimmed});
    persistMessage(QStringLiteral("user"), trimmed);

    if (m_config.autoPersistMemories) {
        QString memoryError;
        const QString saved = m_memoryManager->persistAutoMemories(trimmed, &memoryError);
        if (!saved.isEmpty()) {
            emit systemNotice(QStringLiteral("Persisted memory: %1").arg(saved));
            addDiagnostic(QStringLiteral("memory"), QStringLiteral("Persisted auto memory: %1").arg(saved));
        } else if (!memoryError.isEmpty() && memoryError != QStringLiteral("Failed to persist extracted memories.")) {
            addDiagnostic(QStringLiteral("memory"), memoryError);
        }
    }

    const quint64 serial = ++m_promptPreparationSerial;
    const bool searchEnabled = m_searchBroker->isEnabled();
    const AppConfig config = m_config;

    m_promptPreparationWatcher->setFuture(QtConcurrent::run([this, trimmed, allowExternalSearch, searchEnabled, config, serial]() -> PromptPreparationResult {
        PromptPreparationResult result;
        result.serial = serial;
        result.prompt = trimmed;
        result.sanitizedPreview = m_policy->redactSensitiveText(trimmed);

        const QVector<MemoryRecord> relevantMemories = m_memoryManager->findRelevant(trimmed, config.maxRelevantMemories);
        result.memoryContext = m_memoryManager->formatForPrompt(relevantMemories);

        if (config.preferOutlinePlanning) {
            result.outlinePlan = m_outlinePlanner->planForPrompt(trimmed);
        }
        result.outlineOnlyFirstPass = result.outlinePlan.enabled && isStructuredDocumentRequest(trimmed);

        if (result.outlinePlan.enabled && !result.outlinePlan.sections.isEmpty()) {
            QStringList promptSections;
            QStringList uiSections;
            QSet<QString> seen;
            for (const OutlineSectionPlan &section : result.outlinePlan.sections) {
                const int perSectionLimit = result.outlineOnlyFirstPass ? 1 : qMax(1, config.maxLocalHits / 2);
                const QVector<RagHit> sectionHits = m_rag->searchHits(section.query,
                                                                      perSectionLimit,
                                                                      result.outlinePlan.intent,
                                                                      section.preferredRoles);
                QVector<RagHit> uniqueHits;
                for (const RagHit &hit : sectionHits) {
                    const QString key = hit.filePath + QLatin1Char('#') + QString::number(hit.chunkIndex);
                    if (seen.contains(key)) {
                        continue;
                    }
                    seen.insert(key);
                    uniqueHits.push_back(hit);
                    if (hit.rerankScore > result.bestHitScore) {
                        result.bestHitScore = hit.rerankScore;
                    }
                }
                result.retrievedHits += uniqueHits.size();
                if (uniqueHits.isEmpty()) {
                    continue;
                }

                const QString sectionPromptContext = trimForBudget(m_rag->formatHitsForPrompt(uniqueHits), result.outlineOnlyFirstPass ? 1400 : 2800);
                promptSections << QStringLiteral("SECTION: %1\nOBJECTIVE: %2\nSECTION_CONTEXT:\n%3")
                                      .arg(section.title,
                                           section.objective,
                                           sectionPromptContext);

                QString uiBlock = QStringLiteral("Section: %1\nObjective: %2\nQuery: %3")
                        .arg(section.title, section.objective, section.query);
                if (!section.preferredRoles.isEmpty()) {
                    uiBlock += QStringLiteral("\nPreferred roles: %1").arg(section.preferredRoles.join(QStringLiteral(", ")));
                }
                uiBlock += QStringLiteral("\n\n%1").arg(m_rag->formatHitsForUi(uniqueHits));
                uiSections << uiBlock;
            }
            result.localContext = promptSections.join(QStringLiteral("\n\n"));
            result.localUi = uiSections.isEmpty() ? QStringLiteral("<none>") : uiSections.join(QStringLiteral("\n\n----------------\n\n"));
        } else {
            RetrievalIntent intent = RetrievalIntent::General;
            if (trimmed.contains(QStringLiteral("error"), Qt::CaseInsensitive)
                    || trimmed.contains(QStringLiteral("failed"), Qt::CaseInsensitive)
                    || trimmed.contains(QStringLiteral("alarm"), Qt::CaseInsensitive)) {
                intent = RetrievalIntent::Troubleshooting;
            } else if (trimmed.contains(QStringLiteral("architecture"), Qt::CaseInsensitive)
                       || trimmed.contains(QStringLiteral("topology"), Qt::CaseInsensitive)
                       || trimmed.contains(QStringLiteral("hld"), Qt::CaseInsensitive)
                       || trimmed.contains(QStringLiteral("lld"), Qt::CaseInsensitive)) {
                intent = RetrievalIntent::Architecture;
            } else if (trimmed.contains(QStringLiteral("deploy"), Qt::CaseInsensitive)
                       || trimmed.contains(QStringLiteral("install"), Qt::CaseInsensitive)
                       || trimmed.contains(QStringLiteral("bootstrap"), Qt::CaseInsensitive)) {
                intent = RetrievalIntent::Implementation;
            }

            const QVector<RagHit> localHits = m_rag->searchHits(trimmed, config.maxLocalHits, intent);
            result.retrievedHits = localHits.size();
            if (!localHits.isEmpty()) {
                result.bestHitScore = localHits.first().rerankScore;
            }
            result.localContext = trimForBudget(m_rag->formatHitsForPrompt(localHits), result.outlineOnlyFirstPass ? 4800 : 9600);
            result.localUi = m_rag->formatHitsForUi(localHits);
        }

        result.shouldSearch = allowExternalSearch
                && searchEnabled
                && (m_policy->shouldUseExternalSearch(trimmed) || config.autoSuggestExternalSearch);
        if (result.shouldSearch) {
            result.sanitizedSearchQuery = m_policy->buildSanitizedSearchQuery(trimmed);
        }
        return result;
    }));
}

void ChatController::stopGeneration()
{
    if (!m_busy) {
        return;
    }

    const bool promptPreparationRunning = m_promptPreparationWatcher != nullptr && m_promptPreparationWatcher->isRunning();
    if (promptPreparationRunning) {
        m_promptPreparationSerial += 1;
    } else {
        m_llmClient->stop();
    }
    m_busy = false;
    emit busyChanged(false);
    emit statusChanged(QStringLiteral("Stopped."));
    emit systemNotice(QStringLiteral("Generation stopped by user."));
    addDiagnostic(QStringLiteral("chat"), QStringLiteral("Generation stopped by user after %1 ms").arg(nowMs() - m_requestStartedMs));
}

void ChatController::reindexDocs()
{
    if (m_busy) {
        emit systemNotice(QStringLiteral("Stop the current generation before reindexing local docs."));
        return;
    }

    if (m_indexing) {
        emit systemNotice(QStringLiteral("A document reindex is already running."));
        return;
    }

    m_indexing = true;
    m_rag->setSemanticEnabled(m_config.enableSemanticRetrieval);

    emit indexingStateChanged(true);
    emit indexingProgressChanged(0, 0, QStringLiteral("Preparing local docs..."));
    emit statusChanged(QStringLiteral("Reindexing local docs..."));
    addDiagnostic(QStringLiteral("rag"), QStringLiteral("Reindex started for %1").arg(m_storage->knowledgeRoot()));

    m_reindexWatcher->setFuture(QtConcurrent::run([this]() -> int {
        return m_rag->reindex([this](int value, int maximum, const QString &label) {
            QMetaObject::invokeMethod(this,
                                      [this, value, maximum, label]() {
                                          emit indexingProgressChanged(value, maximum, label);
                                          if (!label.trimmed().isEmpty()) {
                                              emit statusChanged(label);
                                          }
                                      },
                                      Qt::QueuedConnection);
        });
    }));
}

void ChatController::probeBackend()
{
    emit statusChanged(QStringLiteral("Checking Ollama connectivity..."));
    addDiagnostic(QStringLiteral("backend"), QStringLiteral("Probe requested for %1").arg(m_config.ollamaBaseUrl));
    m_llmClient->probe(m_config.ollamaBaseUrl, m_config.ollamaModel);
}

void ChatController::refreshBackendModels()
{
    emit statusChanged(QStringLiteral("Listing Ollama models..."));
    addDiagnostic(QStringLiteral("backend"), QStringLiteral("Listing models from %1").arg(m_config.ollamaBaseUrl));
    m_llmClient->listModels(m_config.ollamaBaseUrl);
}

void ChatController::newConversation()
{
    if (m_busy) {
        emit systemNotice(QStringLiteral("Stop the current generation before starting a new conversation."));
        return;
    }

    QString error;
    const QString id = m_storage->createConversation(QStringLiteral("New conversation"), &error);
    if (id.isEmpty()) {
        emit systemNotice(error.isEmpty() ? QStringLiteral("Failed to create conversation.") : error);
        return;
    }

    m_currentConversationId = id;
    m_history.clear();
    m_currentSummary.clear();
    m_currentOutlinePlanPrompt.clear();
    emit transcriptRestored(QString());
    emit outlinePlanReady(QStringLiteral("<none>"));
    refreshConversationList();
    refreshSummaryPanel();
    emit statusChanged(QStringLiteral("New conversation ready."));
}

void ChatController::loadConversationById(const QString &conversationId)
{
    if (conversationId.trimmed().isEmpty()) {
        return;
    }

    QString error;
    const ConversationRecord record = m_storage->loadConversation(conversationId, &error);
    if (record.id.isEmpty()) {
        emit systemNotice(error.isEmpty() ? QStringLiteral("Failed to load conversation.") : error);
        return;
    }

    m_currentConversationId = record.id;
    m_currentSummary = record.summary;
    m_currentOutlinePlanPrompt.clear();
    m_history.clear();
    m_history.reserve(record.messages.size());
    for (const StoredMessage &message : record.messages) {
        m_history.push_back({message.role, message.content});
    }

    QString stateError;
    m_storage->setLastConversationId(record.id, &stateError);
    emit transcriptRestored(buildTranscriptText());
    emit outlinePlanReady(QStringLiteral("<none>"));
    refreshConversationList();
    refreshMemoryPanel();
    refreshSummaryPanel();
    emit statusChanged(QStringLiteral("Conversation restored."));
    Q_UNUSED(stateError)
}

void ChatController::rememberNote(const QString &text)
{
    QString savedDescription;
    QString error;
    if (!m_memoryManager->saveExplicitNote(text, &savedDescription, &error)) {
        emit systemNotice(error.isEmpty() ? QStringLiteral("Failed to save memory note.") : error);
        return;
    }

    emit systemNotice(savedDescription);
    addDiagnostic(QStringLiteral("memory"), savedDescription);
    refreshMemoryPanel();
}

void ChatController::clearMemories()
{
    QString error;
    if (!m_memoryManager->clearAll(&error)) {
        emit systemNotice(error.isEmpty() ? QStringLiteral("Failed to clear memories.") : error);
        return;
    }

    emit systemNotice(QStringLiteral("All stored memories were cleared."));
    addDiagnostic(QStringLiteral("memory"), QStringLiteral("All stored memories were cleared by the user."));
    refreshMemoryPanel();
}

void ChatController::setBackendModel(const QString &model)
{
    const QString trimmed = model.trimmed();
    if (trimmed.isEmpty() || trimmed == m_config.ollamaModel) {
        return;
    }

    m_config.ollamaModel = trimmed;
    m_llmClient->setGenerationConfig(m_config);
    emit systemNotice(QStringLiteral("Active model set to %1").arg(m_config.ollamaModel));
    addDiagnostic(QStringLiteral("backend"), QStringLiteral("Active model changed to %1").arg(m_config.ollamaModel));
    emit backendSummaryReady(buildBackendSummary());
    emit backendModelsReady(m_availableModels, m_config.ollamaModel);
}

void ChatController::importKnowledgePaths(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }

    QString message;
    const int imported = m_rag->importPaths(paths, m_storage->knowledgeRoot(), &message);
    emit systemNotice(message);
    addDiagnostic(QStringLiteral("ingest"), message);
    if (imported > 0) {
        reindexDocs();
    }
}

void ChatController::onSearchStarted(const QString &query, const QString &requestUrl)
{
    Q_UNUSED(query)
    addDiagnostic(QStringLiteral("search"), QStringLiteral("Calling %1").arg(requestUrl));
}

void ChatController::onSearchFinished(const QString &query,
                                      const QString &formattedContext,
                                      const QString &formattedSources)
{
    Q_UNUSED(query)
    emit externalSourcesReady(formattedSources.isEmpty() ? QStringLiteral("<none>") : formattedSources);
    emit statusChanged(QStringLiteral("External search finished. Generating answer locally..."));
    addDiagnostic(QStringLiteral("search"), QStringLiteral("External search finished"));
    startGeneration(m_pendingPrompt, m_pendingLocalContext, formattedContext, m_pendingMemoryContext);
    m_pendingPrompt.clear();
    m_pendingLocalContext.clear();
    m_pendingMemoryContext.clear();
}

void ChatController::onSearchError(const QString &query, const QString &message)
{
    Q_UNUSED(query)
    emit externalSourcesReady(QStringLiteral("<search error>"));
    emit systemNotice(message);
    addDiagnostic(QStringLiteral("search"), message);
    startGeneration(m_pendingPrompt, m_pendingLocalContext, QString(), m_pendingMemoryContext);
    m_pendingPrompt.clear();
    m_pendingLocalContext.clear();
    m_pendingMemoryContext.clear();
}

void ChatController::onModelStarted()
{
    emit statusChanged(QStringLiteral("Awaiting first local tokens..."));
    addDiagnostic(QStringLiteral("backend"), QStringLiteral("Generation request accepted by backend"));
}

void ChatController::onModelDelta(const QString &text)
{
    if (m_streamChunkCount == 0) {
        emit statusChanged(QStringLiteral("Streaming response locally..."));
        addDiagnostic(QStringLiteral("backend"), QStringLiteral("First token received after %1 ms").arg(nowMs() - m_requestStartedMs));
    }
    ++m_streamChunkCount;
    emit assistantStreamChunk(text);
}

void ChatController::onModelFinished(const QString &fullText)
{
    const QString cleaned = fullText.trimmed();
    m_history.push_back({QStringLiteral("assistant"), cleaned});
    persistMessage(QStringLiteral("assistant"), cleaned);
    updateCurrentSummary();

    m_busy = false;
    emit busyChanged(false);
    emit assistantCompleted(cleaned);
    refreshConversationList();
    refreshSummaryPanel();
    emit statusChanged(QStringLiteral("Ready."));
    addDiagnostic(QStringLiteral("backend"), QStringLiteral("Generation finished in %1 ms with %2 streamed chunk(s) and %3 chars")
                  .arg(nowMs() - m_requestStartedMs).arg(m_streamChunkCount).arg(cleaned.size()));
}

void ChatController::onModelError(const QString &message)
{
    m_busy = false;
    emit busyChanged(false);
    emit systemNotice(message);
    emit statusChanged(QStringLiteral("Error."));
    addDiagnostic(QStringLiteral("backend"), message);
}

void ChatController::onBackendProbeFinished(bool ok, const QString &message)
{
    emit systemNotice(message);
    emit backendSummaryReady(buildBackendSummary());
    emit statusChanged(ok ? QStringLiteral("Ollama reachable.") : QStringLiteral("Ollama unavailable."));
    addDiagnostic(QStringLiteral("backend"), message);
}

void ChatController::onModelsListed(const QStringList &models, const QString &message)
{
    m_availableModels = models;
    emit systemNotice(message);
    emit backendSummaryReady(buildBackendSummary());
    emit backendModelsReady(m_availableModels, m_config.ollamaModel);
    emit statusChanged(QStringLiteral("Ready."));
    addDiagnostic(QStringLiteral("backend"), message);
}

void ChatController::emitStartupNotices()
{
    emit systemNotice(QStringLiteral("Config summary: model=%1 | baseUrl=%2 | dataRoot=%3 | knowledgeRoot=%4")
                      .arg(m_config.ollamaModel,
                           m_config.ollamaBaseUrl,
                           m_config.dataRoot,
                           m_storage->knowledgeRoot()));

    if (m_indexing) {
        emit systemNotice(QStringLiteral("Local docs are being indexed in the background..."));
    } else {
        emit systemNotice(QStringLiteral("Local docs ready on startup: %1 chunks across %2 sources.")
                          .arg(m_startupChunkCount)
                          .arg(m_rag->sourceCount()));
    }

    addDiagnostic(QStringLiteral("startup"), QStringLiteral("Amelia booted with data root %1").arg(m_storage->dataRoot()));
    addDiagnostic(QStringLiteral("startup"), QStringLiteral("Knowledge root %1 contains %2 indexed source(s)")
                  .arg(m_storage->knowledgeRoot())
                  .arg(m_rag->sourceCount()));
    refreshConversationList();
    refreshMemoryPanel();
    refreshSummaryPanel();
    refreshSourceInventory();
    emit outlinePlanReady(QStringLiteral("<none>"));
    emit backendSummaryReady(buildBackendSummary());
}

void ChatController::restoreStartupState()
{
    if (!m_config.restoreLastConversationOnStartup) {
        return;
    }

    const QString lastId = m_storage->lastConversationId();
    if (!lastId.trimmed().isEmpty()) {
        loadConversationById(lastId);
    }
}

void ChatController::startGeneration(const QString &prompt,
                                     const QString &localContext,
                                     const QString &externalContext,
                                     const QString &memoryContext)
{
    const bool needsGrounding = promptRequiresGrounding(prompt);
    const bool hasAnyContext = !localContext.trimmed().isEmpty() || !externalContext.trimmed().isEmpty();

    // Hard refusal: needs grounding, zero context returned.
    if (m_config.requireGroundingForProjectQuestions && needsGrounding && !hasAnyContext) {
        const QString refusal = buildGroundingRefusal(prompt);
        addDiagnostic(QStringLiteral("guardrail"), QStringLiteral("Refused ungrounded answer for project-scoped prompt (no context)"));
        onModelFinished(refusal);
        return;
    }

    // Build prompt messages, optionally injecting a low-confidence warning
    // so the model knows its RAG context is weak before it starts generating.
    const bool contextIsWeak = hasAnyContext && (m_lastBestHitScore < m_config.ragConfidenceThreshold);

    const QVector<LlmChatMessage> messages = buildPromptMessages(prompt,
                                                                 localContext,
                                                                 externalContext,
                                                                 memoryContext,
                                                                 m_currentSummary,
                                                                 contextIsWeak);
    if (contextIsWeak) {
        addDiagnostic(QStringLiteral("guardrail"),
                      QStringLiteral("Low-confidence context (best rerank=%1 < threshold=%2); injected warning into prompt")
                      .arg(QString::number(m_lastBestHitScore, 'f', 2))
                      .arg(QString::number(m_config.ragConfidenceThreshold, 'f', 2)));
    }

    int totalChars = 0;
    for (const LlmChatMessage &message : messages) {
        totalChars += message.content.size();
    }
    addDiagnostic(QStringLiteral("budget"), QStringLiteral("Prompt budgeting applied | local=%1 chars | external=%2 chars | memory=%3 chars | summary=%4 chars | chat_messages=%5 | payload_chars=%6")
                  .arg(localContext.size()).arg(externalContext.size()).arg(memoryContext.size()).arg(m_currentSummary.size()).arg(messages.size()).arg(totalChars));
    addDiagnostic(QStringLiteral("backend"), QStringLiteral("Sending chat request to Ollama (%1 message(s), num_ctx=%2, temperature=%3, top_p=%4, top_k=%5)")
                  .arg(messages.size())
                  .arg(m_config.ollamaNumCtx)
                  .arg(m_config.ollamaTemperature, 0, 'f', 2)
                  .arg(m_config.ollamaTopP, 0, 'f', 2)
                  .arg(m_config.ollamaTopK));
    emit statusChanged(QStringLiteral("Sending request to local model..."));
    m_llmClient->generate(m_config.ollamaBaseUrl, m_config.ollamaModel, messages);
}

QVector<LlmChatMessage> ChatController::buildPromptMessages(const QString &userPrompt,
                                                            const QString &localContext,
                                                            const QString &externalContext,
                                                            const QString &memoryContext,
                                                            const QString &sessionSummary,
                                                            bool contextIsWeak) const
{
    const int localBudget    = m_outlineOnlyFirstPass ? 4800  : 9600;
    const int externalBudget = m_outlineOnlyFirstPass ? 1400  : 2400;
    const int memoryBudget   = 900;
    const int summaryBudget  = 1200;
    const int outlineBudget  = 1400;
    const int historyBudget  = 2400;

    QVector<LlmChatMessage> messages;
    messages.reserve(4);

    messages.push_back({
        QStringLiteral("system"),
        QStringLiteral(
            "You are Amelia, a local coding and cloud operations assistant.\n"
            "\n"
            "GROUNDING RULE: Your answer must be grounded in the supplied LOCAL_CONTEXT "
            "and EXTERNAL_CONTEXT sections.\n"
            "\n"
            "LOCAL_CONTEXT contains project-local knowledge, indexed files, notes, logs, "
            "or other private material retrieved by Amelia.\n"
            "\n"
            "EXTERNAL_CONTEXT contains web results or external references retrieved by "
            "Amelia at runtime. If EXTERNAL_CONTEXT is present, you may use it as valid "
            "evidence.\n"
            "\n"
            "Do NOT claim that you cannot search the internet or access external sources "
            "if EXTERNAL_CONTEXT has been provided. Instead, refer to it naturally as "
            "retrieved external context or retrieved web context.\n"
            "\n"
            "If the supplied context does not contain enough information to answer, respond "
            "exactly: 'I don't know based on the provided context.'\n"
            "\n"
            "Do NOT use built-in model knowledge to fill gaps in the supplied context.\n"
            "Do NOT invent file names, class names, commands, API calls, YAML keys, "
            "configuration values, URLs, or project-specific details.\n"
            "If you are about to write something not supported by the supplied context, "
            "stop and use the fallback sentence instead.\n"
            "\n"
            "Prefer short, factual, direct answers. When you cite a fact, name the source "
            "file or section when possible (for example: 'per config.json:' or "
            "'from retrieved external context:').")
    });

    QStringList developerSections;
    developerSections << QStringLiteral(
        "RUNTIME_RULES:\n"
        "- Before writing each sentence, silently verify that every factual claim is "
        "supported by LOCAL_CONTEXT or EXTERNAL_CONTEXT.\n"
        "- If a claim is not supported, do not write it. Use the fallback sentence.\n"
        "- Never supplement missing context with built-in model knowledge.\n"
        "- Treat project-specific claims about file paths, class names, commands, config "
        "keys, versions, or behaviors as grounded only if they appear explicitly in the "
        "supplied context.\n"
        "- If EXTERNAL_CONTEXT is present, you may summarize it and answer from it.\n"
        "- Do not say you are unable to browse the internet when EXTERNAL_CONTEXT exists; "
        "the application may already have fetched external information for you.\n"
        "- When context is broad but relevant, summarize the strongest supported themes "
        "instead of reflexively refusing. Use the fallback sentence only when the supplied "
        "context is truly insufficient.\n"
        "- Do not role-play, continue hidden reasoning, or break character.\n"
        "- End every response with <END>.");

    if (contextIsWeak) {
        developerSections << QStringLiteral(
            "CONTEXT_QUALITY_WARNING:\n"
            "The retrieved context has a low relevance score for this query. "
            "Be conservative, but still try to summarize the best-supported facts or themes "
            "that are clearly present in the supplied context before refusing. "
            "Use the fallback sentence only if the supplied context is genuinely insufficient.");
    }

    if (m_outlineOnlyFirstPass) {
        developerSections << QStringLiteral(
            "FIRST_PASS_MODE:\n"
            "Return only a compact outline with assumptions, prerequisites, phases, "
            "validation gates, rollback points, and appendix items in markdown.");
    } else if (!m_currentOutlinePlanPrompt.trimmed().isEmpty()) {
        developerSections << QStringLiteral(
            "DOCUMENT_MODE:\n"
            "Follow DOCUMENT_OUTLINE_PLAN and compose the answer section by section "
            "using only the supplied evidence.");
    }

    const QString memoryTrimmed = trimForBudget(memoryContext, memoryBudget);
    if (!memoryTrimmed.trimmed().isEmpty()) {
        developerSections << QStringLiteral("RELEVANT_MEMORIES:\n%1").arg(memoryTrimmed);
    }

    const QString summaryTrimmed = trimForBudget(sessionSummary, summaryBudget);
    if (!summaryTrimmed.trimmed().isEmpty()) {
        developerSections << QStringLiteral("SESSION_SUMMARY:\n%1").arg(summaryTrimmed);
    }

    const QString outlineTrimmed = trimForBudget(m_currentOutlinePlanPrompt, outlineBudget);
    if (!outlineTrimmed.trimmed().isEmpty()) {
        developerSections << QStringLiteral("DOCUMENT_OUTLINE_PLAN:\n%1").arg(outlineTrimmed);
    }

    messages.push_back({QStringLiteral("developer"), developerSections.join(QStringLiteral("\n\n"))});

    QStringList userSections;
    const QString localTrimmed = trimForBudget(localContext, localBudget);
    if (!localTrimmed.trimmed().isEmpty()) {
        userSections << QStringLiteral("LOCAL_CONTEXT:\n%1").arg(localTrimmed);
    }

    const QString externalTrimmed = trimForBudget(externalContext, externalBudget);
    if (!externalTrimmed.trimmed().isEmpty()) {
        userSections << QStringLiteral("EXTERNAL_CONTEXT:\n%1").arg(externalTrimmed);
    }

    QStringList historyLines;
    int historyChars = 0;
    const QVector<Message> history = trimmedHistory();
    for (int i = history.size() - 1; i >= 0; --i) {
        const Message &message = history.at(i);
        if (message.role == QStringLiteral("assistant") && !m_config.includeAssistantHistoryInPrompt) {
            continue;
        }
        if (message.role == QStringLiteral("user") && message.content == userPrompt) {
            continue;
        }
        const QString line = QStringLiteral("%1: %2")
                                 .arg(message.role.toUpper(), message.content.simplified());
        const int cost = line.size() + 2;
        if (!historyLines.isEmpty() && historyChars + cost > historyBudget) {
            break;
        }
        historyLines.prepend(line);
        historyChars += cost;
    }

    if (!historyLines.isEmpty()) {
        userSections << QStringLiteral("RECENT_CONVERSATION:\n%1")
                            .arg(historyLines.join(QStringLiteral("\n\n")));
    }

    userSections << QStringLiteral("USER_REQUEST:\n%1").arg(userPrompt);
    messages.push_back({QStringLiteral("user"), userSections.join(QStringLiteral("\n\n"))});

    return messages;
}

QString ChatController::buildGroundingRefusal(const QString &prompt) const
{
    Q_UNUSED(prompt)
    return QStringLiteral("I don't know based on the provided context. Please index the relevant files, docs, or logs, or enable External Search and try again.");
}

bool ChatController::promptRequiresGrounding(const QString &prompt) const
{
    const QString lower = prompt.toLower();
    if (promptLooksCasual(prompt)) {
        return false;
    }

    const QStringList strongSignals = {
        QStringLiteral("my project"),
        QStringLiteral("our project"),
        QStringLiteral("this project"),
        QStringLiteral("this repo"),
        QStringLiteral("our repo"),
        QStringLiteral("repository"),
        QStringLiteral("codebase"),
        QStringLiteral("source tree"),
        QStringLiteral("this file"),
        QStringLiteral("that file"),
        QStringLiteral("which file"),
        QStringLiteral("class "),
        QStringLiteral("function "),
        QStringLiteral("method "),
        QStringLiteral("pipeline"),
        QStringLiteral("module"),
        QStringLiteral("cmakelists"),
        QStringLiteral("config.json"),
        QStringLiteral("yaml"),
        QStringLiteral("json"),
        QStringLiteral("what can you do"),
        QStringLiteral("your capabilities"),
        QStringLiteral("filesystem"),
        QStringLiteral("directory"),
        QStringLiteral("folder"),
        QStringLiteral("path "),
        QStringLiteral("in this app"),
        QStringLiteral("in amelia"),
        QStringLiteral("our code")
    };

    if (containsAny(lower, strongSignals)) {
        return true;
    }

    return isStructuredDocumentRequest(prompt);
}

bool ChatController::promptLooksCasual(const QString &prompt) const
{
    const QString lower = prompt.trimmed().toLower();
    return lower == QStringLiteral("hi")
            || lower == QStringLiteral("hello")
            || lower == QStringLiteral("hey")
            || lower == QStringLiteral("thanks")
            || lower == QStringLiteral("thank you")
            || lower == QStringLiteral("ok")
            || lower == QStringLiteral("okay");
}

QVector<ChatController::Message> ChatController::trimmedHistory() const
{
    if (m_history.size() <= m_config.maxHistoryTurns) {
        return m_history;
    }
    return m_history.mid(m_history.size() - m_config.maxHistoryTurns);
}

QString ChatController::buildBackendSummary() const
{
    QStringList lines;
    lines << QStringLiteral("Configured base URL: %1").arg(m_config.ollamaBaseUrl);
    lines << QStringLiteral("Configured model: %1").arg(m_config.ollamaModel);
    lines << QStringLiteral("External search timeout: %1 ms").arg(m_config.externalSearchTimeoutMs);
    lines << QStringLiteral("Ollama probe timeout: %1 ms").arg(m_config.ollamaProbeTimeoutMs);
    lines << QStringLiteral("Ollama response-headers timeout: %1 ms").arg(m_config.ollamaResponseHeadersTimeoutMs);
    lines << QStringLiteral("Ollama first-token timeout: %1 ms").arg(m_config.ollamaFirstTokenTimeoutMs);
    lines << QStringLiteral("Ollama inactivity timeout: %1 ms").arg(m_config.ollamaInactivityTimeoutMs);
    lines << QStringLiteral("Ollama total timeout: %1").arg(
        m_config.ollamaTotalTimeoutMs <= 0
            ? QStringLiteral("disabled")
            : QString::number(m_config.ollamaTotalTimeoutMs) + QStringLiteral(" ms"));
    lines << QStringLiteral("Configured num_ctx: %1").arg(m_config.ollamaNumCtx);
    lines << QStringLiteral("Sampling: temperature=%1 | top_p=%2 | top_k=%3 | repeat_penalty=%4")
             .arg(m_config.ollamaTemperature, 0, 'f', 2)
             .arg(m_config.ollamaTopP, 0, 'f', 2)
             .arg(m_config.ollamaTopK)
             .arg(m_config.ollamaRepeatPenalty, 0, 'f', 2);
    lines << QStringLiteral("Grounding required for project questions: %1").arg(m_config.requireGroundingForProjectQuestions ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("RAG confidence threshold: %1").arg(m_config.ragConfidenceThreshold, 0, 'f', 2);
    lines << QStringLiteral("Assistant history in prompt: %1").arg(m_config.includeAssistantHistoryInPrompt ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("Storage root: %1").arg(m_storage != nullptr ? m_storage->dataRoot() : QStringLiteral("<none>"));
    lines << QStringLiteral("Knowledge root: %1").arg(m_storage != nullptr ? m_storage->knowledgeRoot() : QStringLiteral("<none>"));
    lines << QStringLiteral("Current conversation: %1").arg(m_currentConversationId.isEmpty() ? QStringLiteral("<none>") : m_currentConversationId);
    lines << QStringLiteral("Persisted summary available: %1").arg(m_currentSummary.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes"));
    lines << QStringLiteral("RAG sources loaded: %1").arg(m_rag->sourceCount());
    lines << QStringLiteral("RAG chunks loaded: %1").arg(m_rag->chunkCount());
    lines << QStringLiteral("Semantic retrieval: %1").arg(m_config.enableSemanticRetrieval ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    lines << QStringLiteral("Outline planning: %1").arg(m_config.preferOutlinePlanning ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    lines << QStringLiteral("Embedding backend: %1").arg(m_embeddingClient->backendName());
    lines << QStringLiteral("External search default: %1").arg(m_config.enableExternalSearch ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    lines << QStringLiteral("Domain allowlist: %1").arg(m_config.externalSearchDomainAllowlist.isEmpty() ? QStringLiteral("<all domains>") : m_config.externalSearchDomainAllowlist.join(QStringLiteral(", ")));
    lines << QStringLiteral("Available models: %1").arg(m_availableModels.isEmpty() ? QStringLiteral("<unknown>") : m_availableModels.join(QStringLiteral(", ")));
    lines << QStringLiteral("Config search path tip: ~/.amelia_qt6 is both the data root and the preferred config location (config.json).");
    return lines.join(QStringLiteral("\n"));
}

QString ChatController::buildTranscriptText() const
{
    QStringList lines;
    for (const Message &message : m_history) {
        if (message.role == QStringLiteral("user")) {
            lines << QStringLiteral("USER> %1").arg(message.content);
        } else if (message.role == QStringLiteral("assistant")) {
            lines << QStringLiteral("ASSISTANT> %1").arg(message.content);
        } else {
            lines << QStringLiteral("[%1] %2").arg(message.role, message.content);
        }
        lines << QString();
    }
    return lines.join(QStringLiteral("\n"));
}

QString ChatController::ensureConversationForPrompt(const QString &prompt)
{
    if (!m_currentConversationId.isEmpty()) {
        return m_currentConversationId;
    }

    QString error;
    const QString id = m_storage->createConversation(titleFromPrompt(prompt), &error);
    if (id.isEmpty()) {
        emit systemNotice(error.isEmpty() ? QStringLiteral("Failed to create conversation. Messages will remain in-memory only.") : error);
        return QString();
    }

    m_currentConversationId = id;
    m_currentSummary.clear();
    refreshConversationList();
    return id;
}

void ChatController::refreshConversationList()
{
    const QVector<ConversationRecord> records = m_storage->listConversations();
    QStringList ids;
    QStringList titles;
    ids.reserve(records.size());
    titles.reserve(records.size());
    for (const ConversationRecord &record : records) {
        ids << record.id;
        titles << QStringLiteral("%1 — %2").arg(record.title,
                                                  record.updatedAt.isEmpty() ? QStringLiteral("no timestamp") : record.updatedAt);
    }
    emit conversationListReady(ids, titles, m_currentConversationId);
}

void ChatController::refreshMemoryPanel()
{
    emit memoriesViewReady(m_memoryManager->formatForUi(m_memoryManager->loadAll(nullptr)));
}

void ChatController::refreshSummaryPanel()
{
    emit sessionSummaryReady(m_currentSummary.isEmpty() ? QStringLiteral("<none>") : m_currentSummary);
}

void ChatController::refreshSourceInventory()
{
    emit sourceInventoryReady(m_rag->formatInventoryForUi());
}

void ChatController::persistMessage(const QString &role, const QString &content)
{
    if (m_currentConversationId.isEmpty()) {
        return;
    }

    StoredMessage message;
    message.role = role;
    message.content = content;
    message.createdAt = nowIso();

    QString error;
    m_storage->appendMessage(m_currentConversationId, message, &error);

    if (role == QStringLiteral("user")) {
        const QVector<ConversationRecord> records = m_storage->listConversations();
        for (const ConversationRecord &record : records) {
            if (record.id == m_currentConversationId && record.title == QStringLiteral("New conversation")) {
                QString renameError;
                m_storage->renameConversation(m_currentConversationId, titleFromPrompt(content), &renameError);
                break;
            }
        }
    }

    QString stateError;
    m_storage->setLastConversationId(m_currentConversationId, &stateError);
    refreshConversationList();
    Q_UNUSED(error)
    Q_UNUSED(stateError)
}

void ChatController::updateCurrentSummary()
{
    QVector<SummaryMessage> summaryHistory;
    summaryHistory.reserve(m_history.size());
    for (const Message &message : m_history) {
        summaryHistory.push_back({message.role, message.content});
    }

    m_currentSummary = m_sessionSummarizer->summarize(summaryHistory, m_currentSummary, m_config.maxHistoryTurns);
    if (m_currentConversationId.isEmpty() || !m_config.autoSaveSessionSummary) {
        return;
    }

    QString error;
    m_storage->updateSummary(m_currentConversationId, m_currentSummary, &error);
    Q_UNUSED(error)
}

QString ChatController::titleFromPrompt(const QString &prompt) const
{
    QString simplified = prompt.simplified();
    const QStringList words = simplified.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return QStringLiteral("New conversation");
    }

    QStringList titleWords = words.mid(0, qMin(8, words.size()));
    QString title = titleWords.join(QStringLiteral(" "));
    if (words.size() > 8) {
        title += QStringLiteral("...");
    }
    return title;
}

void ChatController::addDiagnostic(const QString &category, const QString &message)
{
    const QString line = QStringLiteral("[%1] [%2] %3")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
                 category,
                 message);
    printDiagnosticToConsole(category, line);
    m_diagnostics.push_back(line);
    while (m_diagnostics.size() > m_config.maxDiagnosticLines) {
        m_diagnostics.removeFirst();
    }
    emitDiagnostics();
}

void ChatController::emitDiagnostics()
{
    emit diagnosticsReady(m_diagnostics.join(QStringLiteral("\n")));
}

void ChatController::seedInitialKnowledge()
{
    if (!m_config.seedDocsIntoKnowledge) {
        return;
    }

    QDir knowledgeDir(m_storage->knowledgeRoot());
    const bool alreadySeeded = !knowledgeDir.entryList(QDir::Files | QDir::NoDotAndDotDot | QDir::AllDirs).isEmpty();
    if (alreadySeeded) {
        return;
    }

    if (!QFileInfo::exists(m_config.docsRoot)) {
        return;
    }

    QString message;
    const int imported = m_rag->importPaths({m_config.docsRoot}, m_storage->knowledgeRoot(), &message);
    if (imported > 0) {
        addDiagnostic(QStringLiteral("ingest"), QStringLiteral("Seeded %1 file(s) from %2").arg(imported).arg(m_config.docsRoot));
    }
}
