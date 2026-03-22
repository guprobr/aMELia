#pragma once

#include "core/appconfig.h"
#include "backend/llmclient.h"

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>
#include <QTimer>

class QNetworkReply;
class QUrl;

class OllamaClient final : public LlmClient {
    Q_OBJECT
public:
    explicit OllamaClient(QObject *parent = nullptr);

    void generate(const QString &baseUrl,
                  const QString &model,
                  const QVector<LlmChatMessage> &messages) override;

    void probe(const QString &baseUrl, const QString &expectedModel = QString());
    void listModels(const QString &baseUrl);
    void setProbeTimeoutMs(int timeoutMs);
    void setResponseHeadersTimeoutMs(int timeoutMs);
    void setFirstTokenTimeoutMs(int timeoutMs);
    void setInactivityTimeoutMs(int timeoutMs);
    void setTotalTimeoutMs(int timeoutMs);
    void setGenerationConfig(const AppConfig &config);
    void setReasoningTraceEnabled(bool enabled);
    void setForceThinkOff(bool enabled);

    void stop() override;

signals:
    void backendProbeFinished(bool ok, const QString &message);
    void modelsListed(const QStringList &models, const QString &message);
    void reasoningTrace(const QString &text);
    void diagnosticMessage(const QString &category, const QString &message);

private slots:
    void onReadyRead();
    void onFinished();
    void onMetaDataChanged();
    void onPhaseTimeout();
    void onTotalTimeout();

private:
    enum class StreamPhase {
        Idle,
        WaitingForResponseHeaders,
        WaitingForFirstToken,
        Streaming
    };

    void resetState();
    void parseBufferedLines(bool flushRemainder);
    void appendModelDelta(const QString &delta);
    void appendReasoningDelta(const QString &delta);
    void processTaggedOutput(bool flushRemainder);
    void appendVisibleDelta(const QString &delta);
    QString sanitizeVisibleText(const QString &text) const;
    QString sanitizeReasoningText(const QString &text) const;
    void flushPendingUiDelta(bool force);
    void flushPendingReasoningDelta(bool force);
    void armPhaseTimer(int timeoutMs);
    void stopTimers();
    void beginWaitingForFirstToken();
    void beginStreaming();
    void abortForTimeout(const QString &message);
    QString thinkRequestModeForModel(const QString &model) const;
    QString summarizePayloadForDiagnostics(const QJsonObject &payload) const;
    QUrl buildEndpointUrl(const QString &baseUrl, const QString &endpointPath) const;
    QString describeNetworkFailure(QNetworkReply *reply,
                                   const QString &action,
                                   const QString &baseUrl,
                                   const QString &model = QString()) const;

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_buffer;
    QString m_accumulated;
    QString m_pendingUiDelta;
    QString m_pendingReasoningDelta;
    QString m_taggedOutputBuffer;
    QString m_reasoningBuffer;
    QString m_activeReasoningCloseTag;
    QString m_activeBaseUrl;
    QString m_activeModel;
    QStringList m_stopSequences = {
        QStringLiteral("<END>"),
        QStringLiteral("<|im_end|>"),
        QStringLiteral("<|endoftext|>")
    };
    bool m_emittedStarted = false;
    bool m_receivedHeaders = false;
    bool m_receivedFirstToken = false;
    bool m_receivedAnyOutput = false;
    bool m_receivedVisibleOutput = false;
    bool m_insideReasoningTrace = false;
    bool m_reasoningTraceEnabled = false;
    bool m_forceThinkOff = false;
    bool m_hiddenThinkingNoticeEmitted = false;
    int m_probeTimeoutMs = 10000;
    int m_httpStatusCode = 0;
    int m_responseHeadersTimeoutMs = 180000;
    int m_firstTokenTimeoutMs = 600000;
    int m_inactivityTimeoutMs = 300000;
    int m_totalTimeoutMs = 0;
    int m_numCtx = 32768;
    int m_topK = 50;
    double m_temperature = 0.15;
    double m_topP = 0.95;
    double m_repeatPenalty = 1.12;
    double m_presencePenalty = 0.0;
    double m_frequencyPenalty = 0.0;
    StreamPhase m_streamPhase = StreamPhase::Idle;
    qint64 m_totalBytesReceived = 0;
    qint64 m_reasoningCharsObserved = 0;
    QString m_requestedThinkMode;
    QString m_streamLogicalError;
    QString m_lastDoneReason;
    QTimer m_phaseTimer;
    QTimer m_totalTimer;
};
