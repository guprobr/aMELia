#pragma once

#include <QSet>
#include <QString>
#include <QVector>

class QFileInfo;
struct ManifestEntry;

// Copies sourceFile into collectionRoot at (a de-duplicated version of) relativePath,
// and appends a new ManifestEntry describing it. Returns false without modifying
// *entries if sourceFile doesn't exist or the copy fails.
bool copyFileIntoCollection(const QString &sourceFile,
                            const QString &collectionRoot,
                            const QString &relativePath,
                            const QString &collectionId,
                            QVector<ManifestEntry> *entries,
                            QSet<QString> *usedRelativePaths);

// Renames if possible (same filesystem), otherwise copies + deletes the original --
// used when moving a stored knowledge file between collections, which may or may not
// be on the same volume.
bool moveStoredKnowledgeFile(const QString &sourcePath, const QString &destinationPath);

// Imports one file or an entire directory tree (recursively, filtered to
// sourceclassifier's extensions()) into a collection, returning how many files were
// actually copied.
int importPathIntoCollection(const QString &path,
                             const QString &collectionId,
                             const QString &collectionRoot,
                             QVector<ManifestEntry> *entries,
                             QSet<QString> *usedRelativePaths);

// Walks upward from path's parent directory removing now-empty directories, stopping
// at stopRoot -- cleans up the directory tree left behind after removing/moving the
// last file out of a collection subfolder.
void pruneEmptyKnowledgeDirectories(const QString &path, const QString &stopRoot);

// True for Amelia's own metadata files (".amelia_*") inside a knowledge root, so
// directory walks can skip them instead of trying to import/index them as content.
bool isReservedKnowledgeMetadataFile(const QFileInfo &info);
