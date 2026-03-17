#include "memorymanager.h"

#include <algorithm>
#include "storagemanager.h"

#include <QDateTime>
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

QVector<MemoryRecord> MemoryManager::findRelevant(const QString &query, int limit) const
{
    QVector<MemoryRecord> memories = loadAll(nullptr);
    if (memories.isEmpty()) {
        return {};
    }

    QString normalized = query.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral(" "));
    const QStringList terms = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);

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

    if (lower.contains(QStringLiteral("remember ")) || lower.startsWith(QStringLiteral("remember that"))) {
        MemoryRecord memory;
        memory.key = normalizeKey(trimmed.left(48));
        memory.value = compact(trimmed, 400);
        memory.category = QStringLiteral("explicit-memory");
        memory.confidence = 1.0;
        memory.createdAt = now;
        memory.updatedAt = now;
        items.push_back(memory);
    }

    if (lower.contains(QStringLiteral("from now on"))
        || lower.contains(QStringLiteral("i prefer"))
        || lower.contains(QStringLiteral("prefer "))
        || lower.contains(QStringLiteral("always "))
        || lower.contains(QStringLiteral("default to"))) {
        MemoryRecord memory;
        memory.key = QStringLiteral("preference-%1").arg(normalizeKey(trimmed.left(40)));
        memory.value = compact(trimmed, 280);
        memory.category = QStringLiteral("preference");
        memory.confidence = 0.95;
        memory.createdAt = now;
        memory.updatedAt = now;
        items.push_back(memory);
    }

    const QRegularExpression platformRegex(QStringLiteral("\b(?:platform|release)\s+(\d+\.\d+)\b"), QRegularExpression::CaseInsensitiveOption);
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

int MemoryManager::scoreMemory(const MemoryRecord &memory, const QStringList &terms)
{
    const QString haystack = QStringLiteral("%1 %2 %3")
            .arg(memory.key.toLower(), memory.value.toLower(), memory.category.toLower());

    int score = 0;
    for (const QString &term : terms) {
        if (term.size() < 3) {
            continue;
        }
        if (haystack.contains(term)) {
            score += 10;
        }
    }
    return score;
}
