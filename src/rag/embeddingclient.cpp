#include "rag/embeddingclient.h"

#include <QDateTime>
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QtMath>

namespace {
constexpr int kFallbackEmbeddingDims = 192;
constexpr int kTemporaryDisableAfterFailures = 2;
constexpr qint64 kTemporaryDisableWindowMs = 5LL * 60LL * 1000LL;
constexpr int kNeuralBatchCharBudget = 1800;
constexpr int kNeuralBatchTokenBudget = 520;
constexpr int kSafeEmbeddingInputChars = 1200;
constexpr int kSafeEmbeddingInputTokenBudget = 320;
constexpr int kStrictEmbeddingInputChars = 760;
constexpr int kStrictEmbeddingInputTokenBudget = 200;
constexpr int kErrorPreviewChars = 360;

QStringList normalizeTokens(const QString &input)
{
    QString text = input.toLower();
    text.replace(QRegularExpression(QStringLiteral("[^a-z0-9.+_-]+")), QStringLiteral(" "));
    const QStringList raw = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    static const QHash<QString, QStringList> synonyms = {
        {QStringLiteral("deploy"), {QStringLiteral("deployment"), QStringLiteral("install"), QStringLiteral("bootstrap")}},
        {QStringLiteral("deployment"), {QStringLiteral("deploy"), QStringLiteral("install"), QStringLiteral("bringup")}},
        {QStringLiteral("runbook"), {QStringLiteral("mop"), QStringLiteral("procedure"), QStringLiteral("playbook")}},
        {QStringLiteral("mop"), {QStringLiteral("runbook"), QStringLiteral("procedure")}},
        {QStringLiteral("ciq"), {QStringLiteral("spreadsheet"), QStringLiteral("inputs")}},
        {QStringLiteral("controller"), {QStringLiteral("controllers"), QStringLiteral("control")}},
        {QStringLiteral("worker"), {QStringLiteral("workers"), QStringLiteral("compute")}},
        {QStringLiteral("idrac"), {QStringLiteral("bmc"), QStringLiteral("virtualmedia")}},
        {QStringLiteral("vpn"), {QStringLiteral("globalprotect")}},
        {QStringLiteral("hld"), {QStringLiteral("topology"), QStringLiteral("architecture")}},
        {QStringLiteral("lld"), {QStringLiteral("implementation"), QStringLiteral("design")}},
        {QStringLiteral("alarm"), {QStringLiteral("error"), QStringLiteral("fault")}},
        {QStringLiteral("failed"), {QStringLiteral("failure"), QStringLiteral("error")}},
        {QStringLiteral("harbor"), {QStringLiteral("registry")}},
        {QStringLiteral("k8s"), {QStringLiteral("kubernetes")}}
    };

    QStringList tokens;
    tokens.reserve(raw.size() * 3);
    for (int i = 0; i < raw.size(); ++i) {
        const QString &token = raw.at(i);
        if (token.size() < 2) {
            continue;
        }
        tokens << token;
        if (synonyms.contains(token)) {
            tokens << synonyms.value(token);
        }
        if (token.size() >= 5) {
            for (int j = 0; j + 3 <= token.size(); ++j) {
                tokens << token.mid(j, 3);
            }
        }
        if (i + 1 < raw.size()) {
            const QString &next = raw.at(i + 1);
            if (next.size() >= 2) {
                tokens << token + QLatin1Char('_') + next;
            }
        }
    }
    return tokens;
}

int bucketForToken(const QString &token)
{
    return static_cast<int>(qHash(token) % kFallbackEmbeddingDims);
}

float signedWeight(const QString &token)
{
    return (qHash(token, 0x9e3779b9U) & 0x1U) == 0U ? 1.0f : -1.0f;
}

QVector<float> embedTextWithHashFallback(const QString &text)
{
    QVector<float> vector(kFallbackEmbeddingDims, 0.0f);
    if (text.trimmed().isEmpty()) {
        return vector;
    }

    const QStringList tokens = normalizeTokens(text);
    if (tokens.isEmpty()) {
        return vector;
    }

    QHash<QString, int> frequencies;
    for (const QString &token : tokens) {
        frequencies[token] += 1;
    }

    float normSquared = 0.0f;
    for (auto it = frequencies.cbegin(); it != frequencies.cend(); ++it) {
        const float weight = 1.0f + qLn(1.0f + static_cast<float>(it.value()));
        const int bucket = bucketForToken(it.key());
        const float signedContribution = weight * signedWeight(it.key());
        vector[bucket] += signedContribution;
    }

    for (const float value : vector) {
        normSquared += value * value;
    }

    if (normSquared <= 0.0f) {
        return vector;
    }

    const float invNorm = 1.0f / qSqrt(normSquared);
    for (float &value : vector) {
        value *= invNorm;
    }
    return vector;
}

bool isProbablyPermanentOllamaFailure(const QString &message)
{
    const QString lower = message.toLower();
    return lower.contains(QStringLiteral("not found"))
            || lower.contains(QStringLiteral("unknown model"))
            || lower.contains(QStringLiteral("no such model"))
            || lower.contains(QStringLiteral("does not support"))
            || lower.contains(QStringLiteral("invalid model"));
}

bool isContextLengthFailure(const QString &message)
{
    const QString lower = message.toLower();
    return lower.contains(QStringLiteral("context length"))
            || lower.contains(QStringLiteral("input length exceeds"))
            || lower.contains(QStringLiteral("prompt is too long"));
}

QString normalizedBaseUrl(QString url)
{
    url = url.trimmed();
    while (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    return url;
}

QString simplifiedError(const QString &message)
{
    QString normalized = message.simplified();
    if (normalized.size() > 180) {
        normalized = normalized.left(177) + QStringLiteral("...");
    }
    return normalized;
}

bool isCancelRequested(const std::atomic_bool *cancelRequested)
{
    return cancelRequested != nullptr && cancelRequested->load(std::memory_order_relaxed);
}

QString sanitizeForEmbedding(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QChar(0x00A0), QLatin1Char(' '));
    text.replace(QRegularExpression(QStringLiteral("[\x00-\x08\x0B\x0C\x0E-\x1F]")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("[ \t]{2,}")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\n{4,}")), QStringLiteral("\n\n\n"));
    return text.trimmed();
}

int estimateEmbeddingTokens(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return 0;
    }

    int tokenUnits = 0;
    bool inAlnum = false;
    static const QString denseTokenChars = QStringLiteral("{}[]()<>:=/\\|@#%&*_+-;,.!?\"");

    for (const QChar ch : text) {
        if (ch.isLetterOrNumber()) {
            tokenUnits += 1;
            inAlnum = true;
            continue;
        }

        if (inAlnum) {
            tokenUnits += 1;
            inAlnum = false;
        }

        if (ch == QLatin1Char('\n')) {
            tokenUnits += 3;
        } else if (ch.isSpace()) {
            tokenUnits += 1;
        } else if (denseTokenChars.contains(ch)) {
            tokenUnits += 4;
        } else {
            tokenUnits += 2;
        }
    }

    if (inAlnum) {
        tokenUnits += 1;
    }

    return qMax(1, static_cast<int>(qCeil(static_cast<double>(tokenUnits) / 4.0)));
}

int chooseEmbeddingSplit(const QString &text, int preferredMaxChars)
{
    const int hardEnd = qMin(text.size(), preferredMaxChars);
    const int minimumSplit = qMax(180, hardEnd / 2);

    int split = text.lastIndexOf(QStringLiteral("\n\n"), hardEnd);
    if (split < minimumSplit) {
        split = text.lastIndexOf(QLatin1Char('\n'), hardEnd);
    }
    if (split < minimumSplit) {
        split = text.lastIndexOf(QRegularExpression(QStringLiteral("[.!?]\\s")), hardEnd);
        if (split >= minimumSplit) {
            split += 1;
        }
    }
    if (split < minimumSplit) {
        split = text.lastIndexOf(QLatin1Char(' '), hardEnd);
    }
    if (split < minimumSplit) {
        split = hardEnd;
    }
    return qMax(minimumSplit, split);
}

QString clipForEmbedding(const QString &text, bool *wasClipped)
{
    QString normalized = sanitizeForEmbedding(text);
    bool clipped = false;

    while (!normalized.isEmpty()
           && (normalized.size() > kSafeEmbeddingInputChars
               || estimateEmbeddingTokens(normalized) > kSafeEmbeddingInputTokenBudget)) {
        const int preferredMaxChars = qMin(normalized.size(), kSafeEmbeddingInputChars);
        const int split = chooseEmbeddingSplit(normalized, preferredMaxChars);
        QString next = normalized.left(split).trimmed();
        if (next == normalized) {
            const int fallbackChars = qMax(180, normalized.size() - qMax(80, normalized.size() / 6));
            next = normalized.left(fallbackChars).trimmed();
        }
        if (next.isEmpty() || next == normalized) {
            break;
        }
        normalized = next;
        clipped = true;
    }

    if (wasClipped != nullptr) {
        *wasClipped = clipped;
    }
    return normalized;
}

QString previewText(QString text, int maxChars = kErrorPreviewChars)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text = text.simplified();
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(qMax(120, maxChars)).trimmed() + QStringLiteral("...");
}

QString extractJsonErrorMessage(const QByteArray &raw)
{
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) {
        return QString();
    }

    const QJsonObject root = doc.object();
    const QString rootError = root.value(QStringLiteral("error")).toString().trimmed();
    if (!rootError.isEmpty()) {
        return rootError;
    }

    const QJsonObject messageObj = root.value(QStringLiteral("message")).toObject();
    const QString messageError = messageObj.value(QStringLiteral("error")).toString().trimmed();
    if (!messageError.isEmpty()) {
        return messageError;
    }

    return QString();
}

EmbeddingClient::NeuralAttemptResult parseModernEmbeddingResponse(const QByteArray &raw)
{
    EmbeddingClient::NeuralAttemptResult result;
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) {
        result.errorMessage = QStringLiteral("invalid JSON response from Ollama embedding endpoint");
        return result;
    }

    const QJsonObject root = doc.object();
    const QString logicalError = root.value(QStringLiteral("error")).toString().trimmed();
    if (!logicalError.isEmpty()) {
        result.errorMessage = logicalError;
        return result;
    }

    const QJsonArray embeddingsArray = root.value(QStringLiteral("embeddings")).toArray();
    if (embeddingsArray.isEmpty()) {
        result.errorMessage = QStringLiteral("embedding response did not contain an embeddings array");
        return result;
    }

    result.embeddings.reserve(embeddingsArray.size());
    for (const QJsonValue &embeddingValue : embeddingsArray) {
        const QJsonArray embeddingArray = embeddingValue.toArray();
        QVector<float> embedding;
        embedding.reserve(embeddingArray.size());
        for (const QJsonValue &value : embeddingArray) {
            embedding.push_back(static_cast<float>(value.toDouble()));
        }
        if (embedding.isEmpty()) {
            result.embeddings.clear();
            result.errorMessage = QStringLiteral("embedding response contained an empty vector");
            return result;
        }
        result.embeddings.push_back(embedding);
    }

    result.usedNeuralBackend = !result.embeddings.isEmpty();
    return result;
}

EmbeddingClient::NeuralAttemptResult parseLegacyEmbeddingResponse(const QByteArray &raw)
{
    EmbeddingClient::NeuralAttemptResult result;
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) {
        result.errorMessage = QStringLiteral("invalid JSON response from legacy Ollama embeddings endpoint");
        return result;
    }

    const QJsonObject root = doc.object();
    const QString logicalError = root.value(QStringLiteral("error")).toString().trimmed();
    if (!logicalError.isEmpty()) {
        result.errorMessage = logicalError;
        return result;
    }

    const QJsonArray embeddingArray = root.value(QStringLiteral("embedding")).toArray();
    if (embeddingArray.isEmpty()) {
        result.errorMessage = QStringLiteral("legacy embedding response did not contain an embedding vector");
        return result;
    }

    QVector<float> embedding;
    embedding.reserve(embeddingArray.size());
    for (const QJsonValue &value : embeddingArray) {
        embedding.push_back(static_cast<float>(value.toDouble()));
    }
    if (embedding.isEmpty()) {
        result.errorMessage = QStringLiteral("legacy embedding response contained an empty vector");
        return result;
    }

    result.embeddings.push_back(embedding);
    result.usedNeuralBackend = true;
    return result;
}
}

EmbeddingClient::EmbeddingClient()
    : m_ollamaBaseUrl(QStringLiteral("http://localhost:11434"))
    , m_embeddingModel(QStringLiteral("embeddinggemma:latest"))
{
}

void EmbeddingClient::configureOllama(const QString &baseUrl,
                                      const QString &model,
                                      int timeoutMs,
                                      int batchSize)
{
    m_ollamaBaseUrl = normalizedBaseUrl(baseUrl);
    m_embeddingModel = model.trimmed();
    m_timeoutMs = qMax(3000, timeoutMs);
    m_batchSize = qMax(1, batchSize);
    m_lastRequestUsedNeural = false;
    m_consecutiveNeuralFailures = 0;
    m_disableNeuralUntilMs = 0;
    m_lastSuccessfulEndpoint = QStringLiteral("/api/embed");
    m_lastErrorSummary.clear();
}

void EmbeddingClient::setDiagnosticCallback(const std::function<void(const QString &, const QString &)> &callback)
{
    m_diagnosticCallback = callback;
}

QString EmbeddingClient::backendName() const
{
    const QString configuredModel = m_embeddingModel.trimmed().isEmpty() ? QStringLiteral("<unset>") : m_embeddingModel.trimmed();
    QString configured = QStringLiteral("ollama-embed(%1").arg(configuredModel);
    if (!m_lastSuccessfulEndpoint.trimmed().isEmpty()) {
        configured += QStringLiteral(" via %1").arg(m_lastSuccessfulEndpoint);
    }
    configured += QLatin1Char(')');

    if (m_lastRequestUsedNeural) {
        return configured;
    }

    QString suffix;
    if (m_disableNeuralUntilMs > QDateTime::currentMSecsSinceEpoch()) {
        suffix = QStringLiteral(" -> local-hash-fallback-v1 (temporary fallback)");
    } else {
        suffix = QStringLiteral(" -> local-hash-fallback-v1");
    }

    if (!m_lastErrorSummary.trimmed().isEmpty()) {
        suffix += QStringLiteral(" [last error: %1]").arg(simplifiedError(m_lastErrorSummary));
    }

    return configured + suffix;
}

QString EmbeddingClient::cacheKey() const
{
    return QStringLiteral("ollama:%1|hash-fallback-v1").arg(
        m_embeddingModel.trimmed().isEmpty() ? QStringLiteral("embeddinggemma:latest") : m_embeddingModel.trimmed());
}

bool EmbeddingClient::isConfigured() const
{
    return !m_ollamaBaseUrl.trimmed().isEmpty() && !m_embeddingModel.trimmed().isEmpty();
}

bool EmbeddingClient::lastRequestUsedNeural() const
{
    return m_lastRequestUsedNeural;
}

int EmbeddingClient::localFallbackDimensions() const
{
    return kFallbackEmbeddingDims;
}

QVector<float> EmbeddingClient::embedText(const QString &text) const
{
    const QVector<QVector<float>> embeddings = embedTexts({text});
    return embeddings.isEmpty() ? QVector<float>() : embeddings.constFirst();
}

QVector<float> EmbeddingClient::embedTextLocalFallback(const QString &text) const
{
    return embedTextWithHashFallback(text);
}

void EmbeddingClient::logDiagnostic(const QString &category, const QString &message) const
{
    if (m_diagnosticCallback) {
        m_diagnosticCallback(category, message);
    }
}

EmbeddingClient::NeuralAttemptResult EmbeddingClient::tryOllamaEmbeddingsViaEndpoint(const QStringList &texts,
                                                                                      const QString &endpointPath,
                                                                                      const std::atomic_bool *cancelRequested) const
{
    NeuralAttemptResult result;
    result.endpointName = endpointPath;

    if (texts.isEmpty() || !isConfigured()) {
        result.errorMessage = QStringLiteral("embedding client is not configured");
        return result;
    }

    QNetworkAccessManager manager;
    const QUrl requestUrl(normalizedBaseUrl(m_ollamaBaseUrl) + endpointPath);
    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    auto executeRequest = [&](const QJsonObject &payload) -> QByteArray {
        if (isCancelRequested(cancelRequested)) {
            result.errorMessage = QStringLiteral("embedding request canceled before dispatch");
            return {};
        }

        const QByteArray requestBody = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        int estimatedTotalTokens = 0;
        int longestInputChars = 0;
        for (const QString &item : texts) {
            estimatedTotalTokens += estimateEmbeddingTokens(item);
            longestInputChars = qMax(longestInputChars, item.size());
        }
        logDiagnostic(QStringLiteral("rag"),
                      QStringLiteral("Ollama embedding request %1 | model=%2 | inputs=%3 | est_tokens=%4 | longest_input_chars=%5 | body_bytes=%6 | timeout_ms=%7")
                          .arg(requestUrl.toString(),
                               m_embeddingModel,
                               QString::number(texts.size()),
                               QString::number(estimatedTotalTokens),
                               QString::number(longestInputChars),
                               QString::number(requestBody.size()),
                               QString::number(m_timeoutMs)));

        QEventLoop loop;
        QTimer timer;
        QTimer cancelPoll;
        timer.setSingleShot(true);
        cancelPoll.setInterval(50);

        QNetworkReply *reply = manager.post(request, requestBody);
        bool canceled = false;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
            if (reply->isRunning()) {
                reply->abort();
            }
        });
        QObject::connect(&cancelPoll, &QTimer::timeout, &loop, [&]() {
            if (!isCancelRequested(cancelRequested)) {
                return;
            }
            canceled = true;
            if (reply->isRunning()) {
                reply->abort();
            }
            loop.quit();
        });
        timer.start(m_timeoutMs);
        cancelPoll.start();
        loop.exec();
        cancelPoll.stop();
        timer.stop();

        if (canceled) {
            result.errorMessage = QStringLiteral("embedding request canceled");
            reply->deleteLater();
            return {};
        }

        const QByteArray raw = reply->readAll();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QString transportError = reply->errorString();
        const QString logicalError = extractJsonErrorMessage(raw);
        const QString preview = previewText(QString::fromUtf8(raw));
        reply->deleteLater();

        logDiagnostic(QStringLiteral("rag"),
                      QStringLiteral("Ollama embedding response %1 | http=%2 | bytes=%3 | preview=%4")
                          .arg(requestUrl.toString(),
                               QString::number(httpStatus),
                               QString::number(raw.size()),
                               preview.isEmpty() ? QStringLiteral("<empty>") : preview));

        if (!ok) {
            result.errorMessage = transportError + QStringLiteral(" | ") + QString::fromUtf8(raw);
            if (endpointPath == QStringLiteral("/api/embed") && httpStatus == 404) {
                result.shouldRetryLegacyEndpoint = true;
            }
            return {};
        }

        if (!logicalError.isEmpty()) {
            result.errorMessage = logicalError;
            return {};
        }

        result.errorMessage.clear();
        return raw;
    };

    if (endpointPath == QStringLiteral("/api/embed")) {
        QJsonArray inputArray;
        for (const QString &text : texts) {
            inputArray.push_back(text);
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("model"), m_embeddingModel);
        payload.insert(QStringLiteral("input"), inputArray.size() == 1 ? inputArray.at(0) : QJsonValue(inputArray));
        payload.insert(QStringLiteral("truncate"), true);

        const QByteArray raw = executeRequest(payload);
        if (raw.isEmpty() && !result.errorMessage.isEmpty()) {
            return result;
        }

        const NeuralAttemptResult parsed = parseModernEmbeddingResponse(raw);
        result.embeddings = parsed.embeddings;
        result.usedNeuralBackend = parsed.usedNeuralBackend;
        if (!parsed.usedNeuralBackend) {
            result.errorMessage = parsed.errorMessage;
        }
        return result;
    }

    if (endpointPath == QStringLiteral("/api/embeddings")) {
        result.embeddings.reserve(texts.size());
        for (const QString &text : texts) {
            QJsonObject payload;
            payload.insert(QStringLiteral("model"), m_embeddingModel);
            payload.insert(QStringLiteral("prompt"), text);

            const QByteArray raw = executeRequest(payload);
            if (raw.isEmpty() && !result.errorMessage.isEmpty()) {
                result.embeddings.clear();
                result.usedNeuralBackend = false;
                return result;
            }

            const NeuralAttemptResult parsed = parseLegacyEmbeddingResponse(raw);
            if (!parsed.usedNeuralBackend || parsed.embeddings.isEmpty()) {
                result.embeddings.clear();
                result.usedNeuralBackend = false;
                result.errorMessage = parsed.errorMessage;
                return result;
            }
            result.embeddings.push_back(parsed.embeddings.constFirst());
        }

        result.usedNeuralBackend = result.embeddings.size() == texts.size();
        if (!result.usedNeuralBackend && result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("legacy embedding endpoint did not return the expected number of vectors");
        }
        return result;
    }

    result.errorMessage = QStringLiteral("unsupported embedding endpoint: %1").arg(endpointPath);
    return result;
}

EmbeddingClient::NeuralAttemptResult EmbeddingClient::tryOllamaEmbeddings(const QStringList &texts,
                                                                           const std::atomic_bool *cancelRequested) const
{
    NeuralAttemptResult result;
    if (texts.isEmpty() || !isConfigured()) {
        result.errorMessage = QStringLiteral("embedding client is not configured");
        return result;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_disableNeuralUntilMs > now) {
        result.errorMessage = QStringLiteral("neural embeddings are temporarily disabled after repeated failures");
        logDiagnostic(QStringLiteral("rag"), result.errorMessage);
        return result;
    }

    result = tryOllamaEmbeddingsViaEndpoint(texts, QStringLiteral("/api/embed"), cancelRequested);
    if (result.usedNeuralBackend) {
        m_lastSuccessfulEndpoint = result.endpointName;
        m_lastErrorSummary.clear();
        m_consecutiveNeuralFailures = 0;
        m_disableNeuralUntilMs = 0;
        return result;
    }

    if (result.shouldRetryLegacyEndpoint) {
        logDiagnostic(QStringLiteral("rag"), QStringLiteral("Modern Ollama embedding endpoint returned 404; retrying legacy /api/embeddings."));
        const NeuralAttemptResult legacy = tryOllamaEmbeddingsViaEndpoint(texts, QStringLiteral("/api/embeddings"), cancelRequested);
        if (legacy.usedNeuralBackend) {
            m_lastSuccessfulEndpoint = legacy.endpointName;
            m_lastErrorSummary.clear();
            m_consecutiveNeuralFailures = 0;
            m_disableNeuralUntilMs = 0;
            return legacy;
        }
        result = legacy;
    }

    ++m_consecutiveNeuralFailures;
    m_lastErrorSummary = result.errorMessage;
    logDiagnostic(QStringLiteral("rag"), QStringLiteral("Ollama embedding failure: %1").arg(simplifiedError(result.errorMessage)));
    if (isProbablyPermanentOllamaFailure(result.errorMessage)
            || m_consecutiveNeuralFailures >= kTemporaryDisableAfterFailures) {
        m_disableNeuralUntilMs = now + kTemporaryDisableWindowMs;
        logDiagnostic(QStringLiteral("rag"),
                      QStringLiteral("Neural embeddings temporarily disabled for %1 ms after repeated failures.")
                          .arg(QString::number(kTemporaryDisableWindowMs)));
    }
    return result;
}

QVector<QVector<float>> EmbeddingClient::embedTexts(const QStringList &texts) const
{
    return embedTexts(texts, {}, nullptr);
}

QVector<QVector<float>> EmbeddingClient::embedTexts(const QStringList &texts,
                                                    const std::function<void(int, int)> &progressCallback) const
{
    return embedTexts(texts, progressCallback, nullptr);
}

QVector<QVector<float>> EmbeddingClient::embedTexts(const QStringList &texts,
                                                    const std::function<void(int, int)> &progressCallback,
                                                    const std::atomic_bool *cancelRequested) const
{
    QVector<QVector<float>> embeddings;
    embeddings.reserve(texts.size());
    if (texts.isEmpty()) {
        m_lastRequestUsedNeural = false;
        if (progressCallback) {
            progressCallback(0, 0);
        }
        return embeddings;
    }

    QStringList preparedTexts;
    preparedTexts.reserve(texts.size());
    QVector<int> preparedTokenEstimates;
    preparedTokenEstimates.reserve(texts.size());
    int clippedCount = 0;
    for (const QString &text : texts) {
        bool clipped = false;
        const QString prepared = clipForEmbedding(text, &clipped);
        if (clipped) {
            ++clippedCount;
        }
        preparedTexts.push_back(prepared);
        preparedTokenEstimates.push_back(estimateEmbeddingTokens(prepared));
    }
    if (clippedCount > 0) {
        logDiagnostic(QStringLiteral("rag"),
                      QStringLiteral("Embedding hard cap applied to %1/%2 input(s); each input is clipped to <= %3 chars and ~= %4 tokens before calling Ollama.")
                          .arg(clippedCount)
                          .arg(texts.size())
                          .arg(kSafeEmbeddingInputChars)
                          .arg(kSafeEmbeddingInputTokenBudget));
    }

    if (isConfigured()) {
        const int batchSizeLimit = qMax(1, m_batchSize);
        QVector<QStringList> batches;
        batches.reserve((preparedTexts.size() / batchSizeLimit) + 2);

        int offset = 0;
        while (offset < preparedTexts.size()) {
            if (isCancelRequested(cancelRequested)) {
                m_lastRequestUsedNeural = false;
                return {};
            }
            QStringList batch;
            int batchChars = 0;
            int batchTokens = 0;
            while (offset < preparedTexts.size() && batch.size() < batchSizeLimit) {
                const QString candidate = preparedTexts.at(offset);
                const int candidateChars = candidate.size();
                const int candidateTokens = preparedTokenEstimates.at(offset);
                const int projectedChars = batchChars + (batch.isEmpty() ? 0 : 1) + candidateChars;
                const int projectedTokens = batchTokens + (batch.isEmpty() ? 0 : 4) + candidateTokens;
                if (!batch.isEmpty() && (projectedChars > kNeuralBatchCharBudget || projectedTokens > kNeuralBatchTokenBudget)) {
                    break;
                }
                batch.push_back(candidate);
                batchChars = projectedChars;
                batchTokens = projectedTokens;
                ++offset;
            }
            if (batch.isEmpty()) {
                batch.push_back(preparedTexts.at(offset));
                ++offset;
            }
            batches.push_back(batch);
        }

        bool allBatchesSucceeded = true;
        QVector<QVector<float>> neuralEmbeddings;
        neuralEmbeddings.reserve(preparedTexts.size());
        int completed = 0;
        for (QStringList batch : batches) {
            if (isCancelRequested(cancelRequested)) {
                neuralEmbeddings.clear();
                m_lastRequestUsedNeural = false;
                return {};
            }

            NeuralAttemptResult attempt = tryOllamaEmbeddings(batch, cancelRequested);
            if (!attempt.usedNeuralBackend && isContextLengthFailure(attempt.errorMessage)) {
                logDiagnostic(QStringLiteral("rag"),
                              QStringLiteral("Ollama reported context-length overflow during batched embeddings; retrying each input individually with stricter token-aware clipping."));
                QVector<QVector<float>> recoveredEmbeddings;
                recoveredEmbeddings.reserve(batch.size());
                bool recovered = true;
                for (const QString &item : batch) {
                    if (isCancelRequested(cancelRequested)) {
                        m_lastRequestUsedNeural = false;
                        return {};
                    }

                    NeuralAttemptResult singleAttempt = tryOllamaEmbeddings({item}, cancelRequested);
                    if (!singleAttempt.usedNeuralBackend && isContextLengthFailure(singleAttempt.errorMessage)) {
                        QString strictItem = sanitizeForEmbedding(item);
                        while (!strictItem.isEmpty()
                               && (strictItem.size() > kStrictEmbeddingInputChars
                                   || estimateEmbeddingTokens(strictItem) > kStrictEmbeddingInputTokenBudget)) {
                            const int split = chooseEmbeddingSplit(strictItem, qMin(strictItem.size(), kStrictEmbeddingInputChars));
                            const QString next = strictItem.left(split).trimmed();
                            if (next.isEmpty() || next == strictItem) {
                                strictItem = strictItem.left(qMax(160, kStrictEmbeddingInputChars / 2)).trimmed();
                                break;
                            }
                            strictItem = next;
                        }
                        logDiagnostic(QStringLiteral("rag"),
                                      QStringLiteral("Retrying a single embedding input with a stricter cap | chars=%1 | est_tokens=%2")
                                          .arg(strictItem.size())
                                          .arg(estimateEmbeddingTokens(strictItem)));
                        singleAttempt = tryOllamaEmbeddings({strictItem}, cancelRequested);
                    }

                    if (!singleAttempt.usedNeuralBackend || singleAttempt.embeddings.size() != 1) {
                        recovered = false;
                        attempt = singleAttempt;
                        break;
                    }
                    recoveredEmbeddings.push_back(singleAttempt.embeddings.constFirst());
                }

                if (recovered) {
                    attempt.embeddings = recoveredEmbeddings;
                    attempt.usedNeuralBackend = recoveredEmbeddings.size() == batch.size();
                    attempt.errorMessage.clear();
                }
            }

            if (!attempt.usedNeuralBackend || attempt.embeddings.size() != batch.size()) {
                allBatchesSucceeded = false;
                neuralEmbeddings.clear();
                break;
            }
            for (const QVector<float> &embedding : attempt.embeddings) {
                neuralEmbeddings.push_back(embedding);
            }
            completed += batch.size();
            if (isCancelRequested(cancelRequested)) {
                m_lastRequestUsedNeural = false;
                return {};
            }
            if (progressCallback) {
                progressCallback(completed, texts.size());
            }
        }
        if (allBatchesSucceeded && neuralEmbeddings.size() == texts.size()) {
            m_lastRequestUsedNeural = true;
            return neuralEmbeddings;
        }
    }

    int completed = 0;
    for (const QString &text : texts) {
        if (isCancelRequested(cancelRequested)) {
            m_lastRequestUsedNeural = false;
            return {};
        }
        embeddings.push_back(embedTextWithHashFallback(text));
        ++completed;
        if (progressCallback) {
            progressCallback(completed, texts.size());
        }
    }
    m_lastRequestUsedNeural = false;
    return embeddings;
}

float EmbeddingClient::cosineSimilarity(const QVector<float> &a, const QVector<float> &b)
{
    if (a.isEmpty() || b.isEmpty() || a.size() != b.size()) {
        return 0.0f;
    }

    float dot = 0.0f;
    float normA = 0.0f;
    float normB = 0.0f;
    for (int i = 0; i < a.size(); ++i) {
        dot += a.at(i) * b.at(i);
        normA += a.at(i) * a.at(i);
        normB += b.at(i) * b.at(i);
    }

    if (normA <= 0.0f || normB <= 0.0f) {
        return 0.0f;
    }
    return dot / (qSqrt(normA) * qSqrt(normB));
}
