#pragma once

#include <QString>
#include <QVector>

struct VectorPoint {
    QString id;
    QVector<float> values;
    QString payloadJson;
};

class QdrantClient {
public:
    QString endpoint() const;
    void setEndpoint(const QString &endpoint);

    bool isConfigured() const;
    bool upsert(const QString &collectionName, const QVector<VectorPoint> &points, QString *errorMessage = nullptr) const;
    QString search(const QString &collectionName, const QVector<float> &queryVector, int limit, QString *errorMessage = nullptr) const;

private:
    QString m_endpoint;
};
