#include "backend/ollamaclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>

namespace {
constexpr auto kAmeliaThinkingOpenTag = "<amelia_thinking>";
constexpr auto kAmeliaThinkingCloseTag = "</amelia_thinking>";
constexpr int kDiagnosticPreviewChars = 520;

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

QString previewForDiagnostics(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text = text.simplified();
    if (text.size() <= kDiagnosticPreviewChars) {
        return text;
    }
    return text.left(kDiagnosticPreviewChars).trimmed() + QStringLiteral("...");
}

bool modelIsGptOss(const QString &model)
{
    return model.trimmed().toLower().contains(QStringLiteral("gpt-oss"));
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
    m_requestedThinkMode = thinkRequestModeForModel(model);

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

    if (m_forceThinkOff) {
        payload.insert(QStringLiteral("think"), false);
        emit diagnosticMessage(QStringLiteral("backend"),
                               QStringLiteral("Forced think=false for this request to reduce backend load and avoid reasoning-heavy stalls on large document-study prompts."));
    } else if (modelIsGptOss(model)) {
        payload.insert(QStringLiteral("think"), QStringLiteral("low"));
        if (!m_reasoningTraceEnabled) {
            emit diagnosticMessage(QStringLiteral("backend"),
                                   QStringLiteral("GPT-OSS defaults to backend thinking; Amelia now requests think=low even when trace capture is off, suppresses surfaced reasoning locally, and only treats visible answer text as real stream progress."));
        }
    } else {
        payload.insert(QStringLiteral("think"), m_reasoningTraceEnabled);
    }

    const QByteArray requestBody = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    emit diagnosticMessage(QStringLiteral("backend"),
                           QStringLiteral("Ollama request %1 | model=%2 | messages=%3 | think=%4 | body_bytes=%5 | preview=%6")
                               .arg(url.toString(),
                                    model,
                                    QString::number(messages.size()),
                                    m_requestedThinkMode,
                                    QString::number(requestBody.size()),
                                    summarizePayloadForDiagnostics(payload)));

    m_reply = m_network.post(request, requestBody);
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
    const QUrl url = buildEndpointUrl(baseUrl, QStringLiteral("/api/tags"));
    QNetworkRequest request(url);
    request.setTransferTimeout(m_probeTimeoutMs);
    emit diagnosticMessage(QStringLiteral("backend"),
                           QStringLiteral("Ollama probe request %1 | timeout_ms=%2")
                               .arg(url.toString())
                               .arg(m_probeTimeoutMs));
    auto *reply = m_network.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, url, baseUrl, expectedModel]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray body = reply->readAll();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        emit diagnosticMessage(QStringLiteral("backend"),
                               QStringLiteral("Ollama probe response %1 | http=%2 | bytes=%3 | preview=%4")
                                   .arg(url.toString())
                                   .arg(httpStatus)
                                   .arg(body.size())
                                   .arg(previewForDiagnostics(QString::fromUtf8(body))));

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
    const QUrl url = buildEndpointUrl(baseUrl, QStringLiteral("/api/tags"));
    QNetworkRequest request(url);
    request.setTransferTimeout(m_probeTimeoutMs);
    emit diagnosticMessage(QStringLiteral("backend"),
                           QStringLiteral("Ollama model-list request %1 | timeout_ms=%2")
                               .arg(url.toString())
                               .arg(m_probeTimeoutMs));
    auto *reply = m_network.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, url, baseUrl]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray body = reply->readAll();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        emit diagnosticMessage(QStringLiteral("backend"),
                               QStringLiteral("Ollama model-list response %1 | http=%2 | bytes=%3 | preview=%4")
                                   .arg(url.toString())
                                   .arg(httpStatus)
                                   .arg(body.size())
                                   .arg(previewForDiagnostics(QString::fromUtf8(body))));

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

void OllamaClient::setForceThinkOff(bool enabled)
{
    m_forceThinkOff = enabled;
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

    const QByteArray chunk = m_reply->readAll();
    m_totalBytesReceived += chunk.size();
    m_buffer += chunk;
    const bool hadVisibleOutput = m_receivedVisibleOutput;
    parseBufferedLines(false);
    flushPendingUiDelta(false);
    flushPendingReasoningDelta(false);

    if (!m_receivedFirstToken && !hadVisibleOutput && m_receivedVisibleOutput) {
        beginStreaming();
    } else if (m_streamPhase == StreamPhase::Streaming && m_receivedVisibleOutput) {
        armPhaseTimer(m_inactivityTimeoutMs);
    }
}

void OllamaClient::onFinished()
{
    if (m_reply == nullptr) {
        return;
    }

    stopTimers();

    const QByteArray tail = m_reply->readAll();
    m_totalBytesReceived += tail.size();
    m_buffer += tail;
    const bool hadVisibleOutput = m_receivedVisibleOutput;
    parseBufferedLines(true);
    processTaggedOutput(true);
    if (!m_receivedFirstToken && (!hadVisibleOutput && m_receivedVisibleOutput)) {
        m_receivedFirstToken = true;
    }
    flushPendingUiDelta(true);
    flushPendingReasoningDelta(true);

    const QNetworkReply::NetworkError error = m_reply->error();
    const int httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString errorText = describeNetworkFailure(m_reply,
                                                     QStringLiteral("generate text with Ollama"),
                                                     m_activeBaseUrl,
                                                     m_activeModel);

    emit diagnosticMessage(QStringLiteral("backend"),
                           QStringLiteral("Ollama chat response complete | http=%1 | bytes=%2 | think=%3 | reasoning_chars=%4 | done_reason=%5 | logical_error=%6")
                               .arg(httpStatus)
                               .arg(m_totalBytesReceived)
                               .arg(m_requestedThinkMode.isEmpty() ? QStringLiteral("<unset>") : m_requestedThinkMode)
                               .arg(m_reasoningCharsObserved)
                               .arg(m_lastDoneReason.isEmpty() ? QStringLiteral("<unset>") : m_lastDoneReason)
                               .arg(m_streamLogicalError.isEmpty() ? QStringLiteral("<none>") : m_streamLogicalError));

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

    if (!m_streamLogicalError.trimmed().isEmpty()) {
        emit responseError(QStringLiteral("Ollama returned an application-level error on a successful HTTP response: %1")
                               .arg(m_streamLogicalError));
        return;
    }

    const QString visibleAnswer = sanitizeVisibleText(m_accumulated).trimmed();
    if (visibleAnswer.isEmpty() && m_reasoningCharsObserved > 0) {
        emit responseError(QStringLiteral("Ollama produced %1 hidden reasoning chars but no visible answer. Amelia treated this as a reasoning-only loop.")
                               .arg(m_reasoningCharsObserved));
        return;
    }

    emit responseFinished(visibleAnswer);
}

void OllamaClient::onMetaDataChanged()
{
    if (m_reply == nullptr) {
        return;
    }

    m_httpStatusCode = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    emit diagnosticMessage(QStringLiteral("backend"),
                           QStringLiteral("Ollama response headers received | http=%1 | content_type=%2")
                               .arg(m_httpStatusCode)
                               .arg(QString::fromUtf8(m_reply->header(QNetworkRequest::ContentTypeHeader).toByteArray())));
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
    m_receivedVisibleOutput = false;
    m_insideReasoningTrace = false;
    m_hiddenThinkingNoticeEmitted = false;
    m_httpStatusCode = 0;
    m_totalBytesReceived = 0;
    m_reasoningCharsObserved = 0;
    m_requestedThinkMode.clear();
    m_streamLogicalError.clear();
    m_lastDoneReason.clear();
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
            emit diagnosticMessage(QStringLiteral("backend"),
                                   QStringLiteral("Ollama chat emitted a non-JSON stream fragment: %1")
                                       .arg(previewForDiagnostics(QString::fromUtf8(line))));
            return;
        }

        const QJsonObject obj = doc.object();
        const QJsonObject messageObj = obj.value(QStringLiteral("message")).toObject();

        const QString logicalError = obj.value(QStringLiteral("error")).toString().trimmed();
        if (!logicalError.isEmpty()) {
            m_streamLogicalError = logicalError;
            emit diagnosticMessage(QStringLiteral("backend"),
                                   QStringLiteral("Ollama chat stream reported logical error: %1").arg(logicalError));
            return;
        }

        const QString doneReason = obj.value(QStringLiteral("done_reason")).toString().trimmed();
        if (!doneReason.isEmpty()) {
            m_lastDoneReason = doneReason;
        }

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

    if (!m_reasoningTraceEnabled && !m_hiddenThinkingNoticeEmitted) {
        m_hiddenThinkingNoticeEmitted = true;
        emit diagnosticMessage(QStringLiteral("backend"),
                               QStringLiteral("The backend emitted a separate thinking stream even though reasoning capture is off. Amelia is hiding it from the UI, and only visible answer text now counts as real stream progress."));
    }

    m_reasoningCharsObserved += sanitized.size();
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
            if (!m_reasoningTraceEnabled && !m_hiddenThinkingNoticeEmitted) {
                m_hiddenThinkingNoticeEmitted = true;
                emit diagnosticMessage(QStringLiteral("backend"),
                                       QStringLiteral("The model emitted <think>-style tagged reasoning while reasoning capture is off. Amelia is stripping it from the visible answer and still using it for loop detection."));
            }
            m_reasoningCharsObserved += note.size();
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
            if (!m_reasoningTraceEnabled && !m_hiddenThinkingNoticeEmitted) {
                m_hiddenThinkingNoticeEmitted = true;
                emit diagnosticMessage(QStringLiteral("backend"),
                                       QStringLiteral("The model ended with hidden reasoning text while reasoning capture is off. Amelia is hiding it from the UI and still using it for loop detection."));
            }
            m_reasoningCharsObserved += note.size();
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
    m_receivedVisibleOutput = true;
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
    emit diagnosticMessage(QStringLiteral("backend"), message);
    emit responseError(message);
}

QString OllamaClient::thinkRequestModeForModel(const QString &model) const
{
    if (m_forceThinkOff) {
        return QStringLiteral("false");
    }
    if (modelIsGptOss(model)) {
        return QStringLiteral("low");
    }
    return m_reasoningTraceEnabled ? QStringLiteral("true") : QStringLiteral("false");
}

QString OllamaClient::summarizePayloadForDiagnostics(const QJsonObject &payload) const
{
    return previewForDiagnostics(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
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
