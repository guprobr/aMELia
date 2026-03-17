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
    QString ollamaModel = QStringLiteral("qwen2.5-coder:7b");
    QString docsRoot = QStringLiteral("./docs/sample");
    QString dataRoot = ameliaDefaultDataRoot();
    QString knowledgeRoot;
    bool enableExternalSearch = false;
    bool autoSuggestExternalSearch = true;
    bool probeOllamaOnStartup = true;
    bool restoreLastConversationOnStartup = true;
    bool autoPersistMemories = true;
    bool autoSaveSessionSummary = true;
    bool seedDocsIntoKnowledge = true;
    bool enableSemanticRetrieval = true;
    bool preferOutlinePlanning = true;
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
    QStringList externalSearchDomainAllowlist;
};

class AppConfigLoader {
public:
    static AppConfig load(const QString &path, QString *errorMessage = nullptr);
};
