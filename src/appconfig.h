#pragma once

#include <QDir>
#include <QString>
#include <QStringList>

inline QString ameliaDefaultDataRoot()
{
    return QDir::home().filePath(QStringLiteral(".amelia_qt6"));
}

struct AppConfig {
    QString ollamaBaseUrl = QStringLiteral("http://127.0.0.1:11434");
    QString ollamaModel = QStringLiteral("qwen2.5-coder:14b");
    QString docsRoot = QStringLiteral("./docs/sample");
    QString dataRoot = ameliaDefaultDataRoot();
    QString knowledgeRoot;
    bool enableExternalSearch = false;
    bool autoSuggestExternalSearch = false;
    bool probeOllamaOnStartup = true;
    bool restoreLastConversationOnStartup = true;
    bool autoPersistMemories = true;
    bool autoSaveSessionSummary = true;
    bool seedDocsIntoKnowledge = true;
    bool enableSemanticRetrieval = true;
    bool preferOutlinePlanning = true;
    bool requireGroundingForProjectQuestions = true;
    bool includeAssistantHistoryInPrompt = false;
    QString searxngUrl = QStringLiteral("http://127.0.0.1:8080/search");
    int maxHistoryTurns = 4;
    int maxLocalHits = 3;
    int maxExternalHits = 2;
    int maxRelevantMemories = 6;
    int externalSearchTimeoutMs = 15000;
    int ollamaProbeTimeoutMs = 10000;
    int ollamaResponseHeadersTimeoutMs = 180000;
    int ollamaFirstTokenTimeoutMs = 600000;
    int ollamaInactivityTimeoutMs = 300000;
    int ollamaTotalTimeoutMs = 0;
    int maxDiagnosticLines = 400;
    int ollamaNumCtx = 32768;
    int ollamaTopK = 50;
    double ollamaTemperature = 0.15;
    double ollamaTopP = 0.95;
    double ollamaRepeatPenalty = 1.12;
    double ollamaPresencePenalty = 0.0;
    double ollamaFrequencyPenalty = 0.0;
    QStringList ollamaStopSequences = { QStringLiteral("<END>") };
    QStringList externalSearchDomainAllowlist;
};

class AppConfigLoader {
public:
    static AppConfig load(const QString &path, QString *errorMessage = nullptr);
};
