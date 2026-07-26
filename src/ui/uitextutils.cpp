#include "ui/uitextutils.h"

#include <QLineEdit>

QStringList splitAssetPaths(const QString &raw)
{
    QString normalized = raw;
    normalized.replace(QLatin1Char('\n'), QLatin1Char(';'));
    normalized.replace(QLatin1Char(','), QLatin1Char(';'));
    const QStringList parts = normalized.split(QLatin1Char(';'), Qt::SkipEmptyParts);

    QStringList paths;
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            paths << trimmed;
        }
    }
    return paths;
}

void appendPathsToLineEdit(QLineEdit *lineEdit, const QStringList &paths)
{
    if (lineEdit == nullptr || paths.isEmpty()) {
        return;
    }
    QStringList merged = splitAssetPaths(lineEdit->text());
    for (const QString &path : paths) {
        if (!merged.contains(path)) {
            merged << path;
        }
    }
    lineEdit->setText(merged.join(QStringLiteral("; ")));
}

QString formatByteCount(qint64 bytes)
{
    static const QStringList units = {QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB"), QStringLiteral("TB")};
    double value = static_cast<double>(qMax<qint64>(0, bytes));
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < units.size() - 1) {
        value /= 1024.0;
        ++unitIndex;
    }

    const int precision = (unitIndex == 0 || value >= 10.0) ? 0 : 1;
    return QStringLiteral("%1 %2").arg(QString::number(value, 'f', precision), units.at(unitIndex));
}
