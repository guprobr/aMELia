#pragma once

#include <QDir>
#include <QString>
#include <QStringList>

inline QString ameliaDefaultDataRoot()
{
    return QDir::home().filePath(QStringLiteral(".amelia_qt6"));
}

inline QString ameliaDefaultOllamaBaseUrl()
{
    return QStringLiteral("http://localhost:11434");
}

inline QString ameliaDefaultSearxngUrl()
{
    return QStringLiteral("http://127.0.0.1:8080/search");
}

QString ameliaPreferredUserConfigPath();
QString ameliaBuiltInDefaultConfigJson(const QString &ollamaBaseUrl = QString(),
                                       const QString &searxngUrl = QString());

struct AppConfig {
    QString ollamaBaseUrl = ameliaDefaultOllamaBaseUrl();
    QString ollamaModel = QStringLiteral("qwen2.5:7b");
    QString docsRoot = QStringLiteral("${dataRoot}/docs/sample");
    QString dataRoot = ameliaDefaultDataRoot();
    QString knowledgeRoot;

    // --- RAG / retrieval ---
    bool enableExternalSearch = false;
    bool autoSuggestExternalSearch = true;
    bool probeOllamaOnStartup = true;
    bool restoreLastConversationOnStartup = true;
    bool autoPersistMemories = true;
    bool autoSaveSessionSummary = true;
    bool seedDocsIntoKnowledge = true;
    bool enableDesktopNotifications = true;
    bool notifyOnTaskStart = true;
    bool notifyOnTaskSuccess = true;
    bool notifyOnTaskFailure = true;

    // Semantic retrieval via the lightweight local hash embedder.
    // Enabled by default so large KBs get better relevance scoring without
    // blocking the UI; Amelia can refresh vectors incrementally in the background.
    bool enableSemanticRetrieval = true;

    bool preferOutlinePlanning = true;
    bool requireGroundingForProjectQuestions = true;

    // Include assistant turns in the history block sent to the model.
    // Enable this so the model can track what it already said and avoid
    // repeating or contradicting itself.
    bool includeAssistantHistoryInPrompt = true;

    QString searxngUrl = QStringLiteral("http://127.0.0.1:8080/search");

    // Keep more history turns so the model has real multi-turn continuity.
    int maxHistoryTurns = 8;

    // More hits = more grounded context for the model.
    int maxLocalHits = 8;

    int maxExternalHits = 2;
    int maxRelevantMemories = 6;
    int externalSearchTimeoutMs = 15000;
    int ollamaProbeTimeoutMs = 10000;
    int ollamaResponseHeadersTimeoutMs = 1800000;
    int ollamaFirstTokenTimeoutMs = 600000;
    int ollamaInactivityTimeoutMs = 300000;
    int ollamaTotalTimeoutMs = 0;
    int maxDiagnosticLines = 400;
    int desktopNotificationTimeoutMs = 2500;
    int ollamaNumCtx = 32768;
    int ollamaTopK = 40;

    // Low temperature keeps factual answers deterministic.
    double ollamaTemperature = 0.05;
    double ollamaTopP = 0.90;
    double ollamaRepeatPenalty = 1.10;
    double ollamaPresencePenalty = 0.0;
    double ollamaFrequencyPenalty = 0.0;

    // <|im_end|> and <|endoftext|> are native Qwen3 end-of-turn tokens.
    // Including them prevents the model from generating garbage after it
    // finishes its answer instead of stopping cleanly.
    QStringList ollamaStopSequences = {
        QStringLiteral("<END>"),
        QStringLiteral("<|im_end|>"),
        QStringLiteral("<|endoftext|>")
    };

    QStringList externalSearchDomainAllowlist;

    // Minimum rerank score a RAG hit must reach before we consider the
    // context "confident enough" to answer a grounded question.
    // Raise this if the model still hallucinates from weak hits; lower if
    // too many valid prompts are refused.
    double ragConfidenceThreshold = 0.95;
};

class AppConfigLoader {
public:
    static AppConfig load(const QString &path, QString *errorMessage = nullptr);
};
