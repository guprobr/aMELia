#include "core/memorymanager.h"

#include <algorithm>
#include "core/storagemanager.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {
QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString compact(const QString &text, int maxLen)
{
    QString result = text.simplified();
    if (result.size() > maxLen) {
        result = result.left(maxLen - 3).trimmed() + QStringLiteral("...");
    }
    return result;
}

QString stripLeadingDirective(const QString &text, const QStringList &patterns)
{
    QString candidate = text.trimmed();
    for (const QString &pattern : patterns) {
        const QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = regex.match(candidate);
        if (match.hasMatch()) {
            candidate = candidate.mid(match.capturedLength()).trimmed();
            break;
        }
    }
    return candidate;
}

QString describeMemoryForUi(const MemoryRecord &memory)
{
    const QString category = memory.category.trimmed().isEmpty() ? QStringLiteral("general") : memory.category.trimmed();
    const QString key = memory.key.trimmed();
    const QString valuePreview = compact(memory.value, 96);

    QString description = QStringLiteral("Stored as a %1 memory").arg(category);
    if (!key.isEmpty()) {
        description += QStringLiteral(" under key '%1'").arg(key);
    }
    description += QStringLiteral(" so Amelia can reuse it when later prompts match this topic.");

    if (!valuePreview.isEmpty()) {
        description += QStringLiteral(" Value preview: %1").arg(valuePreview);
    }

    return description;
}
}

MemoryManager::MemoryManager(StorageManager *storage)
    : m_storage(storage)
{
}

void MemoryManager::setStorage(StorageManager *storage)
{
    m_storage = storage;
}

QVector<MemoryRecord> MemoryManager::loadAll(QString *errorMessage) const
{
    if (m_storage == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Memory storage is not configured.");
        }
        return {};
    }
    return m_storage->loadMemories(errorMessage);
}

int MemoryManager::scoreMemory(const MemoryRecord &memory, const QStringList &terms)
{
    if (terms.isEmpty()) {
        return memory.pinned ? 1 : 0;
    }

    const QString key = memory.key.toLower();
    const QString value = memory.value.toLower();
    const QString category = memory.category.toLower();

    int score = 0;
    QSet<QString> matchedTerms;

    for (const QString &term : terms) {
        if (term.isEmpty() || matchedTerms.contains(term)) {
            continue;
        }

        bool matched = false;

        if (key == term) {
            score += 40;
            matched = true;
        } else if (key.contains(term)) {
            score += 24;
            matched = true;
        }

        if (category == term) {
            score += 20;
            matched = true;
        } else if (category.contains(term)) {
            score += 12;
            matched = true;
        }

        if (value.contains(term)) {
            score += 10;
            matched = true;
        }

        if (matched) {
            matchedTerms.insert(term);
        }
    }

    if (!matchedTerms.isEmpty()) {
        score += matchedTerms.size() * 3;
    }

    if (memory.pinned) {
        score += 8;
    }

    if (memory.confidence >= 0.95) {
        score += 4;
    } else if (memory.confidence >= 0.80) {
        score += 2;
    }

    return score;
}

QVector<MemoryRecord> MemoryManager::findRelevant(const QString &query, int limit) const
{
    QVector<MemoryRecord> memories = loadAll(nullptr);
    if (memories.isEmpty()) {
        return {};
    }

    QString normalized = query.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral(" "));
    const QStringList rawTerms = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    // Filter out very short tokens (e.g. "api", "pod", "log") that would
    // spuriously match almost every memory record and dilute relevance ranking.
    QStringList terms;
    for (const QString &t : rawTerms) {
        if (t.size() >= 4) {
            terms << t;
        }
    }

    struct ScoredMemory {
        MemoryRecord memory;
        int score = 0;
    };

    QVector<ScoredMemory> scored;
    for (const MemoryRecord &memory : memories) {
        const int score = scoreMemory(memory, terms);
        if (score > 0 || memory.pinned) {
            scored.push_back({memory, memory.pinned ? score + 50 : score});
        }
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredMemory &a, const ScoredMemory &b) {
        return a.score > b.score;
    });

    QVector<MemoryRecord> relevant;
    for (const ScoredMemory &item : scored) {
        relevant.push_back(item.memory);
        if (relevant.size() >= limit) {
            break;
        }
    }
    return relevant;
}

QVector<MemoryRecord> MemoryManager::findRelevantForPrompt(const QString &query, int limit) const
{
    const QVector<MemoryRecord> relevant = findRelevant(query, limit * 2);
    QVector<MemoryRecord> filtered;
    filtered.reserve(qMin(relevant.size(), limit));

    for (const MemoryRecord &memory : relevant) {
        if (!isPromptSafeMemory(memory, query)) {
            continue;
        }
        filtered.push_back(memory);
        if (filtered.size() >= limit) {
            break;
        }
    }

    return filtered;
}

QString MemoryManager::formatForPrompt(const QVector<MemoryRecord> &memories) const
{
    if (memories.isEmpty()) {
        return QString();
    }

    QStringList lines;
    for (const MemoryRecord &memory : memories) {
        lines << QStringLiteral("[%1] %2 = %3")
                     .arg(memory.category,
                          memory.key.isEmpty() ? QStringLiteral("note") : memory.key,
                          memory.value);
    }
    return lines.join(QStringLiteral("\n"));
}

QString MemoryManager::formatForUiJson(const QVector<MemoryRecord> &memories) const
{
    QJsonArray array;
    for (const MemoryRecord &memory : memories) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), memory.id);
        obj.insert(QStringLiteral("category"), memory.category);
        obj.insert(QStringLiteral("key"), memory.key);
        obj.insert(QStringLiteral("value"), memory.value);
        obj.insert(QStringLiteral("updatedAt"), memory.updatedAt);
        obj.insert(QStringLiteral("createdAt"), memory.createdAt);
        obj.insert(QStringLiteral("confidence"), memory.confidence);
        obj.insert(QStringLiteral("pinned"), memory.pinned);
        obj.insert(QStringLiteral("description"), describeMemoryForUi(memory));
        array.push_back(obj);
    }

    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString MemoryManager::formatForUi(const QVector<MemoryRecord> &memories) const
{
    if (memories.isEmpty()) {
        return QStringLiteral("<none>");
    }

    QStringList lines;
    for (const MemoryRecord &memory : memories) {
        lines << QStringLiteral("Category: %1").arg(memory.category);
        lines << QStringLiteral("Key: %1").arg(memory.key.isEmpty() ? QStringLiteral("<note>") : memory.key);
        lines << QStringLiteral("Value: %1").arg(memory.value);
        lines << QStringLiteral("Pinned: %1").arg(memory.pinned ? QStringLiteral("yes") : QStringLiteral("no"));
        lines << QStringLiteral("Updated: %1").arg(memory.updatedAt);
        lines << QStringLiteral("");
    }
    return lines.join(QStringLiteral("\n"));
}

bool MemoryManager::saveExplicitNote(const QString &text, QString *savedDescription, QString *errorMessage) const
{
    if (m_storage == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Memory storage is not configured.");
        }
        return false;
    }

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Cannot save an empty memory.");
        }
        return false;
    }

    MemoryRecord memory;
    memory.key = normalizeKey(trimmed.left(42));
    memory.value = compact(trimmed, 500);
    memory.category = QStringLiteral("manual-note");
    memory.confidence = 1.0;
    memory.pinned = true;
    memory.createdAt = nowIso();
    memory.updatedAt = memory.createdAt;

    if (!m_storage->saveMemory(memory, errorMessage)) {
        return false;
    }

    if (savedDescription != nullptr) {
        *savedDescription = QStringLiteral("Saved memory note: %1").arg(compact(memory.value, 120));
    }
    return true;
}

bool MemoryManager::deleteMemoryById(const QString &memoryId, QString *deletedDescription, QString *errorMessage) const
{
    if (m_storage == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Memory storage is not configured.");
        }
        return false;
    }

    const QString trimmedId = memoryId.trimmed();
    if (trimmedId.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Memory id is empty.");
        }
        return false;
    }

    const QVector<MemoryRecord> memories = m_storage->loadMemories(nullptr);
    MemoryRecord deleted;
    bool found = false;
    for (const MemoryRecord &memory : memories) {
        if (memory.id.trimmed() == trimmedId) {
            deleted = memory;
            found = true;
            break;
        }
    }

    if (!found) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Memory not found.");
        }
        return false;
    }

    if (!m_storage->deleteMemoryById(trimmedId, errorMessage)) {
        return false;
    }

    if (deletedDescription != nullptr) {
        *deletedDescription = QStringLiteral("Deleted memory: %1").arg(compact(deleted.value, 120));
    }
    return true;
}

bool MemoryManager::clearAll(QString *errorMessage) const
{
    if (m_storage == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Memory storage is not configured.");
        }
        return false;
    }
    return m_storage->clearMemories(errorMessage);
}

QVector<MemoryRecord> MemoryManager::extractAutoMemories(const QString &userText) const
{
    QVector<MemoryRecord> items;
    const QString trimmed = userText.trimmed();
    if (trimmed.isEmpty()) {
        return items;
    }

    const QString lower = trimmed.toLower();
    const QString now = nowIso();

    const auto pushMemory = [&](const QString &rawValue,
                                const QString &category,
                                const QString &keyPrefix,
                                double confidence) {
        const QString normalizedValue = normalizedCandidateValue(rawValue);
        if (!isPromptSafeCandidate(normalizedValue)) {
            return;
        }

        MemoryRecord memory;
        memory.key = keyPrefix.isEmpty()
            ? normalizeKey(normalizedValue.left(48))
            : QStringLiteral("%1-%2").arg(keyPrefix, normalizeKey(normalizedValue.left(40)));
        memory.value = normalizedValue;
        memory.category = category;
        memory.confidence = confidence;
        memory.createdAt = now;
        memory.updatedAt = now;
        items.push_back(memory);
    };

    if (lower.startsWith(QStringLiteral("remember"))
        || lower.startsWith(QStringLiteral("please remember"))
        || lower.startsWith(QStringLiteral("note that"))) {
        const QString explicitValue = stripLeadingDirective(
            trimmed,
            {
                QStringLiteral(R"(^\s*(?:please\s+)?remember(?:\s+that)?\s*[:,\-]*\s*)"),
                QStringLiteral(R"(^\s*note\s+that\s*[:,\-]*\s*)")
            });
        pushMemory(explicitValue, QStringLiteral("explicit-memory"), QString(), 1.0);
    }

    if (lower.contains(QStringLiteral("from now on"))
        || lower.startsWith(QStringLiteral("i prefer"))
        || lower.startsWith(QStringLiteral("prefer "))
        || lower.startsWith(QStringLiteral("default to"))
        || lower.startsWith(QStringLiteral("always "))
        || lower.startsWith(QStringLiteral("please always "))) {
        const QString preferenceValue = stripLeadingDirective(
            trimmed,
            {
                QStringLiteral(R"(^\s*from\s+now\s+on\s*[:,\-]*\s*)"),
                QStringLiteral(R"(^\s*i\s+prefer\s*[:,\-]*\s*)"),
                QStringLiteral(R"(^\s*prefer\s*[:,\-]*\s*)"),
                QStringLiteral(R"(^\s*(?:please\s+)?always\s*[:,\-]*\s*)"),
                QStringLiteral(R"(^\s*default\s+to\s*[:,\-]*\s*)")
            });
        pushMemory(preferenceValue, QStringLiteral("preference"), QStringLiteral("preference"), 0.95);
    }

    const QRegularExpression platformRegex(
        QStringLiteral("\\b(?:platform|release)\\s+(\\d+\\.\\d+)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = platformRegex.match(trimmed);
    if (match.hasMatch()) {
        MemoryRecord memory;
        memory.key = QStringLiteral("platform_version");
        memory.value = match.captured(1);
        memory.category = QStringLiteral("environment");
        memory.confidence = 0.9;
        memory.createdAt = now;
        memory.updatedAt = now;
        items.push_back(memory);
    }

    return items;
}

QString MemoryManager::persistAutoMemories(const QString &userText, QString *errorMessage) const
{
    if (m_storage == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Memory storage is not configured.");
        }
        return QString();
    }

    const QVector<MemoryRecord> candidates = extractAutoMemories(userText);
    if (candidates.isEmpty()) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return QString();
    }

    QStringList saved;
    for (const MemoryRecord &candidate : candidates) {
        if (!m_storage->saveMemory(candidate, errorMessage)) {
            continue;
        }
        saved << QStringLiteral("%1=%2").arg(candidate.category, compact(candidate.value, 80));
    }

    if (errorMessage != nullptr && saved.isEmpty()) {
        *errorMessage = QStringLiteral("Failed to persist extracted memories.");
    }

    return saved.join(QStringLiteral(" | "));
}

QString MemoryManager::normalizeKey(const QString &text)
{
    QString key = text.toLower();
    key.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    key.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (key.isEmpty()) {
        key = QStringLiteral("memory-note");
    }
    return key.left(64);
}

QString MemoryManager::normalizedCandidateValue(QString text)
{
    text.replace(QRegularExpression(QStringLiteral(R"(^["'`\s]+|["'`\s]+$)")), QString());
    text.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    text = text.trimmed();

    if (!text.isEmpty() && !text.endsWith(QLatin1Char('.'))
        && !text.endsWith(QLatin1Char('!')) && !text.endsWith(QLatin1Char('?'))) {
        text += QLatin1Char('.');
    }

    return compact(text, 220);
}

bool MemoryManager::isPromptSafeCandidate(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    if (trimmed.size() < 3 || trimmed.size() > 220) {
        return false;
    }

    if (trimmed.contains(QLatin1Char('\n')) || trimmed.contains(QStringLiteral("```"))) {
        return false;
    }

    if (trimmed.endsWith(QLatin1Char('?'))) {
        return false;
    }

    const QString lower = trimmed.toLower();
    static const QStringList blockedPhrases = {
        QStringLiteral("assistant"),
        QStringLiteral("system prompt"),
        QStringLiteral("developer"),
        QStringLiteral("local_context"),
        QStringLiteral("external_context"),
        QStringLiteral("relevant_memories"),
        QStringLiteral("session_summary"),
        QStringLiteral("hidden reasoning"),
        QStringLiteral("chain of thought"),
        QStringLiteral("next prompt"),
        QStringLiteral("prompt loop"),
        QStringLiteral("memory loop"),
        QStringLiteral("user prompt"),
        QStringLiteral("instruction block"),
        QStringLiteral("role:"),
        QStringLiteral("assistant>")
    };
    for (const QString &phrase : blockedPhrases) {
        if (lower.contains(phrase)) {
            return false;
        }
    }

    const int punctuationCount = lower.count(QLatin1Char('{')) + lower.count(QLatin1Char('}'))
        + lower.count(QLatin1Char('[')) + lower.count(QLatin1Char(']'))
        + lower.count(QLatin1Char('<')) + lower.count(QLatin1Char('>'));
    if (punctuationCount >= 4) {
        return false;
    }

    const QStringList terms = lower.split(QRegularExpression(QStringLiteral("[^a-z0-9]+")), Qt::SkipEmptyParts);
    if (terms.size() > 32) {
        return false;
    }

    return true;
}

bool MemoryManager::isPromptSafeMemory(const MemoryRecord &memory, const QString &query)
{
    if (!isPromptSafeCandidate(memory.value)) {
        return false;
    }

    const QString normalizedQuery = normalizeKey(query);
    const QString normalizedValue = normalizeKey(memory.value);
    if (!normalizedQuery.isEmpty()
        && !normalizedValue.isEmpty()
        && (normalizedQuery == normalizedValue
            || normalizedQuery.contains(normalizedValue)
            || normalizedValue.contains(normalizedQuery))) {
        return false;
    }

    return true;
}
