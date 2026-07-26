#include "rag/textcleanup.h"

#include <QRegularExpression>
#include <QStringList>

QString trimTrailingWhitespacePerLine(const QString &text)
{
    QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (QString &line : lines) {
        while (!line.isEmpty() && (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t')))) {
            line.chop(1);
        }
    }
    return lines.join(QStringLiteral("\n"));
}

QString collapseExcessBlankLines(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("\\n{4,}")), QStringLiteral("\n\n\n"));
    return text;
}

QString cleanedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QChar(0x00A0), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QStringLiteral("    "));
    text.replace(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]")), QStringLiteral(" "));
    text = trimTrailingWhitespacePerLine(text);
    text = collapseExcessBlankLines(text);
    return text.trimmed();
}
