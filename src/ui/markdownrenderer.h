#pragma once

#include <QPalette>
#include <QString>
#include <QStringList>
#include <QVector>

class QTextDocument;

// One plain-text or fenced-code run of a transcript message, as split by
// splitTranscriptSegments. Code segments are rendered as <pre><code> blocks with a
// language badge and "Copy code" action instead of going through the markdown ->
// QTextDocument path (which would mangle indentation-sensitive content).
struct TranscriptSegment {
    bool isCode = false;
    QString language;
    QString text;
};

// Strips leading/trailing blank lines a fenced code block's content often carries
// right inside the fences, without touching interior blank lines.
QString trimCodeBlockFencePadding(QString code);

// Delegates to TranscriptFormatter::sanitizeRenderableMarkdown -- the shared
// model-output sanitization pass -- before this file's HTML rendering takes over.
QString normalizeRenderableMarkdown(const QString &text);

// Splits text on fenced code blocks (```...```) into alternating plain/code segments,
// preserving each fence's language tag.
QVector<TranscriptSegment> splitTranscriptSegments(const QString &text);

// Extracts and concatenates just the code-fence contents of a message (used by the
// "copy all code" transcript action), separated by a visual divider.
QString extractCodeBlocks(const QString &text);

// Pulls the innerHTML of <body> out of a QTextDocument::toHtml() dump, since Qt always
// wraps rendered markdown in a full HTML document (doctype/head/body) that would be
// redundant nested inside the transcript's own HTML structure.
QString bodyFragmentFromDocument(const QTextDocument &doc);

// Escapes any substring that looks like an HTML/XML tag (<foo>, </foo>) before handing
// text to QTextDocument::setMarkdown, so stray angle brackets in model output (e.g. a
// shell redirect "cmd < file" or a generic type "Vector<T>") don't get silently
// swallowed as unrecognized tags by Qt's markdown-to-HTML conversion.
QString escapeHtmlLikeTags(const QString &text);

// QTextDocument::setMarkdown output double-escapes entities that escapeHtmlLikeTags
// already escaped (e.g. "<" -> "&lt;" -> "&amp;lt;"); this undoes that second pass.
QString decodeDoubleEscapedHtmlEntities(QString html);

// Renders one markdown fragment (a non-code transcript segment) to theme-aware inline-
// styled HTML: code fences/inline code/blockquotes/tables/links all get explicit
// colors and spacing pulled from palette, since Qt's default QTextDocument markdown
// rendering doesn't adapt to the app's light/dark theme on its own.
QString markdownFragmentToHtml(const QString &markdown, const QPalette &palette);

// Renders one full transcript message (role prefix + body, with code segments as
// copyable blocks and a "Copy Answer"/"Convert to PDF" footer for assistant turns) to
// the HTML the transcript's WebEngine view displays. codeBlocks, if non-null, is
// appended with each code segment's text so the UI can wire up per-block "Copy code"
// links by index. forExport strips every interactive-only element (the selection
// checkbox, the footer links, the "Copy code" link) for use in PDF export, where none
// of those controls would do anything.
QString messageToRichHtml(const QString &role,
                          const QString &text,
                          QStringList *codeBlocks,
                          int answerIndex,
                          const QPalette &palette,
                          bool forExport = false);
