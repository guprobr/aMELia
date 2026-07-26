#pragma once

#include <QString>

// Reads a .docx file's body text in-process: opens the file as a zip archive
// (libzip) and linearizes word/document.xml (QXmlStreamReader) back to plain
// text, so no LibreOffice/pandoc/antiword subprocess is needed. Only
// word/document.xml is read -- headers, footers, footnotes, comments, text
// boxes, and embedded objects are not captured.
//
// On success, *text holds the cleaned body text (already passed through
// cleanedText()) and *extractor is set to "docx:xml-native". On failure,
// *extractor is set to "docx:load-failed" and false is returned.
bool readDocxFile(const QString &path, QString *text, QString *extractor);
