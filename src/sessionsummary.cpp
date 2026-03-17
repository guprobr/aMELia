#include "sessionsummary.h"

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
    QStringList lines;

    if (!previousSummary.trimmed().isEmpty()) {
        lines << QStringLiteral("Previous summary: %1").arg(compact(previousSummary, 280));
    }

    QStringList recentUserRequests;
    QStringList recentAssistantConclusions;

    const int start = qMax(0, history.size() - maxMessages);
    for (int i = start; i < history.size(); ++i) {
        const SummaryMessage &message = history.at(i);
        if (message.role == QStringLiteral("user")) {
            recentUserRequests << compact(message.content, 220);
        } else if (message.role == QStringLiteral("assistant")) {
            recentAssistantConclusions << compact(message.content, 260);
        }
    }

    if (!recentUserRequests.isEmpty()) {
        lines << QStringLiteral("Recent user goals:");
        for (const QString &item : recentUserRequests.mid(qMax(0, recentUserRequests.size() - 3))) {
            lines << QStringLiteral("- %1").arg(item);
        }
    }

    if (!recentAssistantConclusions.isEmpty()) {
        lines << QStringLiteral("Recent assistant conclusions:");
        for (const QString &item : recentAssistantConclusions.mid(qMax(0, recentAssistantConclusions.size() - 2))) {
            lines << QStringLiteral("- %1").arg(item);
        }
    }

    if (lines.isEmpty()) {
        return QStringLiteral("No persisted summary yet.");
    }

    return lines.join(QStringLiteral("\n"));
}
