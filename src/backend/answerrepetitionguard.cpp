#include "backend/answerrepetitionguard.h"

#include <QRegularExpression>

void AnswerRepetitionGuard::reset()
{
    m_lineBuffer.clear();
    m_lastLineNormalized.clear();
    m_repeatStreak = 0;
    m_lineFrequency.clear();
    m_recentLinesNormalized.clear();
    m_triggered = false;
}

QString AnswerRepetitionGuard::normalizeLine(const QString &text)
{
    QString normalized = text.toLower();
    normalized.replace(QRegularExpression(QStringLiteral(R"([^a-z0-9]+)")), QStringLiteral(" "));
    normalized = normalized.simplified();
    if (normalized.size() > 160) {
        normalized.truncate(160);
    }
    return normalized;
}

bool AnswerRepetitionGuard::observeDelta(const QString &deltaText)
{
    constexpr int kRepeatStreakThreshold = 5;
    constexpr int kMinNormalizedLineChars = 6;

    if (m_triggered) {
        return false;
    }

    m_lineBuffer += deltaText;
    QStringList completedLines = m_lineBuffer.split(QLatin1Char('\n'));
    m_lineBuffer = completedLines.isEmpty() ? QString() : completedLines.takeLast();

    for (const QString &rawLine : std::as_const(completedLines)) {
        const QString trimmed = rawLine.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const QString normalized = normalizeLine(trimmed);
        if (normalized.size() < kMinNormalizedLineChars) {
            // Too short/generic (e.g. a lone "| --- | --- |" table rule) to be a
            // reliable loop signal on its own.
            continue;
        }

        if (normalized == m_lastLineNormalized) {
            ++m_repeatStreak;
        } else {
            m_repeatStreak = 1;
            m_lastLineNormalized = normalized;
        }

        m_lineFrequency[normalized] = m_lineFrequency.value(normalized) + 1;
        m_recentLinesNormalized.push_back(normalized);
        while (m_recentLinesNormalized.size() > 12) {
            m_recentLinesNormalized.removeFirst();
        }

        if (m_repeatStreak >= kRepeatStreakThreshold) {
            m_triggered = true;
            return true;
        }
    }

    return false;
}
