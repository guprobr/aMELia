#pragma once

#include "llmclient.h"

#include <QByteArray>
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
                  const QString &prompt) override;

    void probe(const QString &baseUrl, const QString &expectedModel = QString());
    void listModels(const QString &baseUrl);
    void setProbeTimeoutMs(int timeoutMs);
    void setResponseHeadersTimeoutMs(int timeoutMs);
    void setFirstTokenTimeoutMs(int timeoutMs);
    void setInactivityTimeoutMs(int timeoutMs);
    void setTotalTimeoutMs(int timeoutMs);
    void setNumCtx(int numCtx);

    void stop() override;

signals:
    void backendProbeFinished(bool ok, const QString &message);
    void modelsListed(const QStringList &models, const QString &message);

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
    void flushPendingUiDelta(bool force);
    void armPhaseTimer(int timeoutMs);
    void stopTimers();
    void beginWaitingForFirstToken();
    void beginStreaming();
    void abortForTimeout(const QString &message);
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
    QString m_activeBaseUrl;
    QString m_activeModel;
    bool m_emittedStarted = false;
    bool m_receivedHeaders = false;
    bool m_receivedFirstToken = false;
    int m_probeTimeoutMs = 10000;
    int m_responseHeadersTimeoutMs = 180000;
    int m_firstTokenTimeoutMs = 600000;
    int m_inactivityTimeoutMs = 300000;
    int m_totalTimeoutMs = 0;
    int m_numCtx = 32768;
    StreamPhase m_streamPhase = StreamPhase::Idle;
    QTimer m_phaseTimer;
    QTimer m_totalTimer;
};
