#include "backend/contextbudgetcalculator.h"

#include "rag/ragindexer.h"

#include <QtGlobal>
#include <cmath>

namespace {
QString readEnvironmentValue(const char *name)
{
    return qEnvironmentVariableIsSet(name)
            ? QString::fromLocal8Bit(qgetenv(name)).trimmed()
            : QString();
}
}

OllamaRuntimeProfile detectOllamaRuntimeProfile()
{
    const QString overrideValue = readEnvironmentValue("AMELIA_OLLAMA_RUNTIME_PROFILE").toLower();
    if (overrideValue == QStringLiteral("cpu")
            || overrideValue == QStringLiteral("cpu-only")
            || overrideValue == QStringLiteral("conservative")) {
        return OllamaRuntimeProfile::CpuConservative;
    }
    if (overrideValue == QStringLiteral("gpu")
            || overrideValue == QStringLiteral("balanced")) {
        return OllamaRuntimeProfile::GpuBalanced;
    }
    if (overrideValue == QStringLiteral("auto")) {
        return OllamaRuntimeProfile::Auto;
    }

    const QString vkVisible = readEnvironmentValue("GGML_VK_VISIBLE_DEVICES");
    if (vkVisible == QStringLiteral("-1")) {
        return OllamaRuntimeProfile::CpuConservative;
    }

    const auto looksGpuEnabled = [](const QString &value) {
        return !value.isEmpty() && value != QStringLiteral("-1") && value.toLower() != QStringLiteral("none");
    };

    if (looksGpuEnabled(readEnvironmentValue("CUDA_VISIBLE_DEVICES"))
            || looksGpuEnabled(readEnvironmentValue("HIP_VISIBLE_DEVICES"))
            || looksGpuEnabled(vkVisible)) {
        return OllamaRuntimeProfile::GpuBalanced;
    }

    if (readEnvironmentValue("OLLAMA_VULKAN") == QStringLiteral("1")) {
        return OllamaRuntimeProfile::GpuBalanced;
    }

    return OllamaRuntimeProfile::Auto;
}

QString ollamaRuntimeProfileName(OllamaRuntimeProfile profile)
{
    switch (profile) {
    case OllamaRuntimeProfile::CpuConservative:
        return QStringLiteral("cpu");
    case OllamaRuntimeProfile::GpuBalanced:
        return QStringLiteral("gpu");
    case OllamaRuntimeProfile::Auto:
    default:
        return QStringLiteral("auto");
    }
}

int computeRunnerFallbackNumCtx(int baseNumCtx)
{
    const int safeBase = qMax(8192, baseNumCtx);
    int fallback = qMax(12288, qMin(24576, (safeBase * 3) / 4));
    if (fallback >= safeBase) {
        fallback = qMax(12288, safeBase - 4096);
    }
    return qMin(fallback, safeBase);
}

int estimatedCharsForTokens(int tokens)
{
    return qMax(0, qRound(static_cast<double>(qMax(tokens, 0)) * kPromptBudgetCharsPerToken));
}

int safeRetrievedContextTokenBudget(int numCtx, bool documentStudy, OllamaRuntimeProfile runtimeProfile)
{
    const int safeNumCtx = qMax(4096, numCtx);
    if (documentStudy) {
        const int answerReserve = qBound(4096, safeNumCtx / 4, 8192);
        const int scaffoldingReserve = qBound(1800, safeNumCtx / 10, 3200);
        const int historyReserve = qBound(600, safeNumCtx / 24, 1200);
        const int available = qMax(2200, safeNumCtx - answerReserve - scaffoldingReserve - historyReserve);

        double contextRatio = 0.50;
        if (runtimeProfile == OllamaRuntimeProfile::CpuConservative) {
            contextRatio = 0.43;
        } else if (runtimeProfile == OllamaRuntimeProfile::GpuBalanced) {
            contextRatio = 0.62;
        }

        return qBound(2200,
                      qRound(static_cast<double>(available) * contextRatio),
                      qMax(2200, safeNumCtx * 2 / 3));
    }

    const int answerReserve = qBound(1024, safeNumCtx / 10, 2048);
    const int scaffoldingReserve = qBound(1000, safeNumCtx / 14, 1800);
    const int historyReserve = qBound(300, safeNumCtx / 30, 700);
    const int available = qMax(900, safeNumCtx - answerReserve - scaffoldingReserve - historyReserve);

    double contextRatio = 0.26;
    if (runtimeProfile == OllamaRuntimeProfile::CpuConservative) {
        contextRatio = 0.22;
    } else if (runtimeProfile == OllamaRuntimeProfile::GpuBalanced) {
        contextRatio = 0.32;
    }

    return qBound(900,
                  qRound(static_cast<double>(available) * contextRatio),
                  qMax(900, safeNumCtx / 3));
}

int safeRetrievedContextCharBudget(int numCtx, bool documentStudy, OllamaRuntimeProfile runtimeProfile)
{
    return estimatedCharsForTokens(safeRetrievedContextTokenBudget(numCtx, documentStudy, runtimeProfile));
}

double normalizedDocumentScale(int textChars, int chunkCount)
{
    const double safeChars = static_cast<double>(qMax(textChars, 1000));
    const double safeChunks = static_cast<double>(qMax(chunkCount, 1));
    const double charScale = (std::log10(safeChars) - std::log10(50000.0))
            / (std::log10(5000000.0) - std::log10(50000.0));
    const double chunkScale = (std::log10(safeChunks) - std::log10(150.0))
            / (std::log10(15000.0) - std::log10(150.0));
    return qBound(0.0, qMax(charScale, chunkScale), 1.0);
}

DocumentStudyRuntimeTuning tuneDocumentStudyRuntime(const DocumentSelectionStats &stats,
                                                    bool prioritized,
                                                    bool exactExtraction,
                                                    int numCtx,
                                                    OllamaRuntimeProfile runtimeProfile)
{
    DocumentStudyRuntimeTuning tuning;
    tuning.localContextBudget = safeRetrievedContextCharBudget(numCtx, true, runtimeProfile);
    if (stats.fileCount <= 0) {
        return tuning;
    }

    const int sizingChars = qMax(stats.maxCharsInFile,
                                 stats.fileCount > 0 ? stats.totalChars / stats.fileCount : stats.totalChars);
    const int sizingChunks = qMax(stats.maxChunksInFile,
                                  stats.fileCount > 0 ? stats.totalChunks / stats.fileCount : stats.totalChunks);
    const double scale = normalizedDocumentScale(sizingChars, sizingChunks);
    const int fileBudgetDivisor = qMax(1, qMin(stats.fileCount, 2));
    const int availablePerFileBudget = qMax(14000,
                                            (tuning.localContextBudget - 2200) / fileBudgetDivisor);

    const int minCoverage = prioritized ? 8 : 6;
    const int maxCoverage = prioritized ? 18 : 14;
    const int coverageBase = prioritized ? 10 : 8;
    const int coverageFromScale = coverageBase + qRound(scale * 6.0);

    const int dynamicPacketBudget = qBound(20000,
                                           28000 + qRound(scale * 24000.0) + (prioritized ? 3000 : 0),
                                           72000);
    tuning.maxCharsPerFile = qMin(dynamicPacketBudget,
                              qMin(availablePerFileBudget,
                                   tuning.localContextBudget));
    tuning.hitPromptFallbackBudget = qBound(800,
                                            qRound(static_cast<double>(tuning.localContextBudget) * 0.05),
                                            1800);

    const int coverageFromBudget = qMax(minCoverage, tuning.maxCharsPerFile / 1800);
    tuning.coveragePerFile = qBound(minCoverage,
                                    qMin(coverageFromScale, coverageFromBudget),
                                    maxCoverage);
    tuning.studyHitFloor = qBound(10, tuning.coveragePerFile + 4, 18);

    if (exactExtraction) {
        tuning.maxCharsPerFile = qMin(tuning.localContextBudget,
                                      qMax(tuning.maxCharsPerFile,
                                           qMin(qMax(22000, availablePerFileBudget),
                                                qMax(22000, tuning.localContextBudget - 1400))));
        tuning.coveragePerFile = qBound(10, tuning.coveragePerFile + 4, 24);
        tuning.studyHitFloor = qBound(14, tuning.coveragePerFile + 6, 24);
        tuning.hitPromptFallbackBudget = qBound(1200,
                                                qRound(static_cast<double>(tuning.localContextBudget) * 0.07),
                                                2400);
    }
    return tuning;
}
