#pragma once

#include <QString>

struct DocumentSelectionStats;

// How many characters Amelia budgets per estimated LLM token when sizing prompt
// sections. Used both to convert a token budget into a char budget here and to
// estimate elapsed-time-to-first-token from a prompt's char count in ChatController.
constexpr double kPromptBudgetCharsPerToken = 3.2;

// Ollama's inference backend behaves very differently on CPU vs GPU (num_ctx headroom,
// how much context it can push before slowing down or OOMing), so retrieval and
// document-study budgets are scaled by which one is currently active.
enum class OllamaRuntimeProfile {
    Auto,
    CpuConservative,
    GpuBalanced,
};

// Per-request sizing for the document-study retrieval path: how many chunks to pull
// per file, the hit-count floor before falling back to a smaller packet, and the
// character ceilings that keep a single huge document from starving the rest of the
// prompt budget.
struct DocumentStudyRuntimeTuning {
    int coveragePerFile = 10;
    int studyHitFloor = 14;
    int maxCharsPerFile = 26000;
    int hitPromptFallbackBudget = 1200;
    int localContextBudget = 32000;
};

// Inspects AMELIA_OLLAMA_RUNTIME_PROFILE and the usual GPU-visibility environment
// variables (CUDA/HIP/Vulkan) to guess whether the local Ollama runner has GPU
// acceleration available.
OllamaRuntimeProfile detectOllamaRuntimeProfile();

QString ollamaRuntimeProfileName(OllamaRuntimeProfile profile);

// A smaller num_ctx to retry a request with after Ollama's runner crashes/exits,
// leaving enough headroom below the original ceiling to actually avoid the failure.
int computeRunnerFallbackNumCtx(int baseNumCtx);

int estimatedCharsForTokens(int tokens);

int safeRetrievedContextTokenBudget(int numCtx, bool documentStudy, OllamaRuntimeProfile runtimeProfile);

int safeRetrievedContextCharBudget(int numCtx, bool documentStudy, OllamaRuntimeProfile runtimeProfile);

// 0..1 scale of "how big is this document" derived from its char/chunk counts on a
// log scale, used to interpolate document-study budgets between small and huge files.
double normalizedDocumentScale(int textChars, int chunkCount);

DocumentStudyRuntimeTuning tuneDocumentStudyRuntime(const DocumentSelectionStats &stats,
                                                    bool prioritized,
                                                    bool exactExtraction,
                                                    int numCtx,
                                                    OllamaRuntimeProfile runtimeProfile);
