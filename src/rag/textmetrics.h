#pragma once

#include <QString>

// Counts letter/number runs as words (locale-aware via QChar::isLetterOrNumber),
// used both as an OCR trigger heuristic (pages with suspiciously few words are
// re-rendered and OCR'd) and for source/chunk metadata.
int countWordsInText(const QString &text);

int countLinesInText(const QString &text);
