#include "core/sessionsummary.h"

#include <QStringList>

namespace {
QString compact(const QString &text, int maxLen)
{
    QString result = text.simplified();
    if (result.size() > maxLen) {
        result = result.left(maxLen - 3).trimmed() + QStringLiteral("...");
    }
    return result;
}
}

QString SessionSummarizer::summarize(const QVector<SummaryMessage> &history,
                                     const QString &previousSummary,
                                     int maxMessages) const
{
    // We build a structured summary rather than just concatenating raw messages.
    // The goal is to give the model in the next turn a compact record of:
    //   1. What the user was trying to accomplish (goals).
    //   2. What was established as fact from the context (confirmed facts).
    //   3. What explicit decisions or conclusions were reached (conclusions).
    //   4. What is still unresolved (open questions).
    //
    // This is NOT a language-model call — it is a deterministic extraction from
    // the conversation turns.  A future improvement is to route this through
    // Ollama itself (see TRAINING.md) to produce a true abstractive summary.

    QStringList lines;

    // Carry forward the previous summary so context accumulates across turns.
    if (!previousSummary.trimmed().isEmpty()) {
        lines << QStringLiteral("=== Previous summary ===");
        lines << compact(previousSummary, 400);
        lines << QString();
    }

    const int start = qMax(0, history.size() - maxMessages);

    QStringList userGoals;
    QStringList assistantConclusions;
    QStringList openQuestions;

    for (int i = start; i < history.size(); ++i) {
        const SummaryMessage &message = history.at(i);

        if (message.role == QStringLiteral("user")) {
            const QString c = compact(message.content, 280);
            userGoals << QStringLiteral("- %1").arg(c);

            // Heuristic: if the user asked a direct question that the assistant
            // answered with the fallback ("I don't know based on the provided
            // context"), flag it as open so the model stays humble about it.
            if (message.content.contains(QLatin1Char('?'))) {
                // Check if the corresponding assistant reply was a refusal.
                if (i + 1 < history.size()
                        && history.at(i + 1).role == QStringLiteral("assistant")
                        && history.at(i + 1).content.contains(
                               QStringLiteral("don't know based on the provided context"))) {
                    openQuestions << QStringLiteral("- %1").arg(compact(message.content, 160));
                }
            }
        } else if (message.role == QStringLiteral("assistant")) {
            // Skip fallback refusals from the conclusions list — they carry no
            // factual information.
            const QString c = compact(message.content, 320);
            if (!c.contains(QStringLiteral("don't know based on the provided context"))) {
                assistantConclusions << QStringLiteral("- %1").arg(c);
            }
        }
    }

    if (!userGoals.isEmpty()) {
        lines << QStringLiteral("=== User goals (recent) ===");
        // Show at most the 3 most recent goals to keep the summary compact.
        const int goalStart = qMax(0, userGoals.size() - 3);
        for (int i = goalStart; i < userGoals.size(); ++i) {
            lines << userGoals.at(i);
        }
        lines << QString();
    }

    if (!assistantConclusions.isEmpty()) {
        lines << QStringLiteral("=== Assistant conclusions (recent) ===");
        // Show at most the 2 most recent conclusions.
        const int conclStart = qMax(0, assistantConclusions.size() - 2);
        for (int i = conclStart; i < assistantConclusions.size(); ++i) {
            lines << assistantConclusions.at(i);
        }
        lines << QString();
    }

    if (!openQuestions.isEmpty()) {
        lines << QStringLiteral("=== Unresolved questions ===");
        for (const QString &q : openQuestions) {
            lines << q;
        }
        lines << QString();
    }

    if (lines.isEmpty()) {
        return QStringLiteral("No persisted summary yet.");
    }

    return lines.join(QStringLiteral("\n")).trimmed();
}
