#include "rag/textmetrics.h"

int countWordsInText(const QString &text)
{
    int words = 0;
    bool inWord = false;
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber()) {
            if (!inWord) {
                ++words;
                inWord = true;
            }
        } else {
            inWord = false;
        }
    }
    return words;
}

int countLinesInText(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return 0;
    }
    return text.count(QLatin1Char('\n')) + 1;
}
