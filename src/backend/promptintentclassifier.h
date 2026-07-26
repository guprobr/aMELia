#pragma once

#include <QString>
#include <QStringList>

// Heuristic keyword classification of a user prompt, used by ChatController to pick
// retrieval strategy (normal RAG vs. document-study vs. exact-extraction), decide
// whether to force an outline-first pass, and detect "rewrite/expand this pasted text"
// requests. These are deliberately simple substring checks, not a learned classifier —
// see the aMELia architecture report for the tradeoffs (English-centric, no
// Portuguese-language coverage yet).

bool containsAny(const QString &text, const QStringList &needles);

// MOP/runbook/playbook/guide-shaped requests that should get an outline-first pass
// before the full answer.
bool isStructuredDocumentRequest(const QString &prompt);

// "extract all / list all / verbatim / preserve indentation"-style requests that want
// ordered source windows instead of a summarized answer.
bool looksLikeExactExtractionPrompt(const QString &prompt);

// Broader "summarize/teach/cover this document" requests, a superset of
// looksLikeExactExtractionPrompt, that should get the document-study retrieval path
// (section coverage, distributed sampling) instead of top-k chunk retrieval.
bool looksLikeDocumentStudyPrompt(const QString &prompt);

struct TransformPromptSpec {
    bool active = false;
    QString instruction;
    QString source;
};

// Detects a prompt shaped like "<short instruction>\n\n<long pasted material>" (e.g.
// "expand this into a tutorial:\n\n<pasted doc>") so the pasted material can be framed
// as SOURCE_MATERIAL to transform rather than as a claim to fact-check against itself.
TransformPromptSpec detectTransformPrompt(const QString &prompt);
