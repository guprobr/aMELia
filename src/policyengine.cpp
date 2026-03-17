#include "policyengine.h"

#include <QRegularExpression>
#include <QSet>

QString PolicyEngine::redactSensitiveText(const QString &text) const
{
    QString sanitized = text;

    const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b")),
        QRegularExpression(QStringLiteral("\\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,}\\b"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("https?://\\S+"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("\\b[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:\\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)+\\b"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("\\b[a-f0-9]{16,}\\b"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("\\b[A-Za-z0-9_\\-]{24,}\\b"))
    };

    for (const auto &pattern : patterns) {
        sanitized.replace(pattern, QStringLiteral(" [REDACTED] "));
    }

    sanitized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return sanitized.trimmed();
}

QString PolicyEngine::buildSanitizedSearchQuery(const QString &prompt) const
{
    const QString redacted = redactSensitiveText(prompt).toLower();
    const QStringList keywords = extractKeywords(redacted);
    return keywords.mid(0, 12).join(QLatin1Char(' '));
}

bool PolicyEngine::shouldUseExternalSearch(const QString &prompt) const
{
    const QString lower = prompt.toLower();
    const QStringList triggerTerms = {
        QStringLiteral("latest"),
        QStringLiteral("release notes"),
        QStringLiteral("cve"),
        QStringLiteral("upstream"),
        QStringLiteral("bug"),
        QStringLiteral("known issue"),
        QStringLiteral("kubernetes"),
        QStringLiteral("containerd"),
        QStringLiteral("harbor"),
        QStringLiteral("error"),
        QStringLiteral("failed"),
        QStringLiteral("regression"),
        QStringLiteral("workaround"),
        QStringLiteral("docs"),
        QStringLiteral("documentation")
    };

    for (const QString &term : triggerTerms) {
        if (lower.contains(term)) {
            return true;
        }
    }

    return false;
}

bool PolicyEngine::looksSensitive(const QString &text) const
{
    return redactSensitiveText(text) != text;
}

QString PolicyEngine::buildPrivacyPreview(const QString &prompt,
                                          const QString &sanitizedQuery,
                                          bool externalSearchAllowed,
                                          bool externalSearchRecommended) const
{
    QStringList lines;
    lines << QStringLiteral("Original prompt:\n%1").arg(prompt);
    lines << QStringLiteral("\nSensitive input detected: %1")
                 .arg(looksSensitive(prompt) ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("External search allowed in UI: %1")
                 .arg(externalSearchAllowed ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("External search recommended by policy: %1")
                 .arg(externalSearchRecommended ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("\nSanitized external query:\n%1")
                 .arg(sanitizedQuery.isEmpty() ? QStringLiteral("<empty>") : sanitizedQuery);
    return lines.join(QString());
}

QStringList PolicyEngine::extractKeywords(const QString &text) const
{
    QString normalized = text;
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9.+_-]+")), QStringLiteral(" "));
    normalized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    normalized = normalized.trimmed();

    static const QSet<QString> stopWords = {
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"), QStringLiteral("with"),
        QStringLiteral("that"), QStringLiteral("this"), QStringLiteral("from"), QStringLiteral("into"),
        QStringLiteral("your"), QStringLiteral("about"), QStringLiteral("what"), QStringLiteral("when"),
        QStringLiteral("where"), QStringLiteral("which"), QStringLiteral("while"), QStringLiteral("would"),
        QStringLiteral("could"), QStringLiteral("there"), QStringLiteral("have"), QStringLiteral("will"),
        QStringLiteral("para"), QStringLiteral("como"), QStringLiteral("isso"), QStringLiteral("esta"),
        QStringLiteral("esse"), QStringLiteral("de"), QStringLiteral("da"), QStringLiteral("do"),
        QStringLiteral("dos"), QStringLiteral("das"), QStringLiteral("uma"), QStringLiteral("que"),
        QStringLiteral("sem"), QStringLiteral("por"), QStringLiteral("com")
    };

    QStringList result;
    QSet<QString> seen;
    const QStringList parts = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part.size() < 3) {
            continue;
        }
        if (part == QStringLiteral("redacted")) {
            continue;
        }
        if (stopWords.contains(part)) {
            continue;
        }
        if (seen.contains(part)) {
            continue;
        }
        seen.insert(part);
        result.push_back(part);
    }

    return result;
}
