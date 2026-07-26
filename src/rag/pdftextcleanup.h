#pragma once

#include <QString>

#include <atomic>

// Detects and strips headers/footers that repeat across most pages of a PDF (e.g. a
// running "Company Confidential" header or page-number footer), which would otherwise
// pollute every chunk with boilerplate rather than content. Returns text unchanged if
// there are fewer than 3 pages (not enough signal to tell "repeated boilerplate" apart
// from "coincidentally similar content").
QString stripRepeatedPdfBoilerplate(const QString &text, const std::atomic_bool *cancelRequested = nullptr);

// Full PDF-specific cleanup pipeline: control-char/line-ending normalization,
// boilerplate stripping, and splicing a "[[PAGE N]]" marker before each non-empty
// page so downstream chunking/outline extraction can reason about page boundaries.
QString cleanedPdfText(QString text, const std::atomic_bool *cancelRequested = nullptr);
