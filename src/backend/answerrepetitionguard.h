#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// Watches the *visible* answer stream for a degenerate repetition loop -- e.g. a local
// model getting stuck regenerating the same markdown table row dozens of times.
// Ollama's num_predict is left uncapped by design (so normal long answers aren't
// truncated), so nothing else stops a stuck model short of the context window filling
// up; this guard flags the pattern as soon as it's unambiguous so ChatController can
// cut generation off instead of burning through the rest of the context.
//
// Line-based rather than char-based: a stuck model typically repeats a whole line
// (a table row, a bullet, a command) rather than a short substring, so comparing
// normalized completed lines catches the pattern with very few false positives.
class AnswerRepetitionGuard {
public:
    void reset();

    // Feeds one streamed delta chunk into the guard's line buffer. Returns true the
    // moment a repetition loop is first detected; after that it keeps returning false
    // (call is a no-op) until the next reset(), matching the existing "trigger once
    // per generation" behavior.
    bool observeDelta(const QString &deltaText);

    QString lastLineNormalized() const { return m_lastLineNormalized; }
    int repeatStreak() const { return m_repeatStreak; }
    bool triggered() const { return m_triggered; }

    // Lowercases, strips non-alphanumeric runs, collapses whitespace, and caps length
    // so near-identical lines (differing only in punctuation/case/table borders)
    // compare equal. Exposed so callers can normalize a line the same way outside of
    // observeDelta (e.g. when trimming the trailing repeated run from the final answer).
    static QString normalizeLine(const QString &text);

private:
    QString m_lineBuffer;
    QString m_lastLineNormalized;
    int m_repeatStreak = 0;
    QHash<QString, int> m_lineFrequency;
    QStringList m_recentLinesNormalized;
    bool m_triggered = false;
};
