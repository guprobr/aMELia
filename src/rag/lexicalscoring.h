#pragma once

#include <QString>
#include <QStringList>

#include "backend/outlineplanner.h"

// Lowercases, strips punctuation, drops stop words, and expands a handful of
// domain-specific synonyms/abbreviations (deploy<->deployment, k8s->kubernetes, ...)
// so a query and a chunk that use different but related terms still overlap.
QStringList queryTerms(const QString &query);

// Lexical relevance of one chunk against the query's terms: term coverage plus a bonus
// for repeated occurrences in the chunk body or filename, plus a bonus for the raw
// query string appearing verbatim.
double lexicalScoreChunk(const QString &text, const QString &fileName, const QStringList &terms, const QString &query);

// Score bonus for a chunk's retrieval role (log/config/procedure/scenario/reference)
// matching the query's classified intent (troubleshooting/architecture/implementation/
// document generation), plus an extra bonus if the role is explicitly preferred by the
// caller (e.g. a document-study pass pinned to "procedure" sections).
double roleBias(RetrievalIntent intent, const QString &role, const QStringList &preferredRoles);

// Builds a preview excerpt centered on the first query-term match (plus a second
// excerpt around a later match if it's far enough away to add new context), rather
// than just showing the start of the chunk.
QString makeExcerpt(const QString &text, const QStringList &terms);

// Heuristic score for how "actionable" a chunk is (code fences, shell/kubectl-style
// commands, warnings, prerequisites, YAML/JSON-shaped lines, lists) -- used to prefer
// concrete instructions over prose when both are otherwise equally relevant.
int chunkActionabilityScore(const QString &text);

int headingLikeLineCount(const QString &text);
int splitHeadingPairCount(const QString &text);

// True if a chunk looks like a table-of-contents / document-structure chunk rather
// than substantive content (used to deprioritize or specially label such chunks in
// retrieval results).
bool chunkLooksLikeStructure(const QString &text);

// Short human-readable explanation of why a chunk matched (lexical overlap strength,
// semantic similarity strength, role), shown in diagnostics/UI next to each hit.
QString matchReason(double lexical, double semantic, const QString &role, bool neuralSemantic);
