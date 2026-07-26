#pragma once

// 0..1 scale of "how big is this document" derived from its char/chunk counts on a
// log scale, used to interpolate document-study packet budgets between small and huge
// files.
double normalizedStudyPacketScale(int textChars, int chunkCount);

// Sizing for one file's document-study packet (outline preview + section-coverage
// budget), scaled by document size so a 2000-page manual doesn't get the same
// preview/coverage split as a 10-page one.
struct DocumentStudyPacketProfile {
    int effectiveOutlineLineLimit = 180;
    int effectiveMaxCharsPerFile = 40000;
    int previewBudget = 0;
    int coverageBudget = 0;
    int minSectionChars = 400;
    int maxSectionCharsCap = 1600;
    int anchorCap = 64;
    bool includeFullDocumentPreview = false;
    int fullDocumentInlineThreshold = 0;
};

DocumentStudyPacketProfile buildDocumentStudyPacketProfile(int textChars,
                                                           int chunkCount,
                                                           int requestedOutlineLineLimit,
                                                           int requestedMaxCharsPerFile);
