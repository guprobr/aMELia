#include "backend/diagnosticsconsole.h"

#include <QtGlobal>
#include <cstdio>

namespace {
QString ansiColorForCategory(const QString &category)
{
    const QString lower = category.toLower();
    if (lower == QStringLiteral("backend"))   return QStringLiteral("\x1b[38;5;39m");
    if (lower == QStringLiteral("search"))    return QStringLiteral("\x1b[38;5;46m");
    if (lower == QStringLiteral("rag"))       return QStringLiteral("\x1b[38;5;44m");
    if (lower == QStringLiteral("memory"))    return QStringLiteral("\x1b[38;5;208m");
    if (lower == QStringLiteral("planner"))   return QStringLiteral("\x1b[38;5;141m");
    if (lower == QStringLiteral("guardrail")) return QStringLiteral("\x1b[38;5;196m");
    if (lower == QStringLiteral("ingest"))    return QStringLiteral("\x1b[38;5;220m");
    if (lower == QStringLiteral("startup"))   return QStringLiteral("\x1b[38;5;213m");
    if (lower == QStringLiteral("budget"))    return QStringLiteral("\x1b[38;5;51m");
    if (lower == QStringLiteral("chat"))      return QStringLiteral("\x1b[38;5;177m");
    if (lower == QStringLiteral("reasoning")) return QStringLiteral("\x1b[38;5;197m");
    return QStringLiteral("\x1b[0m");
}
}

void printDiagnosticToConsole(const QString &category, const QString &line)
{
    const QByteArray payload = line.toUtf8();
    if (qEnvironmentVariableIsSet("NO_COLOR")) {
        std::fprintf(stderr, "%s\n", payload.constData());
    } else {
        const QByteArray color = ansiColorForCategory(category).toUtf8();
        std::fprintf(stderr, "%s%s\x1b[0m\n", color.constData(), payload.constData());
    }
    std::fflush(stderr);
}

bool shouldClassifyDiagnosticAsVerbose(const QString &category, const QString &message)
{
    const QString lowerCategory = category.trimmed().toLower();
    const QString lower = message.trimmed().toLower();

    if (lowerCategory == QStringLiteral("reasoning")) {
        return false;
    }

    return lower.startsWith(QStringLiteral("ollama request "))
            || lower.startsWith(QStringLiteral("ollama probe request "))
            || lower.startsWith(QStringLiteral("ollama probe response "))
            || lower.startsWith(QStringLiteral("ollama model-list request "))
            || lower.startsWith(QStringLiteral("ollama model-list response "))
            || lower.startsWith(QStringLiteral("ollama embedding request "))
            || lower.startsWith(QStringLiteral("ollama embedding response "))
            || lower.startsWith(QStringLiteral("ollama response headers received "))
            || lower.startsWith(QStringLiteral("ollama chat response complete "));
}
