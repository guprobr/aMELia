#pragma once

#include <QString>
#include <QVector>

struct LlmChatMessage;

QString nowIso();
qint64 nowMs();

// Trims text to maxChars, keeping a head+tail slice (with a "[... budget-trimmed ...]"
// marker in between) instead of just truncating the end, so budget-constrained prompt
// sections don't lose their concluding content.
QString trimForBudget(const QString &text, int maxChars);

QString shortSha1(const QString &text);

int countMarker(const QString &text, const QString &marker);

// One-line diagnostic summary of a prompt section (char count, hash, and counts of the
// structural markers ChatController splices into LOCAL_CONTEXT), logged instead of the
// full text so large payloads don't flood the diagnostics view.
QString summarizePromptSectionMarkers(const QString &text);

QString summarizeMessagePayload(const QVector<LlmChatMessage> &messages);

// Normalizes text for prompt-section deduplication: strips control tags the model
// might echo back (<END>, <think>, <amelia_thinking>), lowercases, collapses
// whitespace, and caps length so near-duplicate sections compare equal.
QString normalizePromptDedupKey(QString text);

int longestCommonSubstringLength(const QString &left, const QString &right, int cap = 1200);
