#include "core/transcriptformatter.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

namespace {

QSet<QString> supportedLanguageTokens()
{
    return {
        QStringLiteral("text"),
        QStringLiteral("plain"),
        QStringLiteral("plaintext"),
        QStringLiteral("bash"),
        QStringLiteral("sh"),
        QStringLiteral("shell"),
        QStringLiteral("zsh"),
        QStringLiteral("fish"),
        QStringLiteral("powershell"),
        QStringLiteral("ps1"),
        QStringLiteral("cmd"),
        QStringLiteral("bat"),
        QStringLiteral("c"),
        QStringLiteral("h"),
        QStringLiteral("cpp"),
        QStringLiteral("cxx"),
        QStringLiteral("cc"),
        QStringLiteral("hpp"),
        QStringLiteral("c++"),
        QStringLiteral("objective-c"),
        QStringLiteral("objc"),
        QStringLiteral("swift"),
        QStringLiteral("java"),
        QStringLiteral("kotlin"),
        QStringLiteral("scala"),
        QStringLiteral("go"),
        QStringLiteral("rust"),
        QStringLiteral("zig"),
        QStringLiteral("python"),
        QStringLiteral("py"),
        QStringLiteral("ruby"),
        QStringLiteral("rb"),
        QStringLiteral("php"),
        QStringLiteral("perl"),
        QStringLiteral("lua"),
        QStringLiteral("javascript"),
        QStringLiteral("js"),
        QStringLiteral("typescript"),
        QStringLiteral("ts"),
        QStringLiteral("tsx"),
        QStringLiteral("jsx"),
        QStringLiteral("json"),
        QStringLiteral("yaml"),
        QStringLiteral("yml"),
        QStringLiteral("toml"),
        QStringLiteral("ini"),
        QStringLiteral("xml"),
        QStringLiteral("html"),
        QStringLiteral("css"),
        QStringLiteral("sql"),
        QStringLiteral("cmake"),
        QStringLiteral("dockerfile"),
        QStringLiteral("makefile")
    };
}

bool isSupportedLanguageToken(const QString &language)
{
    static const QSet<QString> tokens = supportedLanguageTokens();
    return tokens.contains(language.trimmed().toLower());
}

bool looksCodeLike(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return false;
    }

    int signalCount = 0;
    const QString lower = text.toLower();
    const QStringList strongSignals = {
        QStringLiteral("#include"),
        QStringLiteral("int main"),
        QStringLiteral("std::"),
        QStringLiteral("->"),
        QStringLiteral("::"),
        QStringLiteral("public static void main"),
        QStringLiteral("console.log"),
        QStringLiteral("def "),
        QStringLiteral("class "),
        QStringLiteral("function "),
        QStringLiteral("import "),
        QStringLiteral("from "),
        QStringLiteral("select "),
        QStringLiteral("insert into"),
        QStringLiteral("update "),
        QStringLiteral("delete from"),
        QStringLiteral("{"),
        QStringLiteral("}"),
        QStringLiteral(";")
    };
    for (const QString &signal : strongSignals) {
        if (lower.contains(signal)) {
            ++signalCount;
        }
    }

    int nonAlphaSignals = 0;
    for (const QChar ch : text) {
        if (ch == QLatin1Char('{') || ch == QLatin1Char('}') || ch == QLatin1Char(';')
                || ch == QLatin1Char('(') || ch == QLatin1Char(')') || ch == QLatin1Char('#')) {
            ++nonAlphaSignals;
        }
    }

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    int indentedLines = 0;
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("    ")) || line.startsWith(QLatin1Char('\t'))) {
            ++indentedLines;
        }
    }

    return signalCount >= 2 || nonAlphaSignals >= 6 || (signalCount >= 1 && indentedLines >= 1);
}

QString decodeEscapedLayoutOutsideQuotes(const QString &text)
{
    QString out;
    out.reserve(text.size());

    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool escapingInsideQuote = false;

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);

        if ((inSingleQuote || inDoubleQuote) && escapingInsideQuote) {
            out += ch;
            escapingInsideQuote = false;
            continue;
        }

        if ((inSingleQuote || inDoubleQuote) && ch == QLatin1Char('\\')) {
            out += ch;
            escapingInsideQuote = true;
            continue;
        }

        if (!inSingleQuote && !inDoubleQuote && ch == QLatin1Char('\\') && i + 1 < text.size()) {
            const QChar next = text.at(i + 1);
            if (next == QLatin1Char('n')) {
                out += QLatin1Char('\n');
                ++i;
                continue;
            }
            if (next == QLatin1Char('r')) {
                ++i;
                continue;
            }
            if (next == QLatin1Char('t')) {
                out += QLatin1Char('\t');
                ++i;
                continue;
            }
            if (next == QLatin1Char('"') || next == QLatin1Char('\'')) {
                out += ch;
                out += next;
                ++i;
                continue;
            }
        }

        if (!inDoubleQuote && ch == QLatin1Char('\'')) {
            inSingleQuote = !inSingleQuote;
            out += ch;
            continue;
        }
        if (!inSingleQuote && ch == QLatin1Char('"')) {
            inDoubleQuote = !inDoubleQuote;
            out += ch;
            continue;
        }

        out += ch;
    }

    return out;
}

QString stripTrailingFenceLine(QString text)
{
    QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) {
        lines.removeLast();
    }
    if (!lines.isEmpty() && lines.last().trimmed().startsWith(QStringLiteral("```"))) {
        lines.removeLast();
    }
    return lines.join(QLatin1Char('\n')).trimmed();
}

QString ensureBlankLineBeforeFence(const QString &text)
{
    QString normalized = text;
    normalized.replace(QRegularExpression(QStringLiteral("([^\\n])\\n(```[A-Za-z0-9_+#\\-]*\\n)")),
                       QStringLiteral("\\1\\n\\n\\2"));
    return normalized;
}

QString ensureBlankLineAfterFence(const QString &text)
{
    QString normalized = text;
    normalized.replace(QRegularExpression(QStringLiteral("(\\n```\\n)([^\\n])")),
                       QStringLiteral("\\1\\n\\2"));
    return normalized;
}

QString closeDanglingFence(QString text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    int fenceCount = 0;
    for (const QString &line : lines) {
        if (line.trimmed().startsWith(QStringLiteral("```"))) {
            ++fenceCount;
        }
    }
    if ((fenceCount % 2) != 0) {
        if (!text.endsWith(QLatin1Char('\n'))) {
            text += QLatin1Char('\n');
        }
        text += QStringLiteral("```\n");
    }
    return text;
}

bool tryRepairEscapedCodeLine(const QString &line, QString *replacement)
{
    if (replacement == nullptr) {
        return false;
    }

    static const QRegularExpression plainPattern(
        QStringLiteral(R"(^\s*([A-Za-z][A-Za-z0-9_+#\-]{0,19})\\n(.+)$)"));
    static const QRegularExpression fencedPattern(
        QStringLiteral(R"(^\s*```\s*([A-Za-z][A-Za-z0-9_+#\-]{0,19})\\n(.+)$)"));

    QRegularExpressionMatch match = fencedPattern.match(line);
    const bool explicitFence = match.hasMatch();
    if (!explicitFence) {
        match = plainPattern.match(line);
    }
    if (!match.hasMatch()) {
        return false;
    }

    const QString language = match.captured(1).trimmed();
    if (!isSupportedLanguageToken(language)) {
        return false;
    }

    QString payload = match.captured(2);
    if (!payload.contains(QStringLiteral("\\n"))) {
        return false;
    }

    payload = decodeEscapedLayoutOutsideQuotes(payload);
    payload = stripTrailingFenceLine(payload);
    if (!looksCodeLike(payload)) {
        return false;
    }

    QString fenced = QStringLiteral("```%1\n%2\n```").arg(language, payload);
    if (!explicitFence && !line.startsWith(QLatin1Char(' ')) && !line.startsWith(QLatin1Char('\t'))) {
        fenced.prepend(QLatin1Char('\n'));
    }
    *replacement = fenced;
    return true;
}

QString rewriteEscapedCodeLines(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    QStringList out;
    out.reserve(lines.size());
    for (const QString &line : lines) {
        QString replacement;
        if (tryRepairEscapedCodeLine(line, &replacement)) {
            out << replacement;
        } else {
            out << line;
        }
    }
    return out.join(QLatin1Char('\n'));
}

QString normalizeInlineSingleLineFences(const QString &text);

QString decodeMarkdownCellText(QString cell)
{
    cell.replace(QRegularExpression(QStringLiteral(R"((?i)<br\s*/?>)")), QStringLiteral("\n"));
    cell.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    cell = normalizeInlineSingleLineFences(cell);
    return cell.trimmed();
}

int countMarkdownFenceDelimiters(const QString &text)
{
    int count = 0;
    int offset = 0;
    while ((offset = text.indexOf(QStringLiteral("```"), offset)) >= 0) {
        ++count;
        offset += 3;
    }
    return count;
}

QStringList splitMarkdownTableRow(const QString &line)
{
    QString working = line.trimmed();
    if (working.startsWith(QLatin1Char('|'))) {
        working.remove(0, 1);
    }
    if (working.endsWith(QLatin1Char('|'))) {
        working.chop(1);
    }

    QStringList cells;
    QString current;
    bool escaped = false;
    bool inFence = false;
    bool inInlineCode = false;
    for (int i = 0; i < working.size(); ++i) {
        const QChar ch = working.at(i);
        if (escaped) {
            current += ch;
            escaped = false;
            continue;
        }
        if (!inFence && ch == QLatin1Char('\\')) {
            current += ch;
            escaped = true;
            continue;
        }
        if (i + 2 < working.size() && working.mid(i, 3) == QStringLiteral("```")) {
            current += QStringLiteral("```");
            inFence = !inFence;
            i += 2;
            continue;
        }
        if (!inFence && ch == QLatin1Char('`')) {
            inInlineCode = !inInlineCode;
            current += ch;
            continue;
        }
        if (!inFence && !inInlineCode && ch == QLatin1Char('|')) {
            cells.push_back(current.trimmed());
            current.clear();
            continue;
        }
        current += ch;
    }
    cells.push_back(current.trimmed());
    return cells;
}

bool isMarkdownTableLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(QLatin1Char('|')) && trimmed.count(QLatin1Char('|')) >= 2;
}

bool isMarkdownTableSeparatorLine(const QString &line)
{
    const QStringList cells = splitMarkdownTableRow(line);
    if (cells.isEmpty()) {
        return false;
    }
    for (const QString &cell : cells) {
        if (cell.isEmpty()) {
            return false;
        }
        for (const QChar ch : cell) {
            if (ch != QLatin1Char('-') && ch != QLatin1Char(':') && !ch.isSpace()) {
                return false;
            }
        }
    }
    return true;
}

QStringList collectLogicalMarkdownTableBlock(const QStringList &lines, int start, int *end)
{
    QStringList rows;
    if (start < 0 || start + 1 >= lines.size()) {
        if (end != nullptr) {
            *end = qMax(0, start);
        }
        return rows;
    }

    rows << lines.at(start);
    rows << lines.at(start + 1);

    int i = start + 2;
    while (i < lines.size()) {
        if (!isMarkdownTableLine(lines.at(i))) {
            break;
        }

        QString row = lines.at(i);
        bool inFence = (countMarkdownFenceDelimiters(row) % 2) != 0;
        while (i + 1 < lines.size()) {
            const QString next = lines.at(i + 1);
            if (!inFence) {
                break;
            }

            row += QLatin1Char('\n');
            row += next;
            ++i;
            if ((countMarkdownFenceDelimiters(next) % 2) != 0) {
                inFence = !inFence;
            }
        }

        rows << row;
        ++i;
    }

    if (end != nullptr) {
        *end = i;
    }
    return rows;
}

bool markdownTableBlockNeedsRewrite(const QStringList &rows)
{
    static const QRegularExpression brPattern(QStringLiteral(R"((?i)<br\s*/?>)"));
    for (const QString &row : rows) {
        if (row.contains(QStringLiteral("```")) || row.contains(brPattern)
                || row.contains(QStringLiteral("<pre"), Qt::CaseInsensitive)
                || row.contains(QStringLiteral("</pre"), Qt::CaseInsensitive)
                || row.contains(QStringLiteral("<code"), Qt::CaseInsensitive)
                || row.contains(QStringLiteral("</code"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

int preferredTableTitleIndex(const QStringList &headers)
{
    static const QStringList preferredHeaders = {
        QStringLiteral("Concept"),
        QStringLiteral("Topic"),
        QStringLiteral("Section"),
        QStringLiteral("Feature"),
        QStringLiteral("Control"),
        QStringLiteral("Title"),
        QStringLiteral("Item")
    };

    for (const QString &preferred : preferredHeaders) {
        const int index = headers.indexOf(preferred);
        if (index >= 0) {
            return index;
        }
    }

    for (int i = 0; i < headers.size(); ++i) {
        if (!headers.at(i).trimmed().isEmpty()) {
            return i;
        }
    }
    return -1;
}

QString normalizeInlineSingleLineFencePayload(const QString &inside)
{
    QString working = inside.trimmed();
    if (working.isEmpty()) {
        return QStringLiteral("```\n```");
    }

    QString language;
    QString payload = working;
    int split = -1;
    for (int i = 0; i < working.size(); ++i) {
        if (working.at(i).isSpace()) {
            split = i;
            break;
        }
    }
    if (split > 0) {
        const QString token = working.left(split).trimmed();
        const QString remainder = working.mid(split + 1).trimmed();
        if (isSupportedLanguageToken(token) && !remainder.isEmpty()) {
            language = token;
            payload = remainder;
        }
    }

    if (language.isEmpty() && !looksCodeLike(payload)) {
        return QStringLiteral("```") + inside + QStringLiteral("```");
    }

    return language.isEmpty()
            ? QStringLiteral("```\n%1\n```").arg(payload)
            : QStringLiteral("```%1\n%2\n```").arg(language, payload);
}

QString normalizeInlineSingleLineFences(const QString &text)
{
    QString out;
    int cursor = 0;
    while (cursor < text.size()) {
        const int open = text.indexOf(QStringLiteral("```"), cursor);
        if (open < 0) {
            out += text.mid(cursor);
            break;
        }

        const int lineEnd = text.indexOf(QLatin1Char('\n'), open);
        const int searchLimit = (lineEnd >= 0) ? lineEnd : text.size();
        const int close = text.indexOf(QStringLiteral("```"), open + 3);
        if (close < 0 || close >= searchLimit) {
            out += text.mid(cursor, open - cursor + 3);
            cursor = open + 3;
            continue;
        }

        out += text.mid(cursor, open - cursor);
        const QString inside = text.mid(open + 3, close - (open + 3));
        out += normalizeInlineSingleLineFencePayload(inside);
        cursor = close + 3;
    }
    return out;
}

void appendStandardField(QStringList *output, const QString &header, const QString &value)
{
    if (output == nullptr) {
        return;
    }

    const QString trimmedHeader = header.trimmed();
    const QString trimmedValue = value.trimmed();
    if (trimmedHeader.isEmpty() || trimmedValue.isEmpty()) {
        return;
    }

    if (trimmedValue.contains(QStringLiteral("```"))) {
        const int fence = trimmedValue.indexOf(QStringLiteral("```"));
        const QString beforeFence = trimmedValue.left(fence).trimmed();
        const QString fromFence = trimmedValue.mid(fence).trimmed();
        if (!beforeFence.isEmpty()) {
            output->append(QStringLiteral("**%1:** %2").arg(trimmedHeader, beforeFence));
            output->append(QString());
        } else {
            output->append(QStringLiteral("**%1**").arg(trimmedHeader));
            output->append(QString());
        }
        output->append(fromFence);
        output->append(QString());
        return;
    }

    if (trimmedValue.contains(QLatin1Char('\n'))) {
        output->append(QStringLiteral("**%1:**").arg(trimmedHeader));
        output->append(trimmedValue);
        output->append(QString());
        return;
    }

    output->append(QStringLiteral("**%1:** %2").arg(trimmedHeader, trimmedValue));
    output->append(QString());
}

QString rewriteUnsafeMarkdownTables(const QString &markdown)
{
    const QStringList lines = markdown.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    QStringList output;
    int i = 0;
    while (i < lines.size()) {
        if (i + 1 >= lines.size() || !isMarkdownTableLine(lines.at(i)) || !isMarkdownTableSeparatorLine(lines.at(i + 1))) {
            output << lines.at(i);
            ++i;
            continue;
        }

        int end = i;
        const QStringList rows = collectLogicalMarkdownTableBlock(lines, i, &end);
        if (rows.size() < 3 || !markdownTableBlockNeedsRewrite(rows)) {
            for (int j = i; j < end; ++j) {
                output << lines.at(j);
            }
            i = end;
            continue;
        }

        const QStringList headers = splitMarkdownTableRow(rows.at(0));
        const int titleIndex = preferredTableTitleIndex(headers);
        int rowNumber = 0;
        for (int row = 2; row < rows.size(); ++row) {
            const QStringList cells = splitMarkdownTableRow(rows.at(row));
            if (cells.isEmpty()) {
                continue;
            }

            ++rowNumber;
            QString title;
            if (titleIndex >= 0 && titleIndex < cells.size()) {
                title = decodeMarkdownCellText(cells.at(titleIndex));
            }
            if (title.isEmpty()) {
                title = QStringLiteral("Item %1").arg(rowNumber);
            }

            output << QStringLiteral("### %1").arg(title);
            output << QString();
            for (int col = 0; col < headers.size() && col < cells.size(); ++col) {
                appendStandardField(&output, headers.at(col), decodeMarkdownCellText(cells.at(col)));
            }
            if (row + 1 < rows.size()) {
                output << QStringLiteral("---");
                output << QString();
            }
        }

        i = end;
    }

    return output.join(QLatin1Char('\n'));
}

QString normalizeFenceAdjacentBreaks(QString text)
{
    text.replace(QRegularExpression(QStringLiteral(R"(```([A-Za-z0-9_+\-]*)<br\s*/?>)"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("```\\1\n"));
    text.replace(QRegularExpression(QStringLiteral(R"((?i)<br\s*/?>```)")),
                 QStringLiteral("\n```"));
    return text;
}

QString normalizeLooseMarkdownSpacing(QString text)
{
    text.replace(QRegularExpression(QStringLiteral(R"(\n{3,})")), QStringLiteral("\n\n"));
    text = ensureBlankLineBeforeFence(text);
    text = ensureBlankLineAfterFence(text);
    return text.trimmed();
}

} // namespace

namespace TranscriptFormatter {

QString sanitizeRenderableMarkdown(const QString &text)
{
    QString cleaned = text;
    cleaned.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    cleaned.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    cleaned.remove(QChar::Null);

    cleaned = rewriteEscapedCodeLines(cleaned);
    cleaned = rewriteUnsafeMarkdownTables(cleaned);
    cleaned = normalizeInlineSingleLineFences(cleaned);
    cleaned = normalizeFenceAdjacentBreaks(cleaned);
    cleaned = closeDanglingFence(cleaned);
    cleaned = normalizeLooseMarkdownSpacing(cleaned);
    return cleaned;
}

QString sanitizeFinalAssistantMarkdown(const QString &text)
{
    return sanitizeRenderableMarkdown(text);
}

} // namespace TranscriptFormatter
