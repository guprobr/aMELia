#include "rag/lexicalscoring.h"

#include "rag/semanticchunker.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QVector>

#include <algorithm>

QStringList queryTerms(const QString &query)
{
    QString normalized = query.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9._+:-]+")), QStringLiteral(" "));
    const QStringList parts = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    static const QSet<QString> stopWords = {
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"), QStringLiteral("with"),
        QStringLiteral("from"), QStringLiteral("into"), QStringLiteral("that"), QStringLiteral("this"),
        QStringLiteral("what"), QStringLiteral("when"), QStringLiteral("where"), QStringLiteral("which"),
        QStringLiteral("your"), QStringLiteral("through"), QStringLiteral("create"), QStringLiteral("write"),
        QStringLiteral("format"), QStringLiteral("whole"), QStringLiteral("please"), QStringLiteral("show"),
        QStringLiteral("explain"), QStringLiteral("about")
    };

    static const QHash<QString, QStringList> expansions = {
        {QStringLiteral("deploy"), {QStringLiteral("deployment"), QStringLiteral("install"), QStringLiteral("bootstrap")}},
        {QStringLiteral("deployment"), {QStringLiteral("deploy"), QStringLiteral("install")}},
        {QStringLiteral("runbook"), {QStringLiteral("mop"), QStringLiteral("playbook"), QStringLiteral("procedure")}},
        {QStringLiteral("mop"), {QStringLiteral("runbook"), QStringLiteral("procedure")}},
        {QStringLiteral("hld"), {QStringLiteral("architecture"), QStringLiteral("topology")}},
        {QStringLiteral("lld"), {QStringLiteral("design"), QStringLiteral("implementation")}},
        {QStringLiteral("k8s"), {QStringLiteral("kubernetes")}},
        {QStringLiteral("harbor"), {QStringLiteral("registry")}}
    };

    QStringList terms;
    QSet<QString> seen;
    for (const QString &part : parts) {
        if (part.size() < 2 || stopWords.contains(part) || seen.contains(part)) {
            continue;
        }
        seen.insert(part);
        terms.push_back(part);
        const auto expansionIt = expansions.constFind(part);
        if (expansionIt != expansions.cend()) {
            for (const QString &expanded : expansionIt.value()) {
                if (!seen.contains(expanded)) {
                    seen.insert(expanded);
                    terms.push_back(expanded);
                }
            }
        }
    }
    return terms;
}

double lexicalScoreChunk(const QString &text, const QString &fileName, const QStringList &terms, const QString &query)
{
    if (terms.isEmpty()) {
        return 0.0;
    }

    const QString lower = text.toLower();
    const QString fileLower = fileName.toLower();
    int matched = 0;
    double score = 0.0;
    for (const QString &term : terms) {
        const int bodyCount = lower.count(term);
        const int fileCount = fileLower.count(term);
        if (bodyCount > 0 || fileCount > 0) {
            ++matched;
        }
        if (bodyCount > 0) {
            score += 0.95 + qMin(1.25, 0.16 * static_cast<double>(bodyCount - 1));
        }
        if (fileCount > 0) {
            score += 0.55 + qMin(0.75, 0.18 * static_cast<double>(fileCount - 1));
        }
    }

    const double coverage = static_cast<double>(matched) / static_cast<double>(terms.size());
    score += coverage * 1.8;

    const QString queryLower = query.toLower().trimmed();
    if (!queryLower.isEmpty() && lower.contains(queryLower)) {
        score += 1.2;
    }
    if (!queryLower.isEmpty() && fileLower.contains(queryLower)) {
        score += 0.75;
    }

    return score;
}

double roleBias(RetrievalIntent intent, const QString &role, const QStringList &preferredRoles)
{
    double bias = 0.0;
    if (intent == RetrievalIntent::DocumentGeneration) {
        if (role == QStringLiteral("scenario")) bias += 0.55;
        else if (role == QStringLiteral("procedure")) bias += 0.48;
        else if (role == QStringLiteral("reference")) bias += 0.24;
        else if (role == QStringLiteral("config")) bias += 0.08;
    } else if (intent == RetrievalIntent::Troubleshooting) {
        if (role == QStringLiteral("log")) bias += 0.55;
        else if (role == QStringLiteral("config")) bias += 0.46;
        else if (role == QStringLiteral("procedure")) bias += 0.22;
        else if (role == QStringLiteral("reference")) bias += 0.12;
    } else if (intent == RetrievalIntent::Architecture) {
        if (role == QStringLiteral("scenario")) bias += 0.58;
        else if (role == QStringLiteral("reference")) bias += 0.30;
        else if (role == QStringLiteral("procedure")) bias += 0.15;
    } else if (intent == RetrievalIntent::Implementation) {
        if (role == QStringLiteral("procedure")) bias += 0.55;
        else if (role == QStringLiteral("config")) bias += 0.32;
        else if (role == QStringLiteral("reference")) bias += 0.24;
        else if (role == QStringLiteral("scenario")) bias += 0.18;
    } else {
        if (role == QStringLiteral("procedure") || role == QStringLiteral("reference")) {
            bias += 0.10;
        }
    }

    for (const QString &preferred : preferredRoles) {
        if (role == preferred.trimmed().toLower()) {
            bias += 0.28;
            break;
        }
    }

    return bias;
}

QString makeExcerpt(const QString &text, const QStringList &terms)
{
    const QString normalized = normalizeBlockText(text);
    if (normalized.isEmpty()) {
        return QString();
    }

    const QString lower = normalized.toLower();
    QVector<int> matchIndexes;
    matchIndexes.reserve(terms.size());
    for (const QString &term : terms) {
        const QString trimmed = term.trimmed().toLower();
        if (trimmed.isEmpty()) {
            continue;
        }
        const int idx = lower.indexOf(trimmed);
        if (idx >= 0) {
            matchIndexes.push_back(idx);
        }
    }

    if (matchIndexes.isEmpty()) {
        return compactPreviewText(normalized, 520);
    }

    std::sort(matchIndexes.begin(), matchIndexes.end());
    const int anchor = matchIndexes.constFirst();
    int start = qMax(0, anchor - 140);
    int end = qMin(normalized.size(), anchor + 420);

    const int previousBreak = normalized.lastIndexOf(QLatin1Char('\n'), start);
    if (previousBreak >= 0 && start - previousBreak <= 120) {
        start = previousBreak + 1;
    }
    const int nextBreak = normalized.indexOf(QLatin1Char('\n'), end);
    if (nextBreak >= 0 && nextBreak - end <= 180) {
        end = nextBreak;
    }

    QString excerpt = normalized.mid(start, end - start).trimmed();
    if (matchIndexes.size() >= 2) {
        const int tailAnchor = matchIndexes.constLast();
        if (tailAnchor - anchor > 280) {
            int tailStart = qMax(0, tailAnchor - 110);
            int tailEnd = qMin(normalized.size(), tailAnchor + 260);
            const int tailPreviousBreak = normalized.lastIndexOf(QLatin1Char('\n'), tailStart);
            if (tailPreviousBreak >= 0 && tailStart - tailPreviousBreak <= 120) {
                tailStart = tailPreviousBreak + 1;
            }
            const int tailNextBreak = normalized.indexOf(QLatin1Char('\n'), tailEnd);
            if (tailNextBreak >= 0 && tailNextBreak - tailEnd <= 180) {
                tailEnd = tailNextBreak;
            }
            const QString tailExcerpt = normalized.mid(tailStart, tailEnd - tailStart).trimmed();
            if (!tailExcerpt.isEmpty() && tailExcerpt != excerpt) {
                excerpt += QStringLiteral("\n[... matching content later in this chunk ...]\n") + tailExcerpt;
            }
        }
    }

    return compactPreviewText(excerpt, 720);
}

int chunkActionabilityScore(const QString &text)
{
    const QString normalized = normalizeBlockText(text);
    if (normalized.isEmpty()) {
        return 0;
    }

    const QString lower = normalized.toLower();
    int score = 0;

    if (normalized.contains(QStringLiteral("```"))) {
        score += 6;
    }
    if (normalized.contains(QRegularExpression(
                QStringLiteral(R"((?:^|\n)\s*(?:\$|#|sudo\s+|kubectl\s+|helm\s+|docker\s+|podman\s+|ansible\s+|openstack\s+|systemctl\s+|nmcli\s+|ip\s+link)\b)"),
                QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption))) {
        score += 7;
    }
    if (lower.contains(QStringLiteral("warning")) || lower.contains(QStringLiteral("caution")) || lower.contains(QStringLiteral("important"))) {
        score += 5;
    }
    if (lower.contains(QStringLiteral("prerequisite")) || lower.contains(QStringLiteral("requirements")) || lower.contains(QStringLiteral("before you begin"))) {
        score += 4;
    }
    if (lower.contains(QStringLiteral("example")) || lower.contains(QStringLiteral("sample")) || lower.contains(QStringLiteral("template"))) {
        score += 3;
    }
    if (lower.contains(QStringLiteral("yaml")) || lower.contains(QStringLiteral("json")) || lower.contains(QStringLiteral("config")) || lower.contains(QStringLiteral("ini"))) {
        score += 4;
    }
    if (normalized.contains(QRegularExpression(QStringLiteral(R"((?:^|\n)\s*[-*]\s+)"), QRegularExpression::MultilineOption))) {
        score += 2;
    }
    if (normalized.contains(QRegularExpression(QStringLiteral(R"((?:^|\n)\s*\d+[.)]\s+)"), QRegularExpression::MultilineOption))) {
        score += 3;
    }
    if (normalized.contains(QRegularExpression(QStringLiteral(R"((?:^|\n)\s*[A-Za-z0-9_.-]+\s*:\s+\S+)"), QRegularExpression::MultilineOption))) {
        score += 2;
    }
    if (normalized.contains(QLatin1Char('{')) || normalized.contains(QLatin1Char('=')) || normalized.contains(QStringLiteral("->"))) {
        score += 1;
    }

    return score;
}

int headingLikeLineCount(const QString &text)
{
    int count = 0;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (isHeadingLikeLine(line.trimmed())) {
            ++count;
        }
    }
    return count;
}

int splitHeadingPairCount(const QString &text)
{
    static const QRegularExpression splitHeadingExpression(
            QStringLiteral(R"((?:^|\n)\s*\d+(?:\.\d+){0,3}[.)]?\s*\n\s*[A-Z][^\n]{3,140})"),
            QRegularExpression::MultilineOption);

    int count = 0;
    QRegularExpressionMatchIterator iterator = splitHeadingExpression.globalMatch(text);
    while (iterator.hasNext()) {
        iterator.next();
        ++count;
    }
    return count;
}

bool chunkLooksLikeStructure(const QString &text)
{
    const QString lower = text.toLower();
    if (lower.contains(QStringLiteral("table of contents"))
            || lower.contains(QStringLiteral("contents"))
            || lower.contains(QStringLiteral("document version history"))
            || lower.contains(QStringLiteral("overview"))) {
        return true;
    }

    return headingLikeLineCount(text) >= 3 || splitHeadingPairCount(text) >= 2;
}

QString matchReason(double lexical, double semantic, const QString &role, bool neuralSemantic)
{
    QStringList reasons;
    if (lexical >= 1.8) {
        reasons << QStringLiteral("strong lexical overlap");
    } else if (lexical > 0.0) {
        reasons << QStringLiteral("keyword overlap");
    }
    if (semantic >= 0.65) {
        reasons << (neuralSemantic
                    ? QStringLiteral("strong embedding similarity")
                    : QStringLiteral("strong surface-form vector similarity"));
    } else if (semantic >= 0.38) {
        reasons << (neuralSemantic
                    ? QStringLiteral("embedding similarity")
                    : QStringLiteral("surface-form vector similarity"));
    }
    if (!role.trimmed().isEmpty()) {
        reasons << QStringLiteral("role=%1").arg(role);
    }
    return reasons.join(QStringLiteral(", "));
}
