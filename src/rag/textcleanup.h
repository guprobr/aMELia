#pragma once

#include <QString>

// Strips trailing spaces/tabs from every line without touching leading
// indentation, so code/YAML/config blocks keep their structure.
QString trimTrailingWhitespacePerLine(const QString &text);

// Caps runs of 4+ blank lines down to 2, so PDF/DOCX page-break artifacts don't
// balloon a chunk's char count with empty vertical space.
QString collapseExcessBlankLines(QString text);

// Baseline normalization applied to every extracted document before chunking:
// line-ending/NBSP/control-char normalization, tabs expanded to spaces, trailing
// whitespace and excess blank lines trimmed.
QString cleanedText(QString text);
