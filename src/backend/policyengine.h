#pragma once

#include <QString>
#include <QStringList>

class PolicyEngine {
public:
    QString redactSensitiveText(const QString &text) const;
    QString buildSanitizedSearchQuery(const QString &prompt) const;
    bool shouldUseExternalSearch(const QString &prompt) const;
    bool looksSensitive(const QString &text) const;
    QString buildPrivacyPreview(const QString &prompt,
                                const QString &sanitizedQuery,
                                bool externalSearchAllowed,
                                bool externalSearchRecommended) const;

private:
    QStringList extractKeywords(const QString &text) const;
};
