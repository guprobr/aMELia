#include "ui/markdownrenderer.h"

#include "core/transcriptformatter.h"
#include "ui/transcriptcolors.h"

#include <QRegularExpression>
#include <QTextDocument>

namespace {

struct MathSpan {
    QString tex;
    bool display = false;
};

// A private-use-area sentinel: inert to CommonMark's inline parsing (no emphasis/
// autolink meaning) and confirmed to round-trip unchanged through
// QTextDocument::setMarkdown()/toHtml(), so it can stand in for a math span while
// Qt's markdown-to-HTML conversion runs.
QString mathPlaceholder(int index)
{
    return QString(QChar(0xE000)) + QString::number(index) + QString(QChar(0xE001));
}

// Pulls $$...$$, \[...\], \(...\), and $...$ spans out of markdown text (skipping
// ones inside inline code) before it goes through QTextDocument::setMarkdown(),
// which would otherwise treat underscores inside math (e.g. "\sum_{i=1}^{n} x_i")
// as CommonMark emphasis markers and mangle the LaTeX source.
QString extractMathSpans(const QString &text, QVector<MathSpan> *mathSpans)
{
    QString out;
    out.reserve(text.size());
    bool inInlineCode = false;

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);

        if (ch == QLatin1Char('`')) {
            inInlineCode = !inInlineCode;
            out += ch;
            continue;
        }

        if (!inInlineCode) {
            QString closer;
            bool display = false;
            int openLen = 0;
            if (text.mid(i, 2) == QStringLiteral("$$")) {
                closer = QStringLiteral("$$");
                display = true;
                openLen = 2;
            } else if (text.mid(i, 2) == QStringLiteral("\\[")) {
                closer = QStringLiteral("\\]");
                display = true;
                openLen = 2;
            } else if (text.mid(i, 2) == QStringLiteral("\\(")) {
                closer = QStringLiteral("\\)");
                display = false;
                openLen = 2;
            }

            if (openLen > 0) {
                const int closeIndex = text.indexOf(closer, i + openLen);
                if (closeIndex >= 0) {
                    MathSpan span;
                    span.tex = text.mid(i + openLen, closeIndex - (i + openLen));
                    span.display = display;
                    out += mathPlaceholder(mathSpans->size());
                    mathSpans->push_back(span);
                    i = closeIndex + closer.size() - 1;
                    continue;
                }
            } else if (ch == QLatin1Char('$') && i + 1 < text.size() && !text.at(i + 1).isSpace()) {
                // Single-dollar inline math, using Pandoc's heuristic for telling it
                // apart from currency: the opening $ needs a non-space character right
                // after it (checked above); the matching closing $ needs a non-space
                // character right before it and must not be followed immediately by a
                // digit, and the span can't cross a blank line. This is what lets
                // "$a \neq 0$" match while "$20,000 and $30,000" doesn't.
                int j = i + 1;
                int closeIndex = -1;
                while (j < text.size()) {
                    if (text.at(j) == QLatin1Char('\n') && j + 1 < text.size() && text.at(j + 1) == QLatin1Char('\n')) {
                        break;
                    }
                    if (text.at(j) == QLatin1Char('$') && !text.at(j - 1).isSpace()) {
                        const bool followedByDigit = (j + 1 < text.size()) && text.at(j + 1).isDigit();
                        if (!followedByDigit) {
                            closeIndex = j;
                        }
                        break;
                    }
                    ++j;
                }
                if (closeIndex >= 0) {
                    MathSpan span;
                    span.tex = text.mid(i + 1, closeIndex - (i + 1));
                    span.display = false;
                    out += mathPlaceholder(mathSpans->size());
                    mathSpans->push_back(span);
                    i = closeIndex;
                    continue;
                }
            }
        }

        out += ch;
    }

    return out;
}

// Restores the spans extractMathSpans() pulled out as empty target elements
// carrying the raw TeX in a data attribute, rather than re-inserting delimited
// text for a client-side scanner to find: since the C++ side already knows exactly
// where every math span is, there's no need for (and no ambiguity risk from) a
// delimiter-scanning pass client-side -- the transcript shell's JS just calls
// katex.render() directly on each of these once the HTML lands in the DOM.
QString restoreMathSpans(QString html, const QVector<MathSpan> &mathSpans)
{
    for (int i = 0; i < mathSpans.size(); ++i) {
        QString tex = mathSpans.at(i).tex;
        tex.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
        tex.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
        tex.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
        tex.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
        const QString replacement = QStringLiteral("<span class=\"katex-target\" data-display=\"%1\" data-tex=\"%2\"></span>")
                .arg(mathSpans.at(i).display ? QStringLiteral("true") : QStringLiteral("false"), tex);
        html.replace(mathPlaceholder(i), replacement);
    }
    return html;
}

} // namespace

QString trimCodeBlockFencePadding(QString code)
{
    while (code.startsWith(QLatin1Char('\n'))) {
        code.remove(0, 1);
    }
    while (code.endsWith(QLatin1Char('\n'))) {
        code.chop(1);
    }
    return code;
}

QString normalizeRenderableMarkdown(const QString &text)
{
    return TranscriptFormatter::sanitizeRenderableMarkdown(text);
}

QVector<TranscriptSegment> splitTranscriptSegments(const QString &text)
{
    QVector<TranscriptSegment> segments;
    QString plainBuffer;
    QString codeBuffer;
    QString currentLanguage;
    bool inCode = false;

    const auto flushPlain = [&segments, &plainBuffer]() {
        if (plainBuffer.isEmpty()) {
            return;
        }
        TranscriptSegment seg;
        seg.isCode = false;
        seg.text = plainBuffer;
        segments.push_back(seg);
        plainBuffer.clear();
    };

    const auto flushCode = [&segments, &codeBuffer, &currentLanguage]() {
        if (codeBuffer.isEmpty()) {
            currentLanguage.clear();
            return;
        }
        TranscriptSegment seg;
        seg.isCode = true;
        seg.language = currentLanguage;
        seg.text = codeBuffer;
        segments.push_back(seg);
        codeBuffer.clear();
        currentLanguage.clear();
    };

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (!inCode) {
            const int fencePos = line.indexOf(QStringLiteral("```"));
            if (fencePos >= 0) {
                const QString beforeFence = line.left(fencePos);
                if (!beforeFence.isEmpty()) {
                    plainBuffer += beforeFence;
                }
                flushPlain();
                currentLanguage = line.mid(fencePos + 3).trimmed();
                inCode = true;
            } else {
                plainBuffer += line;
                if (i + 1 < lines.size()) {
                    plainBuffer += QLatin1Char('\n');
                }
            }
            continue;
        }

        const int fencePos = line.indexOf(QStringLiteral("```"));
        if (fencePos >= 0) {
            const QString beforeFence = line.left(fencePos);
            if (!beforeFence.isEmpty()) {
                codeBuffer += beforeFence;
            }
            flushCode();
            inCode = false;

            const QString trailing = line.mid(fencePos + 3);
            if (!trailing.isEmpty()) {
                plainBuffer += trailing;
                if (i + 1 < lines.size()) {
                    plainBuffer += QLatin1Char('\n');
                }
            }
            continue;
        }

        codeBuffer += line;
        if (i + 1 < lines.size()) {
            codeBuffer += QLatin1Char('\n');
        }
    }

    if (inCode) {
        flushCode();
    } else {
        flushPlain();
    }
    return segments;
}

QString extractCodeBlocks(const QString &text)
{
    QStringList blocks;
    const QString normalizedText = normalizeRenderableMarkdown(text);
    const QVector<TranscriptSegment> segments = splitTranscriptSegments(normalizedText);
    for (const TranscriptSegment &segment : segments) {
        if (segment.isCode && !segment.text.trimmed().isEmpty()) {
            const QString code = trimCodeBlockFencePadding(segment.text);
            if (!code.isEmpty()) {
                blocks << code;
            }
        }
    }
    return blocks.join(QStringLiteral("\n\n---\n\n"));
}

QString bodyFragmentFromDocument(const QTextDocument &doc)
{
    QString html = doc.toHtml();
    const int bodyStart = html.indexOf(QStringLiteral("<body"));
    if (bodyStart >= 0) {
        const int fragmentStart = html.indexOf(QLatin1Char('>'), bodyStart);
        const int bodyEnd = html.indexOf(QStringLiteral("</body>"), fragmentStart);
        if (fragmentStart >= 0 && bodyEnd > fragmentStart) {
            return html.mid(fragmentStart + 1, bodyEnd - fragmentStart - 1);
        }
    }
    return html;
}

QString escapeHtmlLikeTags(const QString &text)
{
    QString sanitized = text;
    QRegularExpression tagPattern(QStringLiteral(R"(<(/?[A-Za-z!][^>\n]{0,200})>)"));
    QRegularExpressionMatchIterator it = tagPattern.globalMatch(sanitized);

    struct Replacement {
        int start = 0;
        int length = 0;
        QString value;
    };
    QVector<Replacement> replacements;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (!match.hasMatch()) {
            continue;
        }
        Replacement repl;
        repl.start = match.capturedStart(0);
        repl.length = match.capturedLength(0);
        repl.value = match.captured(0).toHtmlEscaped();
        replacements.push_back(repl);
    }

    for (int i = replacements.size() - 1; i >= 0; --i) {
        const Replacement &repl = replacements.at(i);
        sanitized.replace(repl.start, repl.length, repl.value);
    }
    return sanitized;
}

QString decodeDoubleEscapedHtmlEntities(QString html)
{
    html.replace(QStringLiteral("&amp;lt;"), QStringLiteral("&lt;"));
    html.replace(QStringLiteral("&amp;gt;"), QStringLiteral("&gt;"));
    html.replace(QStringLiteral("&amp;quot;"), QStringLiteral("&quot;"));
    html.replace(QStringLiteral("&amp;apos;"), QStringLiteral("&apos;"));
    html.replace(QStringLiteral("&amp;#39;"), QStringLiteral("&#39;"));
    return html;
}

QString markdownFragmentToHtml(const QString &markdown, const QPalette &palette)
{
    const QString trimmed = normalizeRenderableMarkdown(markdown).trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    QVector<MathSpan> mathSpans;
    const QString withMathExtracted = extractMathSpans(trimmed, &mathSpans);

    QTextDocument doc;
    doc.setDocumentMargin(0.0);
    doc.setMarkdown(escapeHtmlLikeTags(withMathExtracted));
    QString html = decodeDoubleEscapedHtmlEntities(bodyFragmentFromDocument(doc));

    const QColor base = palette.color(QPalette::Base);
    const QColor textColor = palette.color(QPalette::Text);
    const QColor border = blendColors(palette.color(QPalette::Mid), textColor, 0.10);
    const QColor codeBackground = blendColors(base, palette.color(QPalette::Window), 0.35);
    const QColor inlineCodeBackground = blendColors(base, palette.color(QPalette::Window), 0.20);
    const QColor blockQuoteBackground = blendColors(base, palette.color(QPalette::Highlight), 0.10);
    const QColor tableHeaderBackground = blendColors(palette.color(QPalette::Button), palette.color(QPalette::Highlight), 0.12);
    const QColor linkColor = palette.color(QPalette::Link);

    html.replace(QStringLiteral("<pre"), QStringLiteral("<pre style=\"background:%1;color:%2;padding:12px;border-radius:10px;border:1px solid %3;overflow:auto;\"").arg(cssColor(codeBackground), cssColor(textColor), cssColor(border)));
    html.replace(QStringLiteral("<code"), QStringLiteral("<code style=\"background:%1;color:%2;padding:2px 5px;border-radius:4px;\"").arg(cssColor(inlineCodeBackground), cssColor(textColor)));
    html.replace(QStringLiteral("<blockquote"), QStringLiteral("<blockquote style=\"border-left:4px solid %1;margin:10px 0;padding:6px 12px;color:%2;background:%3;border-radius:6px;\"").arg(cssColor(palette.color(QPalette::Highlight)), cssColor(textColor), cssColor(blockQuoteBackground)));
    html.replace(QStringLiteral("<table"), QStringLiteral("<table style=\"border-collapse:collapse;width:100%;margin:10px 0;\""));
    html.replace(QStringLiteral("<th"), QStringLiteral("<th style=\"border:1px solid %1;padding:6px 8px;background:%2;color:%3;text-align:left;\"").arg(cssColor(border), cssColor(tableHeaderBackground), cssColor(textColor)));
    html.replace(QStringLiteral("<td"), QStringLiteral("<td style=\"border:1px solid %1;padding:6px 8px;color:%2;\"").arg(cssColor(border), cssColor(textColor)));
    html.replace(QStringLiteral("<a href="), QStringLiteral("<a style=\"color:%1;\" href=").arg(cssColor(linkColor)));
    return restoreMathSpans(html, mathSpans);
}

QString messageToRichHtml(const QString &role,
                          const QString &text,
                          QStringList *codeBlocks,
                          int answerIndex,
                          const QPalette &palette)
{
    const QString rolePrefix = transcriptPrefix(role).toHtmlEscaped();
    const QColor accent = transcriptPrefixColor(palette, role);
    const QColor base = palette.color(QPalette::Base);
    const QColor bodyColor = transcriptBodyColor(palette, role);
    const QColor border = blendColors(palette.color(QPalette::Mid), accent, 0.25);
    const QColor cardBackground = blendColors(base, accent, 0.08);
    const QColor codeActionBackground = blendColors(base, palette.color(QPalette::Button), 0.35);
    const QColor codeBackground = blendColors(base, palette.color(QPalette::Window), 0.48);
    const QColor footerBorder = blendColors(border, palette.color(QPalette::WindowText), 0.12);
    const QColor linkColor = palette.color(QPalette::Link);
    QStringList bodyParts;

    const QString normalizedText = normalizeRenderableMarkdown(text);
    const QVector<TranscriptSegment> segments = splitTranscriptSegments(normalizedText);
    for (const TranscriptSegment &segment : segments) {
        if (segment.isCode) {
            const QString code = trimCodeBlockFencePadding(segment.text);
            if (code.isEmpty()) {
                continue;
            }
            const int codeIndex = codeBlocks != nullptr ? codeBlocks->size() : 0;
            if (codeBlocks != nullptr) {
                codeBlocks->push_back(code);
            }
            const QString languageBadge = segment.language.trimmed().isEmpty()
                    ? QStringLiteral("code")
                    : segment.language.trimmed().toHtmlEscaped();
            bodyParts << QStringLiteral(
                "<div style=\"margin:10px 0 14px 0;\">"
                "<div style=\"display:flex;justify-content:space-between;align-items:center;margin:0 0 6px 0;\">"
                "<span style=\"font-size:11px;font-weight:700;color:%1;text-transform:uppercase;letter-spacing:0.08em;\">%2</span>"
                "<a href=\"copycode:%3\" style=\"font-size:12px;color:%4;text-decoration:none;background:%5;padding:4px 8px;border-radius:6px;border:1px solid %6;\">Copy code</a>"
                "</div>"
                "<pre style=\"margin:0;background:%7;color:%8;padding:12px;border-radius:10px;border:1px solid %9;overflow:auto;white-space:pre;tab-size:4;\"><code>%10</code></pre>"
                "</div>")
                .arg(cssColor(accent),
                     languageBadge,
                     QString::number(codeIndex),
                     cssColor(linkColor),
                     cssColor(codeActionBackground),
                     cssColor(border),
                     cssColor(codeBackground),
                     cssColor(palette.color(QPalette::Text)),
                     cssColor(border),
                     code.toHtmlEscaped());
        } else {
            const QString html = markdownFragmentToHtml(segment.text, palette);
            if (!html.trimmed().isEmpty()) {
                bodyParts << html;
            }
        }
    }

    if (bodyParts.isEmpty()) {
        bodyParts << QStringLiteral("<p>%1</p>").arg(normalizedText.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
    }

    QString footerHtml;
    if (role.compare(QStringLiteral("assistant"), Qt::CaseInsensitive) == 0 && answerIndex >= 0) {
        footerHtml = QStringLiteral(
            "<div style=\"margin-top:12px;padding-top:8px;border-top:1px solid %1;text-align:right;\">"
            "<a href=\"copyanswer:%2\" style=\"font-size:12px;color:%3;text-decoration:none;\">Copy Answer</a>"
            "</div>")
            .arg(cssColor(footerBorder), QString::number(answerIndex), cssColor(linkColor));
    }

    return QStringLiteral(
        "<div style=\"margin:8px 0 14px 0;padding:10px 12px;border-radius:12px;background:%1;border:1px solid %2;\">"
        "<div style=\"font-weight:700;color:%3;margin:0 0 8px 0;\">%4</div>"
        "<div style=\"color:%5;\">%6</div>"
        "%7"
        "</div>")
        .arg(cssColor(cardBackground), cssColor(border), cssColor(accent), rolePrefix, cssColor(bodyColor), bodyParts.join(QStringLiteral("\n")), footerHtml);
}
