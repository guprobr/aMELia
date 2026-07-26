#pragma once

#include <atomic>

// Shared cancellation check used throughout document ingestion (PDF cleanup,
// chunking, OCR) so every long-running loop can bail out the same way when the
// user cancels a reindex mid-flight. A null pointer means "no cancellation
// requested" (the caller isn't tracking one), not an error.
inline bool isCancelRequested(const std::atomic_bool *cancelRequested)
{
    return cancelRequested != nullptr && cancelRequested->load(std::memory_order_relaxed);
}
