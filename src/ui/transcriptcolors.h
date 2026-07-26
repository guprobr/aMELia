#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

// Linear-interpolates base toward accent by accentRatio (0..1), used throughout the
// transcript/markdown renderer to derive theme-aware tints from the active QPalette
// instead of hardcoding colors that would look wrong in light or dark mode.
QColor blendColors(const QColor &base, const QColor &accent, qreal accentRatio);

// Formats a QColor as a CSS rgba(...) string for inline HTML styling.
QString cssColor(const QColor &color);

QColor transcriptPrefixColor(const QPalette &palette, const QString &role);
QColor transcriptBodyColor(const QPalette &palette, const QString &role);

// "USER> " / "ASSISTANT> " / "[system] " / "[status] " / "[role] " label prefixed to
// each transcript entry.
QString transcriptPrefix(const QString &role);

QColor diagnosticCategoryColor(const QPalette &palette, const QString &category);
