#include "rag/embeddingclient.h"

#include <QHash>
#include <QRegularExpression>
#include <QtMath>

namespace {
constexpr int kEmbeddingDims = 192;

QStringList normalizeTokens(const QString &input)
{
    QString text = input.toLower();
    text.replace(QRegularExpression(QStringLiteral("[^a-z0-9.+_-]+")), QStringLiteral(" "));
    const QStringList raw = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    static const QHash<QString, QStringList> synonyms = {
        {QStringLiteral("deploy"), {QStringLiteral("deployment"), QStringLiteral("install"), QStringLiteral("bootstrap")}},
        {QStringLiteral("deployment"), {QStringLiteral("deploy"), QStringLiteral("install"), QStringLiteral("bringup")}},
        {QStringLiteral("runbook"), {QStringLiteral("mop"), QStringLiteral("procedure"), QStringLiteral("playbook")}},
        {QStringLiteral("mop"), {QStringLiteral("runbook"), QStringLiteral("procedure")}},
        {QStringLiteral("ciq"), {QStringLiteral("spreadsheet"), QStringLiteral("inputs")}},
        {QStringLiteral("controller"), {QStringLiteral("controllers"), QStringLiteral("control")}},
        {QStringLiteral("worker"), {QStringLiteral("workers"), QStringLiteral("compute")}},
        {QStringLiteral("idrac"), {QStringLiteral("bmc"), QStringLiteral("virtualmedia")}},
        {QStringLiteral("vpn"), {QStringLiteral("globalprotect")}},
        {QStringLiteral("hld"), {QStringLiteral("topology"), QStringLiteral("architecture")}},
        {QStringLiteral("lld"), {QStringLiteral("implementation"), QStringLiteral("design")}},
        {QStringLiteral("alarm"), {QStringLiteral("error"), QStringLiteral("fault")}},
        {QStringLiteral("failed"), {QStringLiteral("failure"), QStringLiteral("error")}},
                {QStringLiteral("harbor"), {QStringLiteral("registry")}},
        {QStringLiteral("k8s"), {QStringLiteral("kubernetes")}}
    };

    QStringList tokens;
    tokens.reserve(raw.size() * 3);
    for (int i = 0; i < raw.size(); ++i) {
        const QString &token = raw.at(i);
        if (token.size() < 2) {
            continue;
        }
        tokens << token;
        if (synonyms.contains(token)) {
            tokens << synonyms.value(token);
        }
        if (token.size() >= 5) {
            for (int j = 0; j + 3 <= token.size(); ++j) {
                tokens << token.mid(j, 3);
            }
        }
        if (i + 1 < raw.size()) {
            const QString &next = raw.at(i + 1);
            if (next.size() >= 2) {
                tokens << token + QLatin1Char('_') + next;
            }
        }
    }
    return tokens;
}

int bucketForToken(const QString &token)
{
    return static_cast<int>(qHash(token) % kEmbeddingDims);
}

float signedWeight(const QString &token)
{
    return (qHash(token, 0x9e3779b9U) & 0x1U) == 0U ? 1.0f : -1.0f;
}
}

QString EmbeddingClient::backendName() const
{
    return QStringLiteral("local-hash-semantic-v1");
}

bool EmbeddingClient::isConfigured() const
{
    return true;
}

QVector<float> EmbeddingClient::embedText(const QString &text) const
{
    QVector<float> vector(kEmbeddingDims, 0.0f);
    if (text.trimmed().isEmpty()) {
        return vector;
    }

    const QStringList tokens = normalizeTokens(text);
    if (tokens.isEmpty()) {
        return vector;
    }

    QHash<QString, int> frequencies;
    for (const QString &token : tokens) {
        frequencies[token] += 1;
    }

    float normSquared = 0.0f;
    for (auto it = frequencies.cbegin(); it != frequencies.cend(); ++it) {
        const float weight = 1.0f + qLn(1.0f + static_cast<float>(it.value()));
        const int bucket = bucketForToken(it.key());
        const float signedContribution = weight * signedWeight(it.key());
        vector[bucket] += signedContribution;
    }

    for (const float value : vector) {
        normSquared += value * value;
    }

    if (normSquared <= 0.0f) {
        return vector;
    }

    const float invNorm = 1.0f / qSqrt(normSquared);
    for (float &value : vector) {
        value *= invNorm;
    }
    return vector;
}

QVector<QVector<float>> EmbeddingClient::embedTexts(const QStringList &texts) const
{
    QVector<QVector<float>> embeddings;
    embeddings.reserve(texts.size());
    for (const QString &text : texts) {
        embeddings.push_back(embedText(text));
    }
    return embeddings;
}

float EmbeddingClient::cosineSimilarity(const QVector<float> &a, const QVector<float> &b)
{
    if (a.isEmpty() || b.isEmpty() || a.size() != b.size()) {
        return 0.0f;
    }

    float dot = 0.0f;
    float normA = 0.0f;
    float normB = 0.0f;
    for (int i = 0; i < a.size(); ++i) {
        dot += a.at(i) * b.at(i);
        normA += a.at(i) * a.at(i);
        normB += b.at(i) * b.at(i);
    }

    if (normA <= 0.0f || normB <= 0.0f) {
        return 0.0f;
    }
    return dot / (qSqrt(normA) * qSqrt(normB));
}
