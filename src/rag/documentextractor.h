#pragma once

#include <QString>

#include <atomic>

// Extracts text from one file for indexing, dispatching by extension: PDF (native
// QPdfDocument text layer, OCR-augmented per-page for near-empty pages), DOCX (libzip
// + XML), or plain text (read + validated non-binary). On success *text holds the
// cleaned extracted text and *extractor identifies which path/outcome was used (e.g.
// "pdf:qtpdf-paged+ocr(3p)", "docx:xml-native", "direct", "binary-skipped",
// "read-failed", "canceled"); readTextFile returns false and *extractor still
// describes why on failure.
bool readTextFile(const QString &path, QString *text, QString *extractor, std::atomic_bool *cancelRequested);

// Human-readable explanation for why a source produced zero indexable chunks, derived
// from the *extractor tag readTextFile set and the resulting text/word counts. Shown
// in the Knowledge Base inventory so a zero-chunk source isn't just a silent gap.
QString fallbackZeroChunkReason(const QString &extractor, int textCharCount, int wordCount);
