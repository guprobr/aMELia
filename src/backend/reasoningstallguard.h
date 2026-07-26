#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// Watches the *hidden* reasoning/thinking stream before any visible answer token has
// arrived, and flags when the backend looks stuck there -- either repeating the same
// note over and over, cycling through a small set of notes, or just running far too
// long. Distinct from AnswerRepetitionGuard, which watches the *visible* answer stream
// once output has actually started.
//
// Unlike a stuck visible answer (which the app can safely truncate and keep), a model
// that never gets past hidden reasoning has produced nothing usable yet, so the
// recovery here is different too: ChatController retries the whole request once with
// thinking disabled instead of truncating.
class ReasoningStallGuard {
public:
    void reset();

    struct Verdict {
        bool triggered = false;
        QString reason;
    };

    // Feeds one hidden-reasoning note into the guard. elapsedSinceRequestMs is the
    // caller's "ms since this request started" (request timing lives on
    // ChatController, not here, since it's shared with non-reasoning diagnostics).
    Verdict observeNote(const QString &text, qint64 elapsedSinceRequestMs);

    int charsBeforeAnswer() const { return m_charsBeforeAnswer; }

private:
    static QString normalizeNote(const QString &text);
    QString buildEvidence() const;

    QString m_lastNormalized;
    QStringList m_recentNormalized;
    QHash<QString, int> m_frequency;
    qint64 m_firstNoteMs = 0;
    int m_charsBeforeAnswer = 0;
    int m_repeatStreak = 0;
};
