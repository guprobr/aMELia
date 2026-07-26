#pragma once

#include <QString>
#include <QStringList>

class QFileInfo;

// File-extension globs for every file type the indexer walks a knowledge root
// looking for (text/code/config formats plus PDF/DOCX).
QStringList extensions();

// Coarse type bucket (log/config/doc/code/misc) derived from file extension, used to
// pick a chunking profile and as an input to detectSourceRole.
QString detectSourceType(const QFileInfo &info);

// Finer-grained retrieval role (log/config/code/scenario/procedure/reference) derived
// from source type plus keyword sniffing over the filename, path, and the first ~4000
// chars of content. Used to bias retrieval scoring toward the role that best matches
// the query's intent (see roleBias in lexicalscoring.h).
QString detectSourceRole(const QFileInfo &info, const QString &sourceType, const QString &text);
