#pragma once

#include <QString>
#include <QVector>

struct SummaryMessage {
    QString role;
    QString content;
};

class SessionSummarizer {
public:
    QString summarize(const QVector<SummaryMessage> &history,
                      const QString &previousSummary,
                      int maxMessages = 8) const;
};
