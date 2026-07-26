#include "ui/transcriptcolors.h"

#include <QtGlobal>

QColor blendColors(const QColor &base, const QColor &accent, qreal accentRatio)
{
    const qreal ratio = qBound<qreal>(0.0, accentRatio, 1.0);
    const qreal inverse = 1.0 - ratio;
    return QColor::fromRgbF(base.redF() * inverse + accent.redF() * ratio,
                            base.greenF() * inverse + accent.greenF() * ratio,
                            base.blueF() * inverse + accent.blueF() * ratio,
                            base.alphaF() * inverse + accent.alphaF() * ratio);
}

QString cssColor(const QColor &color)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(QString::number(color.alphaF(), 'f', 3));
}

QColor transcriptPrefixColor(const QPalette &palette, const QString &role)
{
    const QString lower = role.toLower();
    const QColor highlight = palette.color(QPalette::Highlight);
    const QColor link = palette.color(QPalette::Link);
    const QColor text = palette.color(QPalette::Text);
    const QColor base = palette.color(QPalette::Base);

    if (lower == QStringLiteral("user")) {
        return link.isValid() ? link : highlight;
    }
    if (lower == QStringLiteral("assistant")) {
        return blendColors(highlight, text, 0.20);
    }
    if (lower == QStringLiteral("system")) {
        return blendColors(text, highlight, 0.42);
    }
    if (lower == QStringLiteral("status")) {
        return blendColors(base, highlight, 0.72);
    }
    return blendColors(text, highlight, 0.18);
}

QColor transcriptBodyColor(const QPalette &palette, const QString &role)
{
    const QString lower = role.toLower();
    const QColor text = palette.color(QPalette::Text);
    if (lower == QStringLiteral("system")) {
        return blendColors(text, palette.color(QPalette::Highlight), 0.18);
    }
    return text;
}

QString transcriptPrefix(const QString &role)
{
    const QString lower = role.toLower();
    if (lower == QStringLiteral("user")) {
        return QStringLiteral("USER> ");
    }
    if (lower == QStringLiteral("assistant")) {
        return QStringLiteral("ASSISTANT> ");
    }
    if (lower == QStringLiteral("system")) {
        return QStringLiteral("[system] ");
    }
    if (lower == QStringLiteral("status")) {
        return QStringLiteral("[status] ");
    }
    return QStringLiteral("[") + role + QStringLiteral("] ");
}

QColor diagnosticCategoryColor(const QPalette &palette, const QString &category)
{
    const QString lower = category.toLower();
    const QColor highlight = palette.color(QPalette::Highlight);
    const QColor link = palette.color(QPalette::Link);
    const QColor text = palette.color(QPalette::Text);

    if (lower == QStringLiteral("backend")) {
        return blendColors(link, highlight, 0.30);
    }
    if (lower == QStringLiteral("search")) {
        return blendColors(highlight, QColor(Qt::green), 0.35);
    }
    if (lower == QStringLiteral("rag")) {
        return blendColors(highlight, QColor(Qt::cyan), 0.28);
    }
    if (lower == QStringLiteral("memory")) {
        return blendColors(highlight, QColor(255, 140, 0), 0.35);
    }
    if (lower == QStringLiteral("planner")) {
        return blendColors(highlight, QColor(148, 0, 211), 0.28);
    }
    if (lower == QStringLiteral("guardrail")) {
        return blendColors(highlight, QColor(Qt::red), 0.42);
    }
    if (lower == QStringLiteral("ingest")) {
        return blendColors(highlight, QColor(Qt::yellow), 0.38);
    }
    if (lower == QStringLiteral("startup")) {
        return blendColors(highlight, QColor(255, 105, 180), 0.28);
    }
    if (lower == QStringLiteral("budget")) {
        return blendColors(link, QColor(Qt::cyan), 0.18);
    }
    if (lower == QStringLiteral("chat")) {
        return blendColors(text, highlight, 0.35);
    }
    if (lower == QStringLiteral("reasoning")) {
        return blendColors(text, QColor(148, 0, 211), 0.30);
    }
    return blendColors(text, highlight, 0.18);
}
