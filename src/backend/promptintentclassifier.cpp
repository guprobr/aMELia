#include "backend/promptintentclassifier.h"

#include <QRegularExpression>

bool containsAny(const QString &text, const QStringList &needles)
{
    for (const QString &needle : needles) {
        if (text.contains(needle)) {
            return true;
        }
    }
    return false;
}

bool isStructuredDocumentRequest(const QString &prompt)
{
    const QString lower = prompt.toLower();
    return lower.contains(QStringLiteral("mop"))
            || lower.contains(QStringLiteral("runbook"))
            || lower.contains(QStringLiteral("playbook"))
            || lower.contains(QStringLiteral("guide"))
            || lower.contains(QStringLiteral("markdown"))
            || lower.contains(QStringLiteral(".md"));
}

bool looksLikeExactExtractionPrompt(const QString &prompt)
{
    const QString lower = prompt.toLower();
    const bool extractionVerb = lower.contains(QStringLiteral("extract all"))
            || lower.contains(QStringLiteral("gather all"))
            || lower.contains(QStringLiteral("collect all"))
            || lower.contains(QStringLiteral("capture all"))
            || lower.contains(QStringLiteral("list all"))
            || lower.contains(QStringLiteral("scrape"))
            || lower.contains(QStringLiteral("scraper"))
            || lower.contains(QStringLiteral("exhaustive"))
            || lower.contains(QStringLiteral("verbatim"))
            || lower.contains(QStringLiteral("exact snippet"))
            || lower.contains(QStringLiteral("exact snippets"))
            || lower.contains(QStringLiteral("exact instruction"))
            || lower.contains(QStringLiteral("preserve indentation"))
            || lower.contains(QStringLiteral("do not skip"));
    const bool actionableTarget = lower.contains(QStringLiteral("actionable"))
            || lower.contains(QStringLiteral("snippet"))
            || lower.contains(QStringLiteral("snippets"))
            || lower.contains(QStringLiteral("commands"))
            || lower.contains(QStringLiteral("config snippets"))
            || lower.contains(QStringLiteral("yaml"))
            || lower.contains(QStringLiteral("example files"))
            || lower.contains(QStringLiteral("prerequisites"))
            || lower.contains(QStringLiteral("warnings"))
            || lower.contains(QStringLiteral("ordered procedures"))
            || lower.contains(QStringLiteral("placeholders"))
            || lower.contains(QStringLiteral("appendixes"))
            || lower.contains(QStringLiteral("appendix"));

    return (extractionVerb && actionableTarget)
            || lower.contains(QStringLiteral("return, where applicable:"))
            || lower.contains(QStringLiteral("for each item include"))
            || lower.contains(QStringLiteral("search the entire document"))
            || lower.contains(QStringLiteral("do not skip repeated sections"))
            || lower.contains(QStringLiteral("use one code block per snippet"));
}

bool looksLikeDocumentStudyPrompt(const QString &prompt)
{
    if (looksLikeExactExtractionPrompt(prompt)) {
        return true;
    }

    const QString lower = prompt.toLower();
    return lower.contains(QStringLiteral("summary"))
            || lower.contains(QStringLiteral("summarize"))
            || lower.contains(QStringLiteral("tutorial"))
            || lower.contains(QStringLiteral("chapter"))
            || lower.contains(QStringLiteral("section"))
            || lower.contains(QStringLiteral("contents"))
            || lower.contains(QStringLiteral("table of contents"))
            || lower.contains(QStringLiteral("pdf"))
            || lower.contains(QStringLiteral("manual"))
            || lower.contains(QStringLiteral("guide"))
            || lower.contains(QStringLiteral("overview"))
            || lower.contains(QStringLiteral("high-level design"))
            || lower.contains(QStringLiteral("hld"))
            || lower.contains(QStringLiteral("document"))
            || lower.contains(QStringLiteral("entire"))
            || lower.contains(QStringLiteral("every section"))
            || lower.contains(QStringLiteral("every chapter"))
            || lower.contains(QStringLiteral("all sections"))
            || lower.contains(QStringLiteral("step by step"))
            || lower.contains(QStringLiteral("cover all"))
            || lower.contains(QStringLiteral("no gaps"))
            || lower.contains(QStringLiteral("installation doc"))
            || lower.contains(QStringLiteral("install doc"));
}

TransformPromptSpec detectTransformPrompt(const QString &prompt)
{
    TransformPromptSpec spec;
    const QString cleaned = prompt.trimmed();
    if (cleaned.size() < 500) {
        return spec;
    }

    const QRegularExpression splitRe(QStringLiteral("\\n\\s*\\n"));
    const QRegularExpressionMatch match = splitRe.match(cleaned);
    if (!match.hasMatch()) {
        return spec;
    }

    const QString firstBlock = cleaned.left(match.capturedStart()).trimmed();
    const QString remaining = cleaned.mid(match.capturedEnd()).trimmed();
    if (firstBlock.isEmpty() || remaining.size() < 350) {
        return spec;
    }

    const QString lower = firstBlock.toLower();
    const bool firstLooksInstruction = firstBlock.size() <= 600;
    const bool sourceDominates = remaining.size() >= qMax(450, firstBlock.size() * 2);
    const bool transformVerb = containsAny(lower,
                                           {QStringLiteral("expand"),
                                            QStringLiteral("rewrite"),
                                            QStringLiteral("transform"),
                                            QStringLiteral("convert"),
                                            QStringLiteral("turn this"),
                                            QStringLiteral("turn the"),
                                            QStringLiteral("tutorial"),
                                            QStringLiteral("teach"),
                                            QStringLiteral("explain"),
                                            QStringLiteral("full explanations"),
                                            QStringLiteral("instead of"),
                                            QStringLiteral("elaborate"),
                                            QStringLiteral("improve"),
                                            QStringLiteral("reorganize")});
    const bool sourceCue = lower.contains(QStringLiteral("following"))
            || lower.contains(QStringLiteral("below"))
            || lower.contains(QStringLiteral("pasted"))
            || lower.contains(QStringLiteral("source material"))
            || lower.contains(QStringLiteral("this text"))
            || lower.contains(QStringLiteral("this summary"))
            || lower.contains(QStringLiteral("this answer"));

    if (!(firstLooksInstruction && sourceDominates && (transformVerb || sourceCue))) {
        return spec;
    }

    spec.active = true;
    spec.instruction = firstBlock;
    spec.source = remaining;
    return spec;
}
