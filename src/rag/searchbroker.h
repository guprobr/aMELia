#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QVector>

class QNetworkReply;

struct SearchHit {
    QString title;
    QString url;
    QString domain;
    QString snippet;
};

class SearchBroker : public QObject {
    Q_OBJECT
public:
    explicit SearchBroker(QObject *parent = nullptr);

    void setEnabled(bool enabled);
    void setEndpoint(const QString &endpointUrl);
    void setAllowedDomains(const QStringList &domains);
    void setMaxResults(int maxResults);
    void setRequestTimeoutMs(int timeoutMs);

    bool isEnabled() const;

    void search(const QString &query);

signals:
    void searchStarted(const QString &query, const QString &requestUrl);
    void searchFinished(const QString &query,
                        const QString &formattedContext,
                        const QString &formattedSources);
    void searchError(const QString &query, const QString &message);

private slots:
    void onReplyFinished();

private:
    QVector<SearchHit> parseResults(const QByteArray &jsonData) const;
    QString formatHitsForPrompt(const QVector<SearchHit> &hits) const;
    QString formatHitsForUi(const QVector<SearchHit> &hits) const;
    bool isAllowedDomain(const QString &url) const;

    QNetworkAccessManager m_network;
    QString m_endpointUrl;
    QStringList m_allowedDomains;
    bool m_enabled = false;
    int m_maxResults = 5;
    int m_requestTimeoutMs = 15000;
};
