#include "core/appconfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QString ameliaPreferredUserConfigPath()
{
    return QDir::home().filePath(QStringLiteral(".amelia_qt6/config.json"));
}

QString ameliaBuiltInDefaultConfigJson(const QString &ollamaBaseUrl,
                                       const QString &searxngUrl)
{
    const QString resolvedOllamaBaseUrl = ollamaBaseUrl.trimmed().isEmpty()
        ? ameliaDefaultOllamaBaseUrl()
        : ollamaBaseUrl.trimmed();
    const QString resolvedSearxngUrl = searxngUrl.trimmed().isEmpty()
        ? ameliaDefaultSearxngUrl()
        : searxngUrl.trimmed();

    return QStringLiteral(
R"JSON({
  "ollamaBaseUrl": "%1",
  "ollamaModel": "gpt-oss:20b",
  "docsRoot": "${dataRoot}/docs/sample",
  "dataRoot": "${HOME}/.amelia_qt6",
  "knowledgeRoot": "${dataRoot}/knowledge",
  "enableExternalSearch": false,
  "autoSuggestExternalSearch": true,
  "probeOllamaOnStartup": true,
  "restoreLastConversationOnStartup": true,
  "autoPersistMemories": true,
  "autoSaveSessionSummary": true,
  "seedDocsIntoKnowledge": true,
  "enableSemanticRetrieval": true,
  "ollamaEmbeddingModel": "embeddinggemma:latest",
  "ollamaEmbeddingTimeoutMs": 120000,
  "ollamaEmbeddingBatchSize": 4,
  "enableDesktopNotifications": true,
  "notifyOnTaskStart": true,
  "notifyOnTaskSuccess": true,
  "notifyOnTaskFailure": true,
  "preferOutlinePlanning": true,
  "requireGroundingForProjectQuestions": true,
  "includeAssistantHistoryInPrompt": true,
  "searxngUrl": "%2",
  "maxHistoryTurns": 8,
  "maxLocalHits": 8,
  "maxExternalHits": 2,
  "maxRelevantMemories": 6,
  "externalSearchTimeoutMs": 15000,
  "desktopNotificationTimeoutMs": 2500,
  "ollamaProbeTimeoutMs": 10000,
  "ollamaResponseHeadersTimeoutMs": 1800000,
  "ollamaFirstTokenTimeoutMs": 600000,
  "ollamaInactivityTimeoutMs": 300000,
  "ollamaTotalTimeoutMs": 0,
  "maxDiagnosticLines": 400,
  "ollamaNumCtx": 32768,
  "ollamaTemperature": 0.05,
  "ollamaTopP": 0.90,
  "ollamaTopK": 40,
  "ollamaRepeatPenalty": 1.10,
  "ollamaPresencePenalty": 0.0,
  "ollamaFrequencyPenalty": 0.0,
  "ollamaStopSequences": ["<END>", "<|im_end|>", "<|endoftext|>"],
  "externalSearchDomainAllowlist": []
})JSON")
        .arg(resolvedOllamaBaseUrl, resolvedSearxngUrl);
}

namespace {
QString expandConfiguredPath(QString path, const QString &fallback, const QString &dataRoot = QString())
{
    path = path.trimmed();
    if (path.isEmpty()) {
        return QDir::fromNativeSeparators(fallback);
    }

    if (path == QStringLiteral("~")) {
        path = QDir::homePath();
    } else if (path.startsWith(QStringLiteral("~/"))) {
        path.replace(0, 1, QDir::homePath());
    }

    path.replace(QRegularExpression(QStringLiteral(R"(\$\{HOME\})")), QDir::homePath());
    path.replace(QRegularExpression(QStringLiteral(R"(\$HOME)")), QDir::homePath());

    if (!dataRoot.trimmed().isEmpty()) {
        path.replace(QRegularExpression(QStringLiteral(R"(\$\{dataRoot\})")), dataRoot);
        path.replace(QRegularExpression(QStringLiteral(R"(\$dataRoot)")), dataRoot);
    }

    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString enforcedKnowledgeRootForDataRoot(const QString &dataRoot)
{
    return QDir(dataRoot).filePath(QStringLiteral("knowledge"));
}

int boundedTimeout(int value, int fallback, int minimum, bool allowZero = false)
{
    if (allowZero && value == 0) {
        return 0;
    }
    if (value <= 0) {
        return fallback;
    }
    return qMax(minimum, value);
}

int readTimeoutValue(const QJsonObject &obj,
                     const QString &key,
                     int fallback,
                     int minimum,
                     bool allowZero = false)
{
    if (!obj.contains(key)) {
        return fallback;
    }
    return boundedTimeout(obj.value(key).toInt(fallback), fallback, minimum, allowZero);
}

double boundedDouble(double value, double fallback, double minimum)
{
    if (value < minimum) {
        return fallback;
    }
    return value;
}

double readDoubleValue(const QJsonObject &obj,
                       const QString &key,
                       double fallback,
                       double minimum)
{
    if (!obj.contains(key)) {
        return fallback;
    }
    return boundedDouble(obj.value(key).toDouble(fallback), fallback, minimum);
}

void applyStringEnvOverride(const char *name, QString &target)
{
    if (!qEnvironmentVariableIsSet(name)) {
        return;
    }

    const QString value = qEnvironmentVariable(name).trimmed();
    if (!value.isEmpty()) {
        target = value;
    }
}

void applyIntEnvOverride(const char *name,
                         int &target,
                         int minimum,
                         bool allowZero = false)
{
    if (!qEnvironmentVariableIsSet(name)) {
        return;
    }

    bool ok = false;
    const int value = qEnvironmentVariableIntValue(name, &ok);
    if (!ok) {
        return;
    }

    target = boundedTimeout(value, target, minimum, allowZero);
}

void applyDoubleEnvOverride(const char *name,
                            double &target,
                            double minimum)
{
    if (!qEnvironmentVariableIsSet(name)) {
        return;
    }

    bool ok = false;
    const double value = qEnvironmentVariable(name).toDouble(&ok);
    if (!ok) {
        return;
    }

    target = boundedDouble(value, target, minimum);
}

void applyBoolEnvOverride(const char *name, bool &target)
{
    if (!qEnvironmentVariableIsSet(name)) {
        return;
    }

    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    if (value == QStringLiteral("1") || value == QStringLiteral("true") || value == QStringLiteral("yes") || value == QStringLiteral("on")) {
        target = true;
    } else if (value == QStringLiteral("0") || value == QStringLiteral("false") || value == QStringLiteral("no") || value == QStringLiteral("off")) {
        target = false;
    }
}

void applyEnvOverrides(AppConfig &config)
{
    applyStringEnvOverride("AMELIA_OLLAMA_BASE_URL", config.ollamaBaseUrl);
    applyStringEnvOverride("AMELIA_OLLAMA_MODEL", config.ollamaModel);
    applyStringEnvOverride("AMELIA_OLLAMA_EMBEDDING_MODEL", config.ollamaEmbeddingModel);
    applyIntEnvOverride("AMELIA_EXTERNAL_SEARCH_TIMEOUT_MS", config.externalSearchTimeoutMs, 2000);
    applyIntEnvOverride("AMELIA_OLLAMA_EMBEDDING_TIMEOUT_MS", config.ollamaEmbeddingTimeoutMs, 3000);
    applyIntEnvOverride("AMELIA_OLLAMA_EMBEDDING_BATCH_SIZE", config.ollamaEmbeddingBatchSize, 1);
    applyIntEnvOverride("AMELIA_OLLAMA_PROBE_TIMEOUT_MS", config.ollamaProbeTimeoutMs, 2000);
    applyIntEnvOverride("AMELIA_OLLAMA_RESPONSE_HEADERS_TIMEOUT_MS", config.ollamaResponseHeadersTimeoutMs, 5000);
    applyIntEnvOverride("AMELIA_OLLAMA_FIRST_TOKEN_TIMEOUT_MS", config.ollamaFirstTokenTimeoutMs, 5000);
    applyIntEnvOverride("AMELIA_OLLAMA_INACTIVITY_TIMEOUT_MS", config.ollamaInactivityTimeoutMs, 5000);
    applyIntEnvOverride("AMELIA_OLLAMA_TOTAL_TIMEOUT_MS", config.ollamaTotalTimeoutMs, 5000, true);
    applyIntEnvOverride("AMELIA_DESKTOP_NOTIFICATION_TIMEOUT_MS", config.desktopNotificationTimeoutMs, 1000);
    applyIntEnvOverride("AMELIA_OLLAMA_NUM_CTX", config.ollamaNumCtx, 1024);
    applyIntEnvOverride("AMELIA_OLLAMA_TOP_K", config.ollamaTopK, 1);
    applyDoubleEnvOverride("AMELIA_OLLAMA_TEMPERATURE", config.ollamaTemperature, 0.0);
    applyDoubleEnvOverride("AMELIA_OLLAMA_TOP_P", config.ollamaTopP, 0.0);
    applyDoubleEnvOverride("AMELIA_OLLAMA_REPEAT_PENALTY", config.ollamaRepeatPenalty, 0.0);
    applyDoubleEnvOverride("AMELIA_OLLAMA_PRESENCE_PENALTY", config.ollamaPresencePenalty, 0.0);
    applyDoubleEnvOverride("AMELIA_OLLAMA_FREQUENCY_PENALTY", config.ollamaFrequencyPenalty, 0.0);
    applyBoolEnvOverride("AMELIA_ENABLE_DESKTOP_NOTIFICATIONS", config.enableDesktopNotifications);
    applyBoolEnvOverride("AMELIA_NOTIFY_ON_TASK_START", config.notifyOnTaskStart);
    applyBoolEnvOverride("AMELIA_NOTIFY_ON_TASK_SUCCESS", config.notifyOnTaskSuccess);
    applyBoolEnvOverride("AMELIA_NOTIFY_ON_TASK_FAILURE", config.notifyOnTaskFailure);
}
}

AppConfig AppConfigLoader::load(const QString &path, QString *errorMessage)
{
    AppConfig config;
    config.dataRoot = expandConfiguredPath(config.dataRoot, ameliaDefaultDataRoot());
    config.docsRoot = expandConfiguredPath(config.docsRoot, QDir(config.dataRoot).filePath(QStringLiteral("docs/sample")), config.dataRoot);
    config.knowledgeRoot = enforcedKnowledgeRootForDataRoot(config.dataRoot);

    QFile file(path);
    if (!file.exists()) {
        applyEnvOverrides(config);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Config file not found: %1. Using defaults.").arg(path);
        }
        return config;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        applyEnvOverrides(config);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to open config file: %1. Using defaults.").arg(path);
        }
        return config;
    }

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        applyEnvOverrides(config);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Invalid JSON in config file: %1. Using defaults.").arg(path);
        }
        return config;
    }

    const QJsonObject obj = doc.object();
    QStringList notices;
    config.ollamaBaseUrl = obj.value(QStringLiteral("ollamaBaseUrl")).toString(config.ollamaBaseUrl).trimmed();
    config.ollamaModel = obj.value(QStringLiteral("ollamaModel")).toString(config.ollamaModel).trimmed();
    if (config.ollamaModel == QStringLiteral("qwen2.5-7b")) {
        config.ollamaModel = QStringLiteral("qwen2.5:7b");
    }
    config.dataRoot = expandConfiguredPath(obj.value(QStringLiteral("dataRoot")).toString(config.dataRoot), ameliaDefaultDataRoot());
    config.docsRoot = expandConfiguredPath(obj.value(QStringLiteral("docsRoot")).toString(config.docsRoot), QDir(config.dataRoot).filePath(QStringLiteral("docs/sample")), config.dataRoot);

    const QString effectiveKnowledgeRoot = enforcedKnowledgeRootForDataRoot(config.dataRoot);
    const QString configuredKnowledgeRoot = expandConfiguredPath(
        obj.value(QStringLiteral("knowledgeRoot")).toString(effectiveKnowledgeRoot),
        effectiveKnowledgeRoot,
        config.dataRoot);
    config.knowledgeRoot = effectiveKnowledgeRoot;
    if (obj.contains(QStringLiteral("knowledgeRoot"))
        && QDir::cleanPath(QDir::fromNativeSeparators(configuredKnowledgeRoot))
            != QDir::cleanPath(QDir::fromNativeSeparators(effectiveKnowledgeRoot))) {
        notices << QStringLiteral("Configured knowledgeRoot is ignored outside Amelia's data root. Using %1.")
                       .arg(QDir::toNativeSeparators(effectiveKnowledgeRoot));
    }

    config.enableExternalSearch = obj.value(QStringLiteral("enableExternalSearch")).toBool(config.enableExternalSearch);
    config.autoSuggestExternalSearch = obj.value(QStringLiteral("autoSuggestExternalSearch")).toBool(config.autoSuggestExternalSearch);
    config.probeOllamaOnStartup = obj.value(QStringLiteral("probeOllamaOnStartup")).toBool(config.probeOllamaOnStartup);
    config.restoreLastConversationOnStartup = obj.value(QStringLiteral("restoreLastConversationOnStartup")).toBool(config.restoreLastConversationOnStartup);
    config.autoPersistMemories = obj.value(QStringLiteral("autoPersistMemories")).toBool(config.autoPersistMemories);
    config.autoSaveSessionSummary = obj.value(QStringLiteral("autoSaveSessionSummary")).toBool(config.autoSaveSessionSummary);
    config.seedDocsIntoKnowledge = obj.value(QStringLiteral("seedDocsIntoKnowledge")).toBool(config.seedDocsIntoKnowledge);
    config.enableDesktopNotifications = obj.value(QStringLiteral("enableDesktopNotifications")).toBool(config.enableDesktopNotifications);
    config.notifyOnTaskStart = obj.value(QStringLiteral("notifyOnTaskStart")).toBool(config.notifyOnTaskStart);
    config.notifyOnTaskSuccess = obj.value(QStringLiteral("notifyOnTaskSuccess")).toBool(config.notifyOnTaskSuccess);
    config.notifyOnTaskFailure = obj.value(QStringLiteral("notifyOnTaskFailure")).toBool(config.notifyOnTaskFailure);
    config.enableSemanticRetrieval = obj.value(QStringLiteral("enableSemanticRetrieval")).toBool(config.enableSemanticRetrieval);
    config.ollamaEmbeddingModel = obj.value(QStringLiteral("ollamaEmbeddingModel")).toString(config.ollamaEmbeddingModel).trimmed();
    config.preferOutlinePlanning = obj.value(QStringLiteral("preferOutlinePlanning")).toBool(config.preferOutlinePlanning);
    config.requireGroundingForProjectQuestions = obj.value(QStringLiteral("requireGroundingForProjectQuestions")).toBool(config.requireGroundingForProjectQuestions);
    config.includeAssistantHistoryInPrompt = obj.value(QStringLiteral("includeAssistantHistoryInPrompt")).toBool(config.includeAssistantHistoryInPrompt);
    config.searxngUrl = obj.value(QStringLiteral("searxngUrl")).toString(config.searxngUrl).trimmed();

    config.maxHistoryTurns = obj.value(QStringLiteral("maxHistoryTurns")).toInt(config.maxHistoryTurns);
    config.maxLocalHits = obj.value(QStringLiteral("maxLocalHits")).toInt(config.maxLocalHits);
    config.maxExternalHits = obj.value(QStringLiteral("maxExternalHits")).toInt(config.maxExternalHits);
    config.maxRelevantMemories = obj.value(QStringLiteral("maxRelevantMemories")).toInt(config.maxRelevantMemories);

    config.externalSearchTimeoutMs = readTimeoutValue(obj, QStringLiteral("externalSearchTimeoutMs"), config.externalSearchTimeoutMs, 2000);
    config.ollamaEmbeddingTimeoutMs = readTimeoutValue(obj, QStringLiteral("ollamaEmbeddingTimeoutMs"), config.ollamaEmbeddingTimeoutMs, 3000);
    config.ollamaProbeTimeoutMs = readTimeoutValue(obj, QStringLiteral("ollamaProbeTimeoutMs"), config.ollamaProbeTimeoutMs, 2000);
    config.ollamaResponseHeadersTimeoutMs = readTimeoutValue(obj, QStringLiteral("ollamaResponseHeadersTimeoutMs"), config.ollamaResponseHeadersTimeoutMs, 5000);
    config.ollamaFirstTokenTimeoutMs = readTimeoutValue(obj, QStringLiteral("ollamaFirstTokenTimeoutMs"), config.ollamaFirstTokenTimeoutMs, 5000);
    config.ollamaInactivityTimeoutMs = readTimeoutValue(obj, QStringLiteral("ollamaInactivityTimeoutMs"), config.ollamaInactivityTimeoutMs, 5000);
    config.ollamaTotalTimeoutMs = readTimeoutValue(obj, QStringLiteral("ollamaTotalTimeoutMs"), config.ollamaTotalTimeoutMs, 5000, true);
    config.desktopNotificationTimeoutMs = readTimeoutValue(obj, QStringLiteral("desktopNotificationTimeoutMs"), config.desktopNotificationTimeoutMs, 1000);

    config.maxDiagnosticLines = obj.value(QStringLiteral("maxDiagnosticLines")).toInt(config.maxDiagnosticLines);
    config.ollamaEmbeddingBatchSize = qMax(1, obj.value(QStringLiteral("ollamaEmbeddingBatchSize")).toInt(config.ollamaEmbeddingBatchSize));
    config.ollamaNumCtx = qMax(1024, obj.value(QStringLiteral("ollamaNumCtx")).toInt(config.ollamaNumCtx));
    config.ollamaTopK = qMax(1, obj.value(QStringLiteral("ollamaTopK")).toInt(config.ollamaTopK));
    config.ollamaTemperature = readDoubleValue(obj, QStringLiteral("ollamaTemperature"), config.ollamaTemperature, 0.0);
    config.ollamaTopP = readDoubleValue(obj, QStringLiteral("ollamaTopP"), config.ollamaTopP, 0.0);
    config.ollamaRepeatPenalty = readDoubleValue(obj, QStringLiteral("ollamaRepeatPenalty"), config.ollamaRepeatPenalty, 0.0);
    config.ollamaPresencePenalty = readDoubleValue(obj, QStringLiteral("ollamaPresencePenalty"), config.ollamaPresencePenalty, 0.0);
    config.ollamaFrequencyPenalty = readDoubleValue(obj, QStringLiteral("ollamaFrequencyPenalty"), config.ollamaFrequencyPenalty, 0.0);
    config.ragConfidenceThreshold = readDoubleValue(obj, QStringLiteral("ragConfidenceThreshold"), config.ragConfidenceThreshold, 0.0);

    const bool hasLegacyRequestTimeout = obj.contains(QStringLiteral("requestTimeoutMs"));
    const int legacyRequestTimeoutMs = hasLegacyRequestTimeout
        ? boundedTimeout(obj.value(QStringLiteral("requestTimeoutMs")).toInt(180000), 180000, 5000)
        : 0;

    if (hasLegacyRequestTimeout && !obj.contains(QStringLiteral("ollamaFirstTokenTimeoutMs"))) {
        config.ollamaFirstTokenTimeoutMs = legacyRequestTimeoutMs;
    }
    if (hasLegacyRequestTimeout && !obj.contains(QStringLiteral("ollamaInactivityTimeoutMs"))) {
        config.ollamaInactivityTimeoutMs = legacyRequestTimeoutMs;
    }
    if (hasLegacyRequestTimeout && !obj.contains(QStringLiteral("ollamaResponseHeadersTimeoutMs"))) {
        config.ollamaResponseHeadersTimeoutMs = qMin(legacyRequestTimeoutMs, 180000);
    }
    if (hasLegacyRequestTimeout && !obj.contains(QStringLiteral("ollamaProbeTimeoutMs"))) {
        config.ollamaProbeTimeoutMs = qMin(legacyRequestTimeoutMs, 15000);
    }
    if (hasLegacyRequestTimeout && !obj.contains(QStringLiteral("externalSearchTimeoutMs"))) {
        config.externalSearchTimeoutMs = qMin(legacyRequestTimeoutMs, 20000);
    }

    if (obj.contains(QStringLiteral("ollamaConnectTimeoutMs"))
        && !obj.contains(QStringLiteral("ollamaResponseHeadersTimeoutMs"))) {
        config.ollamaResponseHeadersTimeoutMs = boundedTimeout(
            obj.value(QStringLiteral("ollamaConnectTimeoutMs")).toInt(15000),
            config.ollamaResponseHeadersTimeoutMs,
            5000);
    }

    const QJsonArray stopArray = obj.value(QStringLiteral("ollamaStopSequences")).toArray();
    QStringList stops;
    for (const QJsonValue &value : stopArray) {
        const QString stop = value.toString().trimmed();
        if (!stop.isEmpty()) {
            stops << stop;
        }
    }
    if (!stops.isEmpty()) {
        config.ollamaStopSequences = stops;
    }

    const QJsonArray allowlist = obj.value(QStringLiteral("externalSearchDomainAllowlist")).toArray();
    QStringList domains;
    domains.reserve(allowlist.size());
    for (const QJsonValue &value : allowlist) {
        const QString domain = value.toString().trimmed();
        if (!domain.isEmpty()) {
            domains.push_back(domain);
        }
    }
    config.externalSearchDomainAllowlist = domains;

    applyEnvOverrides(config);

    if (errorMessage != nullptr) {
        *errorMessage = notices.join(QStringLiteral(" "));
    }

    return config;
}
