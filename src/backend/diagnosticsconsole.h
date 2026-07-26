#pragma once

#include <QString>

// Formats and prints one ChatController diagnostic line to stderr, colored by
// category unless NO_COLOR is set in the environment.
void printDiagnosticToConsole(const QString &category, const QString &line);

// True for high-volume, low-signal diagnostic lines (raw Ollama request/response
// logging) that should be filtered out of the default diagnostic view and only shown
// when verbose diagnostics are enabled.
bool shouldClassifyDiagnosticAsVerbose(const QString &category, const QString &message);
