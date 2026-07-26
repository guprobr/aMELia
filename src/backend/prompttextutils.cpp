#include "backend/prompttextutils.h"

#include "backend/llmclient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QStringList>

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString trimForBudget(const QString &text, int maxChars)
{
    const QString normalized = text.trimmed();
    if (maxChars <= 0 || normalized.size() <= maxChars) {
        return normalized;
    }

    const QString marker = QStringLiteral("\n[... budget-trimmed ...]\n");
    const int markerChars = marker.size();
    if (maxChars <= markerChars + 32) {
        return normalized.left(maxChars).trimmed();
    }

    const int remaining = qMax(0, maxChars - markerChars);
    const int headChars = qMax(0, static_cast<int>(remaining * 0.58));
    const int tailChars = qMax(0, remaining - headChars);
    const QString head = normalized.left(headChars).trimmed();
    const QString tail = normalized.right(tailChars).trimmed();
    if (tail.isEmpty() || head == tail) {
        return head;
    }
    return head + marker + tail;
}

QString shortSha1(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}

int countMarker(const QString &text, const QString &marker)
{
    if (text.isEmpty() || marker.isEmpty()) {
        return 0;
    }

    int count = 0;
    int position = 0;
    while ((position = text.indexOf(marker, position)) >= 0) {
        ++count;
        position += marker.size();
    }
    return count;
}

QString summarizePromptSectionMarkers(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return QStringLiteral("chars=0 | sha1=<empty>");
    }

    return QStringLiteral(
                   "chars=%1 | sha1=%2 | doc_packets=%3 | outline_maps=%4 | section_packets=%5 | full_docs=%6 | source_blocks=%7 | budget_trims=%8")
            .arg(text.size())
            .arg(shortSha1(text))
            .arg(countMarker(text, QStringLiteral("=== DOCUMENT_STUDY_PACKET:")))
            .arg(countMarker(text, QStringLiteral("DOCUMENT_OUTLINE_MAP:")))
            .arg(countMarker(text, QStringLiteral("SECTION_COVERAGE_PACKET:")))
            .arg(countMarker(text, QStringLiteral("FULL_DOCUMENT_TEXT:")))
            .arg(countMarker(text, QStringLiteral("--- Source:")))
            .arg(countMarker(text, QStringLiteral("[... budget-trimmed ...]")));
}

QString summarizeMessagePayload(const QVector<LlmChatMessage> &messages)
{
    if (messages.isEmpty()) {
        return QStringLiteral("messages=0 | payload_sha1=<empty>");
    }

    QByteArray payload;
    QStringList layout;
    layout.reserve(messages.size());
    int totalChars = 0;
    for (const LlmChatMessage &message : messages) {
        payload += message.role.toUtf8();
        payload += '\n';
        payload += message.content.toUtf8();
        payload += "\n---\n";
        layout << QStringLiteral("%1:%2").arg(message.role).arg(message.content.size());
        totalChars += message.content.size();
    }

    return QStringLiteral("messages=%1 | total_chars=%2 | payload_sha1=%3 | layout=%4")
            .arg(messages.size())
            .arg(totalChars)
            .arg(QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha1).toHex().left(12)))
            .arg(layout.join(QStringLiteral(", ")));
}

QString normalizePromptDedupKey(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QStringLiteral("<END>"), QString());
    text.replace(QStringLiteral("<think>"), QString());
    text.replace(QStringLiteral("</think>"), QString());
    text.replace(QStringLiteral("<amelia_thinking>"), QString());
    text.replace(QStringLiteral("</amelia_thinking>"), QString());
    text = text.simplified().toLower();
    if (text.size() > 240) {
        text = text.left(240);
    }
    return text;
}

int longestCommonSubstringLength(const QString &left, const QString &right, int cap)
{
    if (left.isEmpty() || right.isEmpty()) {
        return 0;
    }

    QString a = left.left(cap);
    QString b = right.left(cap);
    if (a.size() < b.size()) {
        qSwap(a, b);
    }

    QVector<int> previous(b.size() + 1, 0);
    QVector<int> current(b.size() + 1, 0);
    int best = 0;

    for (int i = 0; i < a.size(); ++i) {
        for (int j = 0; j < b.size(); ++j) {
            if (a.at(i) == b.at(j)) {
                current[j + 1] = previous[j] + 1;
                best = qMax(best, current[j + 1]);
            } else {
                current[j + 1] = 0;
            }
        }
        previous = current;
        current.fill(0);
    }

    return best;
}
