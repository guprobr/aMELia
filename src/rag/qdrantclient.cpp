#include "rag/qdrantclient.h"

QString QdrantClient::endpoint() const
{
    return m_endpoint;
}

void QdrantClient::setEndpoint(const QString &endpoint)
{
    m_endpoint = endpoint;
}

bool QdrantClient::isConfigured() const
{
    return !m_endpoint.trimmed().isEmpty();
}

bool QdrantClient::upsert(const QString &collectionName,
                          const QVector<VectorPoint> &points,
                          QString *errorMessage) const
{
    Q_UNUSED(collectionName)
    Q_UNUSED(points)

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("QdrantClient is a stub. Wire the HTTP API in a future vector-store release.");
    }
    return false;
}

QString QdrantClient::search(const QString &collectionName,
                             const QVector<float> &queryVector,
                             int limit,
                             QString *errorMessage) const
{
    Q_UNUSED(collectionName)
    Q_UNUSED(queryVector)
    Q_UNUSED(limit)

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("QdrantClient is a stub. Wire the HTTP API in a future vector-store release.");
    }
    return QString();
}
