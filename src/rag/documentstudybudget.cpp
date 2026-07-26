#include "rag/documentstudybudget.h"

#include <QtGlobal>
#include <cmath>

double normalizedStudyPacketScale(int textChars, int chunkCount)
{
    const double safeChars = static_cast<double>(qMax(textChars, 1000));
    const double safeChunks = static_cast<double>(qMax(chunkCount, 1));
    const double charScale = (std::log10(safeChars) - std::log10(50000.0))
            / (std::log10(5000000.0) - std::log10(50000.0));
    const double chunkScale = (std::log10(safeChunks) - std::log10(150.0))
            / (std::log10(15000.0) - std::log10(150.0));
    return qBound(0.0, qMax(charScale, chunkScale), 1.0);
}

DocumentStudyPacketProfile buildDocumentStudyPacketProfile(int textChars,
                                                           int chunkCount,
                                                           int requestedOutlineLineLimit,
                                                           int requestedMaxCharsPerFile)
{
    const double scale = normalizedStudyPacketScale(textChars, chunkCount);
    const bool mediumDocument = textChars >= 180000 || chunkCount >= 500;
    const bool largeDocument = textChars >= 500000 || chunkCount >= 1800;
    const bool hugeDocument = textChars >= 1500000 || chunkCount >= 6000;
    const bool massiveDocument = textChars >= 5000000 || chunkCount >= 20000;

    DocumentStudyPacketProfile profile;
    profile.effectiveOutlineLineLimit = qBound(120,
                                               requestedOutlineLineLimit + qRound(scale * 80.0),
                                               360);

    const int requestedBudget = qMax(12000, requestedMaxCharsPerFile);
    profile.effectiveMaxCharsPerFile = requestedBudget;

    if (!mediumDocument) {
        profile.previewBudget = qMin(9000, qMax(2200, profile.effectiveMaxCharsPerFile / 5));
    } else if (!hugeDocument) {
        profile.previewBudget = qMin(6000, qMax(2000, profile.effectiveMaxCharsPerFile / 9));
    } else if (!massiveDocument) {
        profile.previewBudget = qMin(1600, qMax(0, profile.effectiveMaxCharsPerFile / 36));
    } else {
        profile.previewBudget = 0;
    }

    profile.coverageBudget = qMax(2800, profile.effectiveMaxCharsPerFile - profile.previewBudget - 900);
    profile.minSectionChars = !mediumDocument ? 800 : (largeDocument ? (massiveDocument ? 220 : (hugeDocument ? 280 : 300)) : 700);
    profile.maxSectionCharsCap = !mediumDocument ? 4000 : (largeDocument ? (massiveDocument ? 900 : (hugeDocument ? 1500 : 3000)) : 3200);
    profile.anchorCap = qBound(72, 80 + qRound(scale * 56.0), 160);
    if (largeDocument) {
        profile.anchorCap = qMax(profile.anchorCap, 96);
    }
    if (hugeDocument) {
        profile.anchorCap = qMax(profile.anchorCap, 128);
    }
    if (massiveDocument) {
        profile.anchorCap = 160;
    }

    profile.includeFullDocumentPreview = !largeDocument;
    profile.fullDocumentInlineThreshold = qMin(profile.effectiveMaxCharsPerFile, 56000);
    const int previewFraction = largeDocument ? 5 : 3;
    profile.previewBudget = qMin(profile.effectiveMaxCharsPerFile,
                                 qMax(profile.previewBudget, profile.effectiveMaxCharsPerFile / previewFraction));
    return profile;
}
