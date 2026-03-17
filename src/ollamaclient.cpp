#include "ollamaclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

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
                            const QString &prompt)
{
    stop();
    resetState();

    m_activeBaseUrl = baseUrl.trimmed();
    m_activeModel = model.trimmed();

    const QUrl url = buildEndpointUrl(baseUrl, QStringLiteral("/api/generate"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), model);
    payload.insert(QStringLiteral("prompt"), prompt);
    payload.insert(QStringLiteral("stream"), true);

    QJsonObject options;
    if (m_numCtx > 0) {
        options.insert(QStringLiteral("num_ctx"), m_numCtx);
    }
    if (!options.isEmpty()) {
        payload.insert(QStringLiteral("options"), options);
    }

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
                const QString model = obj.value(QStringLiteral("model")).toString();
                if (!name.isEmpty()) {
                    models.push_back(name);
                } else if (!model.isEmpty()) {
                    models.push_back(model);
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
                const QString model = obj.value(QStringLiteral("model")).toString();
                if (!name.isEmpty()) {
                    models.push_back(name);
                } else if (!model.isEmpty()) {
                    models.push_back(model);
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

void OllamaClient::setNumCtx(int numCtx)
{
    m_numCtx = qMax(1024, numCtx);
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
    const QString before = m_accumulated;
    parseBufferedLines(false);
    flushPendingUiDelta(false);

    if (!m_receivedFirstToken && m_accumulated.size() > before.size()) {
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
    const QString before = m_accumulated;
    parseBufferedLines(true);
    if (!m_receivedFirstToken && m_accumulated.size() > before.size()) {
        m_receivedFirstToken = true;
    }
    flushPendingUiDelta(true);

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

    emit responseFinished(m_accumulated);
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
    m_activeBaseUrl.clear();
    m_activeModel.clear();
    m_emittedStarted = false;
    m_receivedHeaders = false;
    m_receivedFirstToken = false;
    m_streamPhase = StreamPhase::Idle;
}

void OllamaClient::parseBufferedLines(bool flushRemainder)
{
    while (true) {
        const int newlineIndex = m_buffer.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        const QByteArray line = m_buffer.left(newlineIndex).trimmed();
        m_buffer.remove(0, newlineIndex + 1);
        if (line.isEmpty()) {
            continue;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            continue;
        }

        const QJsonObject obj = doc.object();
        const QString delta = obj.value(QStringLiteral("response")).toString();
        if (!delta.isEmpty()) {
            m_accumulated += delta;
            m_pendingUiDelta += delta;
        }
    }

    if (!flushRemainder) {
        return;
    }

    const QByteArray tail = m_buffer.trimmed();
    m_buffer.clear();
    if (tail.isEmpty()) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(tail);
    if (!doc.isObject()) {
        return;
    }

    const QString delta = doc.object().value(QStringLiteral("response")).toString();
    if (!delta.isEmpty()) {
        m_accumulated += delta;
        m_pendingUiDelta += delta;
    }
}

void OllamaClient::flushPendingUiDelta(bool force)
{
    if (m_pendingUiDelta.isEmpty()) {
        return;
    }

    const bool shouldFlush = force
            || m_pendingUiDelta.size() >= 180
            || m_pendingUiDelta.contains(QLatin1Char('\n'))
            || m_pendingUiDelta.endsWith(QLatin1Char('.'))
            || m_pendingUiDelta.endsWith(QLatin1Char(':'));

    if (!shouldFlush) {
        return;
    }

    emit responseDelta(m_pendingUiDelta);
    m_pendingUiDelta.clear();
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
