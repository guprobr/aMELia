#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class EmbeddingClient {
public:
    QString backendName() const;
    bool isConfigured() const;
    QVector<float> embedText(const QString &text) const;
    QVector<QVector<float>> embedTexts(const QStringList &texts) const;
    static float cosineSimilarity(const QVector<float> &a, const QVector<float> &b);
};
