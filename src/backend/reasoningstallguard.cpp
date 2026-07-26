#include "backend/reasoningstallguard.h"

#include "backend/prompttextutils.h"

#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

void ReasoningStallGuard::reset()
{
    m_firstNoteMs = 0;
    m_charsBeforeAnswer = 0;
    m_repeatStreak = 0;
    m_lastNormalized.clear();
    m_recentNormalized.clear();
    m_frequency.clear();
}

QString ReasoningStallGuard::normalizeNote(const QString &text)
{
    QString normalized = text.toLower();
    normalized.replace(QRegularExpression(QStringLiteral(R"([^a-z0-9]+)")), QStringLiteral(" "));
    normalized = normalized.simplified();
    if (normalized.size() > 160) {
        normalized.truncate(160);
    }
    return normalized;
}

QString ReasoningStallGuard::buildEvidence() const
{
    int dominantRepeatCount = 0;
    QString dominantSnippet;
    for (auto it = m_frequency.constBegin(); it != m_frequency.constEnd(); ++it) {
        if (it.value() > dominantRepeatCount) {
            dominantRepeatCount = it.value();
            dominantSnippet = it.key();
        }
    }

    QSet<QString> recentUnique;
    for (const QString &note : m_recentNormalized) {
        if (!note.isEmpty()) {
            recentUnique.insert(note);
        }
    }

    QStringList details;
    if (m_repeatStreak >= 3) {
        details << QStringLiteral("repeat streak=%1").arg(m_repeatStreak);
    }
    if (dominantRepeatCount >= 3 && !dominantSnippet.isEmpty()) {
        QString preview = dominantSnippet;
        if (preview.size() > 96) {
            preview = preview.left(93).trimmed() + QStringLiteral("...");
        }
        details << QStringLiteral("dominant note repeated %1x: \"%2\"").arg(dominantRepeatCount).arg(preview);
    }
    if (m_recentNormalized.size() >= 6) {
        details << QStringLiteral("recent unique notes=%1/%2").arg(recentUnique.size()).arg(m_recentNormalized.size());
    }

    if (details.isEmpty()) {
        return QStringLiteral("no clear repetition signature captured");
    }
    return details.join(QStringLiteral(" | "));
}

ReasoningStallGuard::Verdict ReasoningStallGuard::observeNote(const QString &text, qint64 elapsedSinceRequestMs)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    if (m_firstNoteMs <= 0) {
        m_firstNoteMs = nowMs();
    }

    m_charsBeforeAnswer += trimmed.size();

    const QString normalized = normalizeNote(trimmed);
    if (!normalized.isEmpty()) {
        if (normalized == m_lastNormalized) {
            ++m_repeatStreak;
        } else {
            m_repeatStreak = 1;
            m_lastNormalized = normalized;
        }

        m_frequency[normalized] = m_frequency.value(normalized) + 1;
        m_recentNormalized.push_back(normalized);
        while (m_recentNormalized.size() > 8) {
            m_recentNormalized.removeFirst();
        }
    }

    int dominantRepeatCount = 0;
    for (auto it = m_frequency.constBegin(); it != m_frequency.constEnd(); ++it) {
        dominantRepeatCount = qMax(dominantRepeatCount, it.value());
    }

    QSet<QString> recentUnique;
    for (const QString &note : m_recentNormalized) {
        if (!note.isEmpty()) {
            recentUnique.insert(note);
        }
    }

    const bool consecutiveRepeatLoop = m_repeatStreak >= 3 && m_charsBeforeAnswer >= 500;
    const bool dominantRepeatLoop = dominantRepeatCount >= 4 && m_charsBeforeAnswer >= 900;
    const bool lowDiversityLoop = m_recentNormalized.size() >= 6
            && recentUnique.size() <= 2
            && m_charsBeforeAnswer >= 900;
    const bool longStallDetected = elapsedSinceRequestMs >= 180000 && m_charsBeforeAnswer >= 4000;

    if (!consecutiveRepeatLoop && !dominantRepeatLoop && !lowDiversityLoop && !longStallDetected) {
        return {};
    }

    Verdict verdict;
    verdict.triggered = true;
    verdict.reason = (consecutiveRepeatLoop || dominantRepeatLoop || lowDiversityLoop)
            ? QStringLiteral("detected hidden reasoning repetition before any visible answer (%1)").arg(buildEvidence())
            : QStringLiteral("reasoning stream exceeded %1 ms and %2 chars before any visible answer")
                  .arg(elapsedSinceRequestMs)
                  .arg(m_charsBeforeAnswer);
    return verdict;
}
