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
    if (!m_rag->loadCache()) {
        m_startupChunkCount = m_rag->reindex();
    } else {
        m_startupChunkCount = m_rag->chunkCount();
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
    m_llmClient->setNumCtx(m_config.ollamaNumCtx);

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
    if (m_busy) {
        emit systemNotice(QStringLiteral("A request is already running. Stop it before sending a new one."));
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
    emit statusChanged(QStringLiteral("Retrieving local context..."));

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

    const QString sanitizedPreview = m_policy->redactSensitiveText(trimmed);
    emit privacyPreviewReady(sanitizedPreview);

    const QVector<MemoryRecord> relevantMemories = m_memoryManager->findRelevant(trimmed, m_config.maxRelevantMemories);
    const QString memoryContext = m_memoryManager->formatForPrompt(relevantMemories);
    refreshMemoryPanel();

    OutlinePlan outlinePlan;
    if (m_config.preferOutlinePlanning) {
        outlinePlan = m_outlinePlanner->planForPrompt(trimmed);
    }
    m_currentOutlinePlanPrompt = outlinePlan.formatForPrompt();
    emit outlinePlanReady(outlinePlan.formatForUi());
    m_outlineOnlyFirstPass = outlinePlan.enabled && isStructuredDocumentRequest(trimmed);
    if (m_outlineOnlyFirstPass) {
        emit systemNotice(QStringLiteral("Structured document request detected. Amelia will use outline-only first pass to keep the local prompt budget under control."));
        addDiagnostic(QStringLiteral("planner"), QStringLiteral("Outline-only first pass enabled for this request"));
    }
    if (outlinePlan.enabled) {
        addDiagnostic(QStringLiteral("planner"), QStringLiteral("Outline planner activated for %1 section(s)").arg(outlinePlan.sections.size()));
        emit statusChanged(QStringLiteral("Planning outline and retrieving section evidence..."));
    }

    QString localContext;
    QString localUi;
    int retrievedHits = 0;

    if (outlinePlan.enabled && !outlinePlan.sections.isEmpty()) {
        QStringList promptSections;
        QStringList uiSections;
        QSet<QString> seen;
        for (const OutlineSectionPlan &section : outlinePlan.sections) {
            const int perSectionLimit = m_outlineOnlyFirstPass ? 1 : qMax(1, m_config.maxLocalHits / 2);
            const QVector<RagHit> sectionHits = m_rag->searchHits(section.query,
                                                                  perSectionLimit,
                                                                  outlinePlan.intent,
                                                                  section.preferredRoles);
            retrievedHits += sectionHits.size();

            QVector<RagHit> uniqueHits;
            for (const RagHit &hit : sectionHits) {
                const QString key = hit.filePath + QLatin1Char('#') + QString::number(hit.chunkIndex);
                if (seen.contains(key)) {
                    continue;
                }
                seen.insert(key);
                uniqueHits.push_back(hit);
            }
            if (uniqueHits.isEmpty()) {
                continue;
            }
            const QString sectionPromptContext = trimForBudget(m_rag->formatHitsForPrompt(uniqueHits), m_outlineOnlyFirstPass ? 700 : 1400);
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
        localContext = promptSections.join(QStringLiteral("\n\n"));
        localUi = uiSections.isEmpty() ? QStringLiteral("<none>") : uiSections.join(QStringLiteral("\n\n----------------\n\n"));
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

        const QVector<RagHit> localHits = m_rag->searchHits(trimmed, m_config.maxLocalHits, intent);
        retrievedHits = localHits.size();
        localContext = trimForBudget(m_rag->formatHitsForPrompt(localHits), m_outlineOnlyFirstPass ? 2200 : 5200);
        localUi = m_rag->formatHitsForUi(localHits);
    }

    emit localSourcesReady(localUi.isEmpty() ? QStringLiteral("<none>") : localUi);
    addDiagnostic(QStringLiteral("rag"), QStringLiteral("Retrieved %1 local hit(s) from %2 source(s)").arg(retrievedHits).arg(m_rag->sourceCount()));

    const bool shouldSearch = allowExternalSearch
            && m_searchBroker->isEnabled()
            && (m_policy->shouldUseExternalSearch(trimmed) || m_config.autoSuggestExternalSearch);

    if (shouldSearch) {
        const QString sanitizedQuery = m_policy->buildSanitizedSearchQuery(trimmed);
        m_pendingPrompt = trimmed;
        m_pendingLocalContext = localContext;
        m_pendingMemoryContext = memoryContext;
        emit statusChanged(QStringLiteral("Searching external sources with sanitized query..."));
        addDiagnostic(QStringLiteral("search"), QStringLiteral("External search approved with sanitized query: %1").arg(sanitizedQuery));
        m_searchBroker->search(sanitizedQuery);
        return;
    }

    emit externalSourcesReady(QStringLiteral("<none>"));
    startGeneration(trimmed, localContext, QString(), memoryContext);
}

void ChatController::stopGeneration()
{
    if (!m_busy) {
        return;
    }

    m_llmClient->stop();
    m_busy = false;
    emit busyChanged(false);
    emit statusChanged(QStringLiteral("Stopped."));
    emit systemNotice(QStringLiteral("Generation stopped by user."));
    addDiagnostic(QStringLiteral("chat"), QStringLiteral("Generation stopped by user after %1 ms").arg(nowMs() - m_requestStartedMs));
}

void ChatController::reindexDocs()
{
    emit statusChanged(QStringLiteral("Reindexing local docs..."));
    addDiagnostic(QStringLiteral("rag"), QStringLiteral("Reindex started for %1").arg(m_storage->knowledgeRoot()));
    m_rag->setSemanticEnabled(m_config.enableSemanticRetrieval);
    const int chunks = m_rag->reindex();
    emit systemNotice(QStringLiteral("Local docs indexed: %1 chunks across %2 sources.").arg(chunks).arg(m_rag->sourceCount()));
    addDiagnostic(QStringLiteral("rag"), QStringLiteral("Reindex finished: %1 chunks across %2 source(s)").arg(chunks).arg(m_rag->sourceCount()));
    refreshSourceInventory();
    emit statusChanged(QStringLiteral("Ready."));
    emit backendSummaryReady(buildBackendSummary());
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
    m_history.push_back({QStringLiteral("assistant"), fullText});
    persistMessage(QStringLiteral("assistant"), fullText);
    updateCurrentSummary();

    m_busy = false;
    emit busyChanged(false);
    emit assistantCompleted(fullText);
    refreshConversationList();
    refreshSummaryPanel();
    emit statusChanged(QStringLiteral("Ready."));
    addDiagnostic(QStringLiteral("backend"), QStringLiteral("Generation finished in %1 ms with %2 streamed chunk(s) and %3 chars")
                  .arg(nowMs() - m_requestStartedMs).arg(m_streamChunkCount).arg(fullText.size()));
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
    emit systemNotice(QStringLiteral("Local docs ready on startup: %1 chunks across %2 sources.").arg(m_startupChunkCount).arg(m_rag->sourceCount()));
    addDiagnostic(QStringLiteral("startup"), QStringLiteral("Amelia booted with data root %1").arg(m_storage->dataRoot()));
    addDiagnostic(QStringLiteral("startup"), QStringLiteral("Knowledge root %1 contains %2 indexed source(s)").arg(m_storage->knowledgeRoot()).arg(m_rag->sourceCount()));
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
    const QString envelope = buildPromptEnvelope(prompt,
                                                 localContext,
                                                 externalContext,
                                                 memoryContext,
                                                 m_currentSummary);
    addDiagnostic(QStringLiteral("budget"), QStringLiteral("Prompt budgeting applied | local=%1 chars | external=%2 chars | memory=%3 chars | summary=%4 chars | envelope=%5 chars")
                  .arg(localContext.size()).arg(externalContext.size()).arg(memoryContext.size()).arg(m_currentSummary.size()).arg(envelope.size()));
    addDiagnostic(QStringLiteral("backend"), QStringLiteral("Sending prompt envelope to Ollama (%1 chars, num_ctx=%2)").arg(envelope.size()).arg(m_config.ollamaNumCtx));
    emit statusChanged(QStringLiteral("Sending request to local model..."));
    m_llmClient->generate(m_config.ollamaBaseUrl, m_config.ollamaModel, envelope);
}

QString ChatController::buildPromptEnvelope(const QString &userPrompt,
                                            const QString &localContext,
                                            const QString &externalContext,
                                            const QString &memoryContext,
                                            const QString &sessionSummary) const
{
    const int localBudget = m_outlineOnlyFirstPass ? 2600 : 5200;
    const int externalBudget = m_outlineOnlyFirstPass ? 700 : 1500;
    const int memoryBudget = 900;
    const int summaryBudget = 1100;
    const int outlineBudget = 1600;
    const int historyBudget = 1300;

    QStringList sections;
    QStringList instructions;
    instructions << QStringLiteral(
        "You are Amelia, a local-first engineering assistant specialized in code, debugging, distributed systems, Linux, Kubernetes and WRCP. "
        "Treat local context as the primary source. Do not claim to have searched the internet unless SANITIZED_EXTERNAL_CONTEXT is present. "
        "Protect sensitive data and do not reproduce redacted secrets. "
        "When sources disagree, prefer scenario-specific and procedural sources over giant generic references, and explain uncertainty.");

    if (m_outlineOnlyFirstPass) {
        instructions << QStringLiteral(
            "FIRST_PASS_MODE: produce only a compact outline-first answer. Do not write the full final document yet. "
            "Return only: assumptions, prerequisites, deployment phases, validation gates, rollback points, and appendix items in markdown.");
    } else if (!m_currentOutlinePlanPrompt.trimmed().isEmpty()) {
        instructions << QStringLiteral(
            "When the request is a structured document, follow DOCUMENT_OUTLINE_PLAN first and compose the answer section by section.");
    }

    sections << instructions.join(QStringLiteral("\n"));

    const QString memoryTrimmed = trimForBudget(memoryContext, memoryBudget);
    if (!memoryTrimmed.trimmed().isEmpty()) {
        sections << QStringLiteral("RELEVANT_PERSISTED_MEMORIES:\n%1").arg(memoryTrimmed);
    }

    const QString summaryTrimmed = trimForBudget(sessionSummary, summaryBudget);
    if (!summaryTrimmed.trimmed().isEmpty()) {
        sections << QStringLiteral("PERSISTED_SESSION_SUMMARY:\n%1").arg(summaryTrimmed);
    }

    const QString outlineTrimmed = trimForBudget(m_currentOutlinePlanPrompt, outlineBudget);
    if (!outlineTrimmed.trimmed().isEmpty()) {
        sections << QStringLiteral("DOCUMENT_OUTLINE_PLAN:\n%1").arg(outlineTrimmed);
    }

    const QString localTrimmed = trimForBudget(localContext, localBudget);
    if (!localTrimmed.trimmed().isEmpty()) {
        sections << QStringLiteral("LOCAL_WRCP_CONTEXT:\n%1").arg(localTrimmed);
    }

    const QString externalTrimmed = trimForBudget(externalContext, externalBudget);
    if (!externalTrimmed.trimmed().isEmpty()) {
        sections << QStringLiteral("SANITIZED_EXTERNAL_CONTEXT:\n%1").arg(externalTrimmed);
    }

    QStringList historyLines;
    int historyChars = 0;
    const QVector<Message> history = trimmedHistory();
    for (int i = history.size() - 1; i >= 0; --i) {
        const QString line = QStringLiteral("%1: %2").arg(history.at(i).role.toUpper(), history.at(i).content.simplified());
        const int cost = line.size() + 2;
        if (!historyLines.isEmpty() && historyChars + cost > historyBudget) {
            break;
        }
        historyLines.prepend(line);
        historyChars += cost;
    }
    if (!historyLines.isEmpty()) {
        sections << QStringLiteral("RECENT_CONVERSATION:\n%1").arg(historyLines.join(QStringLiteral("\n\n")));
    }

    sections << QStringLiteral("USER_REQUEST:\n%1").arg(userPrompt);
    sections << QStringLiteral("ANSWER:");
    return sections.join(QStringLiteral("\n\n"));
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
