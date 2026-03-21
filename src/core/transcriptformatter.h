#pragma once

#include <QString>

namespace TranscriptFormatter {

QString sanitizeRenderableMarkdown(const QString &text);
QString sanitizeFinalAssistantMarkdown(const QString &text);

}
