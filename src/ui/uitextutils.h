#pragma once

#include <QString>
#include <QStringList>

class QLineEdit;

// Splits a user-typed path list on newlines/commas/semicolons into trimmed,
// non-empty paths -- used for the "paste one or more paths" import fields, which
// accept any of those separators interchangeably.
QStringList splitAssetPaths(const QString &raw);

// Merges paths into lineEdit's existing (semicolon-joined) path list, skipping any
// already present, so dropping the same file twice doesn't duplicate it.
void appendPathsToLineEdit(QLineEdit *lineEdit, const QStringList &paths);

// Formats a byte count with a B/KB/MB/GB/TB unit, for Knowledge Base inventory
// display.
QString formatByteCount(qint64 bytes);
