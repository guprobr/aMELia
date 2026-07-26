#pragma once

#include <QHash>
#include <QString>

#include <atomic>

class QPdfDocument;

// Splits the raw (pre-cleanup) per-page text on form-feed page breaks and
// re-OCRs any page whose extracted word count is suspiciously low -- the
// signature of a scanned page, a photographed diagram, or a CLI screenshot
// with no embedded text layer, all of which the PDF text layer silently
// returns as blank or near-blank. Pages are rendered at 300dpi and OCR'd
// in-process via libtesseract (eng+por, falling back to eng-only if the
// Portuguese language pack isn't installed) -- no external subprocess.
//
// ocrCache is keyed by 0-based PDF page index so a given page is only ever
// rendered/OCR'd once per extraction pass; pagesOcrApplied is incremented by
// however many pages this call actually improved. If OCR tooling isn't
// available (tesseract failed to initialize, e.g. missing eng.traineddata),
// this is a no-op that returns rawExtractedText unchanged.
QString ocrAugmentLowContentPages(QPdfDocument *document,
                                  const QString &rawExtractedText,
                                  std::atomic_bool *cancelRequested,
                                  QHash<int, QString> *ocrCache,
                                  int *pagesOcrApplied);
