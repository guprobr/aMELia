#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>

// True for a "[[PAGE N]]" marker line spliced into extracted PDF/DOCX text at
// page/paragraph-group boundaries (see readTextFile). Shared with the document
// outline extraction code, which also needs to skip these when scanning for
// headings.
bool isPageMarkerLine(const QString &trimmed);

// True for a markdown-style heading (# ...) or a short title/numbered-heading-shaped
// line ("3.2 Configuration", "Overview:"). Shared with document outline extraction.
bool isHeadingLikeLine(const QString &trimmed);

// True if the last non-marker line of text reads as a procedural lead-in ("Run the
// following:", "3. Verify that...") that a following command/output block belongs to.
bool blockEndsWithProceduralLead(const QString &text);

// True if text contains a bullet or code-like line anywhere, used alongside
// blockEndsWithProceduralLead to tell "the block ends with an instruction that expects
// more" apart from "the block already contains the structured content it was leading
// into".
bool blockContainsStructuredContent(const QString &text);

// Collapses blank lines and trims trailing whitespace/ends. The baseline
// normalization applied to every chunk/block of text after it's assembled.
QString normalizeBlockText(const QString &text);

// Truncates text to a natural-looking preview under maxChars (ellipsis-terminated),
// after normalizeBlockText.
QString compactPreviewText(QString text, int maxChars);

// Target/minimum/hard char budgets and overlap for one file's chunks, tuned by source
// type (code/config/log/doc), file size, and whether semantic (embedding-driven)
// chunking is available for this file.
struct ChunkingProfile {
    int targetChunkChars = 1400;
    int minimumChunkChars = 650;
    int hardChunkChars = 1800;
    int overlapChars = 140;
    QString label;
};

ChunkingProfile chooseChunkingProfile(const QString &sourceType,
                                     qint64 fileSizeBytes,
                                     bool semanticReady,
                                     int blockCount);

// Splits raw extracted text into atomic blocks along natural boundaries (headings,
// fenced code, bullet/procedural groups, blank-line paragraph breaks) before semantic
// chunking merges them back into retrieval-sized chunks. This is the structural pass;
// buildSemanticChunksFromBlocks is the merging pass.
QVector<QString> buildSemanticBlocks(const QString &text, const std::atomic_bool *cancelRequested = nullptr);

struct SemanticChunk {
    QString text;
    // Sum (not average/unit-normalized) of the embeddings of every atomic block
    // folded into this chunk. EmbeddingClient::cosineSimilarity normalizes both
    // operands internally, so a raw sum is directionally equivalent to the mean
    // and — importantly — composes correctly when two chunks are later merged
    // (sum(A) + sum(B) == sum(A union B)).
    QVector<float> embedding;
};

// Walks the atomic blocks in document order and only cuts a chunk boundary where the
// block actually being appended is semantically dissimilar (embedding cosine
// similarity) from the chunk built up so far — instead of purely on character counts.
// Character budgets (profile.targetChunkChars / hardChunkChars) remain a safety valve
// so a long run of on-topic content still gets split into retrievable pieces.
// blockEmbeddings may come from the neural backend or the local hash fallback
// (EmbeddingClient degrades automatically); either way this is a real similarity
// signal rather than a regex guess about how a line looks. Pass an empty
// blockEmbeddings (or one that doesn't match blocks.size()) to fall back to a purely
// char-budget-driven split when semantic chunking is unavailable.
QVector<SemanticChunk> buildSemanticChunksFromBlocks(const QVector<QString> &blocks,
                                                     const QVector<QVector<float>> &blockEmbeddings,
                                                     const ChunkingProfile &profile,
                                                     const std::atomic_bool *cancelRequested = nullptr);

// Filters out chunks too short/low-content to be worth indexing (e.g. a lone
// "| --- | --- |" table rule), while still keeping short-but-structured chunks
// (YAML/JSON fragments, short code lines) that carry real signal despite their size.
bool shouldKeepChunkText(const QString &text);
