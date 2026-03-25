#include "rag/searchbroker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>

SearchBroker::SearchBroker(QObject *parent)
    : QObject(parent)
{
}

namespace {
QString cleanupSnippet(QString text)
{
    text.replace(QRegularExpression(QStringLiteral(R"(<[^>]+>)")), QStringLiteral(" "));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    text.replace(QRegularExpression(QStringLiteral(R"([\t\r\n]+)")), QStringLiteral(" "));
    text = text.simplified();
    return text;
}

QString snippetFromObject(const QJsonObject &obj)
{
    static const QStringList candidateKeys = {
        QStringLiteral("content"),
        QStringLiteral("snippet"),
        QStringLiteral("description"),
        QStringLiteral("text"),
        QStringLiteral("summary")
    };

    for (const QString &key : candidateKeys) {
        const QString value = cleanupSnippet(obj.value(key).toString());
        if (!value.isEmpty()) {
            return value;
        }
    }

    const QJsonArray descriptions = obj.value(QStringLiteral("descriptions")).toArray();
    QStringList parts;
    for (const QJsonValue &value : descriptions) {
        const QString cleaned = cleanupSnippet(value.toString());
        if (!cleaned.isEmpty()) {
            parts << cleaned;
        }
        if (parts.join(QStringLiteral(" ")).size() >= 900) {
            break;
        }
    }
    return cleanupSnippet(parts.join(QStringLiteral(" ")));
}
}

void SearchBroker::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void SearchBroker::setEndpoint(const QString &endpointUrl)
{
    m_endpointUrl = endpointUrl.trimmed();
}

void SearchBroker::setAllowedDomains(const QStringList &domains)
{
    m_allowedDomains = domains;
}

void SearchBroker::setMaxResults(int maxResults)
{
    m_maxResults = qMax(1, maxResults);
}

void SearchBroker::setRequestTimeoutMs(int timeoutMs)
{
    m_requestTimeoutMs = qMax(2000, timeoutMs);
}

bool SearchBroker::isEnabled() const
{
    return m_enabled && !m_endpointUrl.trimmed().isEmpty();
}

void SearchBroker::search(const QString &query)
{
    if (!isEnabled() || query.trimmed().isEmpty()) {
        emit searchFinished(query, QString(), QStringLiteral("<none>"));
        return;
    }

    QUrl url(m_endpointUrl);
    QUrlQuery urlQuery(url);
    urlQuery.addQueryItem(QStringLiteral("q"), query);
    urlQuery.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(urlQuery);

    emit searchStarted(query, url.toString());

    QNetworkRequest request(url);
    request.setTransferTimeout(m_requestTimeoutMs);
    QNetworkReply *reply = m_network.get(request);
    reply->setProperty("originalQuery", query);
    connect(reply, &QNetworkReply::finished, this, &SearchBroker::onReplyFinished);
}

void SearchBroker::onReplyFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (reply == nullptr) {
        return;
    }

    const QString originalQuery = reply->property("originalQuery").toString();
    const auto error = reply->error();
    const QString errorText = reply->errorString();
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        emit searchError(originalQuery, QStringLiteral("Search request failed: %1").arg(errorText));
        return;
    }

    const QVector<SearchHit> hits = parseResults(data);
    emit searchFinished(originalQuery, formatHitsForPrompt(hits), formatHitsForUi(hits));
}

QVector<SearchHit> SearchBroker::parseResults(const QByteArray &jsonData) const
{
    QVector<SearchHit> hits;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isObject()) {
        return hits;
    }

    const QJsonArray results = doc.object().value(QStringLiteral("results")).toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject obj = value.toObject();
        const QString title = obj.value(QStringLiteral("title")).toString().trimmed();
        const QString url = obj.value(QStringLiteral("url")).toString().trimmed();
        const QString content = snippetFromObject(obj);

        if (title.isEmpty() && content.isEmpty()) {
            continue;
        }
        if (!isAllowedDomain(url)) {
            continue;
        }

        SearchHit hit;
        hit.title = title;
        hit.url = url;
        hit.snippet = content.left(900).trimmed();
        hit.domain = QUrl(url).host();
        hits.push_back(hit);
        if (hits.size() >= m_maxResults) {
            break;
        }
    }

    return hits;
}

QString SearchBroker::formatHitsForPrompt(const QVector<SearchHit> &hits) const
{
    if (hits.isEmpty()) {
        return QString();
    }

    QStringList lines;
    for (const SearchHit &hit : hits) {
        lines << QStringLiteral("[%1] %2 -- %3")
                     .arg(hit.domain.isEmpty() ? QStringLiteral("web") : hit.domain,
                          hit.title,
                          hit.snippet);
    }
    return lines.join(QStringLiteral("\n\n"));
}

QString SearchBroker::formatHitsForUi(const QVector<SearchHit> &hits) const
{
    if (hits.isEmpty()) {
        return QStringLiteral("<none>");
    }

    QStringList lines;
    for (const SearchHit &hit : hits) {
        lines << QStringLiteral("Title: %1").arg(hit.title);
        lines << QStringLiteral("Domain: %1").arg(hit.domain.isEmpty() ? QStringLiteral("<unknown>") : hit.domain);
        lines << QStringLiteral("URL: %1").arg(hit.url);
        lines << QStringLiteral("Snippet: %1").arg(hit.snippet);
        lines << QString();
    }
    return lines.join(QStringLiteral("\n"));
}

bool SearchBroker::isAllowedDomain(const QString &urlText) const
{
    if (m_allowedDomains.isEmpty()) {
        return true;
    }

    const QUrl url(urlText);
    const QString host = url.host().toLower();
    if (host.isEmpty()) {
        return false;
    }

    for (const QString &domain : m_allowedDomains) {
        const QString lowerDomain = domain.toLower().trimmed();
        if (lowerDomain.isEmpty()) {
            continue;
        }
        if (host == lowerDomain || host.endsWith(QStringLiteral(".") + lowerDomain)) {
            return true;
        }
    }

    return false;
}
