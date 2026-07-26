#pragma once

#include <QString>
#include <QStringList>

// True for a line shaped like a table-of-contents entry: a dotted leader ("Chapter
// 1 .......... 12") or a heading immediately followed by a trailing page number.
bool looksLikeContentsEntry(const QString &trimmed);

// Normalizes a heading/outline-entry line for deduplication: strips a trailing dotted
// page-number leader, collapses whitespace, lowercases.
QString normalizeOutlineKey(QString text);

// Scans text for lines that look like document structure (contents entries, markdown
// headings, split "number line\ntitle line" pairs, or known front-matter labels like
// "Table of Contents") and returns up to maxLines of them, deduplicated. Used to build
// a compact outline preview of a large document without including its full body.
QStringList extractDocumentOutlineLines(const QString &text, int maxLines);

// Trims text to maxChars keeping both a head and tail slice (head-weighted 62/38) with
// a "middle omitted" marker in between, for document-study previews where both the
// start and end of a section carry information.
QString balancedTrimForStudy(QString text, int maxChars);

// Strips a trailing table-of-contents page-number leader ("...... 42" or " 42") from
// a heading line.
QString stripTrailingOutlinePageNumber(QString text);

// True for a numbered top-level heading ("3 Overview", "3. Overview") as opposed to a
// sub-heading.
bool isTopLevelHeadingText(const QString &trimmed);

// True for a line that's just a top-level section number with no title on the same
// line ("3." or "3") -- the title is expected on the next non-empty line.
bool isTopLevelNumberOnlyLine(const QString &trimmed);

// Scans text for top-level section headings (including the "number-only line, title
// on next line" split form) and returns up to maxSections of them, deduplicated by
// normalizeOutlineKey. Coarser than extractDocumentOutlineLines -- used to build a
// document's major-section map rather than a full outline preview.
QStringList extractMajorSectionHeadings(const QString &text, int maxSections);
