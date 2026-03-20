#include "backend/ollamaclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace {
constexpr auto kAmeliaThinkingOpenTag = "<amelia_thinking>";
constexpr auto kAmeliaThinkingCloseTag = "</amelia_thinking>";

int trailingPrefixOverlap(const QString &text, const QString &tag)
{
    const int maxLen = qMin(text.size(), tag.size() - 1);
    for (int len = maxLen; len > 0; --len) {
        if (text.right(len) == tag.left(len)) {
            return len;
        }
    }
    return 0;
}
}

OllamaClient::OllamaClient(QObject *parent)
    : LlmClient(parent)
{
    m_phaseTimer.setSingleShot(true);
    m_totalTimer.setSingleShot(true);

    connect(&m_phaseTimer, &QTimer::timeout, this, &OllamaClient::onPhaseTimeout);
    connect(&m_totalTimer, &QTimer::timeout, this, &OllamaClient::onTotalTimeout);
}

void OllamaClient::generate(const QString &baseUrl,
                            const QString &model,
                            const QVector<LlmChatMessage> &messages)
{
    stop();
    resetState();

    m_activeBaseUrl = baseUrl.trimmed();
    m_activeModel = model.trimmed();

    const QUrl url = buildEndpointUrl(baseUrl, QStringLiteral("/api/chat"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), model);
    payload.insert(QStringLiteral("stream"), true);

    QJsonArray jsonMessages;
    for (const LlmChatMessage &message : messages) {
        QJsonObject obj;
        obj.insert(QStringLiteral("role"), message.role);
        obj.insert(QStringLiteral("content"), message.content);
        jsonMessages.push_back(obj);
    }
    payload.insert(QStringLiteral("messages"), jsonMessages);

    QJsonObject options;
    if (m_numCtx > 0) {
        options.insert(QStringLiteral("num_ctx"), m_numCtx);
    }
    options.insert(QStringLiteral("temperature"), m_temperature);
    options.insert(QStringLiteral("top_p"), m_topP);
    options.insert(QStringLiteral("top_k"), m_topK);
    options.insert(QStringLiteral("repeat_penalty"), m_repeatPenalty);
    options.insert(QStringLiteral("presence_penalty"), m_presencePenalty);
    options.insert(QStringLiteral("frequency_penalty"), m_frequencyPenalty);
    if (!m_stopSequences.isEmpty()) {
        QJsonArray stopArray;
        for (const QString &stop : m_stopSequences) {
            stopArray.push_back(stop);
        }
        options.insert(QStringLiteral("stop"), stopArray);
    }
    payload.insert(QStringLiteral("options"), options);

    // When Diagnostics reasoning capture is enabled, ask Ollama for the
    // model's explicit thinking stream when the backend/model supports it.
    // Otherwise keep thinking suppressed to reduce latency and avoid stray
    // hidden reasoning text bleeding into the visible answer.
    payload.insert(QStringLiteral("think"), m_reasoningTraceEnabled);

    m_reply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    if (m_reply == nullptr) {
        emit responseError(QStringLiteral("Failed to create Ollama network request."));
        return;
    }

    m_streamPhase = StreamPhase::WaitingForResponseHeaders;
    armPhaseTimer(m_responseHeadersTimeoutMs);
    if (m_totalTimeoutMs > 0) {
        m_totalTimer.start(m_totalTimeoutMs);
    }

    m_emittedStarted = true;
    emit responseStarted();

    connect(m_reply, &QNetworkReply::metaDataChanged, this, &OllamaClient::onMetaDataChanged);
    connect(m_reply, &QNetworkReply::readyRead, this, &OllamaClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &OllamaClient::onFinished);
}

void OllamaClient::probe(const QString &baseUrl, const QString &expectedModel)
{
    QNetworkRequest request(buildEndpointUrl(baseUrl, QStringLiteral("/api/tags")));
    request.setTransferTimeout(m_probeTimeoutMs);
    auto *reply = m_network.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, baseUrl, expectedModel]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray body = reply->readAll();

        if (error != QNetworkReply::NoError) {
            const QString message = describeNetworkFailure(reply,
                                                           QStringLiteral("probe Ollama"),
                                                           baseUrl,
                                                           expectedModel);
            reply->deleteLater();
            emit backendProbeFinished(false, message);
            return;
        }

        QStringList models;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            const QJsonArray array = doc.object().value(QStringLiteral("models")).toArray();
            models.reserve(array.size());
            for (const QJsonValue &value : array) {
                const QJsonObject obj = value.toObject();
                const QString name = obj.value(QStringLiteral("name")).toString();
                const QString listedModel = obj.value(QStringLiteral("model")).toString();
                if (!name.isEmpty()) {
                    models.push_back(name);
                } else if (!listedModel.isEmpty()) {
                    models.push_back(listedModel);
                }
            }
        }

        QString message = QStringLiteral("Connected to Ollama at %1.").arg(baseUrl);
        if (models.isEmpty()) {
            message += QStringLiteral(" Server responded, but no local models were listed.");
        } else {
            message += QStringLiteral(" Local models: %1").arg(models.join(QStringLiteral(", ")));
        }

        if (!expectedModel.trimmed().isEmpty() && !models.contains(expectedModel)) {
            message += QStringLiteral(" Configured model '%1' is not currently available.").arg(expectedModel);
        }

        reply->deleteLater();
        emit backendProbeFinished(true, message);
        emit modelsListed(models, message);
    });
}

void OllamaClient::listModels(const QString &baseUrl)
{
    QNetworkRequest request(buildEndpointUrl(baseUrl, QStringLiteral("/api/tags")));
    request.setTransferTimeout(m_probeTimeoutMs);
    auto *reply = m_network.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, baseUrl]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray body = reply->readAll();

        if (error != QNetworkReply::NoError) {
            const QString message = describeNetworkFailure(reply,
                                                           QStringLiteral("list Ollama models"),
                                                           baseUrl);
            reply->deleteLater();
            emit modelsListed(QStringList(), message);
            return;
        }

        QStringList models;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            const QJsonArray array = doc.object().value(QStringLiteral("models")).toArray();
            models.reserve(array.size());
            for (const QJsonValue &value : array) {
                const QJsonObject obj = value.toObject();
                const QString name = obj.value(QStringLiteral("name")).toString();
                const QString listedModel = obj.value(QStringLiteral("model")).toString();
                if (!name.isEmpty()) {
                    models.push_back(name);
                } else if (!listedModel.isEmpty()) {
                    models.push_back(listedModel);
                }
            }
        }

        const QString message = models.isEmpty()
                ? QStringLiteral("Ollama is reachable at %1, but it returned no local models.").arg(baseUrl)
                : QStringLiteral("Available Ollama models: %1").arg(models.join(QStringLiteral(", ")));

        reply->deleteLater();
        emit modelsListed(models, message);
    });
}

void OllamaClient::setProbeTimeoutMs(int timeoutMs)
{
    m_probeTimeoutMs = qMax(2000, timeoutMs);
}

void OllamaClient::setResponseHeadersTimeoutMs(int timeoutMs)
{
    m_responseHeadersTimeoutMs = qMax(5000, timeoutMs);
}

void OllamaClient::setFirstTokenTimeoutMs(int timeoutMs)
{
    m_firstTokenTimeoutMs = qMax(5000, timeoutMs);
}

void OllamaClient::setInactivityTimeoutMs(int timeoutMs)
{
    m_inactivityTimeoutMs = qMax(5000, timeoutMs);
}

void OllamaClient::setTotalTimeoutMs(int timeoutMs)
{
    if (timeoutMs <= 0) {
        m_totalTimeoutMs = 0;
        return;
    }
    m_totalTimeoutMs = qMax(5000, timeoutMs);
}

void OllamaClient::setGenerationConfig(const AppConfig &config)
{
    m_numCtx = qMax(1024, config.ollamaNumCtx);
    m_topK = qMax(1, config.ollamaTopK);
    m_temperature = qMax(0.0, config.ollamaTemperature);
    m_topP = qMax(0.0, config.ollamaTopP);
    m_repeatPenalty = qMax(0.0, config.ollamaRepeatPenalty);
    m_presencePenalty = qMax(0.0, config.ollamaPresencePenalty);
    m_frequencyPenalty = qMax(0.0, config.ollamaFrequencyPenalty);
    m_stopSequences.clear();
    for (const QString &stop : config.ollamaStopSequences) {
        const QString trimmed = stop.trimmed();
        if (!trimmed.isEmpty()) {
            m_stopSequences << trimmed;
        }
    }
}

void OllamaClient::setReasoningTraceEnabled(bool enabled)
{
    m_reasoningTraceEnabled = enabled;
}

void OllamaClient::stop()
{
    stopTimers();
    m_streamPhase = StreamPhase::Idle;

    if (m_reply != nullptr) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void OllamaClient::onReadyRead()
{
    if (m_reply == nullptr) {
        return;
    }

    if (!m_receivedHeaders) {
        beginWaitingForFirstToken();
    }

    m_buffer += m_reply->readAll();
    const bool hadAnyOutput = m_receivedAnyOutput;
    parseBufferedLines(false);
    flushPendingUiDelta(false);
    flushPendingReasoningDelta(false);

    if (!m_receivedFirstToken && !hadAnyOutput && m_receivedAnyOutput) {
        beginStreaming();
    } else if (m_streamPhase == StreamPhase::Streaming) {
        armPhaseTimer(m_inactivityTimeoutMs);
    }
}

void OllamaClient::onFinished()
{
    if (m_reply == nullptr) {
        return;
    }

    stopTimers();

    m_buffer += m_reply->readAll();
    const bool hadAnyOutput = m_receivedAnyOutput;
    parseBufferedLines(true);
    processTaggedOutput(true);
    if (!m_receivedFirstToken && (!hadAnyOutput && m_receivedAnyOutput)) {
        m_receivedFirstToken = true;
    }
    flushPendingUiDelta(true);
    flushPendingReasoningDelta(true);

    const QNetworkReply::NetworkError error = m_reply->error();
    const QString errorText = describeNetworkFailure(m_reply,
                                                     QStringLiteral("generate text with Ollama"),
                                                     m_activeBaseUrl,
                                                     m_activeModel);

    m_reply->deleteLater();
    m_reply = nullptr;
    m_streamPhase = StreamPhase::Idle;

    if (error == QNetworkReply::OperationCanceledError) {
        return;
    }

    if (error != QNetworkReply::NoError) {
        emit responseError(errorText);
        return;
    }

    emit responseFinished(sanitizeVisibleText(m_accumulated).trimmed());
}

void OllamaClient::onMetaDataChanged()
{
    if (m_reply == nullptr) {
        return;
    }

    beginWaitingForFirstToken();
}

void OllamaClient::onPhaseTimeout()
{
    switch (m_streamPhase) {
    case StreamPhase::WaitingForResponseHeaders:
        abortForTimeout(QStringLiteral(
            "Timed out waiting %1 ms for response headers from Ollama at %2 using model '%3'. "
            "Increase ollamaResponseHeadersTimeoutMs if the local backend is slow to load the model or start the response stream.")
                .arg(m_responseHeadersTimeoutMs)
                .arg(m_activeBaseUrl, m_activeModel));
        return;
    case StreamPhase::WaitingForFirstToken:
        abortForTimeout(QStringLiteral(
            "Timed out waiting %1 ms for the first token from Ollama at %2 using model '%3'. "
            "Increase ollamaFirstTokenTimeoutMs if the local model needs longer to begin generating.")
                .arg(m_firstTokenTimeoutMs)
                .arg(m_activeBaseUrl, m_activeModel));
        return;
    case StreamPhase::Streaming:
        abortForTimeout(QStringLiteral(
            "Generation stalled for %1 ms while streaming from Ollama at %2 using model '%3'. "
            "Increase ollamaInactivityTimeoutMs if long generations pause between streamed chunks.")
                .arg(m_inactivityTimeoutMs)
                .arg(m_activeBaseUrl, m_activeModel));
        return;
    case StreamPhase::Idle:
        return;
    }
}

void OllamaClient::onTotalTimeout()
{
    if (m_streamPhase == StreamPhase::Idle) {
        return;
    }

    abortForTimeout(QStringLiteral(
        "Generation exceeded the configured total timeout of %1 ms while talking to Ollama at %2 using model '%3'. "
        "Set ollamaTotalTimeoutMs to 0 to disable the total timeout.")
            .arg(m_totalTimeoutMs)
            .arg(m_activeBaseUrl, m_activeModel));
}

void OllamaClient::resetState()
{
    stopTimers();
    m_buffer.clear();
    m_accumulated.clear();
    m_pendingUiDelta.clear();
    m_pendingReasoningDelta.clear();
    m_taggedOutputBuffer.clear();
    m_reasoningBuffer.clear();
    m_activeReasoningCloseTag.clear();
    m_activeBaseUrl.clear();
    m_activeModel.clear();
    m_emittedStarted = false;
    m_receivedHeaders = false;
    m_receivedFirstToken = false;
    m_receivedAnyOutput = false;
    m_insideReasoningTrace = false;
    m_streamPhase = StreamPhase::Idle;
}

void OllamaClient::parseBufferedLines(bool flushRemainder)
{
    auto handleLine = [this](const QByteArray &lineBytes) {
        const QByteArray line = lineBytes.trimmed();
        if (line.isEmpty()) {
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            return;
        }

        const QJsonObject obj = doc.object();
        const QJsonObject messageObj = obj.value(QStringLiteral("message")).toObject();

        QString delta = obj.value(QStringLiteral("response")).toString();
        if (delta.isEmpty()) {
            delta = messageObj.value(QStringLiteral("content")).toString();
        }
        if (!delta.isEmpty()) {
            appendModelDelta(delta);
        }

        QString reasoningDelta = obj.value(QStringLiteral("thinking")).toString();
        if (reasoningDelta.isEmpty()) {
            reasoningDelta = messageObj.value(QStringLiteral("thinking")).toString();
        }
        if (!reasoningDelta.isEmpty()) {
            appendReasoningDelta(reasoningDelta);
        }
    };

    while (true) {
        const int newlineIndex = m_buffer.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        handleLine(m_buffer.left(newlineIndex));
        m_buffer.remove(0, newlineIndex + 1);
    }

    if (!flushRemainder) {
        return;
    }

    handleLine(m_buffer);
    m_buffer.clear();
}

void OllamaClient::appendModelDelta(const QString &delta)
{
    m_taggedOutputBuffer += delta;
    processTaggedOutput(false);
}

void OllamaClient::appendReasoningDelta(const QString &delta)
{
    const QString sanitized = sanitizeReasoningText(delta);
    if (sanitized.isEmpty()) {
        return;
    }

    m_receivedAnyOutput = true;
    m_pendingReasoningDelta += sanitized;
}

void OllamaClient::processTaggedOutput(bool flushRemainder)
{
    const QString ameliaOpenTag = QString::fromUtf8(kAmeliaThinkingOpenTag);
    const QString ameliaCloseTag = QString::fromUtf8(kAmeliaThinkingCloseTag);
    const QString thinkOpenTag = QStringLiteral("<think>");
    const QString thinkCloseTag = QStringLiteral("</think>");

    auto earliestOpenTag = [&](QString *openTag, QString *closeTag, int *openIndex) {
        const int ameliaIndex = m_taggedOutputBuffer.indexOf(ameliaOpenTag);
        const int thinkIndex = m_taggedOutputBuffer.indexOf(thinkOpenTag);

        if (ameliaIndex < 0 && thinkIndex < 0) {
            *openTag = QString();
            *closeTag = QString();
            *openIndex = -1;
            return;
        }

        if (ameliaIndex >= 0 && (thinkIndex < 0 || ameliaIndex <= thinkIndex)) {
            *openTag = ameliaOpenTag;
            *closeTag = ameliaCloseTag;
            *openIndex = ameliaIndex;
            return;
        }

        *openTag = thinkOpenTag;
        *closeTag = thinkCloseTag;
        *openIndex = thinkIndex;
    };

    auto trailingOverlapForAnyOpenTag = [&]() -> int {
        return qMax(trailingPrefixOverlap(m_taggedOutputBuffer, ameliaOpenTag),
                    trailingPrefixOverlap(m_taggedOutputBuffer, thinkOpenTag));
    };

    while (true) {
        if (!m_insideReasoningTrace) {
            QString openTag;
            QString closeTag;
            int openIndex = -1;
            earliestOpenTag(&openTag, &closeTag, &openIndex);
            if (openIndex < 0) {
                if (m_taggedOutputBuffer.isEmpty()) {
                    break;
                }

                int safeLen = m_taggedOutputBuffer.size();
                if (!flushRemainder) {
                    safeLen -= trailingOverlapForAnyOpenTag();
                }

                if (safeLen <= 0) {
                    break;
                }

                appendVisibleDelta(m_taggedOutputBuffer.left(safeLen));
                m_taggedOutputBuffer.remove(0, safeLen);
                continue;
            }

            if (openIndex > 0) {
                appendVisibleDelta(m_taggedOutputBuffer.left(openIndex));
            }
            m_taggedOutputBuffer.remove(0, openIndex + openTag.size());
            m_insideReasoningTrace = true;
            m_activeReasoningCloseTag = closeTag;
            continue;
        }

        const QString closeTag = m_activeReasoningCloseTag.isEmpty() ? ameliaCloseTag : m_activeReasoningCloseTag;
        const int closeIndex = m_taggedOutputBuffer.indexOf(closeTag);
        if (closeIndex < 0) {
            if (m_taggedOutputBuffer.isEmpty()) {
                break;
            }

            int safeLen = m_taggedOutputBuffer.size();
            if (!flushRemainder) {
                safeLen -= trailingPrefixOverlap(m_taggedOutputBuffer, closeTag);
            }

            if (safeLen <= 0) {
                break;
            }

            m_reasoningBuffer += m_taggedOutputBuffer.left(safeLen);
            m_taggedOutputBuffer.remove(0, safeLen);
            continue;
        }

        m_reasoningBuffer += m_taggedOutputBuffer.left(closeIndex);
        m_taggedOutputBuffer.remove(0, closeIndex + closeTag.size());

        const QString note = sanitizeReasoningText(m_reasoningBuffer).trimmed();
        if (!note.isEmpty()) {
            m_receivedAnyOutput = true;
            emit reasoningTrace(note);
        }

        m_reasoningBuffer.clear();
        m_insideReasoningTrace = false;
        m_activeReasoningCloseTag.clear();
    }

    if (!flushRemainder) {
        return;
    }

    if (m_insideReasoningTrace) {
        const QString note = sanitizeReasoningText(m_reasoningBuffer + m_taggedOutputBuffer).trimmed();
        if (!note.isEmpty()) {
            m_receivedAnyOutput = true;
            emit reasoningTrace(note);
        }
        m_reasoningBuffer.clear();
        m_insideReasoningTrace = false;
        m_activeReasoningCloseTag.clear();
        m_taggedOutputBuffer.clear();
        return;
    }

    if (!m_taggedOutputBuffer.isEmpty()) {
        appendVisibleDelta(m_taggedOutputBuffer);
        m_taggedOutputBuffer.clear();
    }
}

void OllamaClient::appendVisibleDelta(const QString &delta)
{
    const QString sanitized = sanitizeVisibleText(delta);
    if (sanitized.isEmpty()) {
        return;
    }
    m_receivedAnyOutput = true;
    m_accumulated += sanitized;
    m_pendingUiDelta += sanitized;
}

QString OllamaClient::sanitizeVisibleText(const QString &text) const
{
    QString cleaned = text;
    cleaned.replace(QRegularExpression(QStringLiteral(R"(<think>.*?</think>)"), QRegularExpression::DotMatchesEverythingOption), QString());
    cleaned.replace(QStringLiteral("<think>"), QString());
    cleaned.replace(QStringLiteral("</think>"), QString());
    cleaned.replace(QString::fromUtf8(kAmeliaThinkingOpenTag), QString());
    cleaned.replace(QString::fromUtf8(kAmeliaThinkingCloseTag), QString());
    cleaned.replace(QStringLiteral("<END>"), QString());
    return cleaned;
}

QString OllamaClient::sanitizeReasoningText(const QString &text) const
{
    QString cleaned = text;
    cleaned.replace(QStringLiteral("<think>"), QString());
    cleaned.replace(QStringLiteral("</think>"), QString());
    cleaned.replace(QString::fromUtf8(kAmeliaThinkingOpenTag), QString());
    cleaned.replace(QString::fromUtf8(kAmeliaThinkingCloseTag), QString());
    cleaned.replace(QStringLiteral("<END>"), QString());
    return cleaned;
}

void OllamaClient::flushPendingUiDelta(bool force)
{
    if (m_pendingUiDelta.isEmpty()) {
        return;
    }

    const bool shouldFlush = force
            || m_pendingUiDelta.size() >= 220
            || m_pendingUiDelta.contains(QLatin1Char('\n'))
            || m_pendingUiDelta.endsWith(QLatin1Char('.'))
            || m_pendingUiDelta.endsWith(QLatin1Char(':'));

    if (!shouldFlush) {
        return;
    }

    emit responseDelta(m_pendingUiDelta);
    m_pendingUiDelta.clear();
}

void OllamaClient::flushPendingReasoningDelta(bool force)
{
    if (m_pendingReasoningDelta.isEmpty()) {
        return;
    }

    const bool shouldFlush = force
            || m_pendingReasoningDelta.size() >= 140
            || m_pendingReasoningDelta.contains(QLatin1Char('\n'))
            || m_pendingReasoningDelta.endsWith(QLatin1Char('.'))
            || m_pendingReasoningDelta.endsWith(QLatin1Char(':'));

    if (!shouldFlush) {
        return;
    }

    emit reasoningTrace(m_pendingReasoningDelta);
    m_pendingReasoningDelta.clear();
}

void OllamaClient::armPhaseTimer(int timeoutMs)
{
    m_phaseTimer.stop();
    if (timeoutMs > 0) {
        m_phaseTimer.start(timeoutMs);
    }
}

void OllamaClient::stopTimers()
{
    m_phaseTimer.stop();
    m_totalTimer.stop();
}

void OllamaClient::beginWaitingForFirstToken()
{
    m_receivedHeaders = true;
    if (m_streamPhase == StreamPhase::WaitingForResponseHeaders) {
        m_streamPhase = StreamPhase::WaitingForFirstToken;
        armPhaseTimer(m_firstTokenTimeoutMs);
    }
}

void OllamaClient::beginStreaming()
{
    m_receivedFirstToken = true;
    m_streamPhase = StreamPhase::Streaming;
    armPhaseTimer(m_inactivityTimeoutMs);
}

void OllamaClient::abortForTimeout(const QString &message)
{
    QPointer<QNetworkReply> reply = m_reply;
    stop();
    if (reply != nullptr) {
        reply->deleteLater();
    }
    emit responseError(message);
}

QUrl OllamaClient::buildEndpointUrl(const QString &baseUrl, const QString &endpointPath) const
{
    QString normalized = baseUrl.trimmed();
    if (normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    if (normalized.endsWith(QStringLiteral("/api"), Qt::CaseInsensitive)) {
        normalized.chop(4);
    }

    return QUrl(normalized + endpointPath);
}

QString OllamaClient::describeNetworkFailure(QNetworkReply *reply,
                                             const QString &action,
                                             const QString &baseUrl,
                                             const QString &model) const
{
    if (reply == nullptr) {
        return QStringLiteral("Unknown network failure while trying to %1.").arg(action);
    }

    const QString endpoint = baseUrl.trimmed().isEmpty() ? reply->url().toString() : baseUrl.trimmed();
    const QString modelSuffix = model.trimmed().isEmpty() ? QString() : QStringLiteral(" using model '%1'").arg(model);

    switch (reply->error()) {
    case QNetworkReply::ConnectionRefusedError:
        return QStringLiteral("Connection refused while trying to %1 at %2%3. Check whether the Ollama daemon is running and listening on that address.")
                .arg(action, endpoint, modelSuffix);
    case QNetworkReply::HostNotFoundError:
        return QStringLiteral("Host not found while trying to %1 at %2%3. Check the configured Ollama base URL.")
                .arg(action, endpoint, modelSuffix);
    case QNetworkReply::TimeoutError:
        return QStringLiteral("Timed out while trying to %1 at %2%3. The local model may still be loading, the prompt may be too large, or the relevant timeout may be too small.")
                .arg(action, endpoint, modelSuffix);
    case QNetworkReply::OperationCanceledError:
        return QStringLiteral("Request canceled while trying to %1.").arg(action);
    case QNetworkReply::NoError:
        return QString();
    default:
        return QStringLiteral("Failed to %1 at %2%3: %4")
                .arg(action, endpoint, modelSuffix, reply->errorString());
    }
}
