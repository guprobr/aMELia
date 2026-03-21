#include "core/transcriptformatter.h"

#include <QRegularExpression>
#include <QStringList>
#include <QSet>

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
    normalized.replace(QRegularExpression(QStringLiteral(R"(([^\n])\n(```[A-Za-z0-9_+#\-]*\n))")),
                       QStringLiteral("\\1\n\n\\2"));
    return normalized;
}

QString ensureBlankLineAfterFence(const QString &text)
{
    QString normalized = text;
    normalized.replace(QRegularExpression(QStringLiteral(R"((\n```\n)([^\n]))")),
                       QStringLiteral("\\1\n\\2"));
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
    bool explicitFence = match.hasMatch();
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
    cleaned = closeDanglingFence(cleaned);
    cleaned = normalizeLooseMarkdownSpacing(cleaned);
    return cleaned;
}

QString sanitizeFinalAssistantMarkdown(const QString &text)
{
    return sanitizeRenderableMarkdown(text);
}

} // namespace TranscriptFormatter
